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

#include "backends/native/meta-stage-native.h"

#include "backends/meta-backend-private.h"
#include "backends/native/meta-renderer-native.h"
#include "meta/meta-backend.h"
#include "meta/meta-monitor-manager.h"
#include "meta/util.h"

static GQuark quark_view_frame_closure  = 0;

struct _MetaStageNative
{
  ClutterStageCogl parent;

  CoglClosure *frame_closure;

  int64_t presented_frame_counter_sync;
  int64_t presented_frame_counter_complete;
};

static void
clutter_stage_window_iface_init (ClutterStageWindowInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaStageNative, meta_stage_native,
                         CLUTTER_TYPE_STAGE_COGL,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_STAGE_WINDOW,
                                                clutter_stage_window_iface_init))

static void
frame_cb (CoglOnscreen  *onscreen,
          CoglFrameEvent frame_event,
          CoglFrameInfo *frame_info,
          void          *user_data)

{
  MetaStageNative *stage_native = user_data;
  ClutterStageCogl *stage_cogl = CLUTTER_STAGE_COGL (stage_native);
  int64_t global_frame_counter;
  int64_t presented_frame_counter;
  ClutterFrameInfo clutter_frame_info;

  global_frame_counter = cogl_frame_info_get_global_frame_counter (frame_info);

  switch (frame_event)
    {
    case COGL_FRAME_EVENT_SYNC:
      presented_frame_counter = stage_native->presented_frame_counter_sync;
      stage_native->presented_frame_counter_sync = global_frame_counter;
      break;
    case COGL_FRAME_EVENT_COMPLETE:
      presented_frame_counter = stage_native->presented_frame_counter_complete;
      stage_native->presented_frame_counter_complete = global_frame_counter;
      break;
    default:
      g_assert_not_reached ();
    }

  if (global_frame_counter <= presented_frame_counter)
    return;

  clutter_frame_info = (ClutterFrameInfo) {
    .frame_counter = global_frame_counter,
    .refresh_rate = cogl_frame_info_get_refresh_rate (frame_info),
    .presentation_time = cogl_frame_info_get_presentation_time (frame_info)
  };

  _clutter_stage_cogl_presented (stage_cogl, frame_event, &clutter_frame_info);
}

static void
ensure_frame_callback (MetaStageNative  *stage_native,
                       ClutterStageView *stage_view)
{
  CoglFramebuffer *framebuffer;
  CoglOnscreen *onscreen;
  CoglClosure *closure;

  closure = g_object_get_qdata (G_OBJECT (stage_view),
                                quark_view_frame_closure);
  if (closure)
    return;

  framebuffer = clutter_stage_view_get_onscreen (stage_view);
  onscreen = COGL_ONSCREEN (framebuffer);
  closure = cogl_onscreen_add_frame_callback (onscreen,
                                              frame_cb,
                                              stage_native,
                                              NULL);
  g_object_set_qdata (G_OBJECT (stage_view),
                      quark_view_frame_closure,
                      closure);
}

static void
ensure_frame_callbacks (MetaStageNative *stage_native)
{
  MetaBackend *backend = meta_get_backend ();
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  GList *l;

  for (l = meta_renderer_get_views (renderer); l; l = l->next)
    {
      ClutterStageView *stage_view = l->data;

      ensure_frame_callback (stage_native, stage_view);
    }
}

void
meta_stage_native_rebuild_views (MetaStageNative *stage_native)
{
  MetaBackend *backend = meta_get_backend ();
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  ClutterActor *stage = meta_backend_get_stage (backend);

  meta_renderer_rebuild_views (renderer);
  clutter_stage_update_resource_scales (CLUTTER_STAGE (stage));
  ensure_frame_callbacks (stage_native);
}

static gboolean
meta_stage_native_can_clip_redraws (ClutterStageWindow *stage_window)
{
  return TRUE;
}

static void
meta_stage_native_get_geometry (ClutterStageWindow    *stage_window,
                                cairo_rectangle_int_t *geometry)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  if (monitor_manager)
    {
      int width, height;

      meta_monitor_manager_get_screen_size (monitor_manager, &width, &height);
      *geometry = (cairo_rectangle_int_t) {
        .width = width,
        .height = height,
      };
    }
  else
    {
      *geometry = (cairo_rectangle_int_t) {
        .width = 1,
        .height = 1,
      };
    }
}

static GList *
meta_stage_native_get_views (ClutterStageWindow *stage_window)
{
  MetaBackend *backend = meta_get_backend ();
  MetaRenderer *renderer = meta_backend_get_renderer (backend);

  return meta_renderer_get_views (renderer);
}

static int64_t
meta_stage_native_get_frame_counter (ClutterStageWindow *stage_window)
{
  MetaBackend *backend = meta_get_backend ();
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  MetaRendererNative *renderer_native = META_RENDERER_NATIVE (renderer);

  return meta_renderer_native_get_frame_counter (renderer_native);
}

static void
meta_stage_native_finish_frame (ClutterStageWindow *stage_window)
{
  MetaBackend *backend = meta_get_backend ();
  MetaRenderer *renderer = meta_backend_get_renderer (backend);

  meta_renderer_native_finish_frame (META_RENDERER_NATIVE (renderer));
}

static void
meta_stage_native_init (MetaStageNative *stage_native)
{
  stage_native->presented_frame_counter_sync = -1;
  stage_native->presented_frame_counter_complete = -1;
}

static void
meta_stage_native_class_init (MetaStageNativeClass *klass)
{
  quark_view_frame_closure =
    g_quark_from_static_string ("-meta-native-stage-view-frame-closure");
}

static void
clutter_stage_window_iface_init (ClutterStageWindowInterface *iface)
{
  iface->can_clip_redraws = meta_stage_native_can_clip_redraws;
  iface->get_geometry = meta_stage_native_get_geometry;
  iface->get_views = meta_stage_native_get_views;
  iface->get_frame_counter = meta_stage_native_get_frame_counter;
  iface->finish_frame = meta_stage_native_finish_frame;
}
