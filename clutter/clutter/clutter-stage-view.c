/*
 * Copyright (C) 2016 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "clutter-build-config.h"

#include "clutter/clutter-stage-view.h"
#include "clutter/clutter-stage-view-private.h"

#include <cairo-gobject.h>
#include <math.h>

#include "clutter/clutter-damage-history.h"
#include "clutter/clutter-private.h"
#include "clutter/clutter-muffin.h"
#include "cogl/cogl.h"

enum
{
  PROP_0,

  PROP_NAME,
  PROP_LAYOUT,
  PROP_FRAMEBUFFER,
  PROP_OFFSCREEN,
  PROP_USE_SHADOWFB,
  PROP_SCALE,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

typedef struct _ClutterStageViewPrivate
{
  char *name;

  cairo_rectangle_int_t layout;
  float scale;
  CoglFramebuffer *framebuffer;

  CoglOffscreen *offscreen;
  CoglPipeline *offscreen_pipeline;

  gboolean use_shadowfb;
  struct {
    struct {
      CoglDmaBufHandle *handles[2];
      int current_idx;
      ClutterDamageHistory *damage_history;
    } dma_buf;

    CoglOffscreen *framebuffer;
  } shadow;

  CoglScanout *next_scanout;

  gboolean has_redraw_clip;
  cairo_region_t *redraw_clip;

  guint dirty_viewport   : 1;
  guint dirty_projection : 1;
} ClutterStageViewPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (ClutterStageView, clutter_stage_view, G_TYPE_OBJECT)

void
clutter_stage_view_get_layout (ClutterStageView      *view,
                               cairo_rectangle_int_t *rect)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  *rect = priv->layout;
}

/**
 * clutter_stage_view_get_framebuffer:
 * @view: a #ClutterStageView
 *
 * Retrieves the framebuffer of @view to draw to.
 *
 * Returns: (transfer none): a #CoglFramebuffer
 */
CoglFramebuffer *
clutter_stage_view_get_framebuffer (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  if (priv->offscreen)
    return priv->offscreen;
  else if (priv->shadow.framebuffer)
    return priv->shadow.framebuffer;
  else
    return priv->framebuffer;
}

/**
 * clutter_stage_view_get_onscreen:
 * @view: a #ClutterStageView
 *
 * Retrieves the onscreen framebuffer of @view if available.
 *
 * Returns: (transfer none): a #CoglFramebuffer
 */
CoglFramebuffer *
clutter_stage_view_get_onscreen (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->framebuffer;
}

static CoglPipeline *
clutter_stage_view_create_framebuffer_pipeline (CoglFramebuffer *framebuffer)
{
  CoglPipeline *pipeline;

  pipeline = cogl_pipeline_new (cogl_framebuffer_get_context (framebuffer));

  cogl_pipeline_set_layer_filters (pipeline, 0,
                                   COGL_PIPELINE_FILTER_NEAREST,
                                   COGL_PIPELINE_FILTER_NEAREST);
  cogl_pipeline_set_layer_texture (pipeline, 0,
                                   cogl_offscreen_get_texture (framebuffer));
  cogl_pipeline_set_layer_wrap_mode (pipeline, 0,
                                     COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);

  return pipeline;
}

static void
clutter_stage_view_ensure_offscreen_blit_pipeline (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);
  ClutterStageViewClass *view_class =
    CLUTTER_STAGE_VIEW_GET_CLASS (view);

  g_assert (priv->offscreen != NULL);

  if (priv->offscreen_pipeline)
    return;

  priv->offscreen_pipeline =
    clutter_stage_view_create_framebuffer_pipeline (priv->offscreen);

  if (view_class->setup_offscreen_blit_pipeline)
    view_class->setup_offscreen_blit_pipeline (view, priv->offscreen_pipeline);
}

void
clutter_stage_view_invalidate_offscreen_blit_pipeline (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  g_clear_pointer (&priv->offscreen_pipeline, cogl_object_unref);
}

