/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat Inc.
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
 *     Jonas Ådahl <jadahl@gmail.com>
 */

#include "config.h"

#include "backends/x11/nested/meta-stage-x11-nested.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-crtc.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor.h"
#include "backends/meta-output.h"
#include "backends/meta-renderer.h"
#include "backends/x11/nested/meta-renderer-x11-nested.h"
#include "clutter/clutter-mutter.h"

static ClutterStageWindowInterface *clutter_stage_window_parent_iface = NULL;

struct _MetaStageX11Nested
{
  MetaStageX11 parent;

  CoglPipeline *pipeline;
};

static void
clutter_stage_window_iface_init (ClutterStageWindowInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaStageX11Nested, meta_stage_x11_nested,
                         META_TYPE_STAGE_X11,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_STAGE_WINDOW,
                                                clutter_stage_window_iface_init))

typedef struct _MetaStageX11View
{
  CoglTexture *texture;
  ClutterStageViewCogl *view;
} MetaStageX11NestedView;

static void
meta_stage_x11_nested_resize (ClutterStageWindow *stage_window,
                              gint                width,
                              gint                height)
{
  if (!meta_is_stage_views_enabled ())
    {
      MetaBackend *backend = meta_get_backend ();
      MetaRenderer *renderer = meta_backend_get_renderer (backend);
      MetaRendererX11Nested *renderer_x11_nested =
        META_RENDERER_X11_NESTED (renderer);

      meta_renderer_x11_nested_ensure_legacy_view (renderer_x11_nested,
                                                   width, height);
    }

  clutter_stage_window_parent_iface->resize (stage_window, width, height);
}

static gboolean
meta_stage_x11_nested_can_clip_redraws (ClutterStageWindow *stage_window)
{
  return FALSE;
}

static GList *
meta_stage_x11_nested_get_views (ClutterStageWindow *stage_window)
{
  MetaBackend *backend = meta_get_backend ();
  MetaRenderer *renderer = meta_backend_get_renderer (backend);

  return meta_renderer_get_views (renderer);
}

typedef struct
{
  MetaStageX11Nested *stage_nested;
  CoglTexture *texture;
  ClutterStageView *view;
  MetaLogicalMonitor *logical_monitor;
} DrawCrtcData;

static gboolean
draw_view (MetaStageX11Nested *stage_nested,
           MetaRendererView   *renderer_view,
           CoglTexture        *texture)
{
  MetaStageX11 *stage_x11 = META_STAGE_X11 (stage_nested);
  CoglFramebuffer *onscreen = COGL_FRAMEBUFFER (stage_x11->onscreen);
  ClutterStageView *stage_view = CLUTTER_STAGE_VIEW (renderer_view);
  MetaCrtc *crtc;
  MetaCrtcConfig *crtc_config;
  CoglMatrix projection_matrix;
  CoglMatrix transform;
  float texture_width, texture_height;
  float sample_x, sample_y, sample_width, sample_height;
  float s_1, t_1, s_2, t_2;

  texture_width = cogl_texture_get_width (texture);
  texture_height = cogl_texture_get_height (texture);

  crtc = g_object_get_data (G_OBJECT (renderer_view), "crtc");
  crtc_config = crtc->config;

  sample_x = 0;
  sample_y = 0;
  sample_width = texture_width;
  sample_height = texture_height;

  clutter_stage_view_get_offscreen_transformation_matrix (stage_view,
                                                          &transform);

  cogl_framebuffer_push_matrix (onscreen);
  cogl_matrix_init_identity (&projection_matrix);
  cogl_matrix_translate (&projection_matrix, -1, 1, 0);
  cogl_matrix_scale (&projection_matrix, 2, -2, 0);

  cogl_matrix_multiply (&projection_matrix, &projection_matrix, &transform);
  cogl_framebuffer_set_projection_matrix (onscreen, &projection_matrix);

  s_1 = sample_x / texture_width;
  t_1 = sample_y / texture_height;
  s_2 = (sample_x + sample_width) / texture_width;
  t_2 = (sample_y + sample_height) / texture_height;

  cogl_framebuffer_set_viewport (onscreen,
                                 crtc_config->layout.origin.x,
                                 crtc_config->layout.origin.y,
                                 crtc_config->layout.size.width,
                                 crtc_config->layout.size.height);

  cogl_framebuffer_draw_textured_rectangle (onscreen,
                                            stage_nested->pipeline,
                                            0, 0, 1, 1,
                                            s_1, t_1, s_2, t_2);

  cogl_framebuffer_pop_matrix (onscreen);
  return TRUE;
}

static void
meta_stage_x11_nested_finish_frame (ClutterStageWindow *stage_window)
{
  MetaStageX11Nested *stage_nested = META_STAGE_X11_NESTED (stage_window);
  MetaStageX11 *stage_x11 = META_STAGE_X11 (stage_window);
  MetaBackend *backend = meta_get_backend ();
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglFramebuffer *onscreen = COGL_FRAMEBUFFER (stage_x11->onscreen);
  GList *l;

  if (!stage_nested->pipeline)
    stage_nested->pipeline = cogl_pipeline_new (clutter_backend->cogl_context);

  cogl_framebuffer_clear4f (onscreen,
                            COGL_BUFFER_BIT_COLOR,
                            0.0f, 0.0f, 0.0f, 1.0f);

  for (l = meta_renderer_get_views (renderer); l; l = l->next)
    {
      ClutterStageView *view = l->data;
      MetaRendererView *renderer_view = META_RENDERER_VIEW (view);
      CoglFramebuffer *framebuffer;
      CoglTexture *texture;

      framebuffer = clutter_stage_view_get_onscreen (view);
      texture = cogl_offscreen_get_texture (COGL_OFFSCREEN (framebuffer));

      cogl_pipeline_set_layer_texture (stage_nested->pipeline, 0, texture);
      cogl_pipeline_set_layer_wrap_mode (stage_nested->pipeline, 0,
                                         COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);

      draw_view (stage_nested, renderer_view, texture);
    }

  cogl_onscreen_swap_buffers (stage_x11->onscreen);
}

static void
meta_stage_x11_nested_unrealize (ClutterStageWindow *stage_window)
{
  MetaStageX11Nested *stage_nested = META_STAGE_X11_NESTED (stage_window);

  g_clear_pointer (&stage_nested->pipeline, cogl_object_unref);

  clutter_stage_window_parent_iface->unrealize (stage_window);
}

static void
meta_stage_x11_nested_init (MetaStageX11Nested *stage_x11_nested)
{
}

static void
meta_stage_x11_nested_class_init (MetaStageX11NestedClass *klass)
{
}

static void
clutter_stage_window_iface_init (ClutterStageWindowInterface *iface)
{
  clutter_stage_window_parent_iface = g_type_interface_peek_parent (iface);

  iface->resize = meta_stage_x11_nested_resize;
  iface->can_clip_redraws = meta_stage_x11_nested_can_clip_redraws;
  iface->unrealize = meta_stage_x11_nested_unrealize;
  iface->get_views = meta_stage_x11_nested_get_views;
  iface->finish_frame = meta_stage_x11_nested_finish_frame;
}
