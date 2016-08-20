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
#include "cogl-utils.h"
#include <meta/errors.h>
#include "meta-background-actor-private.h"

struct _MetaBackgroundPrivate
{
  MetaScreen *screen;
  CoglHandle  material;

  float texture_width;
  float texture_height;

  cairo_region_t *visible_region;
};

G_DEFINE_TYPE (MetaBackground, meta_background, CLUTTER_TYPE_ACTOR);

static void
meta_background_dispose (GObject *object)
{
  MetaBackground *self = META_BACKGROUND (object);
  MetaBackgroundPrivate *priv = self->priv;

  meta_background_set_visible_region (self, NULL);

  if (priv->material != COGL_INVALID_HANDLE)
    {
      cogl_handle_unref (priv->material);
      priv->material = COGL_INVALID_HANDLE;
    }

  G_OBJECT_CLASS (meta_background_parent_class)->dispose (object);
}

static void
meta_background_get_preferred_width (ClutterActor *actor,
                                     gfloat        for_height,
                                     gfloat       *min_width_p,
                                     gfloat       *natural_width_p)
{
  MetaBackground *self = META_BACKGROUND (actor);
  MetaBackgroundPrivate *priv = self->priv;
  int width, height;

  meta_screen_get_size (priv->screen, &width, &height);

  if (min_width_p)
    *min_width_p = width;
  if (natural_width_p)
    *natural_width_p = width;
}

static void
meta_background_get_preferred_height (ClutterActor *actor,
                                      gfloat        for_width,
                                      gfloat       *min_height_p,
                                      gfloat       *natural_height_p)

{
  MetaBackground *self = META_BACKGROUND (actor);
  MetaBackgroundPrivate *priv = self->priv;
  int width, height;

  meta_screen_get_size (priv->screen, &width, &height);

  if (min_height_p)
    *min_height_p = height;
  if (natural_height_p)
    *natural_height_p = height;
}

static void
meta_background_paint (ClutterActor *actor)
{
  MetaBackground *self = META_BACKGROUND (actor);
  MetaBackgroundPrivate *priv = self->priv;
  guint8 opacity = clutter_actor_get_paint_opacity (actor);
  guint8 color_component;
  int width, height;

  meta_screen_get_size (priv->screen, &width, &height);

  color_component = (int)(0.5 + opacity);

  cogl_material_set_color4ub (priv->material,
                              color_component,
                              color_component,
                              color_component,
                              opacity);

  cogl_set_source (priv->material);

  if (priv->visible_region)
    {
      int n_rectangles = cairo_region_num_rectangles (priv->visible_region);
      int i;

      for (i = 0; i < n_rectangles; i++)
        {
          cairo_rectangle_int_t rect;
          cairo_region_get_rectangle (priv->visible_region, i, &rect);

          cogl_rectangle_with_texture_coords (rect.x, rect.y,
                                              rect.x + rect.width, rect.y + rect.height,
                                              rect.x / priv->texture_width,
                                              rect.y / priv->texture_height,
                                              (rect.x + rect.width) / priv->texture_width,
                                              (rect.y + rect.height) / priv->texture_height);
        }
    }
  else
    {
      cogl_rectangle_with_texture_coords (0.0f, 0.0f,
                                          width, height,
                                          0.0f, 0.0f,
                                          width / priv->texture_width,
                                          height / priv->texture_height);
    }
}

static void
meta_background_class_init (MetaBackgroundClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

  g_type_class_add_private (klass, sizeof (MetaBackgroundPrivate));

  object_class->dispose = meta_background_dispose;

  actor_class->get_preferred_width = meta_background_get_preferred_width;
  actor_class->get_preferred_height = meta_background_get_preferred_height;
  actor_class->paint = meta_background_paint;
}

static void
meta_background_init (MetaBackground *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                            META_TYPE_BACKGROUND,
                                            MetaBackgroundPrivate);
}

ClutterActor *
meta_background_new (MetaScreen *screen)
{
  MetaBackground *self;
  MetaBackgroundPrivate *priv;

  self = g_object_new (META_TYPE_BACKGROUND,
                       NULL);
  priv = self->priv;

  priv->screen = screen;
  priv->material = meta_create_texture_material (NULL);

  return CLUTTER_ACTOR (self);
}

void
meta_background_set_layer (MetaBackground *self,
                           CoglHandle      texture)
{
  MetaBackgroundPrivate *priv = self->priv;
  MetaDisplay *display = meta_screen_get_display (priv->screen);

  /* This may trigger destruction of an old texture pixmap, which, if
   * the underlying X pixmap is already gone has the tendency to trigger
   * X errors inside DRI. For safety, trap errors */
  meta_error_trap_push (display);
  cogl_material_set_layer (priv->material, 0, texture);
  meta_error_trap_pop (display);

  priv->texture_width = cogl_texture_get_width (texture);
  priv->texture_height = cogl_texture_get_height (texture);

  clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
}

void
meta_background_set_layer_wrap_mode (MetaBackground       *self,
                                     CoglMaterialWrapMode  wrap_mode)
{
  MetaBackgroundPrivate *priv = self->priv;

  cogl_material_set_layer_wrap_mode (priv->material, 0, wrap_mode);
}

void
meta_background_set_visible_region (MetaBackground *self,
                                    cairo_region_t *visible_region)
{
  MetaBackgroundPrivate *priv;

  g_return_if_fail (META_IS_BACKGROUND (self));

  priv = self->priv;

  if (priv->visible_region)
    {
      cairo_region_destroy (priv->visible_region);
      priv->visible_region = NULL;
    }

  if (visible_region)
    {
      cairo_rectangle_int_t screen_rect = { 0 };
      meta_screen_get_size (priv->screen, &screen_rect.width, &screen_rect.height);

      /* Doing the intersection here is probably unnecessary - MetaWindowGroup
       * should never compute a visible area that's larger than the root screen!
       * but it's not that expensive and adds some extra robustness.
       */
      priv->visible_region = cairo_region_create_rectangle (&screen_rect);
      cairo_region_intersect (priv->visible_region, visible_region);
    }
}