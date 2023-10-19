/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * meta-background-actor.c: Actor for painting the root window background
 *
 * Copyright 2009 Sander Dijkhuis
 * Copyright 2010 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 *
 * Portions adapted from gnome-shell/src/shell-global.c
 */

/**
 * SECTION:meta-x11-background-actor
 * @title: MetaX11BackgroundActor
 * @short_description: Actor for painting the root window background
 */

#include <config.h>

#include <cogl/winsys/cogl-texture-pixmap-x11.h>

#include <clutter/clutter.h>

#include <X11/Xatom.h>

#include "cogl/cogl.h"
#include "meta/compositor-mutter.h"
#include "meta/meta-x11-errors.h"
#include "meta/meta-x11-display.h"
#include "meta-x11-background-actor-private.h"
#include "meta-x11-display-private.h"

#define FADE_DURATION 1500

/* We allow creating multiple MetaX11BackgroundActors for the same MetaScreen to
 * allow different rendering options to be set for different copies.
 * But we want to share the same underlying CoglTexture for efficiency and
 * to avoid driver bugs that might occur if we created multiple CoglTexturePixmaps
 * for the same pixmap.
 *
 * This structure holds common information.
 */
typedef struct _MetaDisplayBackground MetaDisplayBackground;

struct _MetaDisplayBackground
{
  MetaDisplay *display;
  MetaX11Display *x11_display;
  GSList *actors;

  float texture_width;
  float texture_height;
  CoglTexture *texture;
  CoglPipelineWrapMode wrap_mode;
  guint have_pixmap : 1;
};

typedef struct
{
  MetaDisplayBackground *background;
  ClutterActor *top_actor;
  ClutterActor *bottom_actor;
  cairo_region_t *visible_region;
  float dim_factor;
  gboolean transition_running;
} MetaX11BackgroundActorPrivate;

struct _MetaX11BackgroundActor
{
    ClutterActor parent_instance;

    MetaX11BackgroundActorPrivate *priv;
};

enum
{
  PROP_0,