void
clutter_stage_view_transform_rect_to_onscreen (ClutterStageView            *view,
                                               const cairo_rectangle_int_t *src_rect,
                                               int                          dst_width,
                                               int                          dst_height,
                                               cairo_rectangle_int_t       *dst_rect)
{
  ClutterStageViewClass *view_class = CLUTTER_STAGE_VIEW_GET_CLASS (view);

  return view_class->transform_rect_to_onscreen (view,
                                                 src_rect,
                                                 dst_width,
                                                 dst_height,
                                                 dst_rect);
}

static void
paint_transformed_framebuffer (ClutterStageView     *view,
                               CoglPipeline         *pipeline,
                               CoglFramebuffer      *src_framebuffer,
                               CoglFramebuffer      *dst_framebuffer,
                               const cairo_region_t *redraw_clip)
{
  CoglMatrix matrix;
  unsigned int n_rectangles, i;
  int dst_width, dst_height;
  cairo_rectangle_int_t view_layout;
  cairo_rectangle_int_t onscreen_layout;
  float view_scale;
  float *coordinates;

  dst_width = cogl_framebuffer_get_width (dst_framebuffer);
  dst_height = cogl_framebuffer_get_height (dst_framebuffer);
  clutter_stage_view_get_layout (view, &view_layout);
  clutter_stage_view_transform_rect_to_onscreen (view,
                                                 &(cairo_rectangle_int_t) {
                                                   .width = view_layout.width,
                                                   .height = view_layout.height,
                                                 },
                                                 view_layout.width,
                                                 view_layout.height,
                                                 &onscreen_layout);
  view_scale = clutter_stage_view_get_scale (view);

  cogl_framebuffer_push_matrix (dst_framebuffer);

  cogl_matrix_init_identity (&matrix);
  cogl_matrix_scale (&matrix,
                     1.0 / (dst_width / 2.0),
                     -1.0 / (dst_height / 2.0), 0);
  cogl_matrix_translate (&matrix,
                         -(dst_width / 2.0),
                         -(dst_height / 2.0), 0);
  cogl_framebuffer_set_projection_matrix (dst_framebuffer, &matrix);
  cogl_framebuffer_set_viewport (dst_framebuffer,
                                 0, 0, dst_width, dst_height);

  n_rectangles = cairo_region_num_rectangles (redraw_clip);
  coordinates = g_newa (float, 2 * 4 * n_rectangles);

  for (i = 0; i < n_rectangles; i++)
    {
      cairo_rectangle_int_t src_rect;
      cairo_rectangle_int_t dst_rect;

      cairo_region_get_rectangle (redraw_clip, i, &src_rect);
      _clutter_util_rectangle_offset (&src_rect,
                                      -view_layout.x,
                                      -view_layout.y,
                                      &src_rect);

      clutter_stage_view_transform_rect_to_onscreen (view,
                                                     &src_rect,
                                                     onscreen_layout.width,
                                                     onscreen_layout.height,
                                                     &dst_rect);

      coordinates[i * 8 + 0] = (float) dst_rect.x * view_scale;
      coordinates[i * 8 + 1] = (float) dst_rect.y * view_scale;
      coordinates[i * 8 + 2] = ((float) (dst_rect.x + dst_rect.width) *
      view_scale);
      coordinates[i * 8 + 3] = ((float) (dst_rect.y + dst_rect.height) *
      view_scale);

      coordinates[i * 8 + 4] = (((float) dst_rect.x / (float) dst_width) *
      view_scale);
      coordinates[i * 8 + 5] = (((float) dst_rect.y / (float) dst_height) *
      view_scale);
      coordinates[i * 8 + 6] = ((float) (dst_rect.x + dst_rect.width) /
      (float) dst_width) * view_scale;
      coordinates[i * 8 + 7] = ((float) (dst_rect.y + dst_rect.height) /
      (float) dst_height) * view_scale;
    }

  cogl_framebuffer_draw_textured_rectangles (dst_framebuffer,
                                             pipeline,
                                             coordinates,
                                             n_rectangles);

  cogl_framebuffer_pop_matrix (dst_framebuffer);
}

static gboolean
is_shadowfb_double_buffered (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
  clutter_stage_view_get_instance_private (view);

  return priv->shadow.dma_buf.handles[0] && priv->shadow.dma_buf.handles[1];
}

