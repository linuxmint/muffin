/*
 * Authored By Neil Roberts  <neil@linux.intel.com>
 * and Jasper St. Pierre <jstpierre@mecheye.net>
 *
 * Copyright (C) 2008 Intel Corporation
 * Copyright (C) 2012 Red Hat, Inc.
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION:meta-shaped-texture
 * @title: MetaShapedTexture
 * @short_description: An actor to draw a masked texture.
 */

#include "config.h"

#include "backends/meta-monitor-transform.h"
#include "compositor/meta-shaped-texture-private.h"
#include "core/boxes-private.h"

#include <gdk/gdk.h>
#include <math.h>

#include "cogl/cogl.h"
#include "compositor/clutter-utils.h"
#include "compositor/meta-texture-tower.h"
#include "compositor/region-utils.h"
#include "core/boxes-private.h"
#include "meta/meta-shaped-texture.h"

/* MAX_MIPMAPPING_FPS needs to be as small as possible for the best GPU
 * performance, but higher than the refresh rate of commonly slow updating
 * windows like top or a blinking cursor, so that such windows do get
 * mipmapped.
 */
#define MAX_MIPMAPPING_FPS 5
#define MIN_MIPMAP_AGE_USEC (G_USEC_PER_SEC / MAX_MIPMAPPING_FPS)

/* MIN_FAST_UPDATES_BEFORE_UNMIPMAP allows windows to update themselves
 * occasionally without causing mipmapping to be disabled, so long as such
 * an update takes fewer update_area calls than:
 */
#define MIN_FAST_UPDATES_BEFORE_UNMIPMAP 20

static void meta_shaped_texture_dispose  (GObject    *object);

static void clutter_content_iface_init (ClutterContentInterface *iface);

enum
{
  SIZE_CHANGED,

  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL];

struct _MetaShapedTexture
{
  GObject parent;

  MetaTextureTower *paint_tower;

  CoglTexture *texture;
  CoglTexture *mask_texture;
  CoglSnippet *snippet;

  CoglPipeline *base_pipeline;
  CoglPipeline *masked_pipeline;
  CoglPipeline *unblended_pipeline;

  gboolean is_y_inverted;

  /* The region containing only fully opaque pixels */
  cairo_region_t *opaque_region;

  /* MetaCullable regions, see that documentation for more details */
  cairo_region_t *clip_region;

  gboolean size_invalid;
  MetaMonitorTransform transform;
  gboolean has_viewport_src_rect;
  graphene_rect_t viewport_src_rect;
  gboolean has_viewport_dst_size;
  int viewport_dst_width;
  int viewport_dst_height;

  int tex_width, tex_height;
  int fallback_width, fallback_height;
  int dst_width, dst_height;

  gint64 prev_invalidation, last_invalidation;
  guint fast_updates;
  guint remipmap_timeout_id;
  gint64 earliest_remipmap;

  int buffer_scale;

  guint create_mipmaps : 1;
};

G_DEFINE_TYPE_WITH_CODE (MetaShapedTexture, meta_shaped_texture, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_CONTENT,
                                                clutter_content_iface_init));

static void
meta_shaped_texture_class_init (MetaShapedTextureClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->dispose = meta_shaped_texture_dispose;

  signals[SIZE_CHANGED] = g_signal_new ("size-changed",
                                        G_TYPE_FROM_CLASS (gobject_class),
                                        G_SIGNAL_RUN_LAST,
                                        0,
                                        NULL, NULL, NULL,
                                        G_TYPE_NONE, 0);
}

static void
invalidate_size (MetaShapedTexture *stex)
{
  stex->size_invalid = TRUE;
}

static void
meta_shaped_texture_init (MetaShapedTexture *stex)
{
  stex->paint_tower = meta_texture_tower_new ();

  stex->buffer_scale = 1;
  stex->texture = NULL;
  stex->mask_texture = NULL;
  stex->create_mipmaps = TRUE;
  stex->is_y_inverted = TRUE;
  stex->transform = META_MONITOR_TRANSFORM_NORMAL;
}

static void
update_size (MetaShapedTexture *stex)
{
  int buffer_scale = stex->buffer_scale;
  int dst_width;
  int dst_height;

  if (stex->has_viewport_dst_size)
    {
      dst_width = stex->viewport_dst_width;
      dst_height = stex->viewport_dst_height;
    }
  else if (stex->has_viewport_src_rect)
    {
      dst_width = stex->viewport_src_rect.size.width;
      dst_height = stex->viewport_src_rect.size.height;
    }
  else
    {
      if (meta_monitor_transform_is_rotated (stex->transform))
        {
          if (stex->texture)
            {
              dst_width = stex->tex_height / buffer_scale;
              dst_height = stex->tex_width / buffer_scale;
            }
          else
            {
              dst_width = stex->fallback_height / buffer_scale;
              dst_height = stex->fallback_width / buffer_scale;
            }
        }
      else
        {
          if (stex->texture)
            {
              dst_width = stex->tex_width / buffer_scale;
              dst_height = stex->tex_height / buffer_scale;
            }
          else
            {
              dst_width = stex->fallback_width / buffer_scale;
              dst_height = stex->fallback_height / buffer_scale;
            }
        }
    }

  stex->size_invalid = FALSE;

  if (stex->dst_width != dst_width ||
      stex->dst_height != dst_height)
    {
      stex->dst_width = dst_width;
      stex->dst_height = dst_height;
      meta_shaped_texture_set_mask_texture (stex, NULL);
      clutter_content_invalidate_size (CLUTTER_CONTENT (stex));
      g_signal_emit (stex, signals[SIZE_CHANGED], 0);
    }
}

static void
ensure_size_valid (MetaShapedTexture *stex)
{
  if (stex->size_invalid)
    update_size (stex);
}