  PROP_DIM_FACTOR,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

G_DEFINE_TYPE_WITH_PRIVATE (MetaX11BackgroundActor, meta_x11_background_actor, CLUTTER_TYPE_ACTOR);

static void set_texture                (MetaDisplayBackground *background,
                                        CoglTexture          *texture);
static void set_texture_to_stage_color (MetaDisplayBackground *background);

static void
on_notify_stage_color (GObject              *stage,
                       GParamSpec           *pspec,
                       MetaDisplayBackground *background)
{
  if (!background->have_pixmap)
    set_texture_to_stage_color (background);
}

static void
free_display_background (MetaDisplayBackground *background)
{
  set_texture (background, NULL);

  if (background->display != NULL)
    {
      ClutterActor *stage = meta_get_stage_for_display (background->display);
      g_signal_handlers_disconnect_by_func (stage,
                                            (gpointer) on_notify_stage_color,
                                            background);
      background->display = NULL;
    }
}

static MetaDisplayBackground *
meta_display_background_get (MetaDisplay *display)
{
  MetaDisplayBackground *background;

  background = g_object_get_data (G_OBJECT (display), "meta-display-background");
  if (background == NULL)
    {
      ClutterActor *stage;

      background = g_new0 (MetaDisplayBackground, 1);

      background->display = display;
      background->x11_display = meta_display_get_x11_display (display);
      g_object_set_data_full (G_OBJECT (display), "meta-display-background",
                              background, (GDestroyNotify) free_display_background);

      stage = meta_get_stage_for_display (display);
      g_signal_connect (stage, "notify::background-color",
                        G_CALLBACK (on_notify_stage_color), background);

      meta_x11_background_actor_update (display);
    }

  return background;
}

static void
update_wrap_mode_of_actor (MetaX11BackgroundActor *self)
{
  MetaX11BackgroundActorPrivate *priv = self->priv;

  meta_x11_background_set_layer_wrap_mode (META_X11_BACKGROUND (priv->top_actor), priv->background->wrap_mode);
  meta_x11_background_set_layer_wrap_mode (META_X11_BACKGROUND (priv->bottom_actor), priv->background->wrap_mode);

  /* this ensures the actors also get resized if the stage size changed */
  clutter_actor_queue_relayout (CLUTTER_ACTOR (priv->top_actor));
  clutter_actor_queue_relayout (CLUTTER_ACTOR (priv->bottom_actor));
}

static void
update_wrap_mode (MetaDisplayBackground *background)
{
  GSList *l;
  int width, height;

  meta_display_get_size (background->display, &width, &height);

  /* We turn off repeating when we have a full-screen pixmap to keep from
   * getting artifacts from one side of the image sneaking into the other
   * side of the image via bilinear filtering.
   */
  if (width == background->texture_width && height == background->texture_height)
    background->wrap_mode = COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE;
  else
    background->wrap_mode = COGL_PIPELINE_WRAP_MODE_REPEAT;

  for (l = background->actors; l; l = l->next)
    update_wrap_mode_of_actor (l->data);
}

static void
cancel_transitions (MetaX11BackgroundActor *self)
{
  MetaX11BackgroundActorPrivate *priv = self->priv;

  clutter_actor_remove_all_transitions (priv->top_actor);
  clutter_actor_set_opacity (priv->top_actor, 255);
  meta_x11_background_set_layer (META_X11_BACKGROUND (priv->bottom_actor), priv->background->texture);

  priv->transition_running = FALSE;
}

static void
on_transition_complete (ClutterActor *actor,
                       gpointer      user_data)
{
  MetaX11BackgroundActor *self = (MetaX11BackgroundActor *)user_data;
  MetaX11BackgroundActorPrivate *priv = self->priv;

  meta_x11_background_set_layer (META_X11_BACKGROUND (priv->bottom_actor), priv->background->texture);
  priv->transition_running = FALSE;
}

static void
set_texture_on_actors (MetaX11BackgroundActor *self)
{
  MetaX11BackgroundActorPrivate *priv = self->priv;

  meta_x11_background_set_layer (META_X11_BACKGROUND (priv->bottom_actor), priv->background->texture);
  meta_x11_background_set_layer (META_X11_BACKGROUND (priv->top_actor), priv->background->texture);

  clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
}

static void
set_texture_on_actor (MetaX11BackgroundActor *self)
{
  MetaX11BackgroundActorPrivate *priv = self->priv;
  MetaX11BackgroundTransition background_transition;

  if (priv->transition_running)
    cancel_transitions (self);

  background_transition = meta_prefs_get_background_transition();

  if (background_transition == META_X11_BACKGROUND_TRANSITION_NONE)
  {
    // NO TRANSITION
    clutter_actor_set_opacity (CLUTTER_ACTOR (priv->bottom_actor), 0);
    meta_x11_background_set_layer (META_X11_BACKGROUND (priv->top_actor), priv->background->texture);
    on_transition_complete (priv->top_actor, self);
  }
  else
  {
    if (background_transition == META_X11_BACKGROUND_TRANSITION_FADEIN)
    {
      // FADE_IN TRANSITION
      clutter_actor_set_opacity (CLUTTER_ACTOR (priv->bottom_actor), 0);
    }

    // BLEND TRANSITION
    clutter_actor_set_opacity (CLUTTER_ACTOR (priv->top_actor), 0);
    meta_x11_background_set_layer (META_X11_BACKGROUND (priv->top_actor), priv->background->texture);

    priv->transition_running = TRUE;

    clutter_actor_save_easing_state (priv->top_actor);
    clutter_actor_set_easing_duration (priv->top_actor, FADE_DURATION);
    clutter_actor_set_opacity (priv->top_actor, 255);
    clutter_actor_restore_easing_state (priv->top_actor);

    g_signal_connect (priv->top_actor,
                      "transitions-completed",
                      G_CALLBACK (on_transition_complete),
                      self);

    clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
  }
}

static void
set_texture (MetaDisplayBackground *background,
             CoglTexture          *texture)
{
  GSList *l;

  /* This may trigger destruction of an old texture pixmap, which, if
   * the underlying X pixmap is already gone has the tendency to trigger
   * X errors inside DRI. For safety, trap errors */
  meta_x11_error_trap_push (background->x11_display);
  if (background->texture != NULL)
    {
      cogl_object_unref (background->texture);
      background->texture = NULL;
    }
  meta_x11_error_trap_pop (background->x11_display);

  if (texture != NULL)
    background->texture = cogl_object_ref (texture);

  background->texture_width = cogl_texture_get_width (background->texture);
  background->texture_height = cogl_texture_get_height (background->texture);

  for (l = background->actors; l; l = l->next)
    set_texture_on_actor (l->data);

  update_wrap_mode (background);
}

static CoglTexture *
create_color_texture_4ub (guint8           red,
                          guint8           green,
                          guint8           blue,
                          guint8           alpha,
                          CoglTextureFlags flags)
{
  CoglColor color;
  guint8 pixel[4];

  cogl_color_init_from_4ub (&color, red, green, blue, alpha);
  cogl_color_premultiply (&color);

  pixel[0] = cogl_color_get_red_byte (&color);
  pixel[1] = cogl_color_get_green_byte (&color);
  pixel[2] = cogl_color_get_blue_byte (&color);
  pixel[3] = cogl_color_get_alpha_byte (&color);

  return cogl_texture_new_from_data (1, 1,
                                     flags,
                                     COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                                     COGL_PIXEL_FORMAT_ANY,
                                     4, pixel);
}

/* Sets our material to paint with a 1x1 texture of the stage's background
 * color; doing this when we have no pixmap allows the application to turn
 * off painting the stage. There might be a performance benefit to
 * painting in this case with a solid color, but the normal solid color
 * case is a 1x1 root pixmap, so we'd have to reverse-engineer that to
 * actually pick up the (small?) performance win. This is just a fallback.
 */
static void
set_texture_to_stage_color (MetaDisplayBackground *background)
{
  ClutterActor *stage = meta_get_stage_for_display (background->display);
  ClutterColor color;
  CoglTexture *texture;

  clutter_actor_get_background_color (stage, &color);

  /* Slicing will prevent COGL from using hardware texturing for
   * the tiled 1x1 pixmap, and will cause it to draw the window
   * background in millions of separate 1x1 rectangles */
  texture = create_color_texture_4ub (color.red, color.green,
                                      color.blue, 0xff,
                                      COGL_TEXTURE_NO_SLICING);
  set_texture (background, texture);
  cogl_object_unref (texture);
}

static void
meta_x11_background_actor_dispose (GObject *object)
{
  MetaX11BackgroundActor *self = META_X11_BACKGROUND_ACTOR (object);
  MetaX11BackgroundActorPrivate *priv = self->priv;

  meta_x11_background_actor_set_visible_region (self, NULL);

  if (priv->background != NULL)
    {
      priv->background->actors = g_slist_remove (priv->background->actors, self);
      priv->background = NULL;
    }

  if (priv->top_actor != NULL)
    priv->top_actor = NULL;

  if (priv->bottom_actor != NULL)
    priv->bottom_actor = NULL;

  G_OBJECT_CLASS (meta_x11_background_actor_parent_class)->dispose (object);
}

static void
meta_x11_background_actor_get_preferred_width (ClutterActor *actor,
                                           gfloat        for_height,
                                           gfloat       *min_width_p,
                                           gfloat       *natural_width_p)
{
  MetaX11BackgroundActor *self = META_X11_BACKGROUND_ACTOR (actor);
  MetaX11BackgroundActorPrivate *priv = self->priv;
  int width, height;

  meta_display_get_size (priv->background->display, &width, &height);

  if (min_width_p)
    *min_width_p = width;
  if (natural_width_p)
    *natural_width_p = width;
}

static void
meta_x11_background_actor_get_preferred_height (ClutterActor *actor,
                                            gfloat        for_width,
                                            gfloat       *min_height_p,
                                            gfloat       *natural_height_p)

{
  MetaX11BackgroundActor *self = META_X11_BACKGROUND_ACTOR (actor);
  MetaX11BackgroundActorPrivate *priv = self->priv;
  int width, height;

  meta_display_get_size (priv->background->display, &width, &height);

  if (min_height_p)
    *min_height_p = height;
  if (natural_height_p)
    *natural_height_p = height;
}

static gboolean
meta_x11_background_actor_get_paint_volume (ClutterActor       *actor,
                                        ClutterPaintVolume *volume)
{
  MetaX11BackgroundActor *self = META_X11_BACKGROUND_ACTOR (actor);
  MetaX11BackgroundActorPrivate *priv = self->priv;
  int width, height;

  meta_display_get_size (priv->background->display, &width, &height);

  clutter_paint_volume_set_width (volume, width);
  clutter_paint_volume_set_height (volume, height);

  return TRUE;
}

static void
meta_x11_background_actor_set_dim_factor (MetaX11BackgroundActor *self,
                                      gfloat               dim_factor)
{
  MetaX11BackgroundActorPrivate *priv = self->priv;

  if (priv->dim_factor == dim_factor)
    return;

  priv->dim_factor = dim_factor;

  clutter_actor_queue_redraw (CLUTTER_ACTOR (self));

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_DIM_FACTOR]);
}