static gboolean
init_dma_buf_shadowfbs (ClutterStageView  *view,
                        CoglContext       *cogl_context,
                        int                width,
                        int                height,
                        GError           **error)
{
  ClutterStageViewPrivate *priv =
  clutter_stage_view_get_instance_private (view);
  CoglRenderer *cogl_renderer = cogl_context_get_renderer (cogl_context);
  CoglFramebuffer *initial_shadowfb;

  if (!cogl_clutter_winsys_has_feature (COGL_WINSYS_FEATURE_BUFFER_AGE))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                 "Buffer age not supported");
      return FALSE;
    }

  if (!cogl_is_onscreen (priv->framebuffer))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                 "Tried to use shadow buffer without onscreen");
      return FALSE;
    }

  priv->shadow.dma_buf.handles[0] = cogl_renderer_create_dma_buf (cogl_renderer,
                                                                  width, height,
                                                                  error);
  if (!priv->shadow.dma_buf.handles[0])
    return FALSE;

  priv->shadow.dma_buf.handles[1] = cogl_renderer_create_dma_buf (cogl_renderer,
                                                                  width, height,
                                                                  error);
  if (!priv->shadow.dma_buf.handles[1])
    {
      g_clear_pointer (&priv->shadow.dma_buf.handles[0],
                       cogl_dma_buf_handle_free);
      return FALSE;
    }

  priv->shadow.dma_buf.damage_history = clutter_damage_history_new ();

  initial_shadowfb =
  cogl_dma_buf_handle_get_framebuffer (priv->shadow.dma_buf.handles[0]);
  priv->shadow.framebuffer = cogl_object_ref (initial_shadowfb);

  return TRUE;
}

static CoglOffscreen *
create_offscreen_framebuffer (CoglContext  *context,
                              int           width,
                              int           height,
                              GError      **error)
{
  CoglOffscreen *framebuffer;
  CoglTexture2D *texture;

  texture = cogl_texture_2d_new_with_size (context, width, height);
  cogl_primitive_texture_set_auto_mipmap (COGL_PRIMITIVE_TEXTURE (texture),
                                          FALSE);

  if (!cogl_texture_allocate (COGL_TEXTURE (texture), error))
    {
      cogl_object_unref (texture);
      return FALSE;
    }

  framebuffer = cogl_offscreen_new_with_texture (COGL_TEXTURE (texture));
  cogl_object_unref (texture);
  if (!cogl_framebuffer_allocate (COGL_FRAMEBUFFER (framebuffer), error))
    {
      cogl_object_unref (framebuffer);
      return FALSE;
    }

  return framebuffer;
}

static gboolean
init_fallback_shadowfb (ClutterStageView  *view,
                        CoglContext       *cogl_context,
                        int                width,
                        int                height,
                        GError           **error)
{
  ClutterStageViewPrivate *priv =
  clutter_stage_view_get_instance_private (view);
  CoglOffscreen *offscreen;

  offscreen = create_offscreen_framebuffer (cogl_context, width, height, error);
  if (!offscreen)
    return FALSE;

  priv->shadow.framebuffer = offscreen;
  return TRUE;
}

static void
init_shadowfb (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
  clutter_stage_view_get_instance_private (view);
  g_autoptr (GError) error = NULL;
  int width;
  int height;
  CoglContext *cogl_context;

  width = cogl_framebuffer_get_width (priv->framebuffer);
  height = cogl_framebuffer_get_height (priv->framebuffer);
  cogl_context = cogl_framebuffer_get_context (priv->framebuffer);

  if (init_dma_buf_shadowfbs (view, cogl_context, width, height, &error))
    {
      g_message ("Initialized double buffered shadow fb for %s", priv->name);
      return;
    }

  g_warning ("Failed to initialize double buffered shadow fb for %s: %s",
             priv->name, error->message);
  g_clear_error (&error);

  if (!init_fallback_shadowfb (view, cogl_context, width, height, &error))
    {
      g_warning ("Failed to initialize single buffered shadow fb for %s: %s",
                 priv->name, error->message);
    }
  else
    {
      g_message ("Initialized single buffered shadow fb for %s", priv->name);
    }
}

