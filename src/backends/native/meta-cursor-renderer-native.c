/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
 * Copyright 2020 DisplayLink (UK) Ltd.
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

#include "backends/native/meta-cursor-renderer-native.h"

#include <string.h>
#include <gbm.h>
#include <xf86drm.h>
#include <errno.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-cursor-sprite-xcursor.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-output.h"
#include "backends/native/meta-crtc-kms.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-update.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-renderer-native.h"
#include "core/boxes-private.h"
#include "meta/boxes.h"
#include "meta/meta-backend.h"
#include "meta/util.h"

#ifdef HAVE_WAYLAND
#include "wayland/meta-cursor-sprite-wayland.h"
#include "wayland/meta-wayland-buffer.h"
#endif

#ifndef DRM_CAP_CURSOR_WIDTH
#define DRM_CAP_CURSOR_WIDTH 0x8
#endif
#ifndef DRM_CAP_CURSOR_HEIGHT
#define DRM_CAP_CURSOR_HEIGHT 0x9
#endif

/* When animating a cursor, we usually call drmModeSetCursor2 once per frame.
 * Though, testing shows that we need to triple buffer the cursor buffer in
 * order to avoid glitches when animating the cursor, at least when running on
 * Intel. The reason for this might be (but is not confirmed to be) due to
 * the user space gbm_bo cache, making us reuse and overwrite the kernel side
 * buffer content before it was scanned out. To avoid this, we keep a user space
 * reference to each buffer we set until at least one frame after it was drawn.
 * In effect, this means we three active cursor gbm_bo's: one that that just has
 * been set, one that was previously set and may or may not have been scanned
 * out, and one pending that will be replaced if the cursor sprite changes.
 */
#define HW_CURSOR_BUFFER_COUNT 3

static GQuark quark_cursor_sprite = 0;

struct _MetaCursorRendererNative
{
  MetaCursorRenderer parent;
};

struct _MetaCursorRendererNativePrivate
{
  MetaBackend *backend;

  gboolean hw_state_invalidated;
  gboolean has_hw_cursor;

  MetaCursorSprite *last_cursor;
  guint animation_timeout_id;
};
typedef struct _MetaCursorRendererNativePrivate MetaCursorRendererNativePrivate;

typedef struct _MetaCursorRendererNativeGpuData
{
  gboolean hw_cursor_broken;

  uint64_t cursor_width;
  uint64_t cursor_height;
} MetaCursorRendererNativeGpuData;

typedef enum _MetaCursorGbmBoState
{
  META_CURSOR_GBM_BO_STATE_NONE,
  META_CURSOR_GBM_BO_STATE_SET,
  META_CURSOR_GBM_BO_STATE_INVALIDATED,
} MetaCursorGbmBoState;

typedef struct _MetaCursorNativeGpuState
{
  MetaGpu *gpu;
  guint active_bo;
  MetaCursorGbmBoState pending_bo_state;
  struct gbm_bo *bos[HW_CURSOR_BUFFER_COUNT];
} MetaCursorNativeGpuState;

typedef struct _MetaCursorNativePrivate
{
  GHashTable *gpu_states;

  struct {
    gboolean can_preprocess;
    float current_relative_scale;
    MetaMonitorTransform current_relative_transform;
  } preprocess_state;
} MetaCursorNativePrivate;

static GQuark quark_cursor_renderer_native_gpu_data = 0;

G_DEFINE_TYPE_WITH_PRIVATE (MetaCursorRendererNative, meta_cursor_renderer_native, META_TYPE_CURSOR_RENDERER);

static void
realize_cursor_sprite (MetaCursorRenderer *renderer,
                       MetaCursorSprite   *cursor_sprite,
                       GList              *gpus);

static MetaCursorNativeGpuState *
get_cursor_gpu_state (MetaCursorNativePrivate *cursor_priv,
                      MetaGpuKms              *gpu_kms);

static MetaCursorNativeGpuState *
ensure_cursor_gpu_state (MetaCursorNativePrivate *cursor_priv,
                         MetaGpuKms              *gpu_kms);

static void
invalidate_cursor_gpu_state (MetaCursorSprite *cursor_sprite);

static MetaCursorNativePrivate *
ensure_cursor_priv (MetaCursorSprite *cursor_sprite);

static MetaCursorNativePrivate *
get_cursor_priv (MetaCursorSprite *cursor_sprite);

static MetaCursorRendererNativeGpuData *
meta_cursor_renderer_native_gpu_data_from_gpu (MetaGpuKms *gpu_kms)
{
  return g_object_get_qdata (G_OBJECT (gpu_kms),
                             quark_cursor_renderer_native_gpu_data);
}

static MetaCursorRendererNativeGpuData *
meta_create_cursor_renderer_native_gpu_data (MetaGpuKms *gpu_kms)
{
  MetaCursorRendererNativeGpuData *cursor_renderer_gpu_data;

  cursor_renderer_gpu_data = g_new0 (MetaCursorRendererNativeGpuData, 1);
  g_object_set_qdata_full (G_OBJECT (gpu_kms),
                           quark_cursor_renderer_native_gpu_data,
                           cursor_renderer_gpu_data,
                           g_free);

  return cursor_renderer_gpu_data;
}

static void
meta_cursor_renderer_native_finalize (GObject *object)
{
  MetaCursorRendererNative *renderer = META_CURSOR_RENDERER_NATIVE (object);
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (renderer);

  g_clear_handle_id (&priv->animation_timeout_id, g_source_remove);

  G_OBJECT_CLASS (meta_cursor_renderer_native_parent_class)->finalize (object);
}

static guint
get_pending_cursor_sprite_gbm_bo_index (MetaCursorNativeGpuState *cursor_gpu_state)
{
  return (cursor_gpu_state->active_bo + 1) % HW_CURSOR_BUFFER_COUNT;
}

static struct gbm_bo *
get_pending_cursor_sprite_gbm_bo (MetaCursorNativeGpuState *cursor_gpu_state)
{
  guint pending_bo;

  pending_bo = get_pending_cursor_sprite_gbm_bo_index (cursor_gpu_state);
  return cursor_gpu_state->bos[pending_bo];
}

static struct gbm_bo *
get_active_cursor_sprite_gbm_bo (MetaCursorNativeGpuState *cursor_gpu_state)
{
  return cursor_gpu_state->bos[cursor_gpu_state->active_bo];
}

static void
set_pending_cursor_sprite_gbm_bo (MetaCursorSprite *cursor_sprite,
                                  MetaGpuKms       *gpu_kms,
                                  struct gbm_bo    *bo)
{
  MetaCursorNativePrivate *cursor_priv;
  MetaCursorNativeGpuState *cursor_gpu_state;
  guint pending_bo;

  cursor_priv = ensure_cursor_priv (cursor_sprite);
  cursor_gpu_state = ensure_cursor_gpu_state (cursor_priv, gpu_kms);

  pending_bo = get_pending_cursor_sprite_gbm_bo_index (cursor_gpu_state);
  cursor_gpu_state->bos[pending_bo] = bo;
  cursor_gpu_state->pending_bo_state = META_CURSOR_GBM_BO_STATE_SET;
}

