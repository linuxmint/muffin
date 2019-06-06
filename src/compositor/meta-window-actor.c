/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * SECTION:meta-window-actor
 * @title: MetaWindowActor
 * @short_description: An actor representing a top-level window in the scene graph
 */

#include <config.h>

#include <math.h>

#include <X11/extensions/shape.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xrender.h>

#include <clutter/x11/clutter-x11.h>
#include <cogl/winsys/cogl-texture-pixmap-x11.h>
#include <gdk/gdk.h> /* for gdk_rectangle_union() */

#include <meta/display.h>
#include <meta/errors.h>
#include "frame.h"
#include <meta/window.h>
#include <meta/meta-shaped-texture.h>
#include "xprops.h"

#include "compositor-private.h"
#include "meta-shaped-texture-private.h"
#include "meta-shadow-factory-private.h"
#include "meta-window-actor-private.h"

enum {
  POSITION_CHANGED,
  SIZE_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};


struct _MetaWindowActorPrivate
{
  MetaWindow       *window;
  Window            xwindow;
  MetaScreen       *screen;

  ClutterActor     *actor;

  /* MetaShadowFactory only caches shadows that are actually in use;
   * to avoid unnecessary recomputation we do two things: 1) we store
   * both a focused and unfocused shadow for the window. If the window
   * doesn't have different focused and unfocused shadow parameters,
   * these will be the same. 2) when the shadow potentially changes we
   * don't immediately unreference the old shadow, we just flag it as
   * dirty and recompute it when we next need it (recompute_focused_shadow,
   * recompute_unfocused_shadow.) Because of our extraction of
   * size-invariant window shape, we'll often find that the new shadow
   * is the same as the old shadow.
   */
  MetaShadow       *focused_shadow;
  MetaShadow       *unfocused_shadow;

  Pixmap            back_pixmap;

  Damage            damage;

  guint8            opacity;

  /* If the window is shaped, a region that matches the shape */
  cairo_region_t   *shape_region;
  /* The opaque region, from _NET_WM_OPAQUE_REGION, intersected with
   * the shape region. */
  cairo_region_t   *opaque_region;
  /* The region we should clip to when painting the shadow */
  cairo_region_t   *shadow_clip;

   /* The region that is visible, used to optimize out redraws */
  cairo_region_t   *unobscured_region;

  /* Extracted size-invariant shape used for shadows */
  MetaWindowShape  *shadow_shape;

  gint              last_width;
  gint              last_height;
  gint              last_x;
  gint              last_y;

  gint              freeze_count;

  char *            shadow_class;

  /*
   * These need to be counters rather than flags, since more plugins
   * can implement same effect; the practicality of stacking effects
   * might be dubious, but we have to at least handle it correctly.
   */
  gint              minimize_in_progress;
  gint              maximize_in_progress;
  gint              unmaximize_in_progress;
  gint              tile_in_progress;
  gint              map_in_progress;
  gint              destroy_in_progress;

  /* List of FrameData for recent frames */
  GList            *frames;

  guint		    visible                : 1;
  guint		    argb32                 : 1;
  guint		    disposed               : 1;
  guint             redecorating           : 1;

  guint		    needs_damage_all       : 1;
  guint		    received_damage        : 1;
  guint             repaint_scheduled      : 1;

  /* If set, the client needs to be sent a _NET_WM_FRAME_DRAWN
   * client message using the most recent frame in ->frames */
  guint             send_frame_messages_timer;
  gint64            frame_drawn_time;
  guint             needs_frame_drawn      : 1;

  guint             size_changed_id;
  guint             opacity_changed_id;

  guint		    needs_pixmap           : 1;
  guint             needs_reshape          : 1;
  guint             recompute_focused_shadow   : 1;
  guint             recompute_unfocused_shadow : 1;
  guint             size_changed               : 1;
  guint             position_changed           : 1;
  guint             updates_frozen         : 1;

  guint		    needs_destroy	   : 1;

  guint             no_shadow              : 1;

  guint             unredirected           : 1;

  /* This is used to detect fullscreen windows that need to be unredirected */
  guint             full_damage_frames_count;
  guint             does_full_damage  : 1;

  guint             has_desat_effect : 1;

  guint             reshapes;
  guint             should_have_shadow : 1;
};

typedef struct _FrameData FrameData;

struct _FrameData
{
  int64_t frame_counter;
  guint64 sync_request_serial;
  gint64 frame_drawn_time;
};

enum
{
  PROP_META_WINDOW = 1,
  PROP_META_SCREEN,
  PROP_X_WINDOW,
  PROP_X_WINDOW_ATTRIBUTES,
  PROP_NO_SHADOW,
  PROP_SHADOW_CLASS
};

#define DEFAULT_SHADOW_RADIUS 12
#define DEFAULT_SHADOW_X_OFFSET 0
#define DEFAULT_SHADOW_Y_OFFSET 8

static void meta_window_actor_dispose    (GObject *object);
static void meta_window_actor_finalize   (GObject *object);
static void meta_window_actor_constructed (GObject *object);
static void meta_window_actor_set_property (GObject       *object,
                                            guint         prop_id,
                                            const GValue *value,
                                            GParamSpec   *pspec);
static void meta_window_actor_get_property (GObject      *object,
                                            guint         prop_id,
                                            GValue       *value,
                                            GParamSpec   *pspec);

static void meta_window_actor_pick (ClutterActor       *actor,
			                              const ClutterColor *color);
static void meta_window_actor_paint (ClutterActor *actor);

static gboolean meta_window_actor_get_paint_volume (ClutterActor       *actor,
                                                    ClutterPaintVolume *volume);


static void     meta_window_actor_detach     (MetaWindowActor *self);
static gboolean meta_window_actor_has_shadow (MetaWindowActor *self);

static void meta_window_actor_handle_updates (MetaWindowActor *self);

static void check_needs_reshape (MetaWindowActor *self);

static void do_send_frame_drawn (MetaWindowActor *self, FrameData *frame);
static void do_send_frame_timings (MetaWindowActor  *self,
                                   FrameData        *frame,
                                   gint             refresh_interval,
                                   gint64           presentation_time);

G_DEFINE_TYPE (MetaWindowActor, meta_window_actor, CLUTTER_TYPE_ACTOR);

static void
frame_data_free (FrameData *frame)
{
  g_slice_free (FrameData, frame);
}

static void
meta_window_actor_class_init (MetaWindowActorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  GParamSpec   *pspec;

  g_type_class_add_private (klass, sizeof (MetaWindowActorPrivate));

  object_class->dispose      = meta_window_actor_dispose;
  object_class->finalize     = meta_window_actor_finalize;
  object_class->set_property = meta_window_actor_set_property;
  object_class->get_property = meta_window_actor_get_property;
  object_class->constructed  = meta_window_actor_constructed;

  actor_class->pick = meta_window_actor_pick;
  actor_class->paint = meta_window_actor_paint;
  actor_class->get_paint_volume = meta_window_actor_get_paint_volume;

  pspec = g_param_spec_object ("meta-window",
                               "MetaWindow",
                               "The displayed MetaWindow",
                               META_TYPE_WINDOW,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  g_object_class_install_property (object_class,
                                   PROP_META_WINDOW,
                                   pspec);

  pspec = g_param_spec_pointer ("meta-screen",
				"MetaScreen",
				"MetaScreen",
				G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  g_object_class_install_property (object_class,
                                   PROP_META_SCREEN,
                                   pspec);

  pspec = g_param_spec_ulong ("x-window",
			      "Window",
			      "Window",
			      0,
			      G_MAXULONG,
			      0,
			      G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  g_object_class_install_property (object_class,
                                   PROP_X_WINDOW,
                                   pspec);

  pspec = g_param_spec_boolean ("no-shadow",
                                "No shadow",
                                "Do not add shaddow to this window",
                                FALSE,
                                G_PARAM_READWRITE);

  g_object_class_install_property (object_class,
                                   PROP_NO_SHADOW,
                                   pspec);

  pspec = g_param_spec_string ("shadow-class",
                               "Name of the shadow class for this window.",
                               "NULL means to use the default shadow class for this window type",
                               NULL,
                               G_PARAM_READWRITE);

  g_object_class_install_property (object_class,
                                   PROP_SHADOW_CLASS,
                                   pspec);

  signals[POSITION_CHANGED] =
    g_signal_new ("position-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
  signals[SIZE_CHANGED] =
    g_signal_new ("size-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
meta_window_actor_init (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv;

  priv = self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
						   META_TYPE_WINDOW_ACTOR,
						   MetaWindowActorPrivate);
  priv->opacity = 0xff;
  priv->shadow_class = NULL;
  priv->has_desat_effect = FALSE;
  priv->reshapes = 0;
  priv->should_have_shadow = FALSE;
}

static void
meta_window_actor_reset_mask_texture (MetaWindowActor *self,
                                      cairo_region_t  *shape_region,
                                      gboolean force)
{
  MetaShapedTexture *stex = META_SHAPED_TEXTURE (self->priv->actor);
  if (force)
    meta_shaped_texture_dirty_mask (stex);
  meta_shaped_texture_ensure_mask (stex, shape_region, self->priv->window->frame != NULL);
}

static void
maybe_desaturate_window (ClutterActor *actor)
{
  MetaWindowActor *window = META_WINDOW_ACTOR (actor);
  MetaWindowActorPrivate *priv = window->priv;

  if (!priv->should_have_shadow)
    return;

  guint8 opacity = clutter_actor_get_opacity (actor);

  if (opacity < 255)
    {
      if (priv->has_desat_effect)
        {
          return;
        }
      else
        {
          ClutterEffect *effect = clutter_desaturate_effect_new (0.0);
          clutter_actor_add_effect_with_name (actor, "desaturate-for-transparency", effect);
          priv->has_desat_effect = TRUE;
        }
    }
  else
    {
      /* This is will tend to get called fairly often - opening new windows, various
         events on the window, like minimizing... but it's inexpensive - if the ClutterActor
         priv->effects is NULL, it simply returns.  By default cinnamon and muffin add no
         other effects except the special case of dimmed windows (attached modal dialogs), which
         isn't a frequent occurrence. */

      clutter_actor_remove_effect_by_name (actor, "desaturate-for-transparency");
      priv->has_desat_effect = FALSE;
    }
}

static void
window_decorated_notify (MetaWindow *mw,
                         GParamSpec *arg1,
                         gpointer    data)
{
  MetaWindowActor        *self     = META_WINDOW_ACTOR (data);
  MetaWindowActorPrivate *priv     = self->priv;
  MetaFrame              *frame    = meta_window_get_frame (mw);
  MetaScreen             *screen   = priv->screen;
  MetaDisplay            *display  = meta_screen_get_display (screen);
  Display                *xdisplay = meta_display_get_xdisplay (display);
  Window                  new_xwindow;

  /*
   * Basically, we have to reconstruct the the internals of this object
   * from scratch, as everything has changed.
   */
  priv->redecorating = TRUE;

  if (frame)
    new_xwindow = meta_frame_get_xwindow (frame);
  else
    new_xwindow = meta_window_get_xwindow (mw);

  meta_window_actor_detach (self);

  /*
   * First of all, clean up any resources we are currently using and will
   * be replacing.
   */
  if (priv->damage != None)
    {
      meta_error_trap_push (display);
      XDamageDestroy (xdisplay, priv->damage);
      meta_error_trap_pop (display);
      priv->damage = None;
    }

  priv->xwindow = new_xwindow;

  /*
   * Recreate the contents.
   */
  meta_window_actor_constructed (G_OBJECT (self));
}

static void
window_appears_focused_notify (MetaWindow *mw,
                               GParamSpec *arg1,
                               gpointer    data)
{
  clutter_actor_queue_redraw (CLUTTER_ACTOR (data));
}

static void
clutter_actor_opacity_notify (ClutterActor *actor,
                              GParamSpec   *arg1m,
                              gpointer      data)
{
  maybe_desaturate_window (actor);
}

static void
texture_size_changed (MetaWindow *mw,
                     gpointer    data)
{
  MetaWindowActor *self = META_WINDOW_ACTOR (data);

  g_signal_emit (self, signals[SIZE_CHANGED], 0); // Compatibility
}

static void
window_position_changed (MetaWindow *mw,
                     gpointer    data)
{
  MetaWindowActor *self = META_WINDOW_ACTOR (data);

  g_signal_emit (self, signals[POSITION_CHANGED], 0); // Compatibility
}

static void
meta_window_actor_constructed (GObject *object)
{
  MetaWindowActor        *self     = META_WINDOW_ACTOR (object);
  MetaWindowActorPrivate *priv     = self->priv;
  MetaScreen             *screen   = priv->screen;
  MetaDisplay            *display  = meta_screen_get_display (screen);
  Window                  xwindow  = priv->xwindow;
  MetaWindow             *window   = priv->window;
  Display                *xdisplay = meta_display_get_xdisplay (display);
  XRenderPictFormat      *format;

  priv->damage = XDamageCreate (xdisplay, xwindow,
                                XDamageReportBoundingBox);

  format = XRenderFindVisualFormat (xdisplay, window->xvisual);

  if (format && format->type == PictTypeDirect && format->direct.alphaMask)
    priv->argb32 = TRUE;

  if (!priv->actor)
    {
      priv->actor = meta_shaped_texture_new ();

      priv->size_changed_id = g_signal_connect (priv->actor, "size-changed",
                                                G_CALLBACK (texture_size_changed), self);
      clutter_actor_add_child (CLUTTER_ACTOR (self), priv->actor);

      /*
       * Since we are holding a pointer to this actor independently of the
       * ClutterContainer internals, and provide a public API to access it,
       * add a reference here, so that if someone is messing about with us
       * via the container interface, we do not end up with a dangling pointer.
       * We will release it in dispose().
       */
      g_object_ref (priv->actor);
      priv->opacity_changed_id = g_signal_connect (self, "notify::opacity",
                                                   G_CALLBACK (clutter_actor_opacity_notify), NULL);

      /* Fix for the case when clients try to re-map their windows after re-decorating while
         effects are enabled. For reasons currently unknown, the re-shape doesn't happen when
         #meta_plugin_map_completed is called after a delay. #window_decorated_notify is not
         always called on re-decoration, and when it is, its only called before the actor is
         disposed in this case - we can't track after that. */
      if (meta_prefs_get_desktop_effects () &&
          window->frame != NULL &&
          window->decorated &&
          !window->pending_compositor_effect &&
          !window->unmaps_pending)
        priv->needs_reshape = TRUE;
    }
  else
    {
      /*
       * This is the case where existing window is gaining/loosing frame.
       * Just ensure the actor is top most (i.e., above shadow).
       */
      g_signal_handler_disconnect (priv->actor, priv->size_changed_id);
      g_signal_handler_disconnect (self, priv->opacity_changed_id);
      clutter_actor_set_child_above_sibling (CLUTTER_ACTOR (self), priv->actor, NULL);
    }

  meta_window_actor_update_opacity (self);
  maybe_desaturate_window (CLUTTER_ACTOR (self));

  priv->shape_region = cairo_region_create();
}

static void
meta_window_actor_dispose (GObject *object)
{
  MetaWindowActor        *self = META_WINDOW_ACTOR (object);
  MetaWindowActorPrivate *priv = self->priv;
  MetaScreen *screen;
  MetaDisplay *display;
  Display *xdisplay;
  MetaCompositor *compositor;

  if (priv->disposed)
    return;

  priv->disposed = TRUE;

  if (priv->send_frame_messages_timer != 0)
    {
      g_source_remove (priv->send_frame_messages_timer);
      priv->send_frame_messages_timer = 0;
    }

  screen = priv->screen;
  display = screen->display;
  xdisplay = display->xdisplay;
  compositor = display->compositor;

  meta_window_actor_detach (self);

  g_clear_pointer (&priv->unobscured_region, cairo_region_destroy);
  g_clear_pointer (&priv->shape_region, cairo_region_destroy);
  g_clear_pointer (&priv->opaque_region, cairo_region_destroy);
  g_clear_pointer (&priv->shadow_clip, cairo_region_destroy);

  g_clear_pointer (&priv->shadow_class, free);
  g_clear_pointer (&priv->focused_shadow, meta_shadow_unref);
  g_clear_pointer (&priv->unfocused_shadow, meta_shadow_unref);
  g_clear_pointer (&priv->shadow_shape, meta_window_shape_unref);

  if (priv->damage != None)
    {
      meta_error_trap_push (display);
      XDamageDestroy (xdisplay, priv->damage);
      meta_error_trap_pop (display);

      priv->damage = None;
    }

  compositor->windows = g_list_remove (compositor->windows, (gconstpointer) self);

  g_clear_object (&priv->window);

  /*
   * Release the extra reference we took on the actor.
   */
  g_clear_object (&priv->actor);

  G_OBJECT_CLASS (meta_window_actor_parent_class)->dispose (object);
}

static void
meta_window_actor_finalize (GObject *object)
{
  MetaWindowActor        *self = META_WINDOW_ACTOR (object);
  MetaWindowActorPrivate *priv = self->priv;
  g_list_free_full (priv->frames, (GDestroyNotify) frame_data_free);
  G_OBJECT_CLASS (meta_window_actor_parent_class)->finalize (object);
}

static void
meta_window_actor_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  MetaWindowActor        *self   = META_WINDOW_ACTOR (object);
  MetaWindowActorPrivate *priv = self->priv;

  switch (prop_id)
    {
    case PROP_META_WINDOW:
      {
        if (priv->window)
          g_object_unref (priv->window);
        priv->window = g_value_dup_object (value);

        g_signal_connect_object (priv->window, "notify::decorated",
                                 G_CALLBACK (window_decorated_notify), self, 0);
        g_signal_connect_object (priv->window, "notify::appears-focused",
                                 G_CALLBACK (window_appears_focused_notify), self, 0);
        g_signal_connect_object (priv->window, "position-changed",
                                 G_CALLBACK (window_position_changed), self, 0);
      }
      break;
    case PROP_META_SCREEN:
      priv->screen = g_value_get_pointer (value);
      break;
    case PROP_X_WINDOW:
      priv->xwindow = g_value_get_ulong (value);
      break;
    case PROP_NO_SHADOW:
      {
        gboolean newv = g_value_get_boolean (value);

        if (newv == priv->no_shadow)
          return;

        priv->no_shadow = newv;

        meta_window_actor_invalidate_shadow (self);
      }
      break;
    case PROP_SHADOW_CLASS:
      {
        const char *newv = g_value_get_string (value);

        if (g_strcmp0 (newv, priv->shadow_class) == 0)
          return;

        free (priv->shadow_class);
        priv->shadow_class = g_strdup (newv);

        meta_window_actor_invalidate_shadow (self);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_window_actor_get_property (GObject      *object,
                                guint         prop_id,
                                GValue       *value,
                                GParamSpec   *pspec)
{
  MetaWindowActorPrivate *priv = META_WINDOW_ACTOR (object)->priv;

  switch (prop_id)
    {
    case PROP_META_WINDOW:
      g_value_set_object (value, priv->window);
      break;
    case PROP_META_SCREEN:
      g_value_set_pointer (value, priv->screen);
      break;
    case PROP_X_WINDOW:
      g_value_set_ulong (value, priv->xwindow);
      break;
    case PROP_NO_SHADOW:
      g_value_set_boolean (value, priv->no_shadow);
      break;
    case PROP_SHADOW_CLASS:
      g_value_set_string (value, priv->shadow_class);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static const char *
meta_window_actor_get_shadow_class (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;

  if (priv->shadow_class != NULL)
    return priv->shadow_class;
  else
    {
      MetaWindowType window_type = meta_window_get_window_type (priv->window);

      switch (window_type)
        {
        case META_WINDOW_DROPDOWN_MENU:
          return "dropdown-menu";
        case META_WINDOW_POPUP_MENU:
          return "popup-menu";
        default:
          {
            MetaFrameType frame_type = meta_window_get_frame_type (priv->window);
            return meta_frame_type_to_string (frame_type);
          }
        }
    }
}

static void
meta_window_actor_get_shadow_params (MetaWindowActor  *self,
                                     gboolean          appears_focused,
                                     MetaShadowParams *params)
{
  const char *shadow_class = meta_window_actor_get_shadow_class (self);

  meta_shadow_factory_get_params (meta_shadow_factory_get_default (),
                                  shadow_class, appears_focused,
                                  params);
}

LOCAL_SYMBOL void
meta_window_actor_get_shape_bounds (MetaWindowActor       *self,
                                    cairo_rectangle_int_t *bounds)
{
  MetaWindowActorPrivate *priv = self->priv;

  /* We need to be defensive here because there are corner cases
   * where getting the shape fails on a window being destroyed
   * and similar.
   */
  if (priv->shape_region)
    cairo_region_get_extents (priv->shape_region, bounds);
  else
    bounds->x = bounds->y = bounds->width = bounds->height = 0;
}

static void
meta_window_actor_get_shadow_bounds (MetaWindowActor       *self,
                                     gboolean               appears_focused,
                                     cairo_rectangle_int_t *bounds)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaShadow *shadow = appears_focused ? priv->focused_shadow : priv->unfocused_shadow;
  cairo_rectangle_int_t shape_bounds;
  MetaShadowParams params;

  meta_window_actor_get_shape_bounds (self, &shape_bounds);
  meta_window_actor_get_shadow_params (self, appears_focused, &params);

  meta_shadow_get_bounds (shadow,
                          params.x_offset + shape_bounds.x,
                          params.y_offset + shape_bounds.y,
                          shape_bounds.width,
                          shape_bounds.height,
                          bounds);
}

/* If we have an ARGB32 window that we decorate with a frame, it's
 * probably something like a translucent terminal - something where
 * the alpha channel represents transparency rather than a shape.  We
 * don't want to show the shadow through the translucent areas since
 * the shadow is wrong for translucent windows (it should be
 * translucent itself and colored), and not only that, will /look/
 * horribly wrong - a misplaced big black blob. As a hack, what we
 * want to do is just draw the shadow as normal outside the frame, and
 * inside the frame draw no shadow.  This is also not even close to
 * the right result, but looks OK. We also apply this approach to
 * windows set to be partially translucent with _NET_WM_WINDOW_OPACITY.
 */
static gboolean
clip_shadow_under_window (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;

  return (priv->argb32 || priv->opacity != 0xff) && priv->window->frame;
}

static void
assign_frame_counter_to_frames (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;
  ClutterStage *stage = priv->window->display->compositor->stage;
  GList *l;

  /* If the window is obscured, then we're expecting to deal with sending
   * frame messages in a timeout, rather than in this paint cycle.
   */
  if (priv->send_frame_messages_timer != 0)
    return;

  for (l = priv->frames; l; l = l->next)
    {
      FrameData *frame = l->data;

      if (frame->frame_counter == -1)
        frame->frame_counter = clutter_stage_get_frame_counter (stage);
    }
}

static void
meta_window_actor_pick (ClutterActor       *actor,
			                  const ClutterColor *color)
{
  if (!clutter_actor_should_pick_paint (actor))
    return;

  MetaWindowActor *self = (MetaWindowActor *) actor;
  MetaWindowActorPrivate *priv = self->priv;
  ClutterActorIter iter;
  ClutterActor *child;

  /* If there is no region then use the regular pick */
  if (priv->shape_region == NULL)
    CLUTTER_ACTOR_CLASS (meta_window_actor_parent_class)->pick (actor, color);
  else
    {
      int n_rects;
      float *rectangles;
      int i;
      CoglPipeline *pipeline;
      CoglContext *ctx;
      CoglFramebuffer *fb;
      CoglColor cogl_color;

      n_rects = cairo_region_num_rectangles (priv->shape_region);
      rectangles = g_alloca (sizeof (float) * 4 * n_rects);

      for (i = 0; i < n_rects; i++)
        {
          cairo_rectangle_int_t rect;
          int pos = i * 4;

          cairo_region_get_rectangle (priv->shape_region, i, &rect);

          rectangles[pos + 0] = rect.x;
          rectangles[pos + 1] = rect.y;
          rectangles[pos + 2] = rect.x + rect.width;
          rectangles[pos + 3] = rect.y + rect.height;
        }

      ctx = clutter_backend_get_cogl_context (clutter_get_default_backend ());
      fb = cogl_get_draw_framebuffer ();

      cogl_color_init_from_4ub (&cogl_color, color->red, color->green, color->blue, color->alpha);

      pipeline = cogl_pipeline_new (ctx);
      cogl_pipeline_set_color (pipeline, &cogl_color);
      cogl_framebuffer_draw_rectangles (fb, pipeline, rectangles, n_rects);
      cogl_object_unref (pipeline);
    }

  clutter_actor_iter_init (&iter, actor);

  while (clutter_actor_iter_next (&iter, &child))
    clutter_actor_paint (child);
}

static void
meta_window_actor_paint (ClutterActor *actor)
{
  MetaWindowActor *self = META_WINDOW_ACTOR (actor);
  MetaWindowActorPrivate *priv = self->priv;
  gboolean appears_focused = meta_window_appears_focused (priv->window);
  MetaShadow *shadow = appears_focused ? priv->focused_shadow : priv->unfocused_shadow;
  if (!priv->window->display->shadows_enabled) {
      shadow = NULL;
  }

 /* This window got damage when obscured; we set up a timer
  * to send frame completion events, but since we're drawing
  * the window now (for some other reason) cancel the timer
  * and send the completion events normally */
  if (priv->send_frame_messages_timer != 0)
    {
      g_source_remove (priv->send_frame_messages_timer);
      priv->send_frame_messages_timer = 0;

      assign_frame_counter_to_frames (self);
    }

  if (shadow != NULL)
    {
      MetaShadowParams params;
      cairo_rectangle_int_t shape_bounds;
      cairo_region_t *clip = priv->shadow_clip;

      meta_window_actor_get_shape_bounds (self, &shape_bounds);
      meta_window_actor_get_shadow_params (self, appears_focused, &params);

      /* The frame bounds are already subtracted from priv->shadow_clip
       * if that exists.
       */
      if (!clip && clip_shadow_under_window (self))
        {
          cairo_rectangle_int_t bounds;

          meta_window_actor_get_shadow_bounds (self, appears_focused, &bounds);
          clip = cairo_region_create_rectangle (&bounds);

          cairo_region_subtract (clip, meta_window_get_frame_bounds (priv->window));
        }

      meta_shadow_paint (shadow,
                         params.x_offset + shape_bounds.x,
                         params.y_offset + shape_bounds.y,
                         shape_bounds.width,
                         shape_bounds.height,
                         (clutter_actor_get_paint_opacity (actor) * params.opacity * priv->opacity) / (255 * 255),
                         clip,
                         clip_shadow_under_window (self)); /* clip_strictly - not just as an optimization */

      if (clip && clip != priv->shadow_clip)
        cairo_region_destroy (clip);
    }

  CLUTTER_ACTOR_CLASS (meta_window_actor_parent_class)->paint (actor);
}

static gboolean
meta_window_actor_get_paint_volume (ClutterActor       *actor,
                                    ClutterPaintVolume *volume)
{
  MetaWindowActor *self = META_WINDOW_ACTOR (actor);
  MetaWindowActorPrivate *priv = self->priv;
  gboolean appears_focused = meta_window_appears_focused (priv->window);

  /* The paint volume is computed before paint functions are called
   * so our bounds might not be updated yet. Force an update. */
  meta_window_actor_handle_updates (self);

  if (appears_focused ? priv->focused_shadow : priv->unfocused_shadow)
    {
      cairo_rectangle_int_t shadow_bounds;
      ClutterActorBox shadow_box;

      /* We could compute an full clip region as we do for the window
       * texture, but the shadow is relatively cheap to draw, and
       * a little more complex to clip, so we just catch the case where
       * the shadow is completely obscured and doesn't need to be drawn
       * at all.
       */

      meta_window_actor_get_shadow_bounds (self, appears_focused, &shadow_bounds);
      shadow_box.x1 = shadow_bounds.x;
      shadow_box.x2 = shadow_bounds.x + shadow_bounds.width;
      shadow_box.y1 = shadow_bounds.y;
      shadow_box.y2 = shadow_bounds.y + shadow_bounds.height;

      clutter_paint_volume_union_box (volume, &shadow_box);
    }

  if (priv->actor)
    {
      const ClutterPaintVolume *child_volume;

      child_volume = clutter_actor_get_transformed_paint_volume (CLUTTER_ACTOR (priv->actor), actor);
      if (!child_volume)
        return FALSE;

      clutter_paint_volume_union (volume, child_volume);
    }

  return TRUE;
}

static gboolean
meta_window_actor_has_shadow (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaWindowType window_type = meta_window_get_window_type (priv->window);

  if (priv->no_shadow)
    return FALSE;

  /* Leaving out shadows for maximized and fullscreen windows is an effeciency
   * win and also prevents the unsightly effect of the shadow of maximized
   * window appearing on an adjacent window */
  if ((meta_window_get_maximized (priv->window) == (META_MAXIMIZE_HORIZONTAL | META_MAXIMIZE_VERTICAL)) ||
      meta_window_is_fullscreen (priv->window))
    return FALSE;

  /* Don't shadow tiled windows of any type */

  if (meta_window_get_tile_type (priv->window) != META_WINDOW_TILE_TYPE_NONE)
    return FALSE;

  /*
   * Always put a shadow around windows with a frame - This should override
   * the restriction about not putting a shadow around ARGB windows.
   */
  if (priv->window)
    {
      if (meta_window_get_frame (priv->window))
        return TRUE;
    }

  /*
   * Do not add shadows to ARGB windows; eventually we should generate a
   * shadow from the input shape for such windows.
   */
  if (priv->argb32 || priv->opacity != 0xff)
    return FALSE;
  /*
   * Add shadows to override redirect windows (e.g., Gtk menus).
   */
  if (priv->window->override_redirect)
    return TRUE;

  /*
   * If a window specifies that it has custom frame extents, that likely
   * means that it is drawing a shadow itself. Don't draw our own.
   */
  if (priv->window->has_custom_frame_extents)
    return FALSE;

  /*
   * Don't put shadow around DND icon windows
   */
  if (window_type == META_WINDOW_DND ||
      window_type == META_WINDOW_DESKTOP ||
      window_type == META_WINDOW_DOCK)
    return FALSE;

  if (window_type == META_WINDOW_MENU
#if 0
      || window_type == META_WINDOW_DROPDOWN_MENU
#endif
      )
    return TRUE;

  if (meta_window_is_client_decorated (priv->window))
    {
      return FALSE;
    }

#if 0
  if (window_type == META_WINDOW_TOOLTIP)
    return TRUE;
#endif

  return TRUE;
}

/**
 * meta_window_actor_get_x_window: (skip)
 *
 */
Window
meta_window_actor_get_x_window (MetaWindowActor *self)
{
  if (!self)
    return None;

  return self->priv->xwindow;
}

/**
 * meta_window_actor_get_meta_window:
 *
 * Gets the #MetaWindow object that the the #MetaWindowActor is displaying
 *
 * Return value: (transfer none): the displayed #MetaWindow
 */
MetaWindow *
meta_window_actor_get_meta_window (MetaWindowActor *self)
{
  return self->priv->window;
}

/**
 * meta_window_actor_get_texture:
 *
 * Gets the ClutterActor that is used to display the contents of the window
 *
 * Return value: (transfer none): the #ClutterActor for the contents
 */
ClutterActor *
meta_window_actor_get_texture (MetaWindowActor *self)
{
  return self->priv->actor;
}

/**
 * meta_window_actor_is_destroyed:
 *
 * Gets whether the X window that the actor was displaying has been destroyed
 *
 * Return value: %TRUE when the window is destroyed, otherwise %FALSE
 */
gboolean
meta_window_actor_is_destroyed (MetaWindowActor *self)
{
  return self->priv->disposed || self->priv->needs_destroy;
}

static gboolean
send_frame_messages_timeout (gpointer data)
{
  MetaWindowActor *self = (MetaWindowActor *) data;
  MetaWindowActorPrivate *priv = self->priv;
  GList *l;

  for (l = priv->frames; l;)
    {
      GList *l_next = l->next;
      FrameData *frame = l->data;

      if (frame->frame_counter == -1)
        {
          do_send_frame_drawn (self, frame);
          do_send_frame_timings (self, frame, 0, 0);

          priv->frames = g_list_delete_link (priv->frames, l);
          frame_data_free (frame);
        }

      l = l_next;
    }

  priv->needs_frame_drawn = FALSE;
  priv->send_frame_messages_timer = 0;

  return FALSE;
}

static void
queue_send_frame_messages_timeout (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaWindow *window = meta_window_actor_get_meta_window (self);
  MetaDisplay *display = meta_screen_get_display (priv->screen);
  int64_t current_time;
  float refresh_rate;
  int interval, offset;

  if (priv->send_frame_messages_timer != 0)
    return;

  if (window->monitor)
    {
      refresh_rate = window->monitor->refresh_rate;
    }
  else
    {
      refresh_rate = 60.0f;
    }

  current_time =
    meta_compositor_monotonic_time_to_server_time (display,
                                                   g_get_monotonic_time ());
  interval = (int)(1000000 / refresh_rate) * 6;
  offset = MAX (0, priv->frame_drawn_time + interval - current_time) / 1000;

 /* The clutter master clock source has already been added with META_PRIORITY_REDRAW,
  * so the timer will run *after* the clutter frame handling, if a frame is ready
  * to be drawn when the timer expires.
  */
  priv->send_frame_messages_timer = g_timeout_add_full (META_PRIORITY_REDRAW, offset, send_frame_messages_timeout, self, NULL);
  g_source_set_name_by_id (priv->send_frame_messages_timer, "[muffin] send_frame_messages_timeout");
}

gboolean
meta_window_actor_is_override_redirect (MetaWindowActor *self)
{
  return meta_window_is_override_redirect (self->priv->window);
}

/**
 * meta_window_actor_get_workspace:
 * @self: #MetaWindowActor
 *
 * Returns the index of workspace on which this window is located; if the
 * window is sticky, or is not currently located on any workspace, returns -1.
 * This function is deprecated  and should not be used in newly written code;
 * meta_window_get_workspace() instead.
 *
 * Return value: index of workspace on which this window is
 * located.
 */
gint
meta_window_actor_get_workspace (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv;
  MetaWorkspace          *workspace;

  if (!self)
    return -1;

  priv = self->priv;

  if (!priv->window || meta_window_is_on_all_workspaces (priv->window))
    return -1;

  workspace = meta_window_get_workspace (priv->window);

  if (!workspace)
    return -1;

  return meta_workspace_index (workspace);
}

gboolean
meta_window_actor_showing_on_its_workspace (MetaWindowActor *self)
{
  if (!self)
    return FALSE;

  /* If override redirect: */
  if (!self->priv->window)
    return TRUE;

  return meta_window_showing_on_its_workspace (self->priv->window);
}

static void
meta_window_actor_freeze (MetaWindowActor *self)
{
  self->priv->freeze_count++;
}

static void
update_area (MetaWindowActor *self,
             int x, int y, int width, int height)
{
  MetaWindowActorPrivate *priv = self->priv;
  CoglTexture *texture;

  texture = meta_shaped_texture_get_texture (META_SHAPED_TEXTURE (priv->actor));

  cogl_texture_pixmap_x11_update_area (COGL_TEXTURE_PIXMAP_X11 (texture),
                                       x, y, width, height);
}

static void
meta_window_actor_damage_all (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;
  CoglTexture *texture;

  if (!priv->needs_damage_all || !priv->window->mapped || priv->needs_pixmap)
    return;

  texture = meta_shaped_texture_get_texture (META_SHAPED_TEXTURE (priv->actor));

  priv->needs_damage_all = FALSE;

  update_area (self, 0, 0, cogl_texture_get_width (texture), cogl_texture_get_height (texture));
  priv->repaint_scheduled = meta_shaped_texture_update_area (META_SHAPED_TEXTURE (priv->actor),
                                   0, 0,
                                   cogl_texture_get_width (texture),
                                   cogl_texture_get_height (texture),
                                   clutter_actor_has_mapped_clones (priv->actor) ? NULL : priv->unobscured_region);
}

static void
meta_window_actor_thaw (MetaWindowActor *self)
{
  self->priv->freeze_count--;

  if (G_UNLIKELY (self->priv->freeze_count < 0))
    {
      g_warning ("Error in freeze/thaw accounting.");
      self->priv->freeze_count = 0;
      return;
    }

  if (self->priv->freeze_count)
    return;

  /* We sometimes ignore moves and resizes on frozen windows */
  meta_window_actor_sync_actor_geometry (self, FALSE);

  /* We do this now since we might be going right back into the
   * frozen state */
  meta_window_actor_handle_updates (self);

  /* Since we ignore damage events while a window is frozen for certain effects
   * we may need to issue an update_area() covering the whole pixmap if we
   * don't know what real damage has happened. */
  if (self->priv->needs_damage_all)
    meta_window_actor_damage_all (self);
}

void
meta_window_actor_queue_frame_drawn (MetaWindowActor *self,
                                     gboolean         no_delay_frame)
{
  MetaWindowActorPrivate *priv = self->priv;
  FrameData *frame;

  if (meta_window_actor_is_destroyed (self))
    return;

  frame = g_slice_new0 (FrameData);
  frame->frame_counter = -1;

  priv->needs_frame_drawn = TRUE;

  frame->sync_request_serial = priv->window->sync_request_serial;

  priv->frames = g_list_prepend (priv->frames, frame);

  if (no_delay_frame)
    {
      ClutterActor *stage = priv->window->display->compositor->stage;
      clutter_stage_skip_sync_delay (CLUTTER_STAGE (stage));
    }

  if (!priv->repaint_scheduled)
    {
      gboolean is_obscured = FALSE;
       /* Find out whether the window is completly obscured */
      if (priv->unobscured_region)
        {
          cairo_region_t *unobscured_window_region;
          unobscured_window_region = cairo_region_copy (priv->shape_region);
          cairo_region_intersect (unobscured_window_region, priv->unobscured_region);
          is_obscured = cairo_region_is_empty (unobscured_window_region);
          cairo_region_destroy (unobscured_window_region);
        }

      /* A frame was marked by the client without actually doing any
       * damage, or while we had the window frozen (e.g. during an
       * interactive resize.) We need to make sure that the
       * pre_paint/post_paint functions get called, enabling us to
       * send a _NET_WM_FRAME_DRAWN. We do a 1-pixel redraw to get
       * consistent timing with non-empty frames.
       */
      if (is_obscured)
        {
          queue_send_frame_messages_timeout (self);
        }
      else if (priv->window->mapped && !priv->needs_pixmap)
        {
          const cairo_rectangle_int_t clip = { 0, 0, 1, 1 };
          clutter_actor_queue_redraw_with_clip (priv->actor, &clip);
          priv->repaint_scheduled = TRUE;
        }
    }
}

LOCAL_SYMBOL gboolean
meta_window_actor_effect_in_progress (MetaWindowActor *self)
{
  return (self->priv->minimize_in_progress ||
	  self->priv->maximize_in_progress ||
	  self->priv->unmaximize_in_progress ||
	  self->priv->map_in_progress ||
      self->priv->tile_in_progress ||
	  self->priv->destroy_in_progress);
}

static gboolean
is_frozen (MetaWindowActor *self)
{
  return self->priv->freeze_count ? TRUE : FALSE;
}

static gboolean
is_freeze_thaw_effect (gulong event)
{
  switch (event)
  {
  case META_PLUGIN_DESTROY:
  case META_PLUGIN_MAXIMIZE:
  case META_PLUGIN_UNMAXIMIZE:
  case META_PLUGIN_TILE:
    return TRUE;
    break;
  default:
    return FALSE;
  }
}

static gboolean
start_simple_effect (MetaWindowActor *self,
                     gulong        event)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaCompositor *compositor = priv->screen->display->compositor;
  gint *counter = NULL;
  gboolean use_freeze_thaw = FALSE;

  if (!compositor->plugin_mgr)
    return FALSE;

  switch (event)
  {
  case META_PLUGIN_MINIMIZE:
    counter = &priv->minimize_in_progress;
    break;
  case META_PLUGIN_MAP:
    counter = &priv->map_in_progress;
    break;
  case META_PLUGIN_DESTROY:
    counter = &priv->destroy_in_progress;
    break;
  case META_PLUGIN_UNMAXIMIZE:
  case META_PLUGIN_MAXIMIZE:
  case META_PLUGIN_SWITCH_WORKSPACE:
  case META_PLUGIN_TILE:
    g_assert_not_reached ();
    break;
  }

  g_assert (counter);

  use_freeze_thaw = is_freeze_thaw_effect (event);

  if (use_freeze_thaw)
    meta_window_actor_freeze (self);

  (*counter)++;

  if (!meta_plugin_manager_event_simple (compositor->plugin_mgr,
                                         self,
                                         event))
    {
      (*counter)--;
      if (use_freeze_thaw)
        meta_window_actor_thaw (self);
      return FALSE;
    }

  return TRUE;
}

static void
meta_window_actor_after_effects (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;

  if (priv->needs_destroy)
    {
      clutter_actor_destroy (CLUTTER_ACTOR (self));
      return;
    }

  meta_window_actor_sync_visibility (self);
  meta_window_actor_sync_actor_geometry (self, FALSE);

  if (priv->needs_pixmap)
    clutter_actor_queue_redraw (priv->actor);
}

LOCAL_SYMBOL void
meta_window_actor_effect_completed (MetaWindowActor *self,
                                    gulong           event)
{
  MetaWindowActorPrivate *priv   = self->priv;

  /* NB: Keep in mind that when effects get completed it possible
   * that the corresponding MetaWindow may have be been destroyed.
   * In this case priv->window will == NULL */

  switch (event)
  {
  case META_PLUGIN_MINIMIZE:
    {
      priv->minimize_in_progress--;
      if (priv->minimize_in_progress < 0)
	{
	  g_warning ("Error in minimize accounting.");
	  priv->minimize_in_progress = 0;
	}
    }
    break;
  case META_PLUGIN_MAP:
    /*
     * Make sure that the actor is at the correct place in case
     * the plugin fscked.
     */
    priv->map_in_progress--;
    priv->position_changed = TRUE;
    if (priv->map_in_progress < 0)
      {
	g_warning ("Error in map accounting.");
	priv->map_in_progress = 0;
      }
    break;
  case META_PLUGIN_DESTROY:
    priv->destroy_in_progress--;

    if (priv->destroy_in_progress < 0)
      {
	g_warning ("Error in destroy accounting.");
	priv->destroy_in_progress = 0;
      }
    break;
  case META_PLUGIN_UNMAXIMIZE:
    priv->unmaximize_in_progress--;
    if (priv->unmaximize_in_progress < 0)
      {
	g_warning ("Error in unmaximize accounting.");
	priv->unmaximize_in_progress = 0;
      }
    break;
  case META_PLUGIN_MAXIMIZE:
    priv->maximize_in_progress--;
    if (priv->maximize_in_progress < 0)
      {
	g_warning ("Error in maximize accounting.");
	priv->maximize_in_progress = 0;
      }
    break;
  case META_PLUGIN_TILE:
    priv->tile_in_progress--;
    if (priv->tile_in_progress < 0)
      {
    g_warning ("Error in tile accounting.");
    priv->tile_in_progress = 0;
      }
    break;
  case META_PLUGIN_SWITCH_WORKSPACE:
    g_assert_not_reached ();
    break;
  }

  if (is_freeze_thaw_effect (event))
    meta_window_actor_thaw (self);

  if (!meta_window_actor_effect_in_progress (self))
    meta_window_actor_after_effects (self);
}

/* Called to drop our reference to a window backing pixmap that we
 * previously obtained with XCompositeNameWindowPixmap. We do this
 * when the window is unmapped or when we want to update to a new
 * pixmap for a new size.
 */
static void
meta_window_actor_detach (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv     = self->priv;
  MetaScreen            *screen   = priv->screen;
  MetaDisplay           *display  = meta_screen_get_display (screen);
  Display               *xdisplay = meta_display_get_xdisplay (display);

  if (!priv->back_pixmap)
    return;

  /* Get rid of all references to the pixmap before freeing it; it's unclear whether
   * you are supposed to be able to free a GLXPixmap after freeing the underlying
   * pixmap, but it certainly doesn't work with current DRI/Mesa
   */
  meta_shaped_texture_set_texture (META_SHAPED_TEXTURE (priv->actor), NULL);

  cogl_flush();

  XFreePixmap (xdisplay, priv->back_pixmap);
  priv->back_pixmap = None;

  priv->needs_pixmap = TRUE;
}

LOCAL_SYMBOL gboolean
meta_window_actor_should_unredirect (MetaWindowActor *self)
{
  MetaWindow *metaWindow = meta_window_actor_get_meta_window (self);
  MetaWindowActorPrivate *priv = self->priv;

  if (meta_window_actor_is_destroyed (self))
    return FALSE;

  if (meta_window_requested_dont_bypass_compositor (metaWindow))
    return FALSE;

  if (priv->opacity != 0xff)
    return FALSE;

  if (metaWindow->has_shape)
    return FALSE;

  if (priv->argb32 && !meta_window_requested_bypass_compositor (metaWindow))
    return FALSE;

  if (!meta_window_is_monitor_sized (metaWindow))
    return FALSE;

  if (meta_window_requested_bypass_compositor (metaWindow))
    return TRUE;

  if (meta_window_is_override_redirect (metaWindow))
    return TRUE;

  if (priv->does_full_damage && meta_prefs_get_unredirect_fullscreen_windows ())
    return TRUE;

  return FALSE;
}

static void
fullscreen_sync_toggle (MetaWindowActor *self,
                        gboolean         state)
{
  MetaSyncMethod method = meta_prefs_get_sync_method ();

  if (meta_prefs_get_unredirect_fullscreen_windows () &&
      method != META_SYNC_NONE)
    {
      clutter_stage_x11_update_sync_state (
        self->priv->window->display->compositor->stage,
        state ? method : META_SYNC_NONE
      );
    }
}

LOCAL_SYMBOL void
meta_window_actor_set_redirected (MetaWindowActor *self, gboolean state)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaWindow *metaWindow = priv->window;
  MetaDisplay *display = metaWindow->display;

  Display *xdisplay = meta_display_get_xdisplay (display);
  Window  xwin = meta_window_actor_get_x_window (self);

  if (priv->unredirected != state)
    return;

  meta_error_trap_push (display);

  if (state)
    {
      XCompositeRedirectWindow (xdisplay, xwin, CompositeRedirectManual);
      fullscreen_sync_toggle (self, TRUE);
      priv->unredirected = FALSE;
    }
  else
    {
      fullscreen_sync_toggle (self, FALSE);
      meta_window_actor_detach (self);
      XCompositeUnredirectWindow (xdisplay, xwin, CompositeRedirectManual);
      priv->repaint_scheduled = TRUE;
      priv->unredirected = TRUE;
    }

  meta_error_trap_pop (display);
}

LOCAL_SYMBOL void
meta_window_actor_destroy (MetaWindowActor *self)
{
  MetaWindow *window;
  MetaWindowActorPrivate *priv = self->priv;
  MetaWindowType window_type;

  window = priv->window;
  window_type = meta_window_get_window_type (window);
  meta_window_set_compositor_private (window, NULL);

  if (priv->send_frame_messages_timer != 0)
    {
      g_source_remove (priv->send_frame_messages_timer);
      priv->send_frame_messages_timer = 0;
    }

  if (window_type == META_WINDOW_DROPDOWN_MENU ||
      window_type == META_WINDOW_POPUP_MENU ||
      window_type == META_WINDOW_TOOLTIP ||
      window_type == META_WINDOW_NOTIFICATION ||
      window_type == META_WINDOW_COMBO ||
      window_type == META_WINDOW_DND ||
      window_type == META_WINDOW_OVERRIDE_OTHER)
    {
      /*
       * No effects, just kill it.
       */
      clutter_actor_destroy (CLUTTER_ACTOR (self));
      return;
    }

  priv->needs_destroy = TRUE;

  if (!meta_window_actor_effect_in_progress (self))
    clutter_actor_destroy (CLUTTER_ACTOR (self));
}

LOCAL_SYMBOL void
meta_window_actor_sync_actor_geometry (MetaWindowActor *self,
                                       gboolean         did_placement)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaRectangle window_rect;

  meta_window_get_input_rect (priv->window, &window_rect);

  if (priv->last_width != window_rect.width ||
      priv->last_height != window_rect.height)
    {
      priv->size_changed = TRUE;
      priv->last_width = window_rect.width;
      priv->last_height = window_rect.height;
    }

  if (priv->last_x != window_rect.x ||
      priv->last_y != window_rect.y)
    {
      priv->position_changed = TRUE;
      priv->last_x = window_rect.x;
      priv->last_y = window_rect.y;
    }

  /* Normally we want freezing a window to also freeze its position; this allows
   * windows to atomically move and resize together, either under app control,
   * or because the user is resizing from the left/top. But on initial placement
   * we need to assign a position, since immediately after the window
   * is shown, the map effect will go into effect and prevent further geometry
   * updates.
   */
  if (is_frozen (self) && !did_placement)
    return;

  if (meta_window_actor_effect_in_progress (self))
    return;

  if (priv->size_changed)
    {
      priv->needs_pixmap = TRUE;
      meta_window_actor_update_shape (self);

      clutter_actor_set_size (CLUTTER_ACTOR (self),
                              window_rect.width, window_rect.height);
    }

  if (priv->position_changed)
    {
      clutter_actor_set_position (CLUTTER_ACTOR (self),
                                  window_rect.x, window_rect.y);
    }
}

void
meta_window_actor_show (MetaWindowActor   *self,
                        MetaCompEffect     effect)
{
  MetaWindowActorPrivate *priv = self->priv;
  gulong event;

  g_return_if_fail (!priv->visible);

  priv->visible = TRUE;

  event = 0;
  switch (effect)
    {
    case META_COMP_EFFECT_CREATE:
      event = META_PLUGIN_MAP;
      break;
    case META_COMP_EFFECT_UNMINIMIZE:
      /* FIXME: should have META_PLUGIN_UNMINIMIZE */
      event = META_PLUGIN_MAP;
      break;
    case META_COMP_EFFECT_NONE:
      break;
    case META_COMP_EFFECT_DESTROY:
    case META_COMP_EFFECT_MINIMIZE:
      g_assert_not_reached();
    }

  if (priv->redecorating ||
      priv->screen->display->compositor->switch_workspace_in_progress ||
      event == 0 ||
      !meta_prefs_get_desktop_effects () ||
      !start_simple_effect (self, event))
    {
      clutter_actor_show (CLUTTER_ACTOR (self));
      priv->redecorating = FALSE;
    }
}

LOCAL_SYMBOL void
meta_window_actor_hide (MetaWindowActor *self,
                        MetaCompEffect   effect)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaCompositor *compositor = priv->screen->display->compositor;
  gulong event;

  g_return_if_fail (priv->visible || (!priv->visible && meta_window_is_attached_dialog (priv->window)));

  priv->visible = FALSE;

  /* If a plugin is animating a workspace transition, we have to
   * hold off on hiding the window, and do it after the workspace
   * switch completes
   */
  if (compositor->switch_workspace_in_progress)
    return;

  event = 0;
  switch (effect)
    {
    case META_COMP_EFFECT_DESTROY:
      event = META_PLUGIN_DESTROY;
      break;
    case META_COMP_EFFECT_MINIMIZE:
      event = META_PLUGIN_MINIMIZE;
      break;
    case META_COMP_EFFECT_NONE:
      break;
    case META_COMP_EFFECT_UNMINIMIZE:
    case META_COMP_EFFECT_CREATE:
      g_assert_not_reached();
    }

  if (event == 0 ||
      !meta_prefs_get_desktop_effects () ||
      !start_simple_effect (self, event))
    clutter_actor_hide (CLUTTER_ACTOR (self));
}

LOCAL_SYMBOL void
meta_window_actor_maximize (MetaWindowActor    *self,
                            MetaRectangle      *old_rect,
                            MetaRectangle      *new_rect)
{
  MetaCompositor *compositor = self->priv->screen->display->compositor;
  /* The window has already been resized (in order to compute new_rect),
   * which by side effect caused the actor to be resized. Restore it to the
   * old size and position */
  clutter_actor_set_position (CLUTTER_ACTOR (self), old_rect->x, old_rect->y);
  clutter_actor_set_size (CLUTTER_ACTOR (self), old_rect->width, old_rect->height);

  self->priv->maximize_in_progress++;
  meta_window_actor_freeze (self);

  if (!compositor->plugin_mgr ||
      !meta_plugin_manager_event_maximize (compositor->plugin_mgr,
                                           self,
                                           META_PLUGIN_MAXIMIZE,
                                           new_rect->x, new_rect->y,
                                           new_rect->width, new_rect->height))

    {
      self->priv->maximize_in_progress--;
      meta_window_actor_thaw (self);
    }
}

LOCAL_SYMBOL void
meta_window_actor_unmaximize (MetaWindowActor   *self,
                              MetaRectangle     *old_rect,
                              MetaRectangle     *new_rect)
{
  MetaCompositor *compositor = self->priv->screen->display->compositor;

  /* The window has already been resized (in order to compute new_rect),
   * which by side effect caused the actor to be resized. Restore it to the
   * old size and position */
  clutter_actor_set_position (CLUTTER_ACTOR (self), old_rect->x, old_rect->y);
  clutter_actor_set_size (CLUTTER_ACTOR (self), old_rect->width, old_rect->height);

  self->priv->unmaximize_in_progress++;
  meta_window_actor_freeze (self);

  if (!compositor->plugin_mgr ||
      !meta_plugin_manager_event_maximize (compositor->plugin_mgr,
                                           self,
                                           META_PLUGIN_UNMAXIMIZE,
                                           new_rect->x, new_rect->y,
                                           new_rect->width, new_rect->height))
    {
      self->priv->unmaximize_in_progress--;
      meta_window_actor_thaw (self);
    }
}

LOCAL_SYMBOL void
meta_window_actor_tile (MetaWindowActor    *self,
                        MetaRectangle      *old_rect,
                        MetaRectangle      *new_rect)
{
  MetaCompositor *compositor = self->priv->screen->display->compositor;

  /* The window has already been resized (in order to compute new_rect),
   * which by side effect caused the actor to be resized. Restore it to the
   * old size and position */
  clutter_actor_set_position (CLUTTER_ACTOR (self), old_rect->x, old_rect->y);
  clutter_actor_set_size (CLUTTER_ACTOR (self), old_rect->width, old_rect->height);

  self->priv->tile_in_progress++;
  meta_window_actor_freeze (self);

  if (!compositor->plugin_mgr ||
      !meta_plugin_manager_event_maximize (compositor->plugin_mgr,
                                           self,
                                           META_PLUGIN_TILE,
                                           new_rect->x, new_rect->y,
                                           new_rect->width, new_rect->height))

    {
      self->priv->tile_in_progress--;
      meta_window_actor_thaw (self);
    }
}

LOCAL_SYMBOL MetaWindowActor *
meta_window_actor_new (MetaWindow *window)
{
  MetaScreen *screen = window->screen;
  MetaCompositor *compositor = screen->display->compositor;
  MetaWindowActor *self;
  MetaWindowActorPrivate *priv;
  MetaFrame		 *frame;
  Window		  top_window;
  ClutterActor           *window_group;

  frame = meta_window_get_frame (window);
  if (frame)
    top_window = meta_frame_get_xwindow (frame);
  else
    top_window = meta_window_get_xwindow (window);

  meta_verbose ("add window: Meta %p, xwin 0x%x\n", window, (guint)top_window);

  self = g_object_new (META_TYPE_WINDOW_ACTOR,
                       "meta-window",         window,
                       "x-window",            top_window,
                       "meta-screen",         screen,
                       NULL);

  priv = self->priv;

  priv->last_width = -1;
  priv->last_height = -1;
  priv->last_x = -1;
  priv->last_y = -1;

  priv->needs_pixmap = TRUE;

  meta_window_actor_set_updates_frozen (self,
                                        meta_window_updates_are_frozen (priv->window));

  /* If a window doesn't start off with updates frozen, we should
   * we should send a _NET_WM_FRAME_DRAWN immediately after the first drawn.
   */
  if (priv->window->extended_sync_request_counter && !priv->updates_frozen)
    meta_window_actor_queue_frame_drawn (self, FALSE);

  meta_window_actor_sync_actor_geometry (self, priv->window->placed);

  /* Hang our compositor window state off the MetaWindow for fast retrieval */
  meta_window_set_compositor_private (window, G_OBJECT (self));

  if (window->type == META_WINDOW_DND)
    window_group = compositor->window_group;
  else if (window->layer == META_LAYER_OVERRIDE_REDIRECT)
    window_group = compositor->top_window_group;
  else if (window->type == META_WINDOW_DESKTOP)
    window_group = compositor->bottom_window_group;
  else
    window_group = compositor->window_group;

  clutter_actor_add_child (window_group, CLUTTER_ACTOR (self));

  clutter_actor_hide (CLUTTER_ACTOR (self));

  /* Initial position in the stack is arbitrary; stacking will be synced
   * before we first paint.
   */
  compositor->windows = g_list_append (compositor->windows, self);

  return self;
}

static void
meta_window_actor_update_shape_region (MetaWindowActor *self,
                                       cairo_region_t  *region)
{
  MetaWindowActorPrivate *priv = self->priv;

  g_clear_pointer (&priv->shape_region, cairo_region_destroy);

  /* region must be non-null */
  priv->shape_region = region;
  cairo_region_reference (region);
}

/**
 * meta_window_actor_get_obscured_region:
 * @self: a #MetaWindowActor
 *
 * Gets the region that is completely obscured by the window. Coordinates
 * are relative to the upper-left of the window.
 *
 * Return value: (transfer none): the area obscured by the window,
 *  %NULL is the same as an empty region.
 */
LOCAL_SYMBOL cairo_region_t *
meta_window_actor_get_obscured_region (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;

  if (priv->back_pixmap && priv->opacity == 0xff)
    return priv->opaque_region;
  else
    return NULL;
}

#if 0
/* Print out a region; useful for debugging */
static void
dump_region (cairo_region_t *region)
{
  int n_rects;
  int i;

  n_rects = cairo_region_num_rectangles (region);
  g_print ("[");
  for (i = 0; i < n_rects; i++)
    {
      cairo_rectangle_int_t rect;
      cairo_region_get_rectangle (region, i, &rect);
      g_print ("+%d+%dx%dx%d ",
               rect.x, rect.y, rect.width, rect.height);
    }
  g_print ("]\n");
}
#endif

/**
 * meta_window_actor_set_unobscured_region:
 * @self: a #MetaWindowActor
 * @unobscured_region: the region of the screen that isn't completely
 *  obscured.
 *
 * Provides a hint as to what areas of the window need to queue
 * redraws when damaged. Regions not in @unobscured_region are completely obscured.
 * Unlike meta_window_actor_set_clip_region(), the region here
 * doesn't take into account any clipping that is in effect while drawing.
 */
void
meta_window_actor_set_unobscured_region (MetaWindowActor *self,
                                         cairo_region_t  *unobscured_region)
{
  MetaWindowActorPrivate *priv = self->priv;

  if (priv->unobscured_region)
    cairo_region_destroy (priv->unobscured_region);

  if (unobscured_region)
    priv->unobscured_region = cairo_region_copy (unobscured_region);
  else
    priv->unobscured_region = NULL;
}

/**
 * meta_window_actor_set_visible_region:
 * @self: a #MetaWindowActor
 * @visible_region: the region of the screen that isn't completely
 *  obscured.
 *
 * Provides a hint as to what areas of the window need to be
 * drawn. Regions not in @visible_region are completely obscured.
 * This will be set before painting then unset afterwards.
 */
LOCAL_SYMBOL void
meta_window_actor_set_visible_region (MetaWindowActor *self,
                                      cairo_region_t  *visible_region)
{
  MetaWindowActorPrivate *priv = self->priv;

  meta_shaped_texture_set_clip_region (META_SHAPED_TEXTURE (priv->actor),
                                       visible_region);
}

/**
 * meta_window_actor_set_visible_region_beneath:
 * @self: a #MetaWindowActor
 * @visible_region: the region of the screen that isn't completely
 *  obscured beneath the main window texture.
 *
 * Provides a hint as to what areas need to be drawn *beneath*
 * the main window texture.  This is the relevant visible region
 * when drawing the shadow, properly accounting for areas of the
 * shadow hid by the window itself. This will be set before painting
 * then unset afterwards.
 */
LOCAL_SYMBOL void
meta_window_actor_set_visible_region_beneath (MetaWindowActor *self,
                                              cairo_region_t  *beneath_region)
{
  MetaWindowActorPrivate *priv = self->priv;
  gboolean appears_focused = meta_window_appears_focused (priv->window);

  if (appears_focused ? priv->focused_shadow : priv->unfocused_shadow)
    {
      g_clear_pointer (&priv->shadow_clip, cairo_region_destroy);
      priv->shadow_clip = cairo_region_copy (beneath_region);

      if (clip_shadow_under_window (self))
        cairo_region_subtract (priv->shadow_clip,
                                meta_window_get_frame_bounds (priv->window));
    }
}

/**
 * meta_window_actor_reset_visible_regions:
 * @self: a #MetaWindowActor
 *
 * Unsets the regions set by meta_window_actor_reset_visible_region() and
 * meta_window_actor_reset_visible_region_beneath()
 */
LOCAL_SYMBOL void
meta_window_actor_reset_visible_regions (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;

  meta_shaped_texture_set_clip_region (META_SHAPED_TEXTURE (priv->actor),
                                       NULL);
  g_clear_pointer (&priv->shadow_clip, cairo_region_destroy);
}

static void
check_needs_pixmap (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaScreen *screen = priv->screen;
  MetaDisplay *display = screen->display;
  Display *xdisplay = display->xdisplay;
  MetaCompositor *compositor = display->compositor;
  Window xwindow = priv->xwindow;

  if ((!priv->window->mapped && !priv->window->shaded) || !priv->needs_pixmap)
    return;

  if (xwindow == screen->xroot ||
      xwindow == clutter_x11_get_stage_window (compositor->stage))
    return;

  if (priv->size_changed)
    {
      meta_window_actor_detach (self);
      priv->size_changed = FALSE;
    }

  if (priv->position_changed)
    priv->position_changed = FALSE;

  meta_error_trap_push (display);

  if (priv->back_pixmap == None)
    {
      CoglContext *ctx = clutter_backend_get_cogl_context (clutter_get_default_backend ());
      CoglTexture *texture;

      meta_error_trap_push (display);

      priv->back_pixmap = XCompositeNameWindowPixmap (xdisplay, xwindow);

      if (meta_error_trap_pop_with_return (display) != Success)
        {
          /* Probably a BadMatch if the window isn't viewable; we could
           * GrabServer/GetWindowAttributes/NameWindowPixmap/UngrabServer/Sync
           * to avoid this, but there's no reason to take two round trips
           * when one will do. (We need that Sync if we want to handle failures
           * for any reason other than !viewable. That's unlikely, but maybe
           * we'll BadAlloc or something.)
           */
          priv->back_pixmap = None;
        }

      if (priv->back_pixmap == None)
        {
          meta_verbose ("Unable to get named pixmap for %p\n", self);
          goto out;
        }

      if (compositor->no_mipmaps)
        meta_shaped_texture_set_create_mipmaps (META_SHAPED_TEXTURE (priv->actor),
                                                FALSE);

      texture = COGL_TEXTURE (cogl_texture_pixmap_x11_new (ctx, priv->back_pixmap, FALSE, NULL));

      /*
       * This only works *after* actually setting the pixmap, so we have to
       * do it here.
       * See: http://bugzilla.clutter-project.org/show_bug.cgi?id=2236
       */
      if (G_UNLIKELY (!cogl_texture_pixmap_x11_is_using_tfp_extension (texture)))
        g_warning ("NOTE: Not using GLX TFP!\n");

      meta_shaped_texture_set_texture (META_SHAPED_TEXTURE (priv->actor), texture);
    }

  priv->needs_pixmap = FALSE;

 out:
  meta_error_trap_pop (display);
}

static void
check_needs_shadow (MetaWindowActor *self)
{
  if (!self->priv->window->display->shadows_enabled) {
      return;
  }

  MetaWindowActorPrivate *priv = self->priv;
  MetaShadow *old_shadow = NULL;
  MetaShadow **shadow_location;
  gboolean recompute_shadow;
  gboolean should_have_shadow;
  gboolean appears_focused;

  if (!priv->window->mapped && !priv->window->shaded)
    return;

  /* Calling meta_window_actor_has_shadow() here at every pre-paint is cheap
   * and avoids the need to explicitly handle window type changes, which
   * we would do if tried to keep track of when we might be adding or removing
   * a shadow more explicitly. We only keep track of changes to the *shape* of
   * the shadow with priv->recompute_shadow.
   */

  should_have_shadow = meta_window_actor_has_shadow (self);
  priv->should_have_shadow = should_have_shadow;
  appears_focused = meta_window_appears_focused (priv->window);

  if (appears_focused)
    {
      recompute_shadow = priv->recompute_focused_shadow;
      priv->recompute_focused_shadow = FALSE;
      shadow_location = &priv->focused_shadow;
    }
  else
    {
      recompute_shadow = priv->recompute_unfocused_shadow;
      priv->recompute_unfocused_shadow = FALSE;
      shadow_location = &priv->unfocused_shadow;
    }

  if (!should_have_shadow || recompute_shadow)
    {
      if (*shadow_location != NULL)
        {
          old_shadow = *shadow_location;
          *shadow_location = NULL;
        }
    }

  if (*shadow_location == NULL && should_have_shadow)
    {
      if (priv->shadow_shape == NULL)
        {
          if (priv->shape_region)
            priv->shadow_shape = meta_window_shape_new (priv->shape_region);
        }

      if (priv->shadow_shape != NULL)
        {
          MetaShadowFactory *factory = meta_shadow_factory_get_default ();
          const char *shadow_class = meta_window_actor_get_shadow_class (self);
          cairo_rectangle_int_t shape_bounds;

          meta_window_actor_get_shape_bounds (self, &shape_bounds);
          *shadow_location = meta_shadow_factory_get_shadow (factory,
                                                             priv->shadow_shape,
                                                             shape_bounds.width, shape_bounds.height,
                                                             shadow_class, appears_focused);
        }
    }

  if (old_shadow != NULL)
    meta_shadow_unref (old_shadow);
}

LOCAL_SYMBOL void
meta_window_actor_process_damage (MetaWindowActor    *self,
                                  XDamageNotifyEvent *event)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaCompositor *compositor = priv->window->display->compositor;

  priv->received_damage = TRUE;

  /* Drop damage event for unredirected windows */
  if (priv->unredirected)
    return;

  if (meta_window_is_fullscreen (priv->window) && g_list_last (compositor->windows)->data == self)
    {
      MetaRectangle window_rect;
      meta_window_get_outer_rect (priv->window, &window_rect);

      if (event->area.x == 0 &&
          event->area.y == 0 &&
          window_rect.width == event->area.width &&
          window_rect.height == event->area.height)
        priv->full_damage_frames_count++;
      else
        priv->full_damage_frames_count = 0;

      if (priv->full_damage_frames_count >= 100)
        priv->does_full_damage = TRUE;
    }

  if (priv->freeze_count)
    {
      /* The window is frozen due to an effect in progress: we ignore damage
       * here on the off chance that this will stop the corresponding
       * texture_from_pixmap from being update.
       *
       * needs_damage_all tracks that some unknown damage happened while the
       * window was frozen so that when the window becomes unfrozen we can
       * issue a full window update to cover any lost damage.
       *
       * It should be noted that this is an unreliable mechanism since it's
       * quite likely that drivers will aim to provide a zero-copy
       * implementation of the texture_from_pixmap extension and in those cases
       * any drawing done to the window is always immediately reflected in the
       * texture regardless of damage event handling.
       */
      priv->needs_damage_all = TRUE;
      return;
    }

  if (!priv->window->mapped || priv->needs_pixmap)
    return;

  update_area (self, event->area.x, event->area.y, event->area.width, event->area.height);
  priv->repaint_scheduled = meta_shaped_texture_update_area (META_SHAPED_TEXTURE (priv->actor),
                                   event->area.x,
                                   event->area.y,
                                   event->area.width,
                                   event->area.height,
                                   clutter_actor_has_mapped_clones (priv->actor) ? NULL : priv->unobscured_region);
}

LOCAL_SYMBOL void
meta_window_actor_sync_visibility (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;

  if (CLUTTER_ACTOR_IS_VISIBLE (self) != priv->visible)
    {
      if (priv->visible)
        clutter_actor_show (CLUTTER_ACTOR (self));
      else
        clutter_actor_hide (CLUTTER_ACTOR (self));
    }
}

static inline void
set_integral_bounding_rect (cairo_rectangle_int_t *rect,
                            double x, double y,
                            double width, double height)
{
  rect->x = floor(x);
  rect->y = floor(y);
  rect->width = ceil(x + width) - rect->x;
  rect->height = ceil(y + height) - rect->y;
}

static void
update_corners (MetaWindowActor   *self)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaRectangle outer = priv->window->frame->rect;
  MetaFrameBorders borders;
  cairo_rectangle_int_t corner_rects[4];
  cairo_region_t *corner_region;
  cairo_path_t *corner_path;
  float top_left, top_right, bottom_left, bottom_right;
  float x, y;

  /* need these to build a path */
  cairo_t *cr;
  cairo_surface_t *surface;

  meta_frame_calc_borders (priv->window->frame, &borders);

  outer.width -= borders.invisible.left + borders.invisible.right;
  outer.height -= borders.invisible.top  + borders.invisible.bottom;

  meta_frame_get_corner_radiuses (priv->window->frame,
                                  &top_left,
                                  &top_right,
                                  &bottom_left,
                                  &bottom_right);

  /* Unfortunately, cairo does not allow us to create a context
   * without a surface. Create a 0x0 image surface to "paint to"
   * so we can get the path. */
  surface = cairo_image_surface_create (CAIRO_FORMAT_A8,
                                        0, 0);

  cr = cairo_create (surface);

  /* top left */
  x = borders.invisible.left;
  y = borders.invisible.top;

  set_integral_bounding_rect (&corner_rects[0],
                              x, y, top_left, top_left);

  cairo_arc (cr,
             x + top_left,
             y + top_left,
             top_left,
             0, M_PI*2);


  /* top right */
  x = x + outer.width - top_right;

  set_integral_bounding_rect (&corner_rects[1],
                              x, y, top_right, top_right);

  cairo_arc (cr,
             x,
             y + top_right,
             top_right,
             0, M_PI*2);

  /* bottom right */
  x = borders.invisible.left + outer.width - bottom_right;
  y = y + outer.height - bottom_right;

  set_integral_bounding_rect (&corner_rects[2],
                              x, y, bottom_right, bottom_right);

  cairo_arc (cr,
             x,
             y,
             bottom_right,
             0, M_PI*2);

  /* bottom left */
  x = borders.invisible.left;
  y = borders.invisible.top + outer.height - bottom_left;

  set_integral_bounding_rect (&corner_rects[3],
                              x, y, bottom_left, bottom_left);

  cairo_arc (cr,
             x + bottom_left,
             y,
             bottom_left,
             0, M_PI*2);

  corner_path = cairo_copy_path (cr);

  cairo_surface_destroy (surface);
  cairo_destroy (cr);

  corner_region = cairo_region_create_rectangles (corner_rects, 4);

  meta_shaped_texture_set_overlay_path (META_SHAPED_TEXTURE (priv->actor),
                                        corner_region, corner_path);


  cairo_region_destroy (corner_region);
}

static void
check_needs_reshape (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaScreen *screen = priv->screen;
  MetaDisplay *display = meta_screen_get_display (screen);
  cairo_region_t *region = NULL;
  cairo_rectangle_int_t client_area;
  gboolean full_mask_reset = priv->window->fullscreen;

  if ((!priv->window->mapped && !priv->window->shaded) || !priv->needs_reshape)
    return;

  g_clear_pointer (&priv->shape_region, cairo_region_destroy);
  g_clear_pointer (&priv->shadow_shape, meta_window_shape_unref);
  g_clear_pointer (&priv->opaque_region, cairo_region_destroy);

  meta_window_get_client_area_rect (priv->window, &client_area);

  if (priv->window->frame)
    region = meta_window_get_frame_bounds (priv->window);

  if (region != NULL)
    {
      /* This returns the window's internal frame bounds region,
       * so we need to copy it because we modify it below. */
      region = cairo_region_copy (region);
    }
  else
    {
      /* If we have no region, we have no frame. We have no frame,
       * so just use the client_area instead */
      region = cairo_region_create_rectangle (&client_area);
    }

#ifdef HAVE_SHAPE
  if (priv->window->has_shape)
    {
      Display *xdisplay = meta_display_get_xdisplay (display);
      XRectangle *rects;
      int n_rects, ordering;

      /* Punch out client area. */
      cairo_region_subtract_rectangle (region, &client_area);

      meta_error_trap_push (display);
      rects = XShapeGetRectangles (xdisplay,
                                   priv->window->xwindow,
                                   ShapeBounding,
                                   &n_rects,
                                   &ordering);
      meta_error_trap_pop (display);

      if (rects)
        {
          int i;
          for (i = 0; i < n_rects; i ++)
            {
              cairo_rectangle_int_t rect = { rects[i].x + client_area.x,
                                             rects[i].y + client_area.y,
                                             rects[i].width,
                                             rects[i].height };
              cairo_region_union_rectangle (region, &rect);
            }
          XFree (rects);
        }
    }
#endif

  if (priv->argb32 && priv->window->opaque_region != NULL)
    {
      /* The opaque region is defined to be a part of the
       * window which ARGB32 will always paint with opaque
       * pixels. For these regions, we want to avoid painting
       * windows and shadows beneath them.
       *
       * If the client gives bad coordinates where it does not
       * fully paint, the behavior is defined by the specification
       * to be undefined, and considered a client bug. In mutter's
       * case, graphical glitches will occur.
       */
      priv->opaque_region = cairo_region_copy (priv->window->opaque_region);
      cairo_region_translate (priv->opaque_region, client_area.x, client_area.y);
      cairo_region_intersect (priv->opaque_region, region);
    }
  else if (priv->argb32)
    priv->opaque_region = NULL;
  else
    priv->opaque_region = cairo_region_reference (region);

  if (priv->window->frame)
    update_corners (self);
  else if (priv->window->has_shape && priv->reshapes == 1)
    full_mask_reset = TRUE;
  else if (priv->reshapes < 2)
    priv->reshapes++;

  if (priv->window->frame != NULL || priv->window->has_shape)
    meta_window_actor_reset_mask_texture (self, region, full_mask_reset);

  meta_window_actor_update_shape_region (self, region);

  cairo_region_destroy (region);

  priv->needs_reshape = FALSE;
  meta_window_actor_invalidate_shadow (self);
}

LOCAL_SYMBOL void
meta_window_actor_update_shape (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;

  priv->needs_reshape = TRUE;

  if (is_frozen (self))
    return;

  clutter_actor_queue_redraw (priv->actor);
}

LOCAL_SYMBOL void
meta_window_actor_handle_updates (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaScreen          *screen   = priv->screen;
  MetaDisplay         *display  = meta_screen_get_display (screen);
  Display             *xdisplay = meta_display_get_xdisplay (display);

  if (is_frozen (self))
    {
      /* The window is frozen due to a pending animation: we'll wait until
       * the animation finishes to reshape and repair the window */
      return;
    }

  if (priv->unredirected)
    {
      /* Nothing to do here until/if the window gets redirected again */
      return;
    }

  if (!priv->visible && !priv->needs_pixmap)
    return;

  if (priv->received_damage)
    {
      meta_error_trap_push (display);
      XDamageSubtract (xdisplay, priv->damage, None, None);
      meta_error_trap_pop (display);

      priv->received_damage = FALSE;
    }

  check_needs_pixmap (self);
  check_needs_reshape (self);
  check_needs_shadow (self);
}

void
meta_window_actor_pre_paint (MetaWindowActor *self)
{
  if (meta_window_actor_is_destroyed (self))
    return;

  meta_window_actor_handle_updates (self);

  assign_frame_counter_to_frames (self);
}

static void
do_send_frame_drawn (MetaWindowActor *self, FrameData *frame)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaDisplay *display = meta_screen_get_display (priv->screen);
  MetaWindow *window = meta_window_actor_get_meta_window (self);
  Display *xdisplay = meta_display_get_xdisplay (display);

  XClientMessageEvent ev = { 0, };

  frame->frame_drawn_time = meta_compositor_monotonic_time_to_server_time (display,
                                                                           g_get_monotonic_time ());
  priv->frame_drawn_time = frame->frame_drawn_time;

  ev.type = ClientMessage;
  ev.window = meta_window_get_xwindow (window);
  ev.message_type = display->atom__NET_WM_FRAME_DRAWN;
  ev.format = 32;
  ev.data.l[0] = frame->sync_request_serial & G_GUINT64_CONSTANT(0xffffffff);
  ev.data.l[1] = frame->sync_request_serial >> 32;
  ev.data.l[2] = frame->frame_drawn_time & G_GUINT64_CONSTANT(0xffffffff);
  ev.data.l[3] = frame->frame_drawn_time >> 32;

  meta_error_trap_push (display);
  XSendEvent (xdisplay, ev.window, False, 0, (XEvent*) &ev);
  XFlush (xdisplay);
  meta_error_trap_pop (display);
}


void
meta_window_actor_post_paint (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;

  priv->repaint_scheduled = FALSE;

  if (meta_window_actor_is_destroyed (self))
    return;

  /* If the window had damage, but wasn't actually redrawn because
   * it is obscured, we should wait until timer expiration before
   * sending _NET_WM_FRAME_* messages.
   */
  if (priv->send_frame_messages_timer == 0 &&
      priv->needs_frame_drawn)
    {
      GList *l;

      for (l = priv->frames; l; l = l->next)
        {
          FrameData *frame = l->data;

          if (frame->frame_drawn_time == 0)
            do_send_frame_drawn (self, frame);
        }

      priv->needs_frame_drawn = FALSE;
    }
}

static void
do_send_frame_timings (MetaWindowActor  *self,
                       FrameData        *frame,
                       gint             refresh_interval,
                       gint64           presentation_time)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaDisplay *display = meta_screen_get_display (priv->screen);
  MetaWindow *window = meta_window_actor_get_meta_window (self);
  Display *xdisplay = meta_display_get_xdisplay (display);

  XClientMessageEvent ev = { 0, };

  ev.type = ClientMessage;
  ev.window = meta_window_get_xwindow (window);
  ev.message_type = display->atom__NET_WM_FRAME_TIMINGS;
  ev.format = 32;
  ev.data.l[0] = frame->sync_request_serial & G_GUINT64_CONSTANT(0xffffffff);
  ev.data.l[1] = frame->sync_request_serial >> 32;

  if (presentation_time != 0)
    {
      gint64 presentation_time_server = meta_compositor_monotonic_time_to_server_time (display,
                                                                                       presentation_time);
      gint64 presentation_time_offset = presentation_time_server - frame->frame_drawn_time;
      if (presentation_time_offset == 0)
        presentation_time_offset = 1;

      if ((gint32)presentation_time_offset == presentation_time_offset)
        ev.data.l[2] = presentation_time_offset;
    }

  ev.data.l[3] = refresh_interval;
  ev.data.l[4] = 1000 * META_SYNC_DELAY;

  meta_error_trap_push (display);
  XSendEvent (xdisplay, ev.window, False, 0, (XEvent*) &ev);
  XFlush (xdisplay);
  meta_error_trap_pop (display);
}

static void
send_frame_timings (MetaWindowActor  *self,
                    FrameData        *frame,
                    CoglFrameInfo    *frame_info,
                    gint64            presentation_time)
{
  MetaWindow *window = meta_window_actor_get_meta_window (self);
  float refresh_rate;
  int refresh_interval;

  refresh_rate = window->monitor->refresh_rate;
  /* 0.0 is a flag for not known, but sanity-check against other odd numbers */
  if (refresh_rate >= 1.0)
    refresh_interval = (int) (0.5 + 1000000 / refresh_rate);
  else
    refresh_interval = 0;

  do_send_frame_timings (self, frame, refresh_interval, presentation_time);
}

void
meta_window_actor_frame_complete (MetaWindowActor  *self,
                                  ClutterFrameInfo *frame_info,
                                  gint64            presentation_time)
{
  MetaWindowActorPrivate *priv = self->priv;
  GList *l;

  if (meta_window_actor_is_destroyed (self))
    return;

  for (l = priv->frames; l;)
    {
      GList *l_next = l->next;
      FrameData *frame = l->data;
      gint64 frame_counter = frame_info->frame_counter;

      if (frame->frame_counter != -1 && frame->frame_counter <= frame_counter)
        {
          if (G_UNLIKELY (frame->frame_drawn_time == 0))
            g_warning ("%s: Frame has assigned frame counter but no frame drawn time",
                       priv->window->desc);
          if (G_UNLIKELY (frame->frame_counter < frame_counter))
            g_warning ("%s: frame_complete callback never occurred for frame %" G_GINT64_FORMAT,
                       priv->window->desc, frame->frame_counter);

          priv->frames = g_list_delete_link (priv->frames, l);
          send_frame_timings (self, frame, frame_info, presentation_time);
          frame_data_free (frame);
        }

      l = l_next;
    }
}