void
clutter_stage_view_after_paint (ClutterStageView *view,
                                cairo_region_t   *redraw_clip)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  if (priv->offscreen)
    {
      if (priv->shadow.framebuffer)
        {
          paint_transformed_framebuffer (view,
                                         priv->offscreen_pipeline,
                                         priv->offscreen,
                                         priv->shadow.framebuffer,
                                         redraw_clip);
        }
      else
        {
          paint_transformed_framebuffer (view,
                                         priv->offscreen_pipeline,
                                         priv->offscreen,
                                         priv->framebuffer,
                                         redraw_clip);
        }
    }
}

static gboolean
is_tile_dirty (cairo_rectangle_int_t *tile,
               uint8_t               *current_data,
               uint8_t               *prev_data,
               int                    bpp,
               int                    stride)
{
  int y;

  for (y = tile->y; y < tile->y + tile->height; y++)
    {
      if (memcmp (prev_data + y * stride + tile->x * bpp,
        current_data + y * stride + tile->x * bpp,
        tile->width * bpp) != 0)
        return TRUE;
    }
    return FALSE;
}

static int
flip_dma_buf_idx (int idx)
{
  return (idx + 1) % 2;
}

static cairo_region_t *
find_damaged_tiles (ClutterStageView      *view,
                    const cairo_region_t  *damage_region,
                    GError               **error)
{
  ClutterStageViewPrivate *priv =
  clutter_stage_view_get_instance_private (view);
  cairo_region_t *tile_damage_region;
  cairo_rectangle_int_t damage_extents;
  cairo_rectangle_int_t fb_rect;
  int prev_dma_buf_idx;
  CoglDmaBufHandle *prev_dma_buf_handle;
  uint8_t *prev_data;
  int current_dma_buf_idx;
  CoglDmaBufHandle *current_dma_buf_handle;
  uint8_t *current_data;
  int width, height, stride, bpp;
  int tile_x_min, tile_x_max;
  int tile_y_min, tile_y_max;
  int tile_x, tile_y;
  const int tile_size = 16;

  prev_dma_buf_idx = flip_dma_buf_idx (priv->shadow.dma_buf.current_idx);
  prev_dma_buf_handle = priv->shadow.dma_buf.handles[prev_dma_buf_idx];

  current_dma_buf_idx = priv->shadow.dma_buf.current_idx;
  current_dma_buf_handle = priv->shadow.dma_buf.handles[current_dma_buf_idx];

  width = cogl_dma_buf_handle_get_width (current_dma_buf_handle);
  height = cogl_dma_buf_handle_get_height (current_dma_buf_handle);
  stride = cogl_dma_buf_handle_get_stride (current_dma_buf_handle);
  bpp = cogl_dma_buf_handle_get_bpp (current_dma_buf_handle);

  cogl_framebuffer_finish (priv->shadow.framebuffer);

  if (!cogl_dma_buf_handle_sync_read_start (prev_dma_buf_handle, error))
    return NULL;

  if (!cogl_dma_buf_handle_sync_read_start (current_dma_buf_handle, error))
    goto err_sync_read_current;

  prev_data = cogl_dma_buf_handle_mmap (prev_dma_buf_handle, error);
  if (!prev_data)
    goto err_mmap_prev;
  current_data = cogl_dma_buf_handle_mmap (current_dma_buf_handle, error);
  if (!current_data)
    goto err_mmap_current;

  fb_rect = (cairo_rectangle_int_t) {
    .width = width,
    .height = height,
  };

  cairo_region_get_extents (damage_region, &damage_extents);

  tile_x_min = damage_extents.x / tile_size;
  tile_x_max = ((damage_extents.x + damage_extents.width + tile_size - 1) /
  tile_size);
  tile_y_min = damage_extents.y / tile_size;
  tile_y_max = ((damage_extents.y + damage_extents.height + tile_size - 1) /
  tile_size);

  tile_damage_region = cairo_region_create ();

  for (tile_y = tile_y_min; tile_y <= tile_y_max; tile_y++)
    {
      for (tile_x = tile_x_min; tile_x <= tile_x_max; tile_x++)
         {
           cairo_rectangle_int_t tile = {
           .x = tile_x * tile_size,
           .y = tile_y * tile_size,
           .width = tile_size,
           .height = tile_size,
         };

         if (cairo_region_contains_rectangle (damage_region, &tile) ==
             CAIRO_REGION_OVERLAP_OUT)
           continue;

         _clutter_util_rectangle_intersection (&tile, &fb_rect, &tile);

         if (is_tile_dirty (&tile, current_data, prev_data, bpp, stride))
           cairo_region_union_rectangle (tile_damage_region, &tile);
       }
    }

  if (!cogl_dma_buf_handle_sync_read_end (prev_dma_buf_handle, error))
    {
      g_warning ("Failed to end DMA buffer read synchronization: %s",
                 (*error)->message);
      g_clear_error (error);
    }

  if (!cogl_dma_buf_handle_sync_read_end (current_dma_buf_handle, error))
    {
      g_warning ("Failed to end DMA buffer read synchronization: %s",
                 (*error)->message);
      g_clear_error (error);
    }

  cogl_dma_buf_handle_munmap (prev_dma_buf_handle, prev_data, NULL);
  cogl_dma_buf_handle_munmap (current_dma_buf_handle, current_data, NULL);

  cairo_region_intersect (tile_damage_region, damage_region);

  return tile_damage_region;

  err_mmap_current:
  cogl_dma_buf_handle_munmap (prev_dma_buf_handle, prev_data, NULL);

  err_mmap_prev:
  cogl_dma_buf_handle_sync_read_end (current_dma_buf_handle, NULL);

  err_sync_read_current:
  cogl_dma_buf_handle_sync_read_end (prev_dma_buf_handle, NULL);

  return NULL;
}