static void
calculate_crtc_cursor_hotspot (MetaCursorSprite *cursor_sprite,
                               int              *cursor_hotspot_x,
                               int              *cursor_hotspot_y)
{
  MetaCursorNativePrivate *cursor_priv = get_cursor_priv (cursor_sprite);
  int hot_x, hot_y;
  int width, height;
  float scale;
  MetaMonitorTransform transform;

  scale = cursor_priv->preprocess_state.current_relative_scale;
  transform = cursor_priv->preprocess_state.current_relative_transform;

  meta_cursor_sprite_get_hotspot (cursor_sprite, &hot_x, &hot_y);
  width = meta_cursor_sprite_get_width (cursor_sprite);
  height = meta_cursor_sprite_get_height (cursor_sprite);
  meta_monitor_transform_transform_point (transform,
                                          width, height,
                                          hot_x, hot_y,
                                          &hot_x, &hot_y);
  *cursor_hotspot_x = (int) roundf (hot_x * scale);
  *cursor_hotspot_y = (int) roundf (hot_y * scale);
}

static void
set_crtc_cursor (MetaCursorRendererNative *native,
                 MetaKmsUpdate            *kms_update,
                 MetaCrtc                 *crtc,
                 int                       x,
                 int                       y,
                 MetaCursorSprite         *cursor_sprite)
{
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (native);
  MetaCursorNativePrivate *cursor_priv = get_cursor_priv (cursor_sprite);
  MetaGpuKms *gpu_kms = META_GPU_KMS (meta_crtc_get_gpu (crtc));
  MetaCursorRendererNativeGpuData *cursor_renderer_gpu_data =
    meta_cursor_renderer_native_gpu_data_from_gpu (gpu_kms);
  MetaCursorNativeGpuState *cursor_gpu_state =
    get_cursor_gpu_state (cursor_priv, gpu_kms);
  MetaKmsCrtc *kms_crtc;
  MetaKmsDevice *kms_device;
  MetaKmsPlane *cursor_plane;
  struct gbm_bo *bo;
  union gbm_bo_handle handle;
  int cursor_width, cursor_height;
  MetaFixed16Rectangle src_rect;
  MetaFixed16Rectangle dst_rect;
  MetaKmsAssignPlaneFlag flags;
  int cursor_hotspot_x;
  int cursor_hotspot_y;
  MetaKmsPlaneAssignment *plane_assignment;

  if (cursor_gpu_state->pending_bo_state == META_CURSOR_GBM_BO_STATE_SET)
    bo = get_pending_cursor_sprite_gbm_bo (cursor_gpu_state);
  else
    bo = get_active_cursor_sprite_gbm_bo (cursor_gpu_state);

  kms_crtc = meta_crtc_kms_get_kms_crtc (crtc);
  kms_device = meta_kms_crtc_get_device (kms_crtc);
  cursor_plane = meta_kms_device_get_cursor_plane_for (kms_device, kms_crtc);
  g_return_if_fail (cursor_plane);

  handle = gbm_bo_get_handle (bo);

  cursor_width = cursor_renderer_gpu_data->cursor_width;
  cursor_height = cursor_renderer_gpu_data->cursor_height;
  src_rect = (MetaFixed16Rectangle) {
    .x = meta_fixed_16_from_int (0),
    .y = meta_fixed_16_from_int (0),
    .width = meta_fixed_16_from_int (cursor_width),
    .height = meta_fixed_16_from_int (cursor_height),
  };
  dst_rect = (MetaFixed16Rectangle) {
    .x = meta_fixed_16_from_int (x),
    .y = meta_fixed_16_from_int (y),
    .width = meta_fixed_16_from_int (cursor_width),
    .height = meta_fixed_16_from_int (cursor_height),
  };

  flags = META_KMS_ASSIGN_PLANE_FLAG_NONE;
  if (!priv->hw_state_invalidated && bo == crtc->cursor_renderer_private)
    flags |= META_KMS_ASSIGN_PLANE_FLAG_FB_UNCHANGED;

  plane_assignment = meta_kms_update_assign_plane (kms_update,
                                                   kms_crtc,
                                                   cursor_plane,
                                                   handle.u32,
                                                   src_rect,
                                                   dst_rect,
                                                   flags);

  calculate_crtc_cursor_hotspot (cursor_sprite,
                                 &cursor_hotspot_x,
                                 &cursor_hotspot_y);
  meta_kms_plane_assignment_set_cursor_hotspot (plane_assignment,
                                                cursor_hotspot_x,
                                                cursor_hotspot_y);

  crtc->cursor_renderer_private = bo;

  if (cursor_gpu_state->pending_bo_state == META_CURSOR_GBM_BO_STATE_SET)
    {
      cursor_gpu_state->active_bo =
        (cursor_gpu_state->active_bo + 1) % HW_CURSOR_BUFFER_COUNT;
      cursor_gpu_state->pending_bo_state = META_CURSOR_GBM_BO_STATE_NONE;
    }
}

static void
unset_crtc_cursor (MetaCursorRendererNative *native,
                   MetaKmsUpdate            *kms_update,
                   MetaCrtc                 *crtc)
{
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (native);
  MetaKmsCrtc *kms_crtc;
  MetaKmsDevice *kms_device;
  MetaKmsPlane *cursor_plane;

  if (!priv->hw_state_invalidated && !crtc->cursor_renderer_private)
    return;

  kms_crtc = meta_crtc_kms_get_kms_crtc (crtc);
  kms_device = meta_kms_crtc_get_device (kms_crtc);
  cursor_plane = meta_kms_device_get_cursor_plane_for (kms_device, kms_crtc);

  if (cursor_plane)
    meta_kms_update_unassign_plane (kms_update, kms_crtc, cursor_plane);

  crtc->cursor_renderer_private = NULL;
}

static float
calculate_cursor_crtc_sprite_scale (MetaCursorSprite   *cursor_sprite,
                                    MetaLogicalMonitor *logical_monitor)
{
  if (meta_is_stage_views_scaled ())
    {
      return (meta_logical_monitor_get_scale (logical_monitor) *
              meta_cursor_sprite_get_texture_scale (cursor_sprite));
    }
  else
    {
      return 1.0;
    }
}

typedef struct
{
  MetaCursorRendererNative *in_cursor_renderer_native;
  MetaLogicalMonitor *in_logical_monitor;
  graphene_rect_t in_local_cursor_rect;
  MetaCursorSprite *in_cursor_sprite;
  MetaKmsUpdate *in_kms_update;

  gboolean out_painted;
} UpdateCrtcCursorData;

static gboolean
update_monitor_crtc_cursor (MetaMonitor         *monitor,
                            MetaMonitorMode     *monitor_mode,
                            MetaMonitorCrtcMode *monitor_crtc_mode,
                            gpointer             user_data,
                            GError             **error)
{
  UpdateCrtcCursorData *data = user_data;
  MetaCursorRendererNative *cursor_renderer_native =
    data->in_cursor_renderer_native;
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (cursor_renderer_native);
  MetaCrtc *crtc;
  MetaMonitorTransform transform;
  graphene_rect_t scaled_crtc_rect;
  float scale;
  int crtc_x, crtc_y;
  int crtc_width, crtc_height;

  if (meta_is_stage_views_scaled ())
    scale = meta_logical_monitor_get_scale (data->in_logical_monitor);
  else
    scale = 1.0;

  transform = meta_logical_monitor_get_transform (data->in_logical_monitor);
  transform = meta_monitor_logical_to_crtc_transform (monitor, transform);

  meta_monitor_calculate_crtc_pos (monitor, monitor_mode,
                                   monitor_crtc_mode->output,
                                   transform,
                                   &crtc_x, &crtc_y);

  if (meta_monitor_transform_is_rotated (transform))
    {
      crtc_width = monitor_crtc_mode->crtc_mode->height;
      crtc_height = monitor_crtc_mode->crtc_mode->width;
    }
  else
    {
      crtc_width = monitor_crtc_mode->crtc_mode->width;
      crtc_height = monitor_crtc_mode->crtc_mode->height;
    }

  scaled_crtc_rect = (graphene_rect_t) {
    .origin = {
      .x = crtc_x / scale,
      .y = crtc_y / scale
    },
    .size = {
      .width = crtc_width / scale,
      .height = crtc_height / scale
    },
  };

  crtc = meta_output_get_assigned_crtc (monitor_crtc_mode->output);

  if (priv->has_hw_cursor &&
      graphene_rect_intersection (&scaled_crtc_rect,
                                  &data->in_local_cursor_rect,
                                  NULL))
    {
      MetaMonitorTransform inverted_transform;
      MetaRectangle cursor_rect;
      CoglTexture *texture;
      float crtc_cursor_x, crtc_cursor_y;
      float cursor_crtc_scale;
      int tex_width, tex_height;

      crtc_cursor_x = (data->in_local_cursor_rect.origin.x -
                       scaled_crtc_rect.origin.x) * scale;
      crtc_cursor_y = (data->in_local_cursor_rect.origin.y -
                       scaled_crtc_rect.origin.y) * scale;

      texture = meta_cursor_sprite_get_cogl_texture (data->in_cursor_sprite);
      tex_width = cogl_texture_get_width (texture);
      tex_height = cogl_texture_get_height (texture);

      cursor_crtc_scale =
        calculate_cursor_crtc_sprite_scale (data->in_cursor_sprite,
                                            data->in_logical_monitor);

      cursor_rect = (MetaRectangle) {
        .x = floorf (crtc_cursor_x),
        .y = floorf (crtc_cursor_y),
        .width = roundf (tex_width * cursor_crtc_scale),
        .height = roundf (tex_height * cursor_crtc_scale)
      };

      inverted_transform = meta_monitor_transform_invert (transform);
      meta_rectangle_transform (&cursor_rect,
                                inverted_transform,
                                monitor_crtc_mode->crtc_mode->width,
                                monitor_crtc_mode->crtc_mode->height,
                                &cursor_rect);

      set_crtc_cursor (data->in_cursor_renderer_native,
                       data->in_kms_update,
                       crtc,
                       cursor_rect.x,
                       cursor_rect.y,
                       data->in_cursor_sprite);

      data->out_painted = data->out_painted || TRUE;
    }
  else
    {
      unset_crtc_cursor (data->in_cursor_renderer_native,
                         data->in_kms_update,
                         crtc);
    }

  return TRUE;
}

static void
disable_hw_cursor_for_crtc (MetaKmsCrtc  *kms_crtc,
                            const GError *error)
{
  MetaCrtc *crtc = meta_crtc_kms_from_kms_crtc (kms_crtc);
  MetaGpuKms *gpu_kms = META_GPU_KMS (meta_crtc_get_gpu (crtc));
  MetaCursorRendererNativeGpuData *cursor_renderer_gpu_data =
    meta_cursor_renderer_native_gpu_data_from_gpu (gpu_kms);

  g_warning ("Failed to set hardware cursor (%s), "
             "using OpenGL from now on",
             error->message);
  cursor_renderer_gpu_data->hw_cursor_broken = TRUE;
}

static void
update_hw_cursor (MetaCursorRendererNative *native,
                  MetaCursorSprite         *cursor_sprite)
{
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (native);
  MetaCursorRenderer *renderer = META_CURSOR_RENDERER (native);
  MetaBackend *backend = priv->backend;
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (priv->backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaKmsUpdate *kms_update;
  GList *logical_monitors;
  GList *l;
  graphene_rect_t rect;
  gboolean painted = FALSE;
  g_autoptr (MetaKmsFeedback) feedback = NULL;

  kms_update = meta_kms_ensure_pending_update (kms);

  if (cursor_sprite)
    rect = meta_cursor_renderer_calculate_rect (renderer, cursor_sprite);
  else
    rect = GRAPHENE_RECT_INIT_ZERO;

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      UpdateCrtcCursorData data;
      GList *monitors;
      GList *k;

      data = (UpdateCrtcCursorData) {
        .in_cursor_renderer_native = native,
        .in_logical_monitor = logical_monitor,
        .in_local_cursor_rect = (graphene_rect_t) {
          .origin = {
            .x = rect.origin.x - logical_monitor->rect.x,
            .y = rect.origin.y - logical_monitor->rect.y
          },
          .size = rect.size
        },
        .in_cursor_sprite = cursor_sprite,
        .in_kms_update = kms_update,
      };

      monitors = meta_logical_monitor_get_monitors (logical_monitor);
      for (k = monitors; k; k = k->next)
        {
          MetaMonitor *monitor = k->data;
          MetaMonitorMode *monitor_mode;

          monitor_mode = meta_monitor_get_current_mode (monitor);
          meta_monitor_mode_foreach_crtc (monitor, monitor_mode,
                                          update_monitor_crtc_cursor,
                                          &data,
                                          NULL);
        }

      painted = painted || data.out_painted;
    }

  feedback = meta_kms_post_pending_update_sync (kms);
  if (meta_kms_feedback_get_result (feedback) != META_KMS_FEEDBACK_PASSED)
    {
      for (l = meta_kms_feedback_get_failed_planes (feedback); l; l = l->next)
        {
          MetaKmsPlaneFeedback *plane_feedback = l->data;

          if (!g_error_matches (plane_feedback->error,
                                G_IO_ERROR,
                                G_IO_ERROR_PERMISSION_DENIED))
            {
              disable_hw_cursor_for_crtc (plane_feedback->crtc,
                                          plane_feedback->error);
            }
        }

      priv->has_hw_cursor = FALSE;
    }

  priv->hw_state_invalidated = FALSE;

  if (painted)
    meta_cursor_renderer_emit_painted (renderer, cursor_sprite);
}

static gboolean
has_valid_cursor_sprite_gbm_bo (MetaCursorSprite *cursor_sprite,
                                MetaGpuKms       *gpu_kms)
{
  MetaCursorNativePrivate *cursor_priv;
  MetaCursorNativeGpuState *cursor_gpu_state;

  cursor_priv = get_cursor_priv (cursor_sprite);
  if (!cursor_priv)
    return FALSE;

  cursor_gpu_state = get_cursor_gpu_state (cursor_priv, gpu_kms);
  if (!cursor_gpu_state)
    return FALSE;

  switch (cursor_gpu_state->pending_bo_state)
    {
    case META_CURSOR_GBM_BO_STATE_NONE:
      return get_active_cursor_sprite_gbm_bo (cursor_gpu_state) != NULL;
    case META_CURSOR_GBM_BO_STATE_SET:
      return TRUE;
    case META_CURSOR_GBM_BO_STATE_INVALIDATED:
      return FALSE;
    }

  g_assert_not_reached ();

  return FALSE;
}

