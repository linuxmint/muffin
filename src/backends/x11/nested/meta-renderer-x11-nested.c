/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat
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
 */

#include "config.h"

#include "backends/x11/nested/meta-renderer-x11-nested.h"

#include <glib-object.h>

#include "clutter/x11/clutter-x11.h"
#include "backends/meta-backend-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-output.h"
#include "backends/meta-renderer.h"
#include "backends/meta-renderer-view.h"
#include "core/boxes-private.h"
#include "meta/meta-backend.h"
#include "meta/util.h"

struct _MetaRendererX11Nested
{
  MetaRendererX11 parent;
};

G_DEFINE_TYPE (MetaRendererX11Nested, meta_renderer_x11_nested,
               META_TYPE_RENDERER_X11)

static MetaMonitorTransform
calculate_view_transform (MetaMonitorManager *monitor_manager,
                          MetaLogicalMonitor *logical_monitor)
{
  MetaMonitor *main_monitor;
  MetaOutput *main_output;
  MetaCrtc *crtc;
  MetaMonitorTransform crtc_transform;

  main_monitor = meta_logical_monitor_get_monitors (logical_monitor)->data;
  main_output = meta_monitor_get_main_output (main_monitor);
  crtc = meta_output_get_assigned_crtc (main_output);
  crtc_transform =
    meta_monitor_logical_to_crtc_transform (main_monitor,
                                            logical_monitor->transform);
  /*
   * Pick any monitor and output and check; all CRTCs of a logical monitor will
   * always have the same transform assigned to them.
   */

  if (meta_monitor_manager_is_transform_handled (monitor_manager,
                                                 crtc,
                                                 crtc_transform))
    return META_MONITOR_TRANSFORM_NORMAL;
  else
    return crtc_transform;
}

static MetaRendererView *
get_legacy_view (MetaRenderer *renderer)
{
  GList *views;

  views = meta_renderer_get_views (renderer);
  if (views)
    return META_RENDERER_VIEW (views->data);
  else
    return NULL;
}

static CoglOffscreen *
create_offscreen (CoglContext *cogl_context,
                  int          width,
                  int          height)
{
  CoglTexture2D *texture_2d;
  CoglOffscreen *offscreen;
  GError *error = NULL;

  texture_2d = cogl_texture_2d_new_with_size (cogl_context, width, height);
  offscreen = cogl_offscreen_new_with_texture (COGL_TEXTURE (texture_2d));

  if (!cogl_framebuffer_allocate (COGL_FRAMEBUFFER (offscreen), &error))
    meta_fatal ("Couldn't allocate framebuffer: %s", error->message);

  return offscreen;
}

static void
meta_renderer_x11_nested_resize_legacy_view (MetaRendererX11Nested *renderer_x11_nested,
                                             int                    width,
                                             int                    height)
{
  MetaRenderer *renderer = META_RENDERER (renderer_x11_nested);
  MetaBackend *backend = meta_get_backend ();
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  MetaRendererView *legacy_view;
  cairo_rectangle_int_t view_layout;
  CoglOffscreen *fake_onscreen;

  legacy_view = get_legacy_view (renderer);

  clutter_stage_view_get_layout (CLUTTER_STAGE_VIEW (legacy_view),
                                 &view_layout);
  if (view_layout.width == width &&
      view_layout.height == height)
    return;

  view_layout = (cairo_rectangle_int_t) {
      .width = width,
        .height = height
  };

  fake_onscreen = create_offscreen (cogl_context, width, height);

  g_object_set (G_OBJECT (legacy_view),
                "layout", &view_layout,
                "framebuffer", COGL_FRAMEBUFFER (fake_onscreen),
                NULL);
}

void
meta_renderer_x11_nested_ensure_legacy_view (MetaRendererX11Nested *renderer_x11_nested,
                                             int                    width,
                                             int                    height)
{
  MetaRenderer *renderer = META_RENDERER (renderer_x11_nested);
  MetaBackend *backend = meta_get_backend ();
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  cairo_rectangle_int_t view_layout;
  CoglOffscreen *fake_onscreen;
  MetaRendererView *legacy_view;

  if (get_legacy_view (renderer))
    {
      meta_renderer_x11_nested_resize_legacy_view (renderer_x11_nested,
                                                   width, height);
      return;
    }

  fake_onscreen = create_offscreen (cogl_context, width, height);

  view_layout = (cairo_rectangle_int_t) {
    .width = width,
    .height = height
  };
  legacy_view = g_object_new (META_TYPE_RENDERER_VIEW,
                              "layout", &view_layout,
                              "framebuffer", COGL_FRAMEBUFFER (fake_onscreen),
                              NULL);

  g_assert (!meta_renderer_get_views (renderer));
  meta_renderer_add_view (renderer, legacy_view);
}

static MetaRendererView *
meta_renderer_x11_nested_create_view (MetaRenderer       *renderer,
                                      MetaLogicalMonitor *logical_monitor,
                                      MetaOutput         *output,
                                      MetaCrtc           *crtc)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  MetaMonitorTransform view_transform;
  float view_scale;
  int width, height;
  CoglOffscreen *fake_onscreen;
  CoglOffscreen *offscreen;
  MetaRectangle view_layout;
  MetaRendererView *view;

  view_transform = calculate_view_transform (monitor_manager, logical_monitor);

  if (meta_is_stage_views_scaled ())
    view_scale = logical_monitor->scale;
  else
    view_scale = 1.0;

  width = roundf (crtc->config->layout.size.width * view_scale);
  height = roundf (crtc->config->layout.size.height * view_scale);

  fake_onscreen = create_offscreen (cogl_context, width, height);

  if (view_transform != META_MONITOR_TRANSFORM_NORMAL)
    offscreen = create_offscreen (cogl_context, width, height);
  else
    offscreen = NULL;

  meta_rectangle_from_graphene_rect (&crtc->config->layout,
                                     META_ROUNDING_STRATEGY_ROUND,
                                     &view_layout);

  view = g_object_new (META_TYPE_RENDERER_VIEW,
                       "layout", &view_layout,
                       "framebuffer", COGL_FRAMEBUFFER (fake_onscreen),
                       "offscreen", COGL_FRAMEBUFFER (offscreen),
                       "transform", view_transform,
                       "scale", view_scale,
                       NULL);
  g_object_set_data (G_OBJECT (view), "crtc", crtc);

  return view;
}

static void
meta_renderer_x11_nested_init (MetaRendererX11Nested *renderer_x11_nested)
{
}

static void
meta_renderer_x11_nested_class_init (MetaRendererX11NestedClass *klass)
{
  MetaRendererClass *renderer_class = META_RENDERER_CLASS (klass);

  renderer_class->create_view = meta_renderer_x11_nested_create_view;
}