static void
swap_dma_buf_framebuffer (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
  clutter_stage_view_get_instance_private (view);
  int next_idx;
  CoglDmaBufHandle *next_dma_buf_handle;
  CoglOffscreen *next_framebuffer;

  next_idx = ((priv->shadow.dma_buf.current_idx + 1) %
  G_N_ELEMENTS (priv->shadow.dma_buf.handles));
  priv->shadow.dma_buf.current_idx = next_idx;

  next_dma_buf_handle = priv->shadow.dma_buf.handles[next_idx];
  next_framebuffer =
  cogl_dma_buf_handle_get_framebuffer (next_dma_buf_handle);
  cogl_clear_object (&priv->shadow.framebuffer);
  priv->shadow.framebuffer = cogl_object_ref (next_framebuffer);
}

static void
copy_shadowfb_to_onscreen (ClutterStageView     *view,
                           const cairo_region_t *swap_region)
{
  ClutterStageViewPrivate *priv =
  clutter_stage_view_get_instance_private (view);
  ClutterDamageHistory *damage_history = priv->shadow.dma_buf.damage_history;
  cairo_region_t *damage_region;
  int age;
  int i;

  if (cairo_region_is_empty (swap_region))
    {
      cairo_rectangle_int_t full_damage = {
        .width = cogl_framebuffer_get_width (priv->framebuffer),
        .height = cogl_framebuffer_get_height (priv->framebuffer),
      };
      damage_region = cairo_region_create_rectangle (&full_damage);
    }
  else
    {
      damage_region = cairo_region_copy (swap_region);
    }

  if (is_shadowfb_double_buffered (view))
    {
      CoglOnscreen *onscreen = COGL_ONSCREEN (priv->framebuffer);
      cairo_region_t *changed_region;

      if (cogl_onscreen_get_frame_counter (onscreen) >= 1)
        {
          g_autoptr (GError) error = NULL;

          changed_region = find_damaged_tiles (view, damage_region, &error);
          if (!changed_region)
            {
              int other_dma_buf_idx;

              g_warning ("Disabling actual damage detection: %s",
                         error->message);

              other_dma_buf_idx =
                flip_dma_buf_idx (priv->shadow.dma_buf.current_idx);
              g_clear_pointer (&priv->shadow.dma_buf.handles[other_dma_buf_idx],
                               cogl_dma_buf_handle_free);
            }
        }
      else
        {
          changed_region = cairo_region_copy (damage_region);
        }

      if (changed_region)
        {
          int buffer_age;

          clutter_damage_history_record (damage_history, changed_region);

          buffer_age = cogl_onscreen_get_buffer_age (onscreen);
          if (clutter_damage_history_is_age_valid (damage_history, buffer_age))
            {
              for (age = 1; age <= buffer_age; age++)
                 {
                   const cairo_region_t *old_damage;

                   old_damage = clutter_damage_history_lookup (damage_history, age);
                   cairo_region_union (changed_region, old_damage);
                 }

              cairo_region_destroy (damage_region);
              damage_region = g_steal_pointer (&changed_region);
            }
          else
            {
              cairo_region_destroy (changed_region);
            }
         clutter_damage_history_step (damage_history);
       }
    }

  for (i = 0; i < cairo_region_num_rectangles (damage_region); i++)
    {
      g_autoptr (GError) error = NULL;
      cairo_rectangle_int_t rect;

      cairo_region_get_rectangle (damage_region, i, &rect);

      if (!cogl_blit_framebuffer (priv->shadow.framebuffer,
                                  priv->framebuffer,
                                  rect.x, rect.y,
                                  rect.x, rect.y,
                                  rect.width, rect.height,
                                  &error))
       {
         g_warning ("Failed to blit shadow buffer: %s", error->message);
         cairo_region_destroy (damage_region);
         return;
       }
     }

  cairo_region_destroy (damage_region);

  if (is_shadowfb_double_buffered (view))
    swap_dma_buf_framebuffer (view);
}