static void
set_can_preprocess (MetaCursorSprite     *cursor_sprite,
                    float                 scale,
                    MetaMonitorTransform  transform)
{
  MetaCursorNativePrivate *cursor_priv = get_cursor_priv (cursor_sprite);

  cursor_priv->preprocess_state.current_relative_scale = scale;
  cursor_priv->preprocess_state.current_relative_transform = transform;
  cursor_priv->preprocess_state.can_preprocess = TRUE;

  invalidate_cursor_gpu_state (cursor_sprite);
}

static void
unset_can_preprocess (MetaCursorSprite *cursor_sprite)
{
  MetaCursorNativePrivate *cursor_priv = get_cursor_priv (cursor_sprite);

  memset (&cursor_priv->preprocess_state,
          0,
          sizeof (cursor_priv->preprocess_state));
  cursor_priv->preprocess_state.can_preprocess = FALSE;

  invalidate_cursor_gpu_state (cursor_sprite);
}

static gboolean
get_can_preprocess (MetaCursorSprite *cursor_sprite)
{
  MetaCursorNativePrivate *cursor_priv = get_cursor_priv (cursor_sprite);

  return cursor_priv->preprocess_state.can_preprocess;
}

static float
get_current_relative_scale (MetaCursorSprite *cursor_sprite)
{
  MetaCursorNativePrivate *cursor_priv = get_cursor_priv (cursor_sprite);

  return cursor_priv->preprocess_state.current_relative_scale;
}

static MetaMonitorTransform
get_current_relative_transform (MetaCursorSprite *cursor_sprite)
{
  MetaCursorNativePrivate *cursor_priv = get_cursor_priv (cursor_sprite);

  return cursor_priv->preprocess_state.current_relative_transform;
}

static void
has_cursor_plane (MetaLogicalMonitor *logical_monitor,
                  MetaMonitor        *monitor,
                  MetaOutput         *output,
                  MetaCrtc           *crtc,
                  gpointer            user_data)
{
  gboolean *has_cursor_planes = user_data;
  MetaKmsCrtc *kms_crtc = meta_crtc_kms_get_kms_crtc (crtc);
  MetaKmsDevice *kms_device = meta_kms_crtc_get_device (kms_crtc);

  *has_cursor_planes &= !!meta_kms_device_get_cursor_plane_for (kms_device,
                                                                kms_crtc);
}

static gboolean
crtcs_has_cursor_planes (MetaCursorRenderer *renderer,
                         MetaCursorSprite   *cursor_sprite)
{
  MetaCursorRendererNative *cursor_renderer_native =
    META_CURSOR_RENDERER_NATIVE (renderer);
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (cursor_renderer_native);
  MetaBackend *backend = priv->backend;
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GList *logical_monitors;
  GList *l;
  graphene_rect_t cursor_rect;

  cursor_rect = meta_cursor_renderer_calculate_rect (renderer, cursor_sprite);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      MetaRectangle logical_monitor_layout;
      graphene_rect_t logical_monitor_rect;
      gboolean has_cursor_planes;

      logical_monitor_layout =
        meta_logical_monitor_get_layout (logical_monitor);
      logical_monitor_rect =
        meta_rectangle_to_graphene_rect (&logical_monitor_layout);

      if (!graphene_rect_intersection (&cursor_rect, &logical_monitor_rect,
                                       NULL))
        continue;

      has_cursor_planes = TRUE;
      meta_logical_monitor_foreach_crtc (logical_monitor,
                                         has_cursor_plane,
                                         &has_cursor_planes);
      if (!has_cursor_planes)
        return FALSE;
    }

  return TRUE;
}

static gboolean
get_common_crtc_sprite_scale_for_logical_monitors (MetaCursorRenderer *renderer,
                                                   MetaCursorSprite   *cursor_sprite,
                                                   float              *out_scale)
{
  MetaCursorRendererNative *cursor_renderer_native =
    META_CURSOR_RENDERER_NATIVE (renderer);
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (cursor_renderer_native);
  MetaBackend *backend = priv->backend;
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  graphene_rect_t cursor_rect;
  float scale = 1.0;
  gboolean has_visible_crtc_sprite = FALSE;
  GList *logical_monitors;
  GList *l;

  cursor_rect = meta_cursor_renderer_calculate_rect (renderer, cursor_sprite);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);

  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      graphene_rect_t logical_monitor_rect =
        meta_rectangle_to_graphene_rect (&logical_monitor->rect);
      float tmp_scale;

      if (!graphene_rect_intersection (&cursor_rect,
                                       &logical_monitor_rect,
                                       NULL))
        continue;

      tmp_scale =
        calculate_cursor_crtc_sprite_scale (cursor_sprite, logical_monitor);

      if (has_visible_crtc_sprite && scale != tmp_scale)
        return FALSE;

      has_visible_crtc_sprite = TRUE;
      scale = tmp_scale;
    }

  if (!has_visible_crtc_sprite)
    return FALSE;

  *out_scale = scale;
  return TRUE;
}

static gboolean
get_common_crtc_sprite_transform_for_logical_monitors (MetaCursorRenderer   *renderer,
                                                       MetaCursorSprite     *cursor_sprite,
                                                       MetaMonitorTransform *out_transform)
{
  MetaCursorRendererNative *cursor_renderer_native =
    META_CURSOR_RENDERER_NATIVE (renderer);
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (cursor_renderer_native);
  MetaBackend *backend = priv->backend;
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  graphene_rect_t cursor_rect;
  MetaMonitorTransform transform = META_MONITOR_TRANSFORM_NORMAL;
  gboolean has_visible_crtc_sprite = FALSE;
  GList *logical_monitors;
  GList *l;

  cursor_rect = meta_cursor_renderer_calculate_rect (renderer, cursor_sprite);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);

  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      graphene_rect_t logical_monitor_rect =
        meta_rectangle_to_graphene_rect (&logical_monitor->rect);
      MetaMonitorTransform logical_transform, tmp_transform;
      GList *monitors, *l_mon;

      if (!graphene_rect_intersection (&cursor_rect,
                                       &logical_monitor_rect,
                                       NULL))
        continue;

      logical_transform = meta_logical_monitor_get_transform (logical_monitor);
      monitors = meta_logical_monitor_get_monitors (logical_monitor);
      for (l_mon = monitors; l_mon; l_mon = l_mon->next)
        {
          MetaMonitor *monitor = l_mon->data;

          tmp_transform = meta_monitor_transform_relative_transform (
            meta_cursor_sprite_get_texture_transform (cursor_sprite),
            meta_monitor_logical_to_crtc_transform (monitor, logical_transform));

          if (has_visible_crtc_sprite && transform != tmp_transform)
            return FALSE;

          has_visible_crtc_sprite = TRUE;
          transform = tmp_transform;
        }
    }

  if (!has_visible_crtc_sprite)
    return FALSE;

  *out_transform = transform;
  return TRUE;
}

