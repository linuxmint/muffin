/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * meta-background.c: Actor used to create a background fade effect
 *
 * Copyright 2016 Linux Mint
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
 * and meta-window-actor.c
 */

#include "config.h"

#include <clutter/clutter.h>
#include "cogl/cogl.h"
#include <compositor/cogl-utils.h>
#include "meta/meta-x11-errors.h"
#include "meta-x11-background-actor-private.h"

typedef struct
{
  MetaDisplay *display;
  MetaX11Display *x11_display;
  CoglPipeline  *pipeline;

  float texture_width;
  float texture_height;

  cairo_region_t *visible_region;
} MetaX11BackgroundPrivate;

struct _MetaX11Background
{
    ClutterActor parent_instance;

    MetaX11BackgroundPrivate *priv;
};


G_DEFINE_TYPE_WITH_PRIVATE (MetaX11Background, meta_x11_background, CLUTTER_TYPE_ACTOR);

static void
meta_x11_background_dispose (GObject *object)
{
  MetaX11Background *self = META_X11_BACKGROUND (object);
  MetaX11BackgroundPrivate *priv = self->priv;

  meta_x11_background_set_visible_region (self, NULL);

  if (priv->pipeline != NULL)
    {
      cogl_object_unref (priv->pipeline);
      priv->pipeline = NULL;
    }

  G_OBJECT_CLASS (meta_x11_background_parent_class)->dispose (object);
}

static void
meta_x11_background_get_preferred_width (ClutterActor *actor,
                                     gfloat        for_height,
                                     gfloat       *min_width_p,
                                     gfloat       *natural_width_p)
{
  MetaX11Background *self = META_X11_BACKGROUND (actor);
  MetaX11BackgroundPrivate *priv = self->priv;
  int width, height;

  meta_display_get_size (priv->display, &width, &height);

  if (min_width_p)
    *min_width_p = width;
  if (natural_width_p)
    *natural_width_p = width;
}

static void
meta_x11_background_get_preferred_height (ClutterActor *actor,
                                      gfloat        for_width,
                                      gfloat       *min_height_p,
                                      gfloat       *natural_height_p)

{
  MetaX11Background *self = META_X11_BACKGROUND (actor);
  MetaX11BackgroundPrivate *priv = self->priv;
  int width, height;

  meta_display_get_size (priv->display, &width, &height);

  if (min_height_p)
    *min_height_p = height;
  if (natural_height_p)
    *natural_height_p = height;
}

static void
meta_x11_background_paint (ClutterActor *actor, ClutterPaintContext *paint_context)
{
  MetaX11Background *self = META_X11_BACKGROUND (actor);
  MetaX11BackgroundPrivate *priv = self->priv;
  CoglFramebuffer *framebuffer = clutter_paint_context_get_framebuffer (paint_context);
  guint8 opacity = clutter_actor_get_paint_opacity (actor);
  guint8 color_component;
  int width, height;

  meta_display_get_size (priv->display, &width, &height);

  color_component = (int)(0.5 + opacity);

  cogl_pipeline_set_color4ub (priv->pipeline,
                              color_component,
                              color_component,
                              color_component,
                              opacity);

  if (priv->visible_region)
    {
      int n_rectangles = cairo_region_num_rectangles (priv->visible_region);
      int i;

      for (i = 0; i < n_rectangles; i++)
        {
          cairo_rectangle_int_t rect;
          cairo_region_get_rectangle (priv->visible_region, i, &rect);

          cogl_framebuffer_draw_textured_rectangle (framebuffer,
                                                    priv->pipeline,
                                                    rect.x, rect.y,
                                                    rect.x + rect.width, rect.y + rect.height,
                                                    rect.x / priv->texture_width,
                                                    rect.y / priv->texture_height,
                                                    (rect.x + rect.width) / priv->texture_width,
                                                    (rect.y + rect.height) / priv->texture_height);
        }
    }
  else
    {
      cogl_framebuffer_draw_textured_rectangle (framebuffer,
                                                priv->pipeline,
                                                0.0f, 0.0f,
                                                width, height,
                                                0.0f, 0.0f,
                                                width / priv->texture_width,
                                                height / priv->texture_height);
    }
}

static gboolean
meta_x11_background_get_paint_volume (ClutterActor       *actor,
                                        ClutterPaintVolume *volume)
{
  return clutter_paint_volume_set_from_allocation (volume, actor);
}


static void
meta_x11_background_class_init (MetaX11BackgroundClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

  object_class->dispose = meta_x11_background_dispose;

  actor_class->get_preferred_width = meta_x11_background_get_preferred_width;
  actor_class->get_preferred_height = meta_x11_background_get_preferred_height;
  actor_class->get_paint_volume = meta_x11_background_get_paint_volume;
  actor_class->paint = meta_x11_background_paint;
}

static void
meta_x11_background_init (MetaX11Background *self)
{
  self->priv = meta_x11_background_get_instance_private (self);
}

ClutterActor *
meta_x11_background_new (MetaDisplay *display)
{
  MetaX11Background *self;
  MetaX11BackgroundPrivate *priv;

  self = g_object_new (META_TYPE_X11_BACKGROUND,
                       NULL);
  priv = self->priv;

  priv->display = display;
  priv->x11_display = meta_display_get_x11_display (display);
  priv->pipeline = meta_create_texture_pipeline (NULL);

  return CLUTTER_ACTOR (self);
}

void
meta_x11_background_set_layer (MetaX11Background *self,
                           CoglTexture    *texture)
{
  MetaX11BackgroundPrivate *priv = self->priv;

  /* This may trigger destruction of an old texture pixmap, which, if
   * the underlying X pixmap is already gone has the tendency to trigger
   * X errors inside DRI. For safety, trap errors */
  meta_x11_error_trap_push (priv->x11_display);
  cogl_pipeline_set_layer_texture (priv->pipeline, 0, texture);
  meta_x11_error_trap_pop (priv->x11_display);

  priv->texture_width = cogl_texture_get_width (texture);
  priv->texture_height = cogl_texture_get_height (texture);

  clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
}

void
meta_x11_background_set_layer_wrap_mode (MetaX11Background       *self,
                                     CoglPipelineWrapMode  wrap_mode)
{
  MetaX11BackgroundPrivate *priv = self->priv;

  cogl_pipeline_set_layer_wrap_mode (priv->pipeline, 0, wrap_mode);
}

void
meta_x11_background_set_visible_region (MetaX11Background *self,
                                    cairo_region_t *visible_region)
{
  MetaX11BackgroundPrivate *priv;

  g_return_if_fail (META_IS_X11_BACKGROUND (self));

  priv = self->priv;

  if (priv->visible_region)
    {
      cairo_region_destroy (priv->visible_region);
      priv->visible_region = NULL;
    }

  if (visible_region)
    {
      cairo_rectangle_int_t screen_rect = { 0 };
      meta_display_get_size (priv->display, &screen_rect.width, &screen_rect.height);

      /* Doing the intersection here is probably unnecessary - MetaWindowGroup
       * should never compute a visible area that's larger than the root screen!
       * but it's not that expensive and adds some extra robustness.
       */
      priv->visible_region = cairo_region_create_rectangle (&screen_rect);
      cairo_region_intersect (priv->visible_region, visible_region);
    }
}