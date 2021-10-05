/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * SECTION:meta-window-actor
 * @title: MetaWindowActor
 * @short_description: An actor representing a top-level window in the scene
 *   graph
 *
 * #MetaWindowActor is a #ClutterActor that adds a notion of a window to the
 * Clutter scene graph. It contains a #MetaWindow which provides the windowing
 * API, and the #MetaCompositor that handles it.  For the actual content of the
 * window, it contains a #MetaSurfaceActor.
 *
 * #MetaWindowActor takes care of the rendering features you need for your
 * window. For example, it will take the windows' requested opacity and use
 * that for clutter_actor_set_opacity(). Furthermore, it will also draw a
 * shadow around the window (using #MetaShadow) and deal with synchronization
 * between events of the window and the actual render loop. See
 * MetaWindowActor::first-frame for an example of the latter.
 */

#include "config.h"

#include <gdk/gdk.h>
#include <math.h>
#include <string.h>

#include "backends/meta-screen-cast-window.h"
#include "compositor/compositor-private.h"
#include "compositor/meta-cullable.h"
#include "compositor/meta-shaped-texture-private.h"
#include "compositor/meta-surface-actor-x11.h"
#include "compositor/meta-surface-actor.h"
#include "compositor/meta-window-actor-private.h"
#include "core/boxes-private.h"
#include "core/window-private.h"
#include "meta/window.h"

#ifdef HAVE_WAYLAND
#include "compositor/meta-surface-actor-wayland.h"
#include "wayland/meta-wayland-surface.h"
#endif

typedef enum
{
  INITIALLY_FROZEN,
  DRAWING_FIRST_FRAME,
  EMITTED_FIRST_FRAME
} FirstFrameState;

typedef struct _MetaWindowActorPrivate
{
  MetaWindow *window;
  MetaCompositor *compositor;

  MetaSurfaceActor *surface;

  int geometry_scale;

  /*
   * These need to be counters rather than flags, since more plugins
   * can implement same effect; the practicality of stacking effects
   * might be dubious, but we have to at least handle it correctly.
   */
  gint              minimize_in_progress;
  gint              unminimize_in_progress;
  gint              size_change_in_progress;
  gint              map_in_progress;
  gint              destroy_in_progress;

  guint             freeze_count;

  guint		    visible                : 1;
  guint		    disposed               : 1;

  guint		    needs_destroy	   : 1;

  guint             updates_frozen         : 1;
  guint             first_frame_state      : 2; /* FirstFrameState */
} MetaWindowActorPrivate;

enum
{
  FIRST_FRAME,
  EFFECTS_COMPLETED,
  DAMAGED,
  THAWED,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

enum
{
  PROP_META_WINDOW = 1,
};

static void meta_window_actor_dispose    (GObject *object);
static void meta_window_actor_constructed (GObject *object);
static void meta_window_actor_set_property (GObject       *object,
                                            guint         prop_id,
                                            const GValue *value,
                                            GParamSpec   *pspec);
static void meta_window_actor_get_property (GObject      *object,
                                            guint         prop_id,
                                            GValue       *value,
                                            GParamSpec   *pspec);

static void meta_window_actor_real_assign_surface_actor (MetaWindowActor  *self,
                                                         MetaSurfaceActor *surface_actor);

static void cullable_iface_init (MetaCullableInterface *iface);

static void screen_cast_window_iface_init (MetaScreenCastWindowInterface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (MetaWindowActor, meta_window_actor, CLUTTER_TYPE_ACTOR,
                                  G_ADD_PRIVATE (MetaWindowActor)
                                  G_IMPLEMENT_INTERFACE (META_TYPE_CULLABLE, cullable_iface_init)
                                  G_IMPLEMENT_INTERFACE (META_TYPE_SCREEN_CAST_WINDOW, screen_cast_window_iface_init));

static void
meta_window_actor_class_init (MetaWindowActorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec   *pspec;

  object_class->dispose      = meta_window_actor_dispose;
  object_class->set_property = meta_window_actor_set_property;
  object_class->get_property = meta_window_actor_get_property;
  object_class->constructed  = meta_window_actor_constructed;

  klass->assign_surface_actor = meta_window_actor_real_assign_surface_actor;

  /**
   * MetaWindowActor::first-frame:
   * @actor: the #MetaWindowActor instance
   *
   * The ::first-frame signal will be emitted the first time a frame
   * of window contents has been drawn by the application and Mutter
   * has had the chance to drawn that frame to the screen. If the
   * window starts off initially hidden, obscured, or on on a
   * different workspace, the ::first-frame signal will be emitted
   * even though the user doesn't see the contents.
   *
   * MetaDisplay::window-created is a good place to connect to this
   * signal - at that point, the MetaWindowActor for the window
   * exists, but the window has reliably not yet been drawn.
   * Connecting to an existing window that has already been drawn to
   * the screen is not useful.
   */
  signals[FIRST_FRAME] =
    g_signal_new ("first-frame",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  /**
   * MetaWindowActor::effects-completed:
   * @actor: the #MetaWindowActor instance
   *
   * The ::effects-completed signal will be emitted once all pending compositor
   * effects are completed.
   */
  signals[EFFECTS_COMPLETED] =
    g_signal_new ("effects-completed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  /**
   * MetaWindowActor::damaged:
   * @actor: the #MetaWindowActor instance
   *
   * Notify that one or more of the surfaces of the window have been damaged.
   */
  signals[DAMAGED] =
    g_signal_new ("damaged",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  /**
   * MetaWindowActor::thawed:
   * @actor: the #MetaWindowActor instance
   */
  signals[THAWED] =
    g_signal_new ("thawed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  pspec = g_param_spec_object ("meta-window",
                               "MetaWindow",
                               "The displayed MetaWindow",
                               META_TYPE_WINDOW,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_property (object_class,
                                   PROP_META_WINDOW,
                                   pspec);
}

static void
meta_window_actor_init (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  priv->geometry_scale = 1;
}

static void
window_appears_focused_notify (MetaWindow *mw,
                               GParamSpec *arg1,
                               gpointer    data)
{
  clutter_actor_queue_redraw (CLUTTER_ACTOR (data));
}

gboolean
meta_window_actor_is_opaque (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaWindow *window = priv->window;

  if (window->opacity != 0xff)
    return FALSE;

  if (!priv->surface)
    return FALSE;

  return meta_surface_actor_is_opaque (priv->surface);
}

gboolean
meta_window_actor_is_frozen (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  return priv->surface == NULL || priv->freeze_count > 0;
}

void
meta_window_actor_update_regions (MetaWindowActor *self)
{
  META_WINDOW_ACTOR_GET_CLASS (self)->update_regions (self);
}

static void
meta_window_actor_set_frozen (MetaWindowActor *self,
                              gboolean         frozen)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  if (meta_surface_actor_is_frozen (priv->surface) == frozen)
    return;

  meta_surface_actor_set_frozen (priv->surface, frozen);

  META_WINDOW_ACTOR_GET_CLASS (self)->set_frozen (self, frozen);
}

static void
meta_window_actor_freeze (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  if (priv->freeze_count == 0 && priv->surface)
    meta_window_actor_set_frozen (self, TRUE);

  priv->freeze_count ++;
}

static void
meta_window_actor_sync_thawed_state (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  if (priv->first_frame_state == INITIALLY_FROZEN)
    priv->first_frame_state = DRAWING_FIRST_FRAME;

  if (priv->surface)
    meta_window_actor_set_frozen (self, FALSE);

  /* We sometimes ignore moves and resizes on frozen windows */
  meta_window_actor_sync_actor_geometry (self, FALSE);
}

static void
meta_window_actor_thaw (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  if (priv->freeze_count <= 0)
    g_error ("Error in freeze/thaw accounting");

  priv->freeze_count--;
  if (priv->freeze_count > 0)
    return;

  /* We still might be frozen due to lack of a MetaSurfaceActor */
  if (meta_window_actor_is_frozen (self))
    return;

  meta_window_actor_sync_thawed_state (self);

  g_signal_emit (self, signals[THAWED], 0);
}

static void
meta_window_actor_real_assign_surface_actor (MetaWindowActor  *self,
                                             MetaSurfaceActor *surface_actor)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  g_clear_object (&priv->surface);
  priv->surface = g_object_ref_sink (surface_actor);

  if (meta_window_actor_is_frozen (self))
    meta_window_actor_set_frozen (self, TRUE);
  else
    meta_window_actor_sync_thawed_state (self);
}

void
meta_window_actor_assign_surface_actor (MetaWindowActor  *self,
                                        MetaSurfaceActor *surface_actor)
{
  META_WINDOW_ACTOR_GET_CLASS (self)->assign_surface_actor (self,
                                                            surface_actor);
}

static void
init_surface_actor (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaWindow *window = priv->window;
  MetaSurfaceActor *surface_actor;

  if (!meta_is_wayland_compositor ())
    surface_actor = meta_surface_actor_x11_new (window);
#ifdef HAVE_WAYLAND
  else if (window->surface)
    surface_actor = meta_wayland_surface_get_actor (window->surface);
#endif
  else
    surface_actor = NULL;

  if (surface_actor)
    meta_window_actor_assign_surface_actor (self, surface_actor);
}

static void
meta_window_actor_constructed (GObject *object)
{
  MetaWindowActor *self = META_WINDOW_ACTOR (object);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaWindow *window = priv->window;

  priv->compositor = window->display->compositor;

  /* Hang our compositor window state off the MetaWindow for fast retrieval */
  meta_window_set_compositor_private (window, object);

  init_surface_actor (self);

  meta_window_actor_update_opacity (self);

  meta_window_actor_sync_updates_frozen (self);

  if (meta_window_actor_is_frozen (self))
    priv->first_frame_state = INITIALLY_FROZEN;
  else
    priv->first_frame_state = DRAWING_FIRST_FRAME;

  meta_window_actor_sync_actor_geometry (self, priv->window->placed);
}

static void
meta_window_actor_dispose (GObject *object)
{
  MetaWindowActor *self = META_WINDOW_ACTOR (object);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaCompositor *compositor = priv->compositor;

  if (priv->disposed)
    {
      G_OBJECT_CLASS (meta_window_actor_parent_class)->dispose (object);
      return;
    }

  priv->disposed = TRUE;

  meta_compositor_remove_window_actor (compositor, self);

  g_clear_object (&priv->window);

  if (priv->surface)
    {
      clutter_actor_remove_child (CLUTTER_ACTOR (self),
                                  CLUTTER_ACTOR (priv->surface));
      g_clear_object (&priv->surface);
    }

  G_OBJECT_CLASS (meta_window_actor_parent_class)->dispose (object);
}

static void
meta_window_actor_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  MetaWindowActor *self = META_WINDOW_ACTOR (object);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_META_WINDOW:
      priv->window = g_value_dup_object (value);
      g_signal_connect_object (priv->window, "notify::appears-focused",
                               G_CALLBACK (window_appears_focused_notify), self, 0);
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
  MetaWindowActor *self = META_WINDOW_ACTOR (object);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_META_WINDOW:
      g_value_set_object (value, priv->window);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/**
 * meta_window_actor_get_meta_window:
 * @self: a #MetaWindowActor
 *
 * Gets the #MetaWindow object that the the #MetaWindowActor is displaying
 *
 * Return value: (transfer none): the displayed #MetaWindow
 */
MetaWindow *
meta_window_actor_get_meta_window (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  return priv->window;
}

/**
 * meta_window_actor_get_texture:
 * @self: a #MetaWindowActor
 *
 * Gets the ClutterActor that is used to display the contents of the window,
 * or NULL if no texture is shown yet, because the window is not mapped.
 *
 * Return value: (transfer none): the #ClutterActor for the contents
 */
MetaShapedTexture *
meta_window_actor_get_texture (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  if (priv->surface)
    return meta_surface_actor_get_texture (priv->surface);
  else
    return NULL;
}

/**
 * meta_window_actor_get_surface:
 * @self: a #MetaWindowActor
 *
 * Gets the MetaSurfaceActor that draws the content of this window,
 * or NULL if there is no surface yet associated with this window.
 *
 * Return value: (transfer none): the #MetaSurfaceActor for the contents
 */
MetaSurfaceActor *
meta_window_actor_get_surface (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  return priv->surface;
}

/**
 * meta_window_actor_is_destroyed:
 * @self: a #MetaWindowActor
 *
 * Gets whether the X window that the actor was displaying has been destroyed
 *
 * Return value: %TRUE when the window is destroyed, otherwise %FALSE
 */
gboolean
meta_window_actor_is_destroyed (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  return priv->disposed || priv->needs_destroy;
}

void
meta_window_actor_queue_frame_drawn (MetaWindowActor *self,
                                     gboolean         no_delay_frame)
{
  META_WINDOW_ACTOR_GET_CLASS (self)->queue_frame_drawn (self,
                                                         no_delay_frame);
}

gboolean
meta_window_actor_effect_in_progress (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  return (priv->minimize_in_progress ||
          priv->size_change_in_progress ||
          priv->map_in_progress ||
          priv->destroy_in_progress);
}

static gboolean
is_freeze_thaw_effect (MetaPluginEffect event)
{
  switch (event)
  {
  case META_PLUGIN_DESTROY:
  case META_PLUGIN_SIZE_CHANGE:
    return TRUE;
    break;
  default:
    return FALSE;
  }
}

static gboolean
start_simple_effect (MetaWindowActor  *self,
                     MetaPluginEffect  event)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaCompositor *compositor = priv->compositor;
  MetaPluginManager *plugin_mgr =
    meta_compositor_get_plugin_manager (compositor);
  gint *counter = NULL;
  gboolean use_freeze_thaw = FALSE;

  g_assert (plugin_mgr != NULL);

  switch (event)
  {
  case META_PLUGIN_NONE:
    return FALSE;
  case META_PLUGIN_MINIMIZE:
    counter = &priv->minimize_in_progress;
    break;
  case META_PLUGIN_UNMINIMIZE:
    counter = &priv->unminimize_in_progress;
    break;
  case META_PLUGIN_MAP:
    counter = &priv->map_in_progress;
    break;
  case META_PLUGIN_DESTROY:
    counter = &priv->destroy_in_progress;
    break;
  case META_PLUGIN_SIZE_CHANGE:
  case META_PLUGIN_SWITCH_WORKSPACE:
    g_assert_not_reached ();
    break;
  }

  g_assert (counter);

  use_freeze_thaw = is_freeze_thaw_effect (event);

  if (use_freeze_thaw)
    meta_window_actor_freeze (self);

  (*counter)++;

  if (!meta_plugin_manager_event_simple (plugin_mgr, self, event))
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
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  if (priv->needs_destroy)
    {
      clutter_actor_destroy (CLUTTER_ACTOR (self));
      return;
    }

  g_signal_emit (self, signals[EFFECTS_COMPLETED], 0);
  meta_window_actor_sync_visibility (self);
  meta_window_actor_sync_actor_geometry (self, FALSE);
}

void
meta_window_actor_effect_completed (MetaWindowActor  *self,
                                    MetaPluginEffect  event)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  gboolean inconsistent = FALSE;

  /* NB: Keep in mind that when effects get completed it possible
   * that the corresponding MetaWindow may have be been destroyed.
   * In this case priv->window will == NULL */

  switch (event)
  {
  case META_PLUGIN_NONE:
    break;
  case META_PLUGIN_MINIMIZE:
    {
      priv->minimize_in_progress--;
      if (priv->minimize_in_progress < 0)
        {
          g_warning ("Error in minimize accounting.");
          priv->minimize_in_progress = 0;
          inconsistent = TRUE;
        }
    }
    break;
  case META_PLUGIN_UNMINIMIZE:
    {
      priv->unminimize_in_progress--;
      if (priv->unminimize_in_progress < 0)
       {
         g_warning ("Error in unminimize accounting.");
         priv->unminimize_in_progress = 0;
         inconsistent = TRUE;
       }
    }
    break;
  case META_PLUGIN_MAP:
    /*
     * Make sure that the actor is at the correct place in case
     * the plugin fscked.
     */
    priv->map_in_progress--;

    if (priv->map_in_progress < 0)
      {
        g_warning ("Error in map accounting.");
        priv->map_in_progress = 0;
        inconsistent = TRUE;
      }
    break;
  case META_PLUGIN_DESTROY:
    priv->destroy_in_progress--;

    if (priv->destroy_in_progress < 0)
      {
        g_warning ("Error in destroy accounting.");
        priv->destroy_in_progress = 0;
        inconsistent = TRUE;
      }
    break;
  case META_PLUGIN_SIZE_CHANGE:
    priv->size_change_in_progress--;
    if (priv->size_change_in_progress < 0)
      {
        g_warning ("Error in size change accounting.");
        priv->size_change_in_progress = 0;
        inconsistent = TRUE;
      }
    break;
  case META_PLUGIN_SWITCH_WORKSPACE:
    g_assert_not_reached ();
    break;
  }

  if (is_freeze_thaw_effect (event) && !inconsistent)
    meta_window_actor_thaw (self);

  if (!meta_window_actor_effect_in_progress (self))
    meta_window_actor_after_effects (self);
}

void
meta_window_actor_queue_destroy (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaWindow *window = priv->window;
  MetaWindowType window_type = meta_window_get_window_type (window);

  meta_window_set_compositor_private (window, NULL);

  META_WINDOW_ACTOR_GET_CLASS (self)->queue_destroy (self);

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

MetaWindowActorChanges
meta_window_actor_sync_actor_geometry (MetaWindowActor *self,
                                       gboolean         did_placement)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaRectangle window_rect;
  ClutterActor *actor = CLUTTER_ACTOR (self);
  MetaWindowActorChanges changes = 0;

  meta_window_get_buffer_rect (priv->window, &window_rect);

  /* When running as a Wayland compositor we catch size changes when new
   * buffers are attached */
  if (META_IS_SURFACE_ACTOR_X11 (priv->surface))
    meta_surface_actor_x11_set_size (META_SURFACE_ACTOR_X11 (priv->surface),
                                     window_rect.width, window_rect.height);

  /* Normally we want freezing a window to also freeze its position; this allows
   * windows to atomically move and resize together, either under app control,
   * or because the user is resizing from the left/top. But on initial placement
   * we need to assign a position, since immediately after the window
   * is shown, the map effect will go into effect and prevent further geometry
   * updates.
   */
  if (meta_window_actor_is_frozen (self) && !did_placement)
    return META_WINDOW_ACTOR_CHANGE_POSITION | META_WINDOW_ACTOR_CHANGE_SIZE;

  if (meta_window_actor_effect_in_progress (self))
    return META_WINDOW_ACTOR_CHANGE_POSITION | META_WINDOW_ACTOR_CHANGE_SIZE;

  if (clutter_actor_has_allocation (actor))
    {
      ClutterActorBox box;
      float old_x, old_y;
      float old_width, old_height;

      clutter_actor_get_allocation_box (actor, &box);

      old_x = box.x1;
      old_y = box.y1;
      old_width = box.x2 - box.x1;
      old_height = box.y2 - box.y1;

      if (old_x != window_rect.x || old_y != window_rect.y)
        changes |= META_WINDOW_ACTOR_CHANGE_POSITION;

      if (old_width != window_rect.width || old_height != window_rect.height)
        changes |= META_WINDOW_ACTOR_CHANGE_SIZE;
    }
  else
    {
      changes = META_WINDOW_ACTOR_CHANGE_POSITION | META_WINDOW_ACTOR_CHANGE_SIZE;
    }

  if (changes & META_WINDOW_ACTOR_CHANGE_POSITION)
    clutter_actor_set_position (actor, window_rect.x, window_rect.y);

  if (changes & META_WINDOW_ACTOR_CHANGE_SIZE)
    clutter_actor_set_size (actor, window_rect.width, window_rect.height);

  return changes;
}

void
meta_window_actor_show (MetaWindowActor   *self,
                        MetaCompEffect     effect)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaCompositor *compositor = priv->compositor;
  MetaPluginEffect event;

  g_return_if_fail (!priv->visible);

  priv->visible = TRUE;

  switch (effect)
    {
    case META_COMP_EFFECT_CREATE:
      event = META_PLUGIN_MAP;
      break;
    case META_COMP_EFFECT_UNMINIMIZE:
      event = META_PLUGIN_UNMINIMIZE;
      break;
    case META_COMP_EFFECT_NONE:
      event = META_PLUGIN_NONE;
      break;
    default:
      g_assert_not_reached();
    }

  if (event == META_PLUGIN_MAP)
    meta_window_actor_sync_actor_geometry (self, TRUE);

  if (meta_compositor_is_switching_workspace (compositor) ||
      !start_simple_effect (self, event))
    {
      clutter_actor_show (CLUTTER_ACTOR (self));
    }
}

void
meta_window_actor_hide (MetaWindowActor *self,
                        MetaCompEffect   effect)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaCompositor *compositor = priv->compositor;
  MetaPluginEffect event;

  g_return_if_fail (priv->visible);

  priv->visible = FALSE;

  /* If a plugin is animating a workspace transition, we have to
   * hold off on hiding the window, and do it after the workspace
   * switch completes
   */
  if (meta_compositor_is_switching_workspace (compositor))
    return;

  switch (effect)
    {
    case META_COMP_EFFECT_DESTROY:
      event = META_PLUGIN_DESTROY;
      break;
    case META_COMP_EFFECT_MINIMIZE:
      event = META_PLUGIN_MINIMIZE;
      break;
    case META_COMP_EFFECT_NONE:
      event = META_PLUGIN_NONE;
      break;
    default:
      g_assert_not_reached();
    }

  if (!start_simple_effect (self, event))
    clutter_actor_hide (CLUTTER_ACTOR (self));
}

void
meta_window_actor_size_change (MetaWindowActor    *self,
                               MetaSizeChange      which_change,
                               MetaRectangle      *old_frame_rect,
                               MetaRectangle      *old_buffer_rect)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaCompositor *compositor = priv->compositor;
  MetaPluginManager *plugin_mgr =
    meta_compositor_get_plugin_manager (compositor);

  priv->size_change_in_progress++;
  meta_window_actor_freeze (self);

  if (!meta_plugin_manager_event_size_change (plugin_mgr, self,
                                              which_change, old_frame_rect, old_buffer_rect))
    {
      priv->size_change_in_progress--;
      meta_window_actor_thaw (self);
    }
}