void
meta_shaped_texture_set_clip_region (MetaShapedTexture *stex,
                                     cairo_region_t    *clip_region)
{
  g_clear_pointer (&stex->clip_region, cairo_region_destroy);
  if (clip_region)
    stex->clip_region = cairo_region_reference (clip_region);
}

static void
meta_shaped_texture_reset_pipelines (MetaShapedTexture *stex)
{
  g_clear_pointer (&stex->base_pipeline, cogl_object_unref);
  g_clear_pointer (&stex->masked_pipeline, cogl_object_unref);
  g_clear_pointer (&stex->unblended_pipeline, cogl_object_unref);
}

static void
meta_shaped_texture_dispose (GObject *object)
{
  MetaShapedTexture *stex = (MetaShapedTexture *) object;

  g_clear_handle_id (&stex->remipmap_timeout_id, g_source_remove);

  if (stex->paint_tower)
    meta_texture_tower_free (stex->paint_tower);
  stex->paint_tower = NULL;

  g_clear_pointer (&stex->texture, cogl_object_unref);

  meta_shaped_texture_set_mask_texture (stex, NULL);
  meta_shaped_texture_reset_pipelines (stex);

  g_clear_pointer (&stex->opaque_region, cairo_region_destroy);
  g_clear_pointer (&stex->clip_region, cairo_region_destroy);

  g_clear_pointer (&stex->snippet, cogl_object_unref);

  G_OBJECT_CLASS (meta_shaped_texture_parent_class)->dispose (object);
}