static gboolean
should_have_hw_cursor (MetaCursorRenderer *renderer,
                       MetaCursorSprite   *cursor_sprite,
                       GList              *gpus)
{
  CoglTexture *texture;
  MetaMonitorTransform transform;
  float scale;
  GList *l;

  if (!cursor_sprite)
    return FALSE;

  if (meta_cursor_renderer_is_hw_cursors_inhibited (renderer,
                                                    cursor_sprite))
    return FALSE;

  for (l = gpus; l; l = l->next)
    {
      MetaGpuKms *gpu_kms = l->data;
      MetaCursorRendererNativeGpuData *cursor_renderer_gpu_data;

      cursor_renderer_gpu_data =
        meta_cursor_renderer_native_gpu_data_from_gpu (gpu_kms);
      if (!cursor_renderer_gpu_data)
        return FALSE;

      if (cursor_renderer_gpu_data->hw_cursor_broken)
        return FALSE;

      if (!has_valid_cursor_sprite_gbm_bo (cursor_sprite, gpu_kms))
        return FALSE;
    }

  if (!crtcs_has_cursor_planes (renderer, cursor_sprite))
    return FALSE;

  texture = meta_cursor_sprite_get_cogl_texture (cursor_sprite);
  if (!texture)
    return FALSE;

  if (!get_common_crtc_sprite_scale_for_logical_monitors (renderer,
                                                          cursor_sprite,
                                                          &scale))
    return FALSE;

  if (!get_common_crtc_sprite_transform_for_logical_monitors (renderer,
                                                              cursor_sprite,
                                                              &transform))
    return FALSE;

  if (G_APPROX_VALUE (scale, 1.f, FLT_EPSILON) &&
      transform == META_MONITOR_TRANSFORM_NORMAL)
    return TRUE;
  else
    return get_can_preprocess (cursor_sprite);

  return TRUE;
}

static gboolean
meta_cursor_renderer_native_update_animation (MetaCursorRendererNative *native)
{
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (native);
  MetaCursorRenderer *renderer = META_CURSOR_RENDERER (native);
  MetaCursorSprite *cursor_sprite = meta_cursor_renderer_get_cursor (renderer);

  priv->animation_timeout_id = 0;
  meta_cursor_sprite_tick_frame (cursor_sprite);
  meta_cursor_renderer_force_update (renderer);

  return G_SOURCE_REMOVE;
}

static void
maybe_schedule_cursor_sprite_animation_frame (MetaCursorRendererNative *native,
                                              MetaCursorSprite         *cursor_sprite)
{
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (native);
  gboolean cursor_change;
  guint delay;

  cursor_change = cursor_sprite != priv->last_cursor;
  priv->last_cursor = cursor_sprite;

  if (!cursor_change && priv->animation_timeout_id)
    return;

  g_clear_handle_id (&priv->animation_timeout_id, g_source_remove);

  if (cursor_sprite && meta_cursor_sprite_is_animated (cursor_sprite))
    {
      delay = meta_cursor_sprite_get_current_frame_time (cursor_sprite);

      if (delay == 0)
        return;

      priv->animation_timeout_id =
        g_timeout_add (delay,
                       (GSourceFunc) meta_cursor_renderer_native_update_animation,
                       native);
      g_source_set_name_by_id (priv->animation_timeout_id,
                               "[mutter] meta_cursor_renderer_native_update_animation");
    }
}

static GList *
calculate_cursor_sprite_gpus (MetaCursorRenderer *renderer,
                              MetaCursorSprite   *cursor_sprite)
{
  MetaCursorRendererNative *native = META_CURSOR_RENDERER_NATIVE (renderer);
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (native);
  MetaBackend *backend = priv->backend;
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GList *gpus = NULL;
  GList *logical_monitors;
  GList *l;
  graphene_rect_t cursor_rect;

  cursor_rect = meta_cursor_renderer_calculate_rect (renderer, cursor_sprite);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      MetaRectangle logical_monitor_layout;
      graphene_rect_t logical_monitor_rect;
      GList *monitors, *l_mon;

      logical_monitor_layout =
        meta_logical_monitor_get_layout (logical_monitor);
      logical_monitor_rect =
        meta_rectangle_to_graphene_rect (&logical_monitor_layout);

      if (!graphene_rect_intersection (&cursor_rect, &logical_monitor_rect,
                                       NULL))
        continue;

      monitors = meta_logical_monitor_get_monitors (logical_monitor);
      for (l_mon = monitors; l_mon; l_mon = l_mon->next)
        {
          MetaMonitor *monitor = l_mon->data;
          MetaGpu *gpu;

          gpu = meta_monitor_get_gpu (monitor);
          if (!g_list_find (gpus, gpu))
            gpus = g_list_prepend (gpus, gpu);
        }
    }

  return gpus;
}

static gboolean
meta_cursor_renderer_native_update_cursor (MetaCursorRenderer *renderer,
                                           MetaCursorSprite   *cursor_sprite)
{
  MetaCursorRendererNative *native = META_CURSOR_RENDERER_NATIVE (renderer);
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (native);
  g_autoptr (GList) gpus = NULL;

  if (cursor_sprite)
    {
      meta_cursor_sprite_realize_texture (cursor_sprite);
      gpus = calculate_cursor_sprite_gpus (renderer, cursor_sprite);
      realize_cursor_sprite (renderer, cursor_sprite, gpus);
    }

  maybe_schedule_cursor_sprite_animation_frame (native, cursor_sprite);

  priv->has_hw_cursor = should_have_hw_cursor (renderer, cursor_sprite, gpus);
  update_hw_cursor (native, cursor_sprite);

  return (priv->has_hw_cursor ||
          !cursor_sprite ||
          !meta_cursor_sprite_get_cogl_texture (cursor_sprite));
}

static void
unset_crtc_cursor_renderer_privates (MetaGpu       *gpu,
                                     struct gbm_bo *bo)
{
  GList *l;

  for (l = meta_gpu_get_crtcs (gpu); l; l = l->next)
    {
      MetaCrtc *crtc = l->data;

      if (bo == crtc->cursor_renderer_private)
        crtc->cursor_renderer_private = NULL;
    }
}

static void
cursor_gpu_state_free (MetaCursorNativeGpuState *cursor_gpu_state)
{
  int i;
  struct gbm_bo *active_bo;

  active_bo = get_active_cursor_sprite_gbm_bo (cursor_gpu_state);
  if (active_bo)
    unset_crtc_cursor_renderer_privates (cursor_gpu_state->gpu, active_bo);

  for (i = 0; i < HW_CURSOR_BUFFER_COUNT; i++)
    g_clear_pointer (&cursor_gpu_state->bos[i], gbm_bo_destroy);
  g_free (cursor_gpu_state);
}

static MetaCursorNativeGpuState *
get_cursor_gpu_state (MetaCursorNativePrivate *cursor_priv,
                      MetaGpuKms              *gpu_kms)
{
  return g_hash_table_lookup (cursor_priv->gpu_states, gpu_kms);
}

static MetaCursorNativeGpuState *
ensure_cursor_gpu_state (MetaCursorNativePrivate *cursor_priv,
                         MetaGpuKms              *gpu_kms)
{
  MetaCursorNativeGpuState *cursor_gpu_state;

  cursor_gpu_state = get_cursor_gpu_state (cursor_priv, gpu_kms);
  if (cursor_gpu_state)
    return cursor_gpu_state;

  cursor_gpu_state = g_new0 (MetaCursorNativeGpuState, 1);
  cursor_gpu_state->gpu = META_GPU (gpu_kms);
  g_hash_table_insert (cursor_priv->gpu_states, gpu_kms, cursor_gpu_state);

  return cursor_gpu_state;
}