#if 0
/* Print out a region; useful for debugging */
static void
print_region (cairo_region_t *region)
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

#if 0
/* Dump a region to a PNG file; useful for debugging */
static void
see_region (cairo_region_t *region,
            int             width,
            int             height,
            char           *filename)
{
  cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_A8, width, height);
  cairo_t *cr = cairo_create (surface);

  gdk_cairo_region (cr, region);
  cairo_fill (cr);

  cairo_surface_write_to_png (surface, filename);
  cairo_destroy (cr);
  cairo_surface_destroy (surface);
}
#endif


static void
meta_window_actor_cull_out (MetaCullable   *cullable,
                            cairo_region_t *unobscured_region,
                            cairo_region_t *clip_region)
{
  meta_cullable_cull_out_children (cullable, unobscured_region, clip_region);
}

static void
meta_window_actor_reset_culling (MetaCullable *cullable)
{
  meta_cullable_reset_culling_children (cullable);
}

static void
cullable_iface_init (MetaCullableInterface *iface)
{
  iface->cull_out = meta_window_actor_cull_out;
  iface->reset_culling = meta_window_actor_reset_culling;
}

void
meta_window_actor_sync_visibility (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  if (CLUTTER_ACTOR_IS_VISIBLE (self) != priv->visible)
    {
      if (priv->visible)
        clutter_actor_show (CLUTTER_ACTOR (self));
      else
        clutter_actor_hide (CLUTTER_ACTOR (self));
    }
}

void
meta_window_actor_pre_paint (MetaWindowActor *self)
{
  if (meta_window_actor_is_destroyed (self))
    return;

  META_WINDOW_ACTOR_GET_CLASS (self)->pre_paint (self);
}

void
meta_window_actor_post_paint (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

  META_WINDOW_ACTOR_GET_CLASS (self)->post_paint (self);

  if (meta_window_actor_is_destroyed (self))
    return;

  if (priv->first_frame_state == DRAWING_FIRST_FRAME)
    {
      priv->first_frame_state = EMITTED_FIRST_FRAME;
      g_signal_emit (self, signals[FIRST_FRAME], 0);
    }
}

void
meta_window_actor_frame_complete (MetaWindowActor  *self,
                                  ClutterFrameInfo *frame_info,
                                  gint64            presentation_time)
{
  META_WINDOW_ACTOR_GET_CLASS (self)->frame_complete (self,
                                                      frame_info,
                                                      presentation_time);
}

void
meta_window_actor_update_opacity (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaWindow *window = priv->window;

  if (priv->surface)
    clutter_actor_set_opacity (CLUTTER_ACTOR (priv->surface), window->opacity);
}

static void
meta_window_actor_set_updates_frozen (MetaWindowActor *self,
                                      gboolean         updates_frozen)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);

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

