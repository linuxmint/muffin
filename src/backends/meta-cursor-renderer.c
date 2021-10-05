/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "backends/meta-cursor-renderer.h"

#include <math.h>

#include "backends/meta-stage-private.h"
#include "clutter/clutter.h"
#include "clutter/clutter-mutter.h"
#include "cogl/cogl.h"
#include "meta/meta-backend.h"
#include "meta/util.h"

G_DEFINE_INTERFACE (MetaHwCursorInhibitor, meta_hw_cursor_inhibitor,
                    G_TYPE_OBJECT)

struct _MetaCursorRendererPrivate
{
  float current_x;
  float current_y;

  MetaCursorSprite *displayed_cursor;
  MetaOverlay *stage_overlay;
  gboolean handled_by_backend;
  guint post_paint_func_id;

  GList *hw_cursor_inhibitors;
};
typedef struct _MetaCursorRendererPrivate MetaCursorRendererPrivate;

enum
{
  CURSOR_PAINTED,
  LAST_SIGNAL
};
static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE_WITH_PRIVATE (MetaCursorRenderer, meta_cursor_renderer, G_TYPE_OBJECT);

static gboolean
meta_hw_cursor_inhibitor_is_cursor_sprite_inhibited (MetaHwCursorInhibitor *inhibitor,
                                                     MetaCursorSprite      *cursor_sprite)
{
  MetaHwCursorInhibitorInterface *iface =
    META_HW_CURSOR_INHIBITOR_GET_IFACE (inhibitor);

  return iface->is_cursor_sprite_inhibited (inhibitor, cursor_sprite);
}

static void
meta_hw_cursor_inhibitor_default_init (MetaHwCursorInhibitorInterface *iface)
{
}

void
meta_cursor_renderer_emit_painted (MetaCursorRenderer *renderer,
                                   MetaCursorSprite   *cursor_sprite)
{
  g_signal_emit (renderer, signals[CURSOR_PAINTED], 0, cursor_sprite);
}

static void
align_cursor_position (MetaCursorRenderer *renderer,
                       graphene_rect_t    *rect)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);
  MetaBackend *backend = meta_get_backend ();
  ClutterActor *stage = meta_backend_get_stage (backend);
  ClutterStageView *view;
  cairo_rectangle_int_t view_layout;
  float view_scale;

  view = clutter_stage_get_view_at (CLUTTER_STAGE (stage),
                                    priv->current_x,
                                    priv->current_y);
  if (!view)
    return;

  clutter_stage_view_get_layout (view, &view_layout);
  view_scale = clutter_stage_view_get_scale (view);

  graphene_rect_offset (rect, -view_layout.x, -view_layout.y);
  rect->origin.x = floorf (rect->origin.x * view_scale) / view_scale;
  rect->origin.y = floorf (rect->origin.y * view_scale) / view_scale;
  graphene_rect_offset (rect, view_layout.x, view_layout.y);
}

static void
queue_redraw (MetaCursorRenderer *renderer,
              MetaCursorSprite   *cursor_sprite)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);
  MetaBackend *backend = meta_get_backend ();
  ClutterActor *stage = meta_backend_get_stage (backend);
  CoglTexture *texture;
  graphene_rect_t rect = GRAPHENE_RECT_INIT_ZERO;

  /* During early initialization, we can have no stage */
  if (!stage)
    return;

  if (cursor_sprite)
    {
      rect = meta_cursor_renderer_calculate_rect (renderer, cursor_sprite);
      align_cursor_position (renderer, &rect);
    }

  if (!priv->stage_overlay)
    priv->stage_overlay = meta_stage_create_cursor_overlay (META_STAGE (stage));

  if (cursor_sprite && !priv->handled_by_backend)
    texture = meta_cursor_sprite_get_cogl_texture (cursor_sprite);
  else
    texture = NULL;

  meta_stage_update_cursor_overlay (META_STAGE (stage), priv->stage_overlay,
                                    texture, &rect);
}

static gboolean
meta_cursor_renderer_post_paint (gpointer data)
{
  MetaCursorRenderer *renderer = META_CURSOR_RENDERER (data);
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);

  if (priv->displayed_cursor && !priv->handled_by_backend)
    meta_cursor_renderer_emit_painted (renderer, priv->displayed_cursor);

  return TRUE;
}

static gboolean
meta_cursor_renderer_real_update_cursor (MetaCursorRenderer *renderer,
                                         MetaCursorSprite   *cursor_sprite)
{
  if (cursor_sprite)
    meta_cursor_sprite_realize_texture (cursor_sprite);

  return FALSE;
}

static void
meta_cursor_renderer_finalize (GObject *object)
{
  MetaCursorRenderer *renderer = META_CURSOR_RENDERER (object);
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);
  MetaBackend *backend = meta_get_backend ();
  ClutterActor *stage = meta_backend_get_stage (backend);

  if (priv->stage_overlay)
    meta_stage_remove_cursor_overlay (META_STAGE (stage), priv->stage_overlay);

  clutter_threads_remove_repaint_func (priv->post_paint_func_id);

  G_OBJECT_CLASS (meta_cursor_renderer_parent_class)->finalize (object);
}