void
clutter_stage_view_before_swap_buffer (ClutterStageView     *view,
                                       const cairo_region_t *swap_region)
{
  ClutterStageViewPrivate *priv =
  clutter_stage_view_get_instance_private (view);

  if (priv->shadow.framebuffer)
    copy_shadowfb_to_onscreen (view, swap_region);
}

float
clutter_stage_view_get_scale (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->scale;
}

typedef void (*FrontBufferCallback) (CoglFramebuffer *framebuffer,
                                     gconstpointer    user_data);

static void
clutter_stage_view_foreach_front_buffer (ClutterStageView    *view,
                                         FrontBufferCallback  callback,
                                         gconstpointer        user_data)
{
  ClutterStageViewPrivate *priv =
  clutter_stage_view_get_instance_private (view);

  if (priv->offscreen)
    {
      callback (priv->offscreen, user_data);
    }
  else if (priv->shadow.framebuffer)
    {
      if (is_shadowfb_double_buffered (view))
        {
          int i;

          for (i = 0; i < G_N_ELEMENTS (priv->shadow.dma_buf.handles); i++)
             {
               CoglDmaBufHandle *handle = priv->shadow.dma_buf.handles[i];
               CoglFramebuffer *framebuffer =
               cogl_dma_buf_handle_get_framebuffer (handle);

               callback (framebuffer, user_data);
             }
       }
     else
       {
        callback (priv->shadow.framebuffer, user_data);
       }
    }
  else
    {
      callback (priv->framebuffer, user_data);
    }
}

gboolean
clutter_stage_view_is_dirty_viewport (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->dirty_viewport;
}

void
clutter_stage_view_invalidate_viewport (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
  clutter_stage_view_get_instance_private (view);

  priv->dirty_viewport = TRUE;
}

static void
set_framebuffer_viewport (CoglFramebuffer *framebuffer,
                          gconstpointer    user_data)
{
  const graphene_rect_t *rect = user_data;

  cogl_framebuffer_set_viewport (framebuffer,
                                 rect->origin.x,
                                 rect->origin.y,
                                 rect->size.width,
                                 rect->size.height);
}

void
clutter_stage_view_set_viewport (ClutterStageView *view,
                                 float             x,
                                 float             y,
                                 float             width,
                                 float             height)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);
  graphene_rect_t rect;

  priv->dirty_viewport = FALSE;

  rect = (graphene_rect_t) {
    .origin = { .x = x, .y = y },
    .size = { .width = width, .height = height },
  };
  clutter_stage_view_foreach_front_buffer (view,
                                           set_framebuffer_viewport,
                                           &rect);
}

gboolean
clutter_stage_view_is_dirty_projection (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->dirty_projection;
}

static void
set_framebuffer_projection_matrix (CoglFramebuffer *framebuffer,
                                   gconstpointer    user_data)
{
  cogl_framebuffer_set_projection_matrix (framebuffer, user_data);
}