void
meta_window_actor_sync_updates_frozen (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (self);
  MetaWindow *window = priv->window;

  meta_window_actor_set_updates_frozen (self, meta_window_updates_are_frozen (window));
}

MetaWindowActor *
meta_window_actor_from_window (MetaWindow *window)
{
  return META_WINDOW_ACTOR (meta_window_get_compositor_private (window));
}

void
meta_window_actor_set_geometry_scale (MetaWindowActor *window_actor,
                                      int              geometry_scale)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (window_actor);
  CoglMatrix child_transform;

  if (priv->geometry_scale == geometry_scale)
    return;

  priv->geometry_scale = geometry_scale;

  cogl_matrix_init_identity (&child_transform);
  cogl_matrix_scale (&child_transform, geometry_scale, geometry_scale, 1);
  clutter_actor_set_child_transform (CLUTTER_ACTOR (window_actor),
                                     &child_transform);
}

int
meta_window_actor_get_geometry_scale (MetaWindowActor *window_actor)
{
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (window_actor);

  return priv->geometry_scale;
}

static void
meta_window_actor_get_buffer_bounds (MetaScreenCastWindow *screen_cast_window,
                                     MetaRectangle        *bounds)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (screen_cast_window);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (window_actor);
  MetaShapedTexture *stex;
  int buffer_scale;

  stex = meta_surface_actor_get_texture (priv->surface);
  buffer_scale = meta_shaped_texture_get_buffer_scale (stex);
  *bounds = (MetaRectangle) {
    .width = meta_shaped_texture_get_width (stex) * buffer_scale,
    .height = meta_shaped_texture_get_height (stex) * buffer_scale,
  };
}

static void
meta_window_actor_transform_relative_position (MetaScreenCastWindow *screen_cast_window,
                                               double                x,
                                               double                y,
                                               double               *x_out,
                                               double               *y_out)

{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (screen_cast_window);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (window_actor);
  MetaRectangle bounds;
  graphene_point3d_t v1 = { 0.f, }, v2 = { 0.f, };

  meta_window_actor_get_buffer_bounds (screen_cast_window, &bounds);

  v1.x = CLAMP ((float) x,
                bounds.x,
                bounds.x + bounds.width);
  v1.y = CLAMP ((float) y,
                bounds.y,
                bounds.y + bounds.height);

  clutter_actor_apply_transform_to_point (CLUTTER_ACTOR (priv->surface),
                                          &v1,
                                          &v2);

  *x_out = (double) v2.x;
  *y_out = (double) v2.y;
}

static gboolean
meta_window_actor_transform_cursor_position (MetaScreenCastWindow *screen_cast_window,
                                             MetaCursorSprite     *cursor_sprite,
                                             graphene_point_t     *cursor_position,
                                             float                *out_cursor_scale,
                                             graphene_point_t     *out_relative_cursor_position)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (screen_cast_window);
  MetaWindowActorPrivate *priv =
    meta_window_actor_get_instance_private (window_actor);
  MetaWindow *window;

  window = priv->window;
  if (!meta_window_has_pointer (window))
    return FALSE;

  if (cursor_sprite &&
      meta_cursor_sprite_get_cogl_texture (cursor_sprite) &&
      out_cursor_scale)
    {
      MetaShapedTexture *stex;
      double texture_scale;
      float cursor_texture_scale;

      stex = meta_surface_actor_get_texture (priv->surface);
      texture_scale = meta_shaped_texture_get_buffer_scale (stex);
      cursor_texture_scale = meta_cursor_sprite_get_texture_scale (cursor_sprite);

      *out_cursor_scale = texture_scale / cursor_texture_scale;
    }

  if (out_relative_cursor_position)
    {
      clutter_actor_transform_stage_point (CLUTTER_ACTOR (priv->surface),
                                           cursor_position->x,
                                           cursor_position->y,
                                           &out_relative_cursor_position->x,
                                           &out_relative_cursor_position->y);
    }

  return TRUE;
}

static void
meta_window_actor_capture_into (MetaScreenCastWindow *screen_cast_window,
                                MetaRectangle        *bounds,
                                uint8_t              *data)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (screen_cast_window);
  cairo_surface_t *image;
  uint8_t *cr_data;
  int cr_stride;
  int cr_width;
  int cr_height;
  int bpp = 4;

  if (meta_window_actor_is_destroyed (window_actor))
    return;

  image = meta_window_actor_get_image (window_actor, bounds);
  cr_data = cairo_image_surface_get_data (image);
  cr_width = cairo_image_surface_get_width (image);
  cr_height = cairo_image_surface_get_height (image);
  cr_stride = cairo_image_surface_get_stride (image);

  if (cr_width == bounds->width && cr_height == bounds->height)
    {
      memcpy (data, cr_data, cr_height * cr_stride);
    }
  else
    {
      int width = MIN (bounds->width, cr_width);
      int height = MIN (bounds->height, cr_height);
      int stride = width * bpp;
      uint8_t *src, *dst;

      src = cr_data;
      dst = data;

      for (int i = 0; i < height; i++)
        {
          memcpy (dst, src, stride);
          if (width < bounds->width)
            memset (dst + stride, 0, (bounds->width * bpp) - stride);

          src += cr_stride;
          dst += bounds->width * bpp;
        }

      for (int i = height; i < bounds->height; i++)
        {
          memset (dst, 0, bounds->width * bpp);
          dst += bounds->width * bpp;
        }
    }

  cairo_surface_destroy (image);
}

static gboolean
meta_window_actor_blit_to_framebuffer (MetaScreenCastWindow *screen_cast_window,
                                       MetaRectangle        *bounds,
                                       CoglFramebuffer      *framebuffer)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (screen_cast_window);
  ClutterActor *actor = CLUTTER_ACTOR (window_actor);
  ClutterPaintContext *paint_context;
  MetaRectangle scaled_clip;
  CoglColor clear_color;
  float resource_scale;
  float width, height;
  float x, y;

  if (meta_window_actor_is_destroyed (window_actor))
    return FALSE;

  clutter_actor_get_size (actor, &width, &height);

  if (width == 0 || height == 0)
    return FALSE;

  if (!clutter_actor_get_resource_scale (actor, &resource_scale))
    return FALSE;

  clutter_actor_inhibit_culling (actor);

  width = ceilf (width * resource_scale);
  height = ceilf (height * resource_scale);

  clutter_actor_get_position (actor, &x, &y);

  cogl_color_init_from_4ub (&clear_color, 0, 0, 0, 0);
  cogl_framebuffer_clear (framebuffer, COGL_BUFFER_BIT_COLOR, &clear_color);
  cogl_framebuffer_orthographic (framebuffer, 0, 0, width, height, 0, 1.0);
  cogl_framebuffer_set_viewport (framebuffer, 0, 0, width, height);

  meta_rectangle_scale_double (bounds, resource_scale,
                               META_ROUNDING_STRATEGY_GROW,
                               &scaled_clip);
  meta_rectangle_intersect (&scaled_clip,
                            &(MetaRectangle) {
                              .width = width,
                              .height = height,
                            },
                            &scaled_clip);

  cogl_framebuffer_push_rectangle_clip (framebuffer,
                                        scaled_clip.x, scaled_clip.y,
                                        scaled_clip.x + scaled_clip.width,
                                        scaled_clip.y + scaled_clip.height);

  cogl_framebuffer_push_matrix (framebuffer);
  cogl_framebuffer_scale (framebuffer, resource_scale, resource_scale, 1);
  cogl_framebuffer_translate (framebuffer, -x, -y, 0);

  paint_context = clutter_paint_context_new_for_framebuffer (framebuffer);
  clutter_actor_paint (actor, paint_context);
  clutter_paint_context_destroy (paint_context);

  cogl_framebuffer_pop_matrix (framebuffer);
  cogl_framebuffer_pop_clip (framebuffer);

  clutter_actor_uninhibit_culling (actor);

  return TRUE;
}

static gboolean
meta_window_actor_has_damage (MetaScreenCastWindow *screen_cast_window)
{
  return clutter_actor_has_damage (CLUTTER_ACTOR (screen_cast_window));
}

static void
screen_cast_window_iface_init (MetaScreenCastWindowInterface *iface)
{
  iface->get_buffer_bounds = meta_window_actor_get_buffer_bounds;
  iface->transform_relative_position = meta_window_actor_transform_relative_position;
  iface->transform_cursor_position = meta_window_actor_transform_cursor_position;
  iface->capture_into = meta_window_actor_capture_into;
  iface->blit_to_framebuffer = meta_window_actor_blit_to_framebuffer;
  iface->has_damage = meta_window_actor_has_damage;
}

MetaWindowActor *
meta_window_actor_from_actor (ClutterActor *actor)
{
  if (!META_IS_SURFACE_ACTOR (actor))
    return NULL;

  do
    {
      actor = clutter_actor_get_parent (actor);

      if (META_IS_WINDOW_ACTOR (actor))
        return META_WINDOW_ACTOR (actor);
    }
  while (actor != NULL);

  return NULL;
}

