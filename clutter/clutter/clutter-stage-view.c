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

#include "clutter/clutter-private.h"

enum
{
  PROP_0,

  PROP_LAYOUT,
  PROP_FRAMEBUFFER,
  PROP_OFFSCREEN,
  PROP_SHADOWFB,
  PROP_SCALE,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

typedef struct _ClutterStageViewPrivate
{
  cairo_rectangle_int_t layout;
  float scale;
  CoglFramebuffer *framebuffer;

  CoglOffscreen *offscreen;
  CoglPipeline *offscreen_pipeline;

  CoglOffscreen *shadowfb;
  CoglPipeline *shadowfb_pipeline;

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
  else if (priv->shadowfb)
    return priv->shadowfb;
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

static void
clutter_stage_view_ensure_shadowfb_blit_pipeline (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  if (priv->shadowfb_pipeline)
    return;

  priv->shadowfb_pipeline =
    clutter_stage_view_create_framebuffer_pipeline (priv->shadowfb);
}

void
clutter_stage_view_invalidate_offscreen_blit_pipeline (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  g_clear_pointer (&priv->offscreen_pipeline, cogl_object_unref);
}

static void
clutter_stage_view_copy_to_framebuffer (ClutterStageView *view,
                                        CoglPipeline     *pipeline,
                                        CoglFramebuffer  *src_framebuffer,
                                        CoglFramebuffer  *dst_framebuffer,
                                        gboolean          can_blit)
{
  CoglMatrix matrix;

  /* First, try with blit */
  if (can_blit)
    {
      if (cogl_blit_framebuffer (src_framebuffer,
                                 dst_framebuffer,
                                 0, 0,
                                 0, 0,
                                 cogl_framebuffer_get_width (dst_framebuffer),
                                 cogl_framebuffer_get_height (dst_framebuffer),
                                 NULL))
        return;
    }

  /* If blit fails, fallback to the slower painting method */
  cogl_framebuffer_push_matrix (dst_framebuffer);

  cogl_matrix_init_identity (&matrix);
  cogl_matrix_translate (&matrix, -1, 1, 0);
  cogl_matrix_scale (&matrix, 2, -2, 0);
  cogl_framebuffer_set_projection_matrix (dst_framebuffer, &matrix);

  cogl_framebuffer_draw_rectangle (dst_framebuffer,
                                   pipeline,
                                   0, 0, 1, 1);

  cogl_framebuffer_pop_matrix (dst_framebuffer);
}

void
clutter_stage_view_after_paint (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  if (priv->offscreen)
    {
      gboolean can_blit;
      CoglMatrix matrix;

      clutter_stage_view_ensure_offscreen_blit_pipeline (view);
      clutter_stage_view_get_offscreen_transformation_matrix (view, &matrix);
      can_blit = cogl_matrix_is_identity (&matrix);

      if (priv->shadowfb)
        {
          clutter_stage_view_copy_to_framebuffer (view,
                                                  priv->offscreen_pipeline,
                                                  priv->offscreen,
                                                  priv->shadowfb,
                                                  can_blit);
        }
      else
        {
          clutter_stage_view_copy_to_framebuffer (view,
                                                  priv->offscreen_pipeline,
                                                  priv->offscreen,
                                                  priv->framebuffer,
                                                  can_blit);
        }
    }

  if (priv->shadowfb)
    {
      clutter_stage_view_ensure_shadowfb_blit_pipeline (view);
      clutter_stage_view_copy_to_framebuffer (view,
                                              priv->shadowfb_pipeline,
                                              priv->shadowfb,
                                              priv->framebuffer,
                                              TRUE);
    }
}

float
clutter_stage_view_get_scale (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->scale;
}

gboolean
clutter_stage_view_is_dirty_viewport (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->dirty_viewport;
}

void
clutter_stage_view_set_dirty_viewport (ClutterStageView *view,
                                       gboolean          dirty)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  priv->dirty_viewport = dirty;
}

gboolean
clutter_stage_view_is_dirty_projection (ClutterStageView *view)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  return priv->dirty_projection;
}

void
clutter_stage_view_set_dirty_projection (ClutterStageView *view,
                                         gboolean          dirty)
{
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  priv->dirty_projection = dirty;
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

void
clutter_stage_view_transform_to_onscreen (ClutterStageView *view,
                                          gfloat           *x,
                                          gfloat           *y)
{
  gfloat z = 0, w = 1;
  CoglMatrix matrix;

  clutter_stage_view_get_offscreen_transformation_matrix (view, &matrix);
  cogl_matrix_get_inverse (&matrix, &matrix);
  cogl_matrix_transform_point (&matrix, x, y, &z, &w);
}

static void
clutter_stage_default_get_offscreen_transformation_matrix (ClutterStageView *view,
                                                           CoglMatrix       *matrix)
{
  cogl_matrix_init_identity (matrix);
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
    case PROP_LAYOUT:
      g_value_set_boxed (value, &priv->layout);
      break;
    case PROP_FRAMEBUFFER:
      g_value_set_boxed (value, priv->framebuffer);
      break;
    case PROP_OFFSCREEN:
      g_value_set_boxed (value, priv->offscreen);
      break;
    case PROP_SHADOWFB:
      g_value_set_boxed (value, priv->shadowfb);
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
    case PROP_SHADOWFB:
      priv->shadowfb = g_value_dup_boxed (value);
      break;
    case PROP_SCALE:
      priv->scale = g_value_get_float (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
clutter_stage_view_dispose (GObject *object)
{
  ClutterStageView *view = CLUTTER_STAGE_VIEW (object);
  ClutterStageViewPrivate *priv =
    clutter_stage_view_get_instance_private (view);

  g_clear_pointer (&priv->framebuffer, cogl_object_unref);
  g_clear_pointer (&priv->shadowfb, cogl_object_unref);
  g_clear_pointer (&priv->offscreen, cogl_object_unref);
  g_clear_pointer (&priv->offscreen_pipeline, cogl_object_unref);
  g_clear_pointer (&priv->shadowfb_pipeline, cogl_object_unref);
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
  object_class->dispose = clutter_stage_view_dispose;

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

  obj_props[PROP_SHADOWFB] =
    g_param_spec_boxed ("shadowfb",
                        "Shadow framebuffer",
                        "Framebuffer used as intermediate shadow buffer",
                        COGL_TYPE_HANDLE,
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