static void
invalidate_cursor_gpu_state (MetaCursorSprite *cursor_sprite)
{
  MetaCursorNativePrivate *cursor_priv = get_cursor_priv (cursor_sprite);
  GHashTableIter iter;
  MetaCursorNativeGpuState *cursor_gpu_state;

  g_hash_table_iter_init (&iter, cursor_priv->gpu_states);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &cursor_gpu_state))
    {
      guint pending_bo;
      pending_bo = get_pending_cursor_sprite_gbm_bo_index (cursor_gpu_state);
      g_clear_pointer (&cursor_gpu_state->bos[pending_bo],
                       gbm_bo_destroy);
      cursor_gpu_state->pending_bo_state = META_CURSOR_GBM_BO_STATE_INVALIDATED;
    }
}

static void
on_cursor_sprite_texture_changed (MetaCursorSprite *cursor_sprite)
{
  invalidate_cursor_gpu_state (cursor_sprite);
}

static void
cursor_priv_free (MetaCursorNativePrivate *cursor_priv)
{
  g_hash_table_destroy (cursor_priv->gpu_states);
  g_free (cursor_priv);
}

static MetaCursorNativePrivate *
get_cursor_priv (MetaCursorSprite *cursor_sprite)
{
  return g_object_get_qdata (G_OBJECT (cursor_sprite), quark_cursor_sprite);
}

static MetaCursorNativePrivate *
ensure_cursor_priv (MetaCursorSprite *cursor_sprite)
{
  MetaCursorNativePrivate *cursor_priv;

  cursor_priv = get_cursor_priv (cursor_sprite);
  if (cursor_priv)
    return cursor_priv;

  cursor_priv = g_new0 (MetaCursorNativePrivate, 1);
  cursor_priv->gpu_states =
    g_hash_table_new_full (g_direct_hash,
                           g_direct_equal,
                           NULL,
                           (GDestroyNotify) cursor_gpu_state_free);
  g_object_set_qdata_full (G_OBJECT (cursor_sprite),
                           quark_cursor_sprite,
                           cursor_priv,
                           (GDestroyNotify) cursor_priv_free);

  g_signal_connect (cursor_sprite, "texture-changed",
                    G_CALLBACK (on_cursor_sprite_texture_changed), NULL);

  unset_can_preprocess (cursor_sprite);

  return cursor_priv;
}

static void
load_cursor_sprite_gbm_buffer_for_gpu (MetaCursorRendererNative *native,
                                       MetaGpuKms               *gpu_kms,
                                       MetaCursorSprite         *cursor_sprite,
                                       uint8_t                  *pixels,
                                       uint                      width,
                                       uint                      height,
                                       int                       rowstride,
                                       uint32_t                  gbm_format)
{
  uint64_t cursor_width, cursor_height;
  MetaCursorRendererNativeGpuData *cursor_renderer_gpu_data;
  struct gbm_device *gbm_device;

  cursor_renderer_gpu_data =
    meta_cursor_renderer_native_gpu_data_from_gpu (gpu_kms);
  if (!cursor_renderer_gpu_data)
    return;

  cursor_width = (uint64_t) cursor_renderer_gpu_data->cursor_width;
  cursor_height = (uint64_t) cursor_renderer_gpu_data->cursor_height;

  if (width > cursor_width || height > cursor_height)
    {
      meta_warning ("Invalid theme cursor size (must be at most %ux%u)\n",
                    (unsigned int)cursor_width, (unsigned int)cursor_height);
      return;
    }

  gbm_device = meta_gbm_device_from_gpu (gpu_kms);
  if (gbm_device_is_format_supported (gbm_device, gbm_format,
                                      GBM_BO_USE_CURSOR | GBM_BO_USE_WRITE))
    {
      struct gbm_bo *bo;
      uint8_t buf[4 * cursor_width * cursor_height];
      uint i;

      bo = gbm_bo_create (gbm_device, cursor_width, cursor_height,
                          gbm_format, GBM_BO_USE_CURSOR | GBM_BO_USE_WRITE);
      if (!bo)
        {
          meta_warning ("Failed to allocate HW cursor buffer\n");
          return;
        }

      memset (buf, 0, sizeof(buf));
      for (i = 0; i < height; i++)
        memcpy (buf + i * 4 * cursor_width, pixels + i * rowstride, width * 4);
      if (gbm_bo_write (bo, buf, cursor_width * cursor_height * 4) != 0)
        {
          meta_warning ("Failed to write cursors buffer data: %s",
                        g_strerror (errno));
          gbm_bo_destroy (bo);
          return;
        }

      set_pending_cursor_sprite_gbm_bo (cursor_sprite, gpu_kms, bo);
    }
  else
    {
      meta_warning ("HW cursor for format %d not supported\n", gbm_format);
    }
}

static gboolean
is_cursor_hw_state_valid (MetaCursorSprite *cursor_sprite,
                          MetaGpuKms       *gpu_kms)
{
  MetaCursorNativePrivate *cursor_priv;
  MetaCursorNativeGpuState *cursor_gpu_state;

  cursor_priv = get_cursor_priv (cursor_sprite);
  if (!cursor_priv)
    return FALSE;

  cursor_gpu_state = get_cursor_gpu_state (cursor_priv, gpu_kms);
  if (!cursor_gpu_state)
    return FALSE;

  switch (cursor_gpu_state->pending_bo_state)
    {
    case META_CURSOR_GBM_BO_STATE_SET:
    case META_CURSOR_GBM_BO_STATE_NONE:
      return TRUE;
    case META_CURSOR_GBM_BO_STATE_INVALIDATED:
      return FALSE;
    }

  g_assert_not_reached ();
  return FALSE;
}

static gboolean
is_cursor_scale_and_transform_valid (MetaCursorRenderer *renderer,
                                     MetaCursorSprite   *cursor_sprite)
{
  MetaMonitorTransform transform;
  float scale;

  if (!get_common_crtc_sprite_scale_for_logical_monitors (renderer,
                                                          cursor_sprite,
                                                          &scale))
    return FALSE;

  if (!get_common_crtc_sprite_transform_for_logical_monitors (renderer,
                                                              cursor_sprite,
                                                              &transform))
    return FALSE;

  return (scale == get_current_relative_scale (cursor_sprite) &&
          transform == get_current_relative_transform (cursor_sprite));
}