void
clutter_stage_view_invalidate_projection (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

    priv->dirty_projection = TRUE;
}

void
clutter_stage_view_set_projection (ClutterStageView *view,
                                   const CoglMatrix *matrix)
{
  ClutterStageViewPrivate *priv =
  clutter_stage_view_get_instance_private (view);

  priv->dirty_projection = FALSE;
  clutter_stage_view_foreach_front_buffer (view,
                                           set_framebuffer_projection_matrix,
                                           matrix);
}

void
clutter_stage_view_get_offscreen_transformation_matrix (ClutterStageView *view,
                                                        CoglMatrix       *matrix)
{
  ClutterStageViewClass *view_class = CLUTTER_STAGE_VIEW_GET_CLASS (view);

  view_class->get_offscreen_transformation_matrix (view, matrix);
}

void
clutter_stage_view_add_redraw_clip (ClutterStageView            *view,
                                    const cairo_rectangle_int_t *clip)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  if (priv->has_redraw_clip && !priv->redraw_clip)
    return;

  if (!clip)
    {
      g_clear_pointer (&priv->redraw_clip, cairo_region_destroy);
      priv->has_redraw_clip = TRUE;
      return;
    }

  if (clip->width == 0 || clip->height == 0)
    return;

  if (!priv->redraw_clip)
    {
      if (!clutter_util_rectangle_equal (&priv->layout, clip))
        priv->redraw_clip = cairo_region_create_rectangle (clip);
    }
  else
    {
      cairo_region_union_rectangle (priv->redraw_clip, clip);

      if (cairo_region_num_rectangles (priv->redraw_clip) == 1)
        {
          cairo_rectangle_int_t redraw_clip_extents;

          cairo_region_get_extents (priv->redraw_clip, &redraw_clip_extents);
          if (clutter_util_rectangle_equal (&priv->layout, &redraw_clip_extents))
            g_clear_pointer (&priv->redraw_clip, cairo_region_destroy);
        }
    }

  priv->has_redraw_clip = TRUE;
}

gboolean
clutter_stage_view_has_redraw_clip (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->has_redraw_clip;
}

gboolean
clutter_stage_view_has_full_redraw_clip (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->has_redraw_clip && !priv->redraw_clip;
}

const cairo_region_t *
clutter_stage_view_peek_redraw_clip (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->redraw_clip;
}

cairo_region_t *
clutter_stage_view_take_redraw_clip (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  priv->has_redraw_clip = FALSE;

  return g_steal_pointer (&priv->redraw_clip);
}

static void
clutter_stage_default_get_offscreen_transformation_matrix (ClutterStageView *view,
                                                           CoglMatrix       *matrix)
{
  cogl_matrix_init_identity (matrix);
}

void
clutter_stage_view_assign_next_scanout (ClutterStageView *view,
                                        CoglScanout      *scanout)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  g_set_object (&priv->next_scanout, scanout);
}

CoglScanout *
clutter_stage_view_take_scanout (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return g_steal_pointer (&priv->next_scanout);
}

static void
clutter_stage_view_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  ClutterStageView *view = CLUTTER_STAGE_VIEW (object);
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, priv->name);
      break;
    case PROP_LAYOUT:
      g_value_set_boxed (value, &priv->layout);
      break;
    case PROP_FRAMEBUFFER:
      g_value_set_boxed (value, priv->framebuffer);
      break;
    case PROP_OFFSCREEN:
      g_value_set_boxed (value, priv->offscreen);
      break;
    case PROP_USE_SHADOWFB:
      g_value_set_boolean (value, priv->use_shadowfb);
      break;
    case PROP_SCALE:
      g_value_set_float (value, priv->scale);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