LOCAL_SYMBOL void
meta_window_actor_invalidate_shadow (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;

  priv->recompute_focused_shadow = TRUE;
  priv->recompute_unfocused_shadow = TRUE;

  if (is_frozen (self))
    return;

  clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
}

LOCAL_SYMBOL void
meta_window_actor_update_opacity (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaDisplay *display = meta_screen_get_display (priv->screen);
  MetaCompositor *compositor = meta_display_get_compositor (display);
  Window xwin = meta_window_get_xwindow (priv->window);
  gulong value;
  guint8 opacity;

  if (meta_prop_get_cardinal (display, xwin,
                              compositor->atom_net_wm_window_opacity,
                              &value))
    {
      opacity = (guint8)((gfloat)value * 255.0 / ((gfloat)0xffffffff));
    }
  else
    opacity = 255;

  self->priv->opacity = opacity;
  clutter_actor_set_opacity (self->priv->actor, opacity);
}

void
meta_window_actor_set_updates_frozen (MetaWindowActor *self,
                                      gboolean         updates_frozen)
{
  MetaWindowActorPrivate *priv = self->priv;

  updates_frozen = updates_frozen != FALSE;

  if (priv->updates_frozen != updates_frozen)
    {
      priv->updates_frozen = updates_frozen;
      if (updates_frozen)
        meta_window_actor_freeze (self);
      else
        meta_window_actor_thaw (self);
    }
}