void
meta_window_actor_notify_damaged (MetaWindowActor *window_actor)
{
  g_signal_emit (window_actor, signals[DAMAGED], 0);
}

/**
 * meta_window_actor_get_image:
 * @self: A #MetaWindowActor
 * @clip: (nullable): A clipping rectangle, to help prevent extra processing.
 * In the case that the clipping rectangle is partially or fully
 * outside the bounds of the actor, the rectangle will be clipped.
 *
 * Flattens the layers of @self into one ARGB32 image by alpha blending
 * the images, and returns the flattened image.
 *
 * Returns: (nullable) (transfer full): a new cairo surface to be freed with
 * cairo_surface_destroy().
 */
cairo_surface_t *
meta_window_actor_get_image (MetaWindowActor *self,
                             MetaRectangle   *clip)
{
  MetaWindowActorPrivate *priv = meta_window_actor_get_instance_private (self);
  ClutterActor *actor = CLUTTER_ACTOR (self);
  MetaBackend *backend = meta_get_backend ();
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  float resource_scale;
  float width, height;
  CoglTexture2D *texture;
  g_autoptr (GError) error = NULL;
  CoglOffscreen *offscreen;
  CoglFramebuffer *framebuffer;
  CoglColor clear_color;
  float x, y;
  MetaRectangle scaled_clip;
  ClutterPaintContext *paint_context;
  cairo_surface_t *surface = NULL;

  if (!priv->surface)
    return NULL;

  clutter_actor_inhibit_culling (actor);

  if (clutter_actor_get_n_children (actor) == 1)
    {
      MetaShapedTexture *stex;
      MetaRectangle *surface_clip = NULL;

      if (clip)
        {

          int geometry_scale;

          geometry_scale =
            meta_window_actor_get_geometry_scale (self);

          surface_clip = g_alloca (sizeof (MetaRectangle));
          surface_clip->x = clip->x / geometry_scale,
          surface_clip->y = clip->y / geometry_scale;
          surface_clip->width = clip->width / geometry_scale;
          surface_clip->height = clip->height / geometry_scale;
        }

      stex = meta_surface_actor_get_texture (priv->surface);
      surface = meta_shaped_texture_get_image (stex, surface_clip);
      goto out;
    }

  clutter_actor_get_size (actor, &width, &height);

  if (width == 0 || height == 0)
    goto out;

  if (!clutter_actor_get_resource_scale (actor, &resource_scale))
    goto out;

  width = ceilf (width * resource_scale);
  height = ceilf (height * resource_scale);

  texture = cogl_texture_2d_new_with_size (cogl_context, width, height);
  if (!texture)
    goto out;

  cogl_primitive_texture_set_auto_mipmap (COGL_PRIMITIVE_TEXTURE (texture),
                                          FALSE);

  offscreen = cogl_offscreen_new_with_texture (COGL_TEXTURE (texture));
  framebuffer = COGL_FRAMEBUFFER (offscreen);

  cogl_object_unref (texture);

  if (!cogl_framebuffer_allocate (framebuffer, &error))
    {
      g_warning ("Failed to allocate framebuffer for screenshot: %s",
                 error->message);
      cogl_object_unref (framebuffer);
      cogl_object_unref (texture);
      goto out;
    }

  cogl_color_init_from_4ub (&clear_color, 0, 0, 0, 0);
  clutter_actor_get_position (actor, &x, &y);

  cogl_framebuffer_clear (framebuffer, COGL_BUFFER_BIT_COLOR, &clear_color);
  cogl_framebuffer_orthographic (framebuffer, 0, 0, width, height, 0, 1.0);
  cogl_framebuffer_scale (framebuffer, resource_scale, resource_scale, 1);
  cogl_framebuffer_translate (framebuffer, -x, -y, 0);

  paint_context = clutter_paint_context_new_for_framebuffer (framebuffer);
  clutter_actor_paint (actor, paint_context);
  clutter_paint_context_destroy (paint_context);

  if (clip)
    {
      meta_rectangle_scale_double (clip, resource_scale,
                                   META_ROUNDING_STRATEGY_GROW,
                                   &scaled_clip);
      meta_rectangle_intersect (&scaled_clip,
                                &(MetaRectangle) {
                                  .width = width,
                                  .height = height,
                                },
                                &scaled_clip);
    }
  else
    {
      scaled_clip = (MetaRectangle) {
        .width = width,
        .height = height,
      };
    }

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                        scaled_clip.width, scaled_clip.height);
  cogl_framebuffer_read_pixels (framebuffer,
                                scaled_clip.x, scaled_clip.y,
                                scaled_clip.width, scaled_clip.height,
                                CLUTTER_CAIRO_FORMAT_ARGB32,
                                cairo_image_surface_get_data (surface));

  cogl_object_unref (framebuffer);

  cairo_surface_mark_dirty (surface);

out:
  clutter_actor_uninhibit_culling (actor);
  return surface;
}