clutter_stage_view_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  ClutterStageView *view = CLUTTER_STAGE_VIEW (object);
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);
  cairo_rectangle_int_t *layout;

  switch (prop_id)
    {
    case PROP_NAME:
      priv->name = g_value_dup_string (value);
      break;
    case PROP_LAYOUT:
      layout = g_value_get_boxed (value);
      priv->layout = *layout;
      break;
    case PROP_FRAMEBUFFER:
      priv->framebuffer = g_value_dup_boxed (value);
#ifndef G_DISABLE_CHECKS
      if (priv->framebuffer)
        {
          int fb_width, fb_height;

          fb_width = cogl_framebuffer_get_width (priv->framebuffer);
          fb_height = cogl_framebuffer_get_height (priv->framebuffer);

          g_warn_if_fail (fabsf (roundf (fb_width / priv->scale) -
                                 fb_width / priv->scale) < FLT_EPSILON);
          g_warn_if_fail (fabsf (roundf (fb_height / priv->scale) -
                                 fb_height / priv->scale) < FLT_EPSILON);
        }
#endif
      break;
    case PROP_OFFSCREEN:
      priv->offscreen = g_value_dup_boxed (value);
      break;
    case PROP_USE_SHADOWFB:
      priv->use_shadowfb = g_value_get_boolean (value);
      break;
    case PROP_SCALE:
      priv->scale = g_value_get_float (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
clutter_stage_view_constructed (GObject *object)
{
  ClutterStageView *view = CLUTTER_STAGE_VIEW (object);
  ClutterStageViewPrivate *priv =
  clutter_stage_view_get_instance_private (view);

  if (priv->use_shadowfb)
    init_shadowfb (view);

  G_OBJECT_CLASS (clutter_stage_view_parent_class)->constructed (object);
}

static void
clutter_stage_view_dispose (GObject *object)
{
  ClutterStageView *view = CLUTTER_STAGE_VIEW (object);
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);
  int i;

  g_clear_pointer (&priv->name, g_free);
  g_clear_pointer (&priv->framebuffer, cogl_object_unref);

  g_clear_pointer (&priv->shadow.framebuffer, cogl_object_unref);
  for (i = 0; i < G_N_ELEMENTS (priv->shadow.dma_buf.handles); i++)
    {
      g_clear_pointer (&priv->shadow.dma_buf.handles[i],
                       cogl_dma_buf_handle_free);
    }
  g_clear_pointer (&priv->shadow.dma_buf.damage_history,
                   clutter_damage_history_free);

  g_clear_pointer (&priv->offscreen, cogl_object_unref);
  g_clear_pointer (&priv->offscreen_pipeline, cogl_object_unref);
  g_clear_pointer (&priv->redraw_clip, cairo_region_destroy);

  G_OBJECT_CLASS (clutter_stage_view_parent_class)->dispose (object);
}

static void
clutter_stage_view_init (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  priv->dirty_viewport = TRUE;
  priv->dirty_projection = TRUE;
  priv->scale = 1.0;
}

static void
clutter_stage_view_class_init (ClutterStageViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  klass->get_offscreen_transformation_matrix =
    clutter_stage_default_get_offscreen_transformation_matrix;

  object_class->get_property = clutter_stage_view_get_property;
  object_class->set_property = clutter_stage_view_set_property;
  object_class->constructed = clutter_stage_view_constructed;
  object_class->dispose = clutter_stage_view_dispose;

  obj_props[PROP_NAME] =
    g_param_spec_string ("name",
                         "Name",
                         "Name of view",
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  obj_props[PROP_LAYOUT] =
    g_param_spec_boxed ("layout",
                        "View layout",
                        "The view layout on the screen",
                        CAIRO_GOBJECT_TYPE_RECTANGLE_INT,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT |
                        G_PARAM_STATIC_STRINGS);

  obj_props[PROP_FRAMEBUFFER] =
    g_param_spec_boxed ("framebuffer",
                        "View framebuffer",
                        "The front buffer of the view",
                        COGL_TYPE_HANDLE,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT |
                        G_PARAM_STATIC_STRINGS);

  obj_props[PROP_OFFSCREEN] =
    g_param_spec_boxed ("offscreen",
                        "Offscreen buffer",
                        "Framebuffer used as intermediate buffer",
                        COGL_TYPE_HANDLE,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  obj_props[PROP_USE_SHADOWFB] =
    g_param_spec_boolean ("use-shadowfb",
                          "Use shadowfb",
                          "Whether to use one or more shadow framebuffers",
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

  obj_props[PROP_SCALE] =
    g_param_spec_float ("scale",
                        "View scale",
                        "The view scale",
                        0.5, G_MAXFLOAT, 1.0,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT |
                        G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST, obj_props);
}