static CoglPipeline *
get_base_pipeline (MetaShapedTexture *stex,
                   CoglContext       *ctx)
{
  CoglPipeline *pipeline;
  CoglMatrix matrix;

  if (stex->base_pipeline)
    return stex->base_pipeline;

  pipeline = cogl_pipeline_new (ctx);
  cogl_pipeline_set_layer_wrap_mode_s (pipeline, 0,
                                       COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
  cogl_pipeline_set_layer_wrap_mode_t (pipeline, 0,
                                       COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
  cogl_pipeline_set_layer_wrap_mode_s (pipeline, 1,
                                       COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
  cogl_pipeline_set_layer_wrap_mode_t (pipeline, 1,
                                       COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);

  cogl_matrix_init_identity (&matrix);

  if (!stex->is_y_inverted)
    {
      cogl_matrix_scale (&matrix, 1, -1, 1);
      cogl_matrix_translate (&matrix, 0, -1, 0);
      cogl_pipeline_set_layer_matrix (pipeline, 0, &matrix);
    }

  if (stex->transform != META_MONITOR_TRANSFORM_NORMAL)
    {
      graphene_euler_t euler;

      cogl_matrix_translate (&matrix, 0.5, 0.5, 0.0);
      switch (stex->transform)
        {
        case META_MONITOR_TRANSFORM_90:
          graphene_euler_init_with_order (&euler, 0.0, 0.0, 90.0,
                                          GRAPHENE_EULER_ORDER_SYXZ);
          break;
        case META_MONITOR_TRANSFORM_180:
          graphene_euler_init_with_order (&euler, 0.0, 0.0, 180.0,
                                          GRAPHENE_EULER_ORDER_SYXZ);
          break;
        case META_MONITOR_TRANSFORM_270:
          graphene_euler_init_with_order (&euler, 0.0, 0.0, 270.0,
                                          GRAPHENE_EULER_ORDER_SYXZ);
          break;
        case META_MONITOR_TRANSFORM_FLIPPED:
          graphene_euler_init_with_order (&euler, 0.0, 180.0, 0.0,
                                          GRAPHENE_EULER_ORDER_SYXZ);
          break;
        case META_MONITOR_TRANSFORM_FLIPPED_90:
          graphene_euler_init_with_order (&euler, 180.0, 0.0, 90.0,
                                          GRAPHENE_EULER_ORDER_SYXZ);
          break;
        case META_MONITOR_TRANSFORM_FLIPPED_180:
          graphene_euler_init_with_order (&euler, 0.0, 180.0, 180.0,
                                          GRAPHENE_EULER_ORDER_SYXZ);
          break;
        case META_MONITOR_TRANSFORM_FLIPPED_270:
          graphene_euler_init_with_order (&euler, 180.0, 0.0, 270.0,
                                          GRAPHENE_EULER_ORDER_SYXZ);
          break;
        case META_MONITOR_TRANSFORM_NORMAL:
          g_assert_not_reached ();
        }
      cogl_matrix_rotate_euler (&matrix, &euler);
      cogl_matrix_translate (&matrix, -0.5, -0.5, 0.0);
    }

  if (stex->has_viewport_src_rect)
    {
      float scaled_tex_width = stex->tex_width / (float) stex->buffer_scale;
      float scaled_tex_height = stex->tex_height / (float) stex->buffer_scale;

      if (meta_monitor_transform_is_rotated (stex->transform))
        {
          cogl_matrix_scale (&matrix,
                             stex->viewport_src_rect.size.width /
                             scaled_tex_height,
                             stex->viewport_src_rect.size.height /
                             scaled_tex_width,
                             1);
        }
      else
        {
          cogl_matrix_scale (&matrix,
                             stex->viewport_src_rect.size.width /
                             scaled_tex_width,
                             stex->viewport_src_rect.size.height /
                             scaled_tex_height,
                             1);
        }

      cogl_matrix_translate (&matrix,
                             stex->viewport_src_rect.origin.x /
                             stex->viewport_src_rect.size.width,
                             stex->viewport_src_rect.origin.y /
                             stex->viewport_src_rect.size.height,
                             0);
    }

  cogl_pipeline_set_layer_matrix (pipeline, 0, &matrix);
  cogl_pipeline_set_layer_matrix (pipeline, 1, &matrix);

  if (stex->snippet)
    cogl_pipeline_add_layer_snippet (pipeline, 0, stex->snippet);

  stex->base_pipeline = pipeline;

  return stex->base_pipeline;
}

static CoglPipeline *
get_unmasked_pipeline (MetaShapedTexture *stex,
                       CoglContext       *ctx)
{
  return get_base_pipeline (stex, ctx);
}

static CoglPipeline *
get_masked_pipeline (MetaShapedTexture *stex,
                     CoglContext       *ctx)
{
  CoglPipeline *pipeline;

  if (stex->masked_pipeline)
    return stex->masked_pipeline;

  pipeline = cogl_pipeline_copy (get_base_pipeline (stex, ctx));
  cogl_pipeline_set_layer_combine (pipeline, 1,
                                   "RGBA = MODULATE (PREVIOUS, TEXTURE[A])",
                                   NULL);

  stex->masked_pipeline = pipeline;

  return pipeline;
}

static CoglPipeline *
get_unblended_pipeline (MetaShapedTexture *stex,
                        CoglContext       *ctx)
{
  CoglPipeline *pipeline;

  if (stex->unblended_pipeline)
    return stex->unblended_pipeline;

  pipeline = cogl_pipeline_copy (get_base_pipeline (stex, ctx));
  cogl_pipeline_set_layer_combine (pipeline, 0,
                                   "RGBA = REPLACE (TEXTURE)",
                                   NULL);

  stex->unblended_pipeline = pipeline;

  return pipeline;
}

static void
paint_clipped_rectangle_node (MetaShapedTexture     *stex,
                              ClutterPaintNode      *root_node,
                              CoglPipeline          *pipeline,
                              cairo_rectangle_int_t *rect,
                              ClutterActorBox       *alloc)
{
  g_autoptr (ClutterPaintNode) node = NULL;
  float ratio_h, ratio_v;
  float x1, y1, x2, y2;
  float coords[8];
  float alloc_width;
  float alloc_height;

  ratio_h = clutter_actor_box_get_width (alloc) / (float) stex->dst_width;
  ratio_v = clutter_actor_box_get_height (alloc) / (float) stex->dst_height;

  x1 = alloc->x1 + rect->x * ratio_h;
  y1 = alloc->y1 + rect->y * ratio_v;
  x2 = alloc->x1 + (rect->x + rect->width) * ratio_h;
  y2 = alloc->y1 + (rect->y + rect->height) * ratio_v;

  alloc_width = alloc->x2 - alloc->x1;
  alloc_height = alloc->y2 - alloc->y1;

  coords[0] = rect->x / alloc_width * ratio_h;
  coords[1] = rect->y / alloc_height * ratio_v;
  coords[2] = (rect->x + rect->width) / alloc_width * ratio_h;
  coords[3] = (rect->y + rect->height) / alloc_height * ratio_v;

  coords[4] = coords[0];
  coords[5] = coords[1];
  coords[6] = coords[2];
  coords[7] = coords[3];

  node = clutter_pipeline_node_new (pipeline);
  clutter_paint_node_set_static_name (node, "MetaShapedTexture (clipped)");
  clutter_paint_node_add_child (root_node, node);

  clutter_paint_node_add_multitexture_rectangle (node,
                                                 &(ClutterActorBox) {
                                                   .x1 = x1,
                                                   .y1 = y1,
                                                   .x2 = x2,
                                                   .y2 = y2,
                                                 },
                                                 coords, 8);
}

static void
set_cogl_texture (MetaShapedTexture *stex,
                  CoglTexture       *cogl_tex)
{
  int width, height;

  cogl_clear_object (&stex->texture);

  if (cogl_tex != NULL)
    {
      stex->texture = cogl_object_ref (cogl_tex);
      width = cogl_texture_get_width (COGL_TEXTURE (cogl_tex));
      height = cogl_texture_get_height (COGL_TEXTURE (cogl_tex));
    }
  else
    {
      width = 0;
      height = 0;
    }

  if (stex->tex_width != width ||
      stex->tex_height != height)
    {
      stex->tex_width = width;
      stex->tex_height = height;
      update_size (stex);
    }

  /* NB: We don't queue a redraw of the actor here because we don't
   * know how much of the buffer has changed with respect to the
   * previous buffer. We only queue a redraw in response to surface
   * damage. */

  if (stex->create_mipmaps)
    meta_texture_tower_set_base_texture (stex->paint_tower, cogl_tex);
}

static gboolean
texture_is_idle_and_not_mipmapped (gpointer user_data)
{
  MetaShapedTexture *stex = META_SHAPED_TEXTURE (user_data);

  if ((g_get_monotonic_time () - stex->earliest_remipmap) < 0)
    return G_SOURCE_CONTINUE;

  clutter_content_invalidate (CLUTTER_CONTENT (stex));
  stex->remipmap_timeout_id = 0;

  return G_SOURCE_REMOVE;
}

static inline void
flip_ints (int *x,
           int *y)
{
  int tmp;

  tmp = *x;
  *x = *y;
  *y = tmp;
}

static void
do_paint_content (MetaShapedTexture   *stex,
                  ClutterPaintNode    *root_node,
                  ClutterPaintContext *paint_context,
                  CoglTexture         *paint_tex,
                  ClutterActorBox     *alloc,
                  uint8_t              opacity)
{
  int dst_width, dst_height;
  cairo_rectangle_int_t content_rect;
  gboolean use_opaque_region;
  cairo_region_t *blended_tex_region;
  CoglContext *ctx;
  CoglPipelineFilter filter;
  CoglFramebuffer *framebuffer;
  int sample_width, sample_height;

  ensure_size_valid (stex);

  dst_width = stex->dst_width;
  dst_height = stex->dst_height;

  if (dst_width == 0 || dst_height == 0) /* no contents yet */
    return;

  content_rect = (cairo_rectangle_int_t) {
    .x = 0,
    .y = 0,
    .width = dst_width,
    .height = dst_height,
  };

  /* Use nearest-pixel interpolation if the texture is unscaled. This
   * improves performance, especially with software rendering.
   */

  framebuffer = clutter_paint_node_get_framebuffer (root_node);
  if (!framebuffer)
    framebuffer = clutter_paint_context_get_framebuffer (paint_context);

  if (stex->has_viewport_src_rect)
    {
      sample_width = stex->viewport_src_rect.size.width * stex->buffer_scale;
      sample_height = stex->viewport_src_rect.size.height * stex->buffer_scale;
    }
  else
    {
      sample_width = cogl_texture_get_width (stex->texture);
      sample_height = cogl_texture_get_height (stex->texture);
    }
  if (meta_monitor_transform_is_rotated (stex->transform))
    flip_ints (&sample_width, &sample_height);

  if (meta_actor_painting_untransformed (framebuffer,
                                         dst_width, dst_height,
                                         sample_width, sample_height,
                                         NULL, NULL))
    filter = COGL_PIPELINE_FILTER_NEAREST;
  else
    filter = COGL_PIPELINE_FILTER_LINEAR;

  ctx = clutter_backend_get_cogl_context (clutter_get_default_backend ());

  use_opaque_region = stex->opaque_region && opacity == 255;

  if (use_opaque_region)
    {
      if (stex->clip_region)
        blended_tex_region = cairo_region_copy (stex->clip_region);
      else
        blended_tex_region = cairo_region_create_rectangle (&content_rect);

      cairo_region_subtract (blended_tex_region, stex->opaque_region);
    }
  else
    {
      if (stex->clip_region)
        blended_tex_region = cairo_region_reference (stex->clip_region);
      else
        blended_tex_region = NULL;
    }

  /* Limit to how many separate rectangles we'll draw; beyond this just
   * fall back and draw the whole thing */
#define MAX_RECTS 16

  if (blended_tex_region)
    {
      int n_rects = cairo_region_num_rectangles (blended_tex_region);
      if (n_rects > MAX_RECTS)
        {
          /* Fall back to taking the fully blended path. */
          use_opaque_region = FALSE;

          g_clear_pointer (&blended_tex_region, cairo_region_destroy);
        }
    }

  /* First, paint the unblended parts, which are part of the opaque region. */
  if (use_opaque_region)
    {
      cairo_region_t *region;
      int n_rects;
      int i;

      if (stex->clip_region)
        {
          region = cairo_region_copy (stex->clip_region);
          cairo_region_intersect (region, stex->opaque_region);
        }
      else
        {
          region = cairo_region_reference (stex->opaque_region);
        }

      if (!cairo_region_is_empty (region))
        {
          CoglPipeline *opaque_pipeline;

          opaque_pipeline = get_unblended_pipeline (stex, ctx);
          cogl_pipeline_set_layer_texture (opaque_pipeline, 0, paint_tex);
          cogl_pipeline_set_layer_filters (opaque_pipeline, 0, filter, filter);

          n_rects = cairo_region_num_rectangles (region);
          for (i = 0; i < n_rects; i++)
            {
              cairo_rectangle_int_t rect;
              cairo_region_get_rectangle (region, i, &rect);
              paint_clipped_rectangle_node (stex, root_node,
                                            opaque_pipeline,
                                            &rect, alloc);
            }
        }

      cairo_region_destroy (region);
    }

  /* Now, go ahead and paint the blended parts. */

  /* We have three cases:
   *   1) blended_tex_region has rectangles - paint the rectangles.
   *   2) blended_tex_region is empty - don't paint anything
   *   3) blended_tex_region is NULL - paint fully-blended.
   *
   *   1) and 3) are the times where we have to paint stuff. This tests
   *   for 1) and 3).
   */
  if (!blended_tex_region || !cairo_region_is_empty (blended_tex_region))
    {
      CoglPipeline *blended_pipeline;

      if (stex->mask_texture == NULL)
        {
          blended_pipeline = get_unmasked_pipeline (stex, ctx);
        }
      else
        {
          blended_pipeline = get_masked_pipeline (stex, ctx);
          cogl_pipeline_set_layer_texture (blended_pipeline, 1, stex->mask_texture);
          cogl_pipeline_set_layer_filters (blended_pipeline, 1, filter, filter);
        }

      cogl_pipeline_set_layer_texture (blended_pipeline, 0, paint_tex);
      cogl_pipeline_set_layer_filters (blended_pipeline, 0, filter, filter);

      CoglColor color;
      cogl_color_init_from_4ub (&color, opacity, opacity, opacity, opacity);
      cogl_pipeline_set_color (blended_pipeline, &color);

      if (blended_tex_region)
        {
          /* 1) blended_tex_region is not empty. Paint the rectangles. */
          int i;
          int n_rects = cairo_region_num_rectangles (blended_tex_region);

          for (i = 0; i < n_rects; i++)
            {
              cairo_rectangle_int_t rect;
              cairo_region_get_rectangle (blended_tex_region, i, &rect);

              if (!gdk_rectangle_intersect (&content_rect, &rect, &rect))
                continue;

              paint_clipped_rectangle_node (stex, root_node,
                                            blended_pipeline,
                                            &rect, alloc);
            }
        }
      else
        {
          g_autoptr (ClutterPaintNode) node = NULL;

          node = clutter_pipeline_node_new (blended_pipeline);
          clutter_paint_node_set_static_name (node, "MetaShapedTexture (unclipped)");
          clutter_paint_node_add_child (root_node, node);

          /* 3) blended_tex_region is NULL. Do a full paint. */
          clutter_paint_node_add_rectangle (node, alloc);
        }
    }

  g_clear_pointer (&blended_tex_region, cairo_region_destroy);
}

static CoglTexture *
select_texture_for_paint (MetaShapedTexture   *stex,
                          ClutterPaintContext *paint_context)
{
  CoglTexture *texture = NULL;
  int64_t now;

  if (!stex->texture)
    return NULL;

  now = g_get_monotonic_time ();

  if (stex->create_mipmaps && stex->last_invalidation)
    {
      int64_t age = now - stex->last_invalidation;

      if (age >= MIN_MIPMAP_AGE_USEC ||
          stex->fast_updates < MIN_FAST_UPDATES_BEFORE_UNMIPMAP)
        {
          texture = meta_texture_tower_get_paint_texture (stex->paint_tower,
                                                          paint_context);
        }
    }

  if (!texture)
    {
      texture = stex->texture;

      if (stex->create_mipmaps)
        {
          /* Minus 1000 to ensure we don't fail the age test in timeout */
          stex->earliest_remipmap = now + MIN_MIPMAP_AGE_USEC - 1000;

          if (!stex->remipmap_timeout_id)
            stex->remipmap_timeout_id =
              g_timeout_add (MIN_MIPMAP_AGE_USEC / 1000,
                             texture_is_idle_and_not_mipmapped,
                             stex);
        }
    }

  return texture;
}

static void
meta_shaped_texture_paint_content (ClutterContent      *content,
                                   ClutterActor        *actor,
                                   ClutterPaintNode    *root_node,
                                   ClutterPaintContext *paint_context)
{
  MetaShapedTexture *stex = META_SHAPED_TEXTURE (content);
  ClutterActorBox alloc;
  CoglTexture *paint_tex = NULL;
  uint8_t opacity;

  if (stex->clip_region && cairo_region_is_empty (stex->clip_region))
    return;

  /* The GL EXT_texture_from_pixmap extension does allow for it to be
   * used together with SGIS_generate_mipmap, however this is very
   * rarely supported. Also, even when it is supported there
   * are distinct performance implications from:
   *
   *  - Updating mipmaps that we don't need
   *  - Having to reallocate pixmaps on the server into larger buffers
   *
   * So, we just unconditionally use our mipmap emulation code. If we
   * wanted to use SGIS_generate_mipmap, we'd have to  query COGL to
   * see if it was supported (no API currently), and then if and only
   * if that was the case, set the clutter texture quality to HIGH.
   * Setting the texture quality to high without SGIS_generate_mipmap
   * support for TFP textures will result in fallbacks to XGetImage.
   */
  paint_tex = select_texture_for_paint (stex, paint_context);
  if (!paint_tex)
    return;

  opacity = clutter_actor_get_paint_opacity (actor);
  clutter_actor_get_content_box (actor, &alloc);

  do_paint_content (stex, root_node, paint_context, paint_tex, &alloc, opacity);
}

static gboolean
meta_shaped_texture_get_preferred_size (ClutterContent *content,
                                        float          *width,
                                        float          *height)
{
  MetaShapedTexture *stex = META_SHAPED_TEXTURE (content);

  ensure_size_valid (stex);

  if (width)
    *width = stex->dst_width;

  if (height)
    *height = stex->dst_height;

  return TRUE;
}

static void
clutter_content_iface_init (ClutterContentInterface *iface)
{
  iface->paint_content = meta_shaped_texture_paint_content;
  iface->get_preferred_size = meta_shaped_texture_get_preferred_size;
}

void
meta_shaped_texture_set_create_mipmaps (MetaShapedTexture *stex,
					gboolean           create_mipmaps)
{
  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  create_mipmaps = create_mipmaps != FALSE;

  if (create_mipmaps != stex->create_mipmaps)
    {
      CoglTexture *base_texture;
      stex->create_mipmaps = create_mipmaps;
      base_texture = create_mipmaps ? stex->texture : NULL;
      meta_texture_tower_set_base_texture (stex->paint_tower, base_texture);
    }
}

void
meta_shaped_texture_set_mask_texture (MetaShapedTexture *stex,
                                      CoglTexture       *mask_texture)
{
  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  g_clear_pointer (&stex->mask_texture, cogl_object_unref);

  if (mask_texture != NULL)
    {
      stex->mask_texture = mask_texture;
      cogl_object_ref (stex->mask_texture);
    }

  clutter_content_invalidate (CLUTTER_CONTENT (stex));
}

/**
 * meta_shaped_texture_update_area:
 * @stex: #MetaShapedTexture
 * @x: the x coordinate of the damaged area
 * @y: the y coordinate of the damaged area
 * @width: the width of the damaged area
 * @height: the height of the damaged area
 * @clip: (out): the resulting clip region
 *
 * Repairs the damaged area indicated by @x, @y, @width and @height
 * and potentially queues a redraw.
 *
 * Return value: Whether a redraw have been queued or not
 */
gboolean
meta_shaped_texture_update_area (MetaShapedTexture     *stex,
                                 int                    x,
                                 int                    y,
                                 int                    width,
                                 int                    height,
                                 cairo_rectangle_int_t *clip)
{
  MetaMonitorTransform inverted_transform;

  if (stex->texture == NULL)
    return FALSE;

  *clip = (cairo_rectangle_int_t) {
    .x = x,
    .y = y,
    .width = width,
    .height = height
  };

  meta_rectangle_scale_double (clip,
                               1.0 / stex->buffer_scale,
                               META_ROUNDING_STRATEGY_SHRINK,
                               clip);

  inverted_transform = meta_monitor_transform_invert (stex->transform);
  ensure_size_valid (stex);
  meta_rectangle_transform (clip,
                            inverted_transform,
                            stex->dst_width,
                            stex->dst_height,
                            clip);

  if (stex->has_viewport_src_rect || stex->has_viewport_dst_size)
    {
      graphene_rect_t viewport;
      graphene_rect_t inverted_viewport;
      float dst_width;
      float dst_height;
      int inverted_dst_width;
      int inverted_dst_height;

      if (stex->has_viewport_src_rect)
        {
          viewport = stex->viewport_src_rect;
        }
      else
        {
          viewport = (graphene_rect_t) {
            .origin.x = 0,
            .origin.y = 0,
            .size.width = stex->tex_width,
            .size.height = stex->tex_height,
          };
        }

      if (stex->has_viewport_dst_size)
        {
          dst_width = (float) stex->viewport_dst_width;
          dst_height = (float) stex->viewport_dst_height;
        }
      else
        {
          dst_width = (float) stex->tex_width;
          dst_height = (float) stex->tex_height;
        }

      inverted_viewport = (graphene_rect_t) {
        .origin.x = -(viewport.origin.x * (dst_width / viewport.size.width)),
        .origin.y = -(viewport.origin.y * (dst_height / viewport.size.height)),
        .size.width = dst_width,
        .size.height = dst_height
      };
      inverted_dst_width = ceilf (viewport.size.width);
      inverted_dst_height = ceilf (viewport.size.height);

      meta_rectangle_crop_and_scale (clip,
                                     &inverted_viewport,
                                     inverted_dst_width,
                                     inverted_dst_height,
                                     clip);
    }

  meta_texture_tower_update_area (stex->paint_tower,
                                  x,
                                  y,
                                  width,
                                  height);

  stex->prev_invalidation = stex->last_invalidation;
  stex->last_invalidation = g_get_monotonic_time ();

  if (stex->prev_invalidation)
    {
      gint64 interval = stex->last_invalidation - stex->prev_invalidation;
      gboolean fast_update = interval < MIN_MIPMAP_AGE_USEC;

      if (!fast_update)
        stex->fast_updates = 0;
      else if (stex->fast_updates < MIN_FAST_UPDATES_BEFORE_UNMIPMAP)
        stex->fast_updates++;
    }

  return TRUE;
}

/**
 * meta_shaped_texture_set_texture:
 * @stex: The #MetaShapedTexture
 * @pixmap: The #CoglTexture to display
 */
void
meta_shaped_texture_set_texture (MetaShapedTexture *stex,
                                 CoglTexture       *texture)
{
  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  if (stex->texture == texture)
    return;

  set_cogl_texture (stex, texture);
}

/**
 * meta_shaped_texture_set_is_y_inverted: (skip)
 */
void
meta_shaped_texture_set_is_y_inverted (MetaShapedTexture *stex,
                                       gboolean           is_y_inverted)
{
  if (stex->is_y_inverted == is_y_inverted)
    return;

  meta_shaped_texture_reset_pipelines (stex);

  stex->is_y_inverted = is_y_inverted;
}

/**
 * meta_shaped_texture_set_snippet: (skip)
 */
void
meta_shaped_texture_set_snippet (MetaShapedTexture *stex,
                                 CoglSnippet       *snippet)
{
  if (stex->snippet == snippet)
    return;

  meta_shaped_texture_reset_pipelines (stex);

  g_clear_pointer (&stex->snippet, cogl_object_unref);
  if (snippet)
    stex->snippet = cogl_object_ref (snippet);
}

/**
 * meta_shaped_texture_get_texture:
 * @stex: The #MetaShapedTexture
 *
 * Returns: (transfer none): the unshaped texture
 */
CoglTexture *
meta_shaped_texture_get_texture (MetaShapedTexture *stex)
{
  g_return_val_if_fail (META_IS_SHAPED_TEXTURE (stex), NULL);
  return COGL_TEXTURE (stex->texture);
}

/**
 * meta_shaped_texture_set_opaque_region:
 * @stex: a #MetaShapedTexture
 * @opaque_region: (transfer full): the region of the texture that
 *   can have blending turned off.
 *
 * As most windows have a large portion that does not require blending,
 * we can easily turn off blending if we know the areas that do not
 * require blending. This sets the region where we will not blend for
 * optimization purposes.
 */
void
meta_shaped_texture_set_opaque_region (MetaShapedTexture *stex,
                                       cairo_region_t    *opaque_region)
{
  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  if (stex->opaque_region)
    cairo_region_destroy (stex->opaque_region);

  if (opaque_region)
    stex->opaque_region = cairo_region_reference (opaque_region);
  else
    stex->opaque_region = NULL;
}

cairo_region_t *
meta_shaped_texture_get_opaque_region (MetaShapedTexture *stex)
{
  return stex->opaque_region;
}

gboolean
meta_shaped_texture_has_alpha (MetaShapedTexture *stex)
{
  CoglTexture *texture;

  texture = stex->texture;
  if (!texture)
    return TRUE;

  switch (cogl_texture_get_components (texture))
    {
    case COGL_TEXTURE_COMPONENTS_A:
    case COGL_TEXTURE_COMPONENTS_RGBA:
      return TRUE;
    case COGL_TEXTURE_COMPONENTS_RG:
    case COGL_TEXTURE_COMPONENTS_RGB:
    case COGL_TEXTURE_COMPONENTS_DEPTH:
      return FALSE;
    }

  g_warn_if_reached ();
  return FALSE;
}

gboolean
meta_shaped_texture_is_opaque (MetaShapedTexture *stex)
{
  CoglTexture *texture;
  cairo_rectangle_int_t opaque_rect;

  texture = stex->texture;
  if (!texture)
    return FALSE;

  if (!meta_shaped_texture_has_alpha (stex))
    return TRUE;

  if (!stex->opaque_region)
    return FALSE;

  if (cairo_region_num_rectangles (stex->opaque_region) != 1)
    return FALSE;

  cairo_region_get_extents (stex->opaque_region, &opaque_rect);

  ensure_size_valid (stex);

  return meta_rectangle_equal (&opaque_rect,
                               &(MetaRectangle) {
                                .width = stex->dst_width,
                                .height = stex->dst_height
                               });
}

void
meta_shaped_texture_set_transform (MetaShapedTexture    *stex,
                                   MetaMonitorTransform  transform)
{
  if (stex->transform == transform)
    return;

  stex->transform = transform;

  meta_shaped_texture_reset_pipelines (stex);
  invalidate_size (stex);
}

void
meta_shaped_texture_set_viewport_src_rect (MetaShapedTexture *stex,
                                           graphene_rect_t   *src_rect)
{
  if (!stex->has_viewport_src_rect ||
      stex->viewport_src_rect.origin.x != src_rect->origin.x ||
      stex->viewport_src_rect.origin.y != src_rect->origin.y ||
      stex->viewport_src_rect.size.width != src_rect->size.width ||
      stex->viewport_src_rect.size.height != src_rect->size.height)
    {
      stex->has_viewport_src_rect = TRUE;
      stex->viewport_src_rect = *src_rect;
      meta_shaped_texture_reset_pipelines (stex);
      invalidate_size (stex);
    }
}

void
meta_shaped_texture_reset_viewport_src_rect (MetaShapedTexture *stex)
{
  if (!stex->has_viewport_src_rect)
    return;

  stex->has_viewport_src_rect = FALSE;
  meta_shaped_texture_reset_pipelines (stex);
  invalidate_size (stex);
}

void
meta_shaped_texture_set_viewport_dst_size (MetaShapedTexture *stex,
                                           int                dst_width,
                                           int                dst_height)
{
  if (!stex->has_viewport_dst_size ||
      stex->viewport_dst_width != dst_width ||
      stex->viewport_dst_height != dst_height)
    {
      stex->has_viewport_dst_size = TRUE;
      stex->viewport_dst_width = dst_width;
      stex->viewport_dst_height = dst_height;
      invalidate_size (stex);
    }
}

void
meta_shaped_texture_reset_viewport_dst_size (MetaShapedTexture *stex)
{
  if (!stex->has_viewport_dst_size)
    return;

  stex->has_viewport_dst_size = FALSE;
  invalidate_size (stex);
}

static gboolean
should_get_via_offscreen (MetaShapedTexture *stex)
{
  if (!cogl_texture_is_get_data_supported (stex->texture))
    return TRUE;

  if (stex->has_viewport_src_rect || stex->has_viewport_dst_size)
    return TRUE;

  switch (stex->transform)
    {
    case META_MONITOR_TRANSFORM_90:
    case META_MONITOR_TRANSFORM_180:
    case META_MONITOR_TRANSFORM_270:
    case META_MONITOR_TRANSFORM_FLIPPED:
    case META_MONITOR_TRANSFORM_FLIPPED_90:
    case META_MONITOR_TRANSFORM_FLIPPED_180:
    case META_MONITOR_TRANSFORM_FLIPPED_270:
      return TRUE;
    case META_MONITOR_TRANSFORM_NORMAL:
      break;
    }

  return FALSE;
}

static cairo_surface_t *
get_image_via_offscreen (MetaShapedTexture     *stex,
                         cairo_rectangle_int_t *clip,
                         int                    image_width,
                         int                    image_height)
{
  g_autoptr (ClutterPaintNode) root_node = NULL;
  ClutterBackend *clutter_backend = clutter_get_default_backend ();
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  CoglTexture *image_texture;
  GError *error = NULL;
  CoglOffscreen *offscreen;
  CoglFramebuffer *fb;
  CoglMatrix projection_matrix;
  cairo_rectangle_int_t fallback_clip;
  ClutterColor clear_color;
  ClutterPaintContext *paint_context;
  cairo_surface_t *surface;

  if (!clip)
    {
      fallback_clip = (cairo_rectangle_int_t) {
        .width = image_width,
        .height = image_height,
      };
      clip = &fallback_clip;
    }

  image_texture =
    COGL_TEXTURE (cogl_texture_2d_new_with_size (cogl_context,
                                                 image_width,
                                                 image_height));
  cogl_primitive_texture_set_auto_mipmap (COGL_PRIMITIVE_TEXTURE (image_texture),
                                          FALSE);
  if (!cogl_texture_allocate (COGL_TEXTURE (image_texture), &error))
    {
      g_error_free (error);
      cogl_object_unref (image_texture);
      return FALSE;
    }

  offscreen = cogl_offscreen_new_with_texture (COGL_TEXTURE (image_texture));
  fb = COGL_FRAMEBUFFER (offscreen);
  cogl_object_unref (image_texture);
  if (!cogl_framebuffer_allocate (fb, &error))
    {
      g_error_free (error);
      cogl_object_unref (fb);
      return FALSE;
    }

  cogl_framebuffer_push_matrix (fb);
  cogl_matrix_init_identity (&projection_matrix);
  cogl_matrix_scale (&projection_matrix,
                     1.0 / (image_width / 2.0),
                     -1.0 / (image_height / 2.0), 0);
  cogl_matrix_translate (&projection_matrix,
                         -(image_width / 2.0),
                         -(image_height / 2.0), 0);

  cogl_framebuffer_set_projection_matrix (fb, &projection_matrix);

  clear_color = (ClutterColor) { 0, 0, 0, 0 };

  root_node = clutter_root_node_new (fb, &clear_color, COGL_BUFFER_BIT_COLOR);
  clutter_paint_node_set_static_name (root_node, "MetaShapedTexture.offscreen");

  paint_context = clutter_paint_context_new_for_framebuffer (fb);

  do_paint_content (stex, root_node, paint_context,
                    stex->texture,
                    &(ClutterActorBox) {
                      0, 0,
                      image_width,
                      image_height,
                    },
                    255);

  clutter_paint_node_paint (root_node, paint_context);
  clutter_paint_context_destroy (paint_context);

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                        clip->width, clip->height);
  cogl_framebuffer_read_pixels (fb,
                                clip->x, clip->y,
                                clip->width, clip->height,
                                CLUTTER_CAIRO_FORMAT_ARGB32,
                                cairo_image_surface_get_data (surface));
  cogl_object_unref (fb);

  cairo_surface_mark_dirty (surface);

  return surface;
}

/**
 * meta_shaped_texture_get_image:
 * @stex: A #MetaShapedTexture
 * @clip: (nullable): A clipping rectangle, to help prevent extra processing.
 * In the case that the clipping rectangle is partially or fully
 * outside the bounds of the texture, the rectangle will be clipped.
 *
 * Flattens the two layers of the shaped texture into one ARGB32
 * image by alpha blending the two images, and returns the flattened
 * image.
 *
 * Returns: (nullable) (transfer full): a new cairo surface to be freed with
 * cairo_surface_destroy().
 */
cairo_surface_t *
meta_shaped_texture_get_image (MetaShapedTexture     *stex,
                               cairo_rectangle_int_t *clip)
{
  cairo_rectangle_int_t *image_clip = NULL;
  CoglTexture *texture, *mask_texture;
  cairo_surface_t *surface;

  g_return_val_if_fail (META_IS_SHAPED_TEXTURE (stex), NULL);

  texture = COGL_TEXTURE (stex->texture);

  if (texture == NULL)
    return NULL;

  ensure_size_valid (stex);

  if (stex->dst_width == 0 || stex->dst_height == 0)
    return NULL;

  if (clip != NULL)
    {
      cairo_rectangle_int_t dst_rect;

      image_clip = alloca (sizeof (cairo_rectangle_int_t));
      dst_rect = (cairo_rectangle_int_t) {
        .width = stex->dst_width,
        .height = stex->dst_height,
      };

      if (!meta_rectangle_intersect (&dst_rect, clip,
                                     image_clip))
        return NULL;

      *image_clip = (MetaRectangle) {
        .x = image_clip->x * stex->buffer_scale,
        .y = image_clip->y * stex->buffer_scale,
        .width = image_clip->width * stex->buffer_scale,
        .height = image_clip->height * stex->buffer_scale,
      };
    }

  if (should_get_via_offscreen (stex))
    {
      int image_width;
      int image_height;

      image_width = stex->dst_width * stex->buffer_scale;
      image_height = stex->dst_height * stex->buffer_scale;
      return get_image_via_offscreen (stex,
                                      image_clip,
                                      image_width,
                                      image_height);
    }

  if (image_clip)
    texture = cogl_texture_new_from_sub_texture (texture,
                                                 image_clip->x,
                                                 image_clip->y,
                                                 image_clip->width,
                                                 image_clip->height);

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                        cogl_texture_get_width (texture),
                                        cogl_texture_get_height (texture));

  cogl_texture_get_data (texture, CLUTTER_CAIRO_FORMAT_ARGB32,
                         cairo_image_surface_get_stride (surface),
                         cairo_image_surface_get_data (surface));

  cairo_surface_mark_dirty (surface);

  if (image_clip)
    cogl_object_unref (texture);

  mask_texture = stex->mask_texture;
  if (mask_texture != NULL)
    {
      cairo_t *cr;
      cairo_surface_t *mask_surface;

      if (image_clip)
        mask_texture =
          cogl_texture_new_from_sub_texture (mask_texture,
                                             image_clip->x,
                                             image_clip->y,
                                             image_clip->width,
                                             image_clip->height);

      mask_surface = cairo_image_surface_create (CAIRO_FORMAT_A8,
                                                 cogl_texture_get_width (mask_texture),
                                                 cogl_texture_get_height (mask_texture));

      cogl_texture_get_data (mask_texture, COGL_PIXEL_FORMAT_A_8,
                             cairo_image_surface_get_stride (mask_surface),
                             cairo_image_surface_get_data (mask_surface));

      cairo_surface_mark_dirty (mask_surface);

      cr = cairo_create (surface);
      cairo_set_source_surface (cr, mask_surface, 0, 0);
      cairo_set_operator (cr, CAIRO_OPERATOR_DEST_IN);
      cairo_paint (cr);
      cairo_destroy (cr);

      cairo_surface_destroy (mask_surface);

      if (image_clip)
        cogl_object_unref (mask_texture);
    }

  return surface;
}

void
meta_shaped_texture_set_fallback_size (MetaShapedTexture *stex,
                                       int                fallback_width,
                                       int                fallback_height)
{
  stex->fallback_width = fallback_width;
  stex->fallback_height = fallback_height;

  invalidate_size (stex);
}

MetaShapedTexture *
meta_shaped_texture_new (void)
{
  return g_object_new (META_TYPE_SHAPED_TEXTURE, NULL);
}

void
meta_shaped_texture_set_buffer_scale (MetaShapedTexture *stex,
                                      int                buffer_scale)
{
  g_return_if_fail (META_IS_SHAPED_TEXTURE (stex));

  if (buffer_scale == stex->buffer_scale)
    return;

  stex->buffer_scale = buffer_scale;

  invalidate_size (stex);
}

int
meta_shaped_texture_get_buffer_scale (MetaShapedTexture *stex)
{
  g_return_val_if_fail (META_IS_SHAPED_TEXTURE (stex), 1.0);

  return stex->buffer_scale;
}

int
meta_shaped_texture_get_width (MetaShapedTexture *stex)
{
  g_return_val_if_fail (META_IS_SHAPED_TEXTURE (stex), 0);

  ensure_size_valid (stex);

  return stex->dst_width;
}

int
meta_shaped_texture_get_height (MetaShapedTexture *stex)
{
  g_return_val_if_fail (META_IS_SHAPED_TEXTURE (stex), 0);

  ensure_size_valid (stex);

  return stex->dst_height;
}