static void
meta_x11_background_actor_get_property (GObject         *object,
                                    guint            prop_id,
                                    GValue          *value,
                                    GParamSpec      *pspec)
{
  MetaX11BackgroundActor *self = META_X11_BACKGROUND_ACTOR (object);
  MetaX11BackgroundActorPrivate *priv = self->priv;

  switch (prop_id)
    {
    case PROP_DIM_FACTOR:
      g_value_set_float (value, priv->dim_factor);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_x11_background_actor_set_property (GObject         *object,
                                    guint            prop_id,
                                    const GValue    *value,
                                    GParamSpec      *pspec)
{
  MetaX11BackgroundActor *self = META_X11_BACKGROUND_ACTOR (object);

  switch (prop_id)
    {
    case PROP_DIM_FACTOR:
      meta_x11_background_actor_set_dim_factor (self, g_value_get_float (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_x11_background_actor_class_init (MetaX11BackgroundActorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  GParamSpec *pspec;

  object_class->dispose = meta_x11_background_actor_dispose;
  object_class->get_property = meta_x11_background_actor_get_property;
  object_class->set_property = meta_x11_background_actor_set_property;

  actor_class->get_preferred_width = meta_x11_background_actor_get_preferred_width;
  actor_class->get_preferred_height = meta_x11_background_actor_get_preferred_height;
  actor_class->get_paint_volume = meta_x11_background_actor_get_paint_volume;

  /**
   * MetaX11BackgroundActor:dim-factor:
   *
   * Factor to dim the background by, between 0.0 (black) and 1.0 (original
   * colors)
   */
  pspec = g_param_spec_float ("dim-factor",
                              "Dim factor",
                              "Factor to dim the background by",
                              0.0, 1.0,
                              1.0,
                              G_PARAM_READWRITE);
  obj_props[PROP_DIM_FACTOR] = pspec;
  g_object_class_install_property (object_class, PROP_DIM_FACTOR, pspec);
}

static void
meta_x11_background_actor_init (MetaX11BackgroundActor *self)
{
  self->priv = meta_x11_background_actor_get_instance_private (self);
  self->priv->dim_factor = 1.0;
  self->priv->transition_running = FALSE;
}

/**
 * meta_x11_background_actor_new_for_display:
 * @display: the #MetaDisplay
 *
 * Creates a new actor to draw the background for the given display.
 *
 * Return value: (transfer none): the newly created background actor
 */
ClutterActor *
meta_x11_background_actor_new_for_display (MetaDisplay *display)
{
  MetaX11BackgroundActor *self;
  MetaX11BackgroundActorPrivate *priv;

  g_return_val_if_fail (META_IS_DISPLAY (display), NULL);

  if (meta_is_wayland_compositor ())
    return NULL;

  self = g_object_new (META_TYPE_X11_BACKGROUND_ACTOR, NULL);
  priv = self->priv;

  priv->background = meta_display_background_get (display);
  priv->background->actors = g_slist_prepend (priv->background->actors, self);

  priv->bottom_actor = meta_x11_background_new (display);
  clutter_actor_add_child (CLUTTER_ACTOR (self), priv->bottom_actor);
  priv->top_actor = meta_x11_background_new (display);
  clutter_actor_add_child (CLUTTER_ACTOR (self), priv->top_actor);

  set_texture_on_actors (self);
  update_wrap_mode_of_actor (self);

  return CLUTTER_ACTOR (self);
}

/**
 * meta_x11_background_actor_update:
 * @display: a #MetaDisplay
 *
 * Refetches the _XROOTPMAP_ID property for the root window and updates
 * the contents of the background actor based on that. There's no attempt
 * to optimize out pixmap values that don't change (since a root pixmap
 * could be replaced by with another pixmap with the same ID under some
 * circumstances), so this should only be called when we actually receive
 * a PropertyNotify event for the property.
 */
void
meta_x11_background_actor_update (MetaDisplay *display)
{
  MetaX11Display *x11_display;
  MetaDisplayBackground *background;
  Atom type;
  int format;
  gulong nitems;
  gulong bytes_after;
  guchar *data;
  Pixmap root_pixmap_id;

  x11_display = meta_display_get_x11_display (display);
  background = meta_display_background_get (display);

  root_pixmap_id = None;
  if (!XGetWindowProperty (meta_x11_display_get_xdisplay (x11_display),
                           meta_x11_display_get_xroot (x11_display),
                           x11_display->atom_x_root_pixmap,
                           0, LONG_MAX,
                           False,
                           AnyPropertyType,
                           &type, &format, &nitems, &bytes_after, &data) &&
                           type != None)
    {
      /* Got a property. */
      if (type == XA_PIXMAP && format == 32 && nitems == 1)
        {
          /* Was what we expected. */
          root_pixmap_id = *(Pixmap *)data;
        }

      XFree(data);
    }

  if (root_pixmap_id != None)
    {
      CoglHandle texture;
      CoglContext *context = clutter_backend_get_cogl_context (clutter_get_default_backend ());

      GError *error = NULL;

      meta_x11_error_trap_push (x11_display);
      texture = cogl_texture_pixmap_x11_new (context, root_pixmap_id, FALSE, &error);
      meta_x11_error_trap_pop (x11_display);

      if (texture != NULL)
        {
          set_texture (background, texture);
          cogl_object_unref (texture);

          background->have_pixmap = True;
          return;
        }
      else
        {
          g_warning ("Failed to create background texture from pixmap: %s",
                     error->message);
          g_error_free (error);
        }
    }

  background->have_pixmap = False;
  set_texture_to_stage_color (background);
}

/**
 * meta_x11_background_actor_set_visible_region:
 * @self: a #MetaX11BackgroundActor
 * @visible_region: (allow-none): the area of the actor (in allocate-relative
 *   coordinates) that is visible.
 *
 * Sets the area of the background that is unobscured by overlapping windows.
 * This is used to optimize and only paint the visible portions.
 */
void
meta_x11_background_actor_set_visible_region (MetaX11BackgroundActor *self,
                                          cairo_region_t      *visible_region)
{
  MetaX11BackgroundActorPrivate *priv;

  g_return_if_fail (META_IS_X11_BACKGROUND_ACTOR (self));

  priv = self->priv;

  if (priv->top_actor != NULL)
    meta_x11_background_set_visible_region (META_X11_BACKGROUND (priv->top_actor), visible_region);
}

/**
 * meta_x11_background_actor_screen_size_changed:
 * @display: a #MetaScreen
 *
 * Called by the compositor when the size of the #MetaScreen changes
 */
void
meta_x11_background_actor_screen_size_changed (MetaDisplay *x11_display)
{
  MetaDisplayBackground *background = meta_display_background_get (x11_display);

  update_wrap_mode (background);
}