static cairo_surface_t *
scale_and_transform_cursor_sprite_cpu (uint8_t              *pixels,
                                       int                   width,
                                       int                   height,
                                       int                   rowstride,
                                       float                 scale,
                                       MetaMonitorTransform  transform)
{
  cairo_t *cr;
  cairo_surface_t *source_surface;
  cairo_surface_t *target_surface;
  int image_width;
  int image_height;

  image_width = ceilf (width * scale);
  image_height = ceilf (height * scale);

  target_surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                               image_width,
                                               image_height);

  cr = cairo_create (target_surface);
  if (transform != META_MONITOR_TRANSFORM_NORMAL)
    {
      cairo_translate (cr, 0.5 * image_width, 0.5 * image_height);
      switch (transform)
        {
        case META_MONITOR_TRANSFORM_90:
          cairo_rotate (cr, M_PI * 1.5);
          break;
        case META_MONITOR_TRANSFORM_180:
          cairo_rotate (cr, M_PI);
          break;
        case META_MONITOR_TRANSFORM_270:
          cairo_rotate (cr, M_PI * 0.5);
          break;
        case META_MONITOR_TRANSFORM_FLIPPED:
          cairo_scale (cr, 1, -1);
          break;
        case META_MONITOR_TRANSFORM_FLIPPED_90:
          cairo_rotate (cr, M_PI * 1.5);
          cairo_scale (cr, -1, 1);
          break;
        case META_MONITOR_TRANSFORM_FLIPPED_180:
          cairo_rotate (cr, M_PI);
          cairo_scale (cr, 1, -1);
          break;
        case META_MONITOR_TRANSFORM_FLIPPED_270:
          cairo_rotate (cr, M_PI * 0.5);
          cairo_scale (cr, -1, 1);
          break;
        case META_MONITOR_TRANSFORM_NORMAL:
          g_assert_not_reached ();
        }
      cairo_translate (cr, -0.5 * image_width, -0.5 * image_height);
    }
  cairo_scale (cr, scale, scale);

  source_surface = cairo_image_surface_create_for_data (pixels,
                                                        CAIRO_FORMAT_ARGB32,
                                                        width,
                                                        height,
                                                        rowstride);

  cairo_set_source_surface (cr, source_surface, 0, 0);
  cairo_paint (cr);
  cairo_destroy (cr);
  cairo_surface_destroy (source_surface);

  return target_surface;
}

static void
load_scaled_and_transformed_cursor_sprite (MetaCursorRendererNative *native,
                                           MetaGpuKms               *gpu_kms,
                                           MetaCursorSprite         *cursor_sprite,
                                           float                     relative_scale,
                                           MetaMonitorTransform      relative_transform,
                                           uint8_t                  *data,
                                           int                       width,
                                           int                       height,
                                           int                       rowstride,
                                           uint32_t                  gbm_format)
{
  if (!G_APPROX_VALUE (relative_scale, 1.f, FLT_EPSILON) ||
      relative_transform != META_MONITOR_TRANSFORM_NORMAL)
    {
      cairo_surface_t *surface;

      surface = scale_and_transform_cursor_sprite_cpu (data,
                                                       width,
                                                       height,
                                                       rowstride,
                                                       relative_scale,
                                                       relative_transform);

      load_cursor_sprite_gbm_buffer_for_gpu (native,
                                             gpu_kms,
                                             cursor_sprite,
                                             cairo_image_surface_get_data (surface),
                                             cairo_image_surface_get_width (surface),
                                             cairo_image_surface_get_width (surface),
                                             cairo_image_surface_get_stride (surface),
                                             gbm_format);

      cairo_surface_destroy (surface);
    }
  else
    {
      load_cursor_sprite_gbm_buffer_for_gpu (native,
                                             gpu_kms,
                                             cursor_sprite,
                                             data,
                                             width,
                                             height,
                                             rowstride,
                                             gbm_format);
    }
}

#ifdef HAVE_WAYLAND
static void
realize_cursor_sprite_from_wl_buffer_for_gpu (MetaCursorRenderer      *renderer,
                                              MetaGpuKms              *gpu_kms,
                                              MetaCursorSpriteWayland *sprite_wayland)
{
  MetaCursorRendererNative *native = META_CURSOR_RENDERER_NATIVE (renderer);
  MetaCursorSprite *cursor_sprite = META_CURSOR_SPRITE (sprite_wayland);
  MetaCursorRendererNativeGpuData *cursor_renderer_gpu_data;
  uint64_t cursor_width, cursor_height;
  CoglTexture *texture;
  uint width, height;
  MetaWaylandBuffer *buffer;
  struct wl_resource *buffer_resource;
  struct wl_shm_buffer *shm_buffer;

  cursor_renderer_gpu_data =
    meta_cursor_renderer_native_gpu_data_from_gpu (gpu_kms);
  if (!cursor_renderer_gpu_data || cursor_renderer_gpu_data->hw_cursor_broken)
    return;

  if (is_cursor_hw_state_valid (cursor_sprite, gpu_kms) &&
      is_cursor_scale_and_transform_valid (renderer, cursor_sprite))
    return;

  buffer = meta_cursor_sprite_wayland_get_buffer (sprite_wayland);
  if (!buffer)
    return;

  buffer_resource = meta_wayland_buffer_get_resource (buffer);
  if (!buffer_resource)
    return;

  ensure_cursor_priv (cursor_sprite);

  shm_buffer = wl_shm_buffer_get (buffer_resource);
  if (shm_buffer)
    {
      int rowstride = wl_shm_buffer_get_stride (shm_buffer);
      uint8_t *buffer_data;
      float relative_scale;
      MetaMonitorTransform relative_transform;
      uint32_t gbm_format;

      if (!get_common_crtc_sprite_scale_for_logical_monitors (renderer,
                                                              cursor_sprite,
                                                              &relative_scale))
        {
          unset_can_preprocess (cursor_sprite);
          return;
        }

      if (!get_common_crtc_sprite_transform_for_logical_monitors (renderer,
                                                                  cursor_sprite,
                                                                  &relative_transform))
        {
          unset_can_preprocess (cursor_sprite);
          return;
        }

      set_can_preprocess (cursor_sprite,
                          relative_scale,
                          relative_transform);

      wl_shm_buffer_begin_access (shm_buffer);
      buffer_data = wl_shm_buffer_get_data (shm_buffer);

      width = wl_shm_buffer_get_width (shm_buffer);
      height = wl_shm_buffer_get_height (shm_buffer);

      switch (wl_shm_buffer_get_format (shm_buffer))
        {
        case WL_SHM_FORMAT_ARGB8888:
          gbm_format = GBM_FORMAT_ARGB8888;
          break;
        case WL_SHM_FORMAT_XRGB8888:
          gbm_format = GBM_FORMAT_XRGB8888;
          break;
        default:
          g_warn_if_reached ();
          gbm_format = GBM_FORMAT_ARGB8888;
        }

      load_scaled_and_transformed_cursor_sprite (native,
                                                 gpu_kms,
                                                 cursor_sprite,
                                                 relative_scale,
                                                 relative_transform,
                                                 buffer_data,
                                                 width,
                                                 height,
                                                 rowstride,
                                                 gbm_format);

      wl_shm_buffer_end_access (shm_buffer);
    }
  else
    {
      struct gbm_device *gbm_device;
      struct gbm_bo *bo;

      /* HW cursors have a predefined size (at least 64x64), which usually is
       * bigger than cursor theme size, so themed cursors must be padded with
       * transparent pixels to fill the overlay. This is trivial if we have CPU
       * access to the data, but it's not possible if the buffer is in GPU
       * memory (and possibly tiled too), so if we don't get the right size, we
       * fallback to GL. */
      cursor_width = (uint64_t) cursor_renderer_gpu_data->cursor_width;
      cursor_height = (uint64_t) cursor_renderer_gpu_data->cursor_height;

      texture = meta_cursor_sprite_get_cogl_texture (cursor_sprite);
      width = cogl_texture_get_width (texture);
      height = cogl_texture_get_height (texture);

      if (width != cursor_width || height != cursor_height)
        {
          meta_warning ("Invalid cursor size (must be 64x64), falling back to software (GL) cursors\n");
          return;
        }

      gbm_device = meta_gbm_device_from_gpu (gpu_kms);
      bo = gbm_bo_import (gbm_device,
                          GBM_BO_IMPORT_WL_BUFFER,
                          buffer,
                          GBM_BO_USE_CURSOR);
      if (!bo)
        {
          meta_warning ("Importing HW cursor from wl_buffer failed\n");
          return;
        }

      unset_can_preprocess (cursor_sprite);

      set_pending_cursor_sprite_gbm_bo (cursor_sprite, gpu_kms, bo);
    }
}
#endif