static void
meta_cursor_renderer_class_init (MetaCursorRendererClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_cursor_renderer_finalize;
  klass->update_cursor = meta_cursor_renderer_real_update_cursor;

  signals[CURSOR_PAINTED] = g_signal_new ("cursor-painted",
                                          G_TYPE_FROM_CLASS (klass),
                                          G_SIGNAL_RUN_LAST,
                                          0,
                                          NULL, NULL, NULL,
                                          G_TYPE_NONE, 1,
                                          G_TYPE_POINTER);
}

static void
meta_cursor_renderer_init (MetaCursorRenderer *renderer)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);

  priv->post_paint_func_id =
    clutter_threads_add_repaint_func_full (CLUTTER_REPAINT_FLAGS_POST_PAINT,
                                           meta_cursor_renderer_post_paint,
                                           renderer,
                                           NULL);
}

graphene_rect_t
meta_cursor_renderer_calculate_rect (MetaCursorRenderer *renderer,
                                     MetaCursorSprite   *cursor_sprite)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);
  CoglTexture *texture;
  int hot_x, hot_y;
  int width, height;
  float texture_scale;

  texture = meta_cursor_sprite_get_cogl_texture (cursor_sprite);
  if (!texture)
    return (graphene_rect_t) GRAPHENE_RECT_INIT_ZERO;

  meta_cursor_sprite_get_hotspot (cursor_sprite, &hot_x, &hot_y);
  texture_scale = meta_cursor_sprite_get_texture_scale (cursor_sprite);
  width = cogl_texture_get_width (texture);
  height = cogl_texture_get_height (texture);

  return (graphene_rect_t) {
    .origin = {
      .x = priv->current_x - (hot_x * texture_scale),
      .y = priv->current_y - (hot_y * texture_scale)
    },
    .size = {
      .width = width * texture_scale,
      .height = height * texture_scale
    }
  };
}

static void
meta_cursor_renderer_update_cursor (MetaCursorRenderer *renderer,
                                    MetaCursorSprite   *cursor_sprite)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);
  gboolean handled_by_backend;
  gboolean should_redraw = FALSE;

  if (cursor_sprite)
    meta_cursor_sprite_prepare_at (cursor_sprite,
                                   (int) priv->current_x,
                                   (int) priv->current_y);

  handled_by_backend =
    META_CURSOR_RENDERER_GET_CLASS (renderer)->update_cursor (renderer,
                                                              cursor_sprite);
  if (handled_by_backend != priv->handled_by_backend)
    {
      priv->handled_by_backend = handled_by_backend;
      should_redraw = TRUE;
    }

  if (!handled_by_backend)
    should_redraw = TRUE;

  if (should_redraw)
    queue_redraw (renderer, cursor_sprite);
}

MetaCursorRenderer *
meta_cursor_renderer_new (void)
{
  return g_object_new (META_TYPE_CURSOR_RENDERER, NULL);
}

void
meta_cursor_renderer_set_cursor (MetaCursorRenderer *renderer,
                                 MetaCursorSprite   *cursor_sprite)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);

  if (priv->displayed_cursor == cursor_sprite)
    return;
  priv->displayed_cursor = cursor_sprite;

  meta_cursor_renderer_update_cursor (renderer, cursor_sprite);
}

void
meta_cursor_renderer_force_update (MetaCursorRenderer *renderer)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);

  meta_cursor_renderer_update_cursor (renderer, priv->displayed_cursor);
}

void
meta_cursor_renderer_set_position (MetaCursorRenderer *renderer,
                                   float               x,
                                   float               y)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);

  g_assert (meta_is_wayland_compositor ());

  priv->current_x = x;
  priv->current_y = y;

  meta_cursor_renderer_update_cursor (renderer, priv->displayed_cursor);
}

graphene_point_t
meta_cursor_renderer_get_position (MetaCursorRenderer *renderer)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);

  return (graphene_point_t) {
    .x = priv->current_x,
    .y = priv->current_y
  };
}

MetaCursorSprite *
meta_cursor_renderer_get_cursor (MetaCursorRenderer *renderer)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);

  return priv->displayed_cursor;
}

void
meta_cursor_renderer_add_hw_cursor_inhibitor (MetaCursorRenderer    *renderer,
                                              MetaHwCursorInhibitor *inhibitor)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);

  priv->hw_cursor_inhibitors = g_list_prepend (priv->hw_cursor_inhibitors,
                                               inhibitor);
}

void
meta_cursor_renderer_remove_hw_cursor_inhibitor (MetaCursorRenderer    *renderer,
                                                 MetaHwCursorInhibitor *inhibitor)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);

  priv->hw_cursor_inhibitors = g_list_remove (priv->hw_cursor_inhibitors,
                                              inhibitor);
}

gboolean
meta_cursor_renderer_is_hw_cursors_inhibited (MetaCursorRenderer *renderer,
                                              MetaCursorSprite   *cursor_sprite)
{
  MetaCursorRendererPrivate *priv =
    meta_cursor_renderer_get_instance_private (renderer);
  GList *l;

  for (l = priv->hw_cursor_inhibitors; l; l = l->next)
    {
      MetaHwCursorInhibitor *inhibitor = l->data;

      if (meta_hw_cursor_inhibitor_is_cursor_sprite_inhibited (inhibitor,
                                                               cursor_sprite))
        return TRUE;
    }

  return FALSE;
}