static void
realize_cursor_sprite_from_xcursor_for_gpu (MetaCursorRenderer      *renderer,
                                            MetaGpuKms              *gpu_kms,
                                            MetaCursorSpriteXcursor *sprite_xcursor)
{
  MetaCursorRendererNative *native = META_CURSOR_RENDERER_NATIVE (renderer);
  MetaCursorRendererNativeGpuData *cursor_renderer_gpu_data;
  MetaCursorSprite *cursor_sprite = META_CURSOR_SPRITE (sprite_xcursor);
  XcursorImage *xc_image;
  float relative_scale;
  MetaMonitorTransform relative_transform;

  ensure_cursor_priv (cursor_sprite);

  cursor_renderer_gpu_data =
    meta_cursor_renderer_native_gpu_data_from_gpu (gpu_kms);
  if (!cursor_renderer_gpu_data || cursor_renderer_gpu_data->hw_cursor_broken)
    return;

  if (is_cursor_hw_state_valid (cursor_sprite, gpu_kms) &&
      is_cursor_scale_and_transform_valid (renderer, cursor_sprite))
    return;

  if (!get_common_crtc_sprite_scale_for_logical_monitors (renderer,
                                                          cursor_sprite,
                                                          &relative_scale))
    {
      unset_can_preprocess (cursor_sprite);
      return;
    }

  if (!get_common_crtc_sprite_transform_for_logical_monitors (renderer,
                                                              cursor_sprite,
                                                              &relative_transform))
    {
      unset_can_preprocess (cursor_sprite);
      return;
    }

  set_can_preprocess (cursor_sprite,
                      relative_scale,
                      relative_transform);

  xc_image = meta_cursor_sprite_xcursor_get_current_image (sprite_xcursor);

  load_scaled_and_transformed_cursor_sprite (native,
                                             gpu_kms,
                                             cursor_sprite,
                                             relative_scale,
                                             relative_transform,
                                             (uint8_t *) xc_image->pixels,
                                             xc_image->width,
                                             xc_image->height,
                                             xc_image->width * 4,
                                             GBM_FORMAT_ARGB8888);
}

static void
realize_cursor_sprite_for_gpu (MetaCursorRenderer *renderer,
                               MetaGpuKms         *gpu_kms,
                               MetaCursorSprite   *cursor_sprite)
{
  if (META_IS_CURSOR_SPRITE_XCURSOR (cursor_sprite))
    {
      MetaCursorSpriteXcursor *sprite_xcursor =
        META_CURSOR_SPRITE_XCURSOR (cursor_sprite);

      realize_cursor_sprite_from_xcursor_for_gpu (renderer,
                                                  gpu_kms,
                                                  sprite_xcursor);
    }
#ifdef HAVE_WAYLAND
  else if (META_IS_CURSOR_SPRITE_WAYLAND (cursor_sprite))
    {
      MetaCursorSpriteWayland *sprite_wayland =
        META_CURSOR_SPRITE_WAYLAND (cursor_sprite);

      realize_cursor_sprite_from_wl_buffer_for_gpu (renderer,
                                                    gpu_kms,
                                                    sprite_wayland);
    }
#endif
}

static void
realize_cursor_sprite (MetaCursorRenderer *renderer,
                       MetaCursorSprite   *cursor_sprite,
                       GList              *gpus)
{
  GList *l;

  for (l = gpus; l; l = l->next)
    {
      MetaGpuKms *gpu_kms = l->data;

      realize_cursor_sprite_for_gpu (renderer, gpu_kms, cursor_sprite);
    }
}

static void
meta_cursor_renderer_native_class_init (MetaCursorRendererNativeClass *klass)
{
  MetaCursorRendererClass *renderer_class = META_CURSOR_RENDERER_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_cursor_renderer_native_finalize;
  renderer_class->update_cursor = meta_cursor_renderer_native_update_cursor;

  quark_cursor_sprite = g_quark_from_static_string ("-meta-cursor-native");
  quark_cursor_renderer_native_gpu_data =
    g_quark_from_static_string ("-meta-cursor-renderer-native-gpu-data");
}

static void
force_update_hw_cursor (MetaCursorRendererNative *native)
{
  MetaCursorRenderer *renderer = META_CURSOR_RENDERER (native);
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (native);

  priv->hw_state_invalidated = TRUE;
  meta_cursor_renderer_force_update (renderer);
}

static void
on_monitors_changed (MetaMonitorManager       *monitors,
                     MetaCursorRendererNative *native)
{
  force_update_hw_cursor (native);
}

static void
init_hw_cursor_support_for_gpu (MetaGpuKms *gpu_kms)
{
  MetaKmsDevice *kms_device = meta_gpu_kms_get_kms_device (gpu_kms);
  MetaCursorRendererNativeGpuData *cursor_renderer_gpu_data;
  struct gbm_device *gbm_device;
  uint64_t width, height;

  gbm_device = meta_gbm_device_from_gpu (gpu_kms);
  if (!gbm_device)
    return;

  cursor_renderer_gpu_data =
    meta_create_cursor_renderer_native_gpu_data (gpu_kms);

  if (!meta_kms_device_get_cursor_size (kms_device, &width, &height))
    {
      width = 64;
      height = 64;
    }

  cursor_renderer_gpu_data->cursor_width = width;
  cursor_renderer_gpu_data->cursor_height = height;
}

static void
on_gpu_added_for_cursor (MetaBackend *backend,
                         MetaGpuKms  *gpu_kms)
{
  init_hw_cursor_support_for_gpu (gpu_kms);
}

static void
init_hw_cursor_support (MetaCursorRendererNative *cursor_renderer_native)
{
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (cursor_renderer_native);
  GList *gpus;
  GList *l;

  gpus = meta_backend_get_gpus (priv->backend);
  for (l = gpus; l; l = l->next)
    {
      MetaGpuKms *gpu_kms = l->data;

      init_hw_cursor_support_for_gpu (gpu_kms);
    }
}

MetaCursorRendererNative *
meta_cursor_renderer_native_new (MetaBackend *backend)
{
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaCursorRendererNative *cursor_renderer_native;
  MetaCursorRendererNativePrivate *priv;

  cursor_renderer_native =
    g_object_new (META_TYPE_CURSOR_RENDERER_NATIVE, NULL);
  priv =
    meta_cursor_renderer_native_get_instance_private (cursor_renderer_native);

  g_signal_connect_object (monitor_manager, "monitors-changed-internal",
                           G_CALLBACK (on_monitors_changed),
                           cursor_renderer_native, 0);
  g_signal_connect (backend, "gpu-added",
                    G_CALLBACK (on_gpu_added_for_cursor), NULL);

  priv->backend = backend;
  priv->hw_state_invalidated = TRUE;

  init_hw_cursor_support (cursor_renderer_native);

  return cursor_renderer_native;
}

static void
meta_cursor_renderer_native_init (MetaCursorRendererNative *native)
{
}
