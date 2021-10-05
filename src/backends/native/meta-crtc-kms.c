/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2013-2017 Red Hat
 * Copyright (C) 2018 DisplayLink (UK) Ltd.
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
 */

#include "config.h"

#include "backends/native/meta-crtc-kms.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/native/meta-gpu-kms.h"
#include "backends/native/meta-output-kms.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-plane.h"
#include "backends/native/meta-kms-update.h"

#define ALL_TRANSFORMS_MASK ((1 << META_MONITOR_N_TRANSFORMS) - 1)

typedef struct _MetaCrtcKms
{
  MetaKmsCrtc *kms_crtc;

  MetaKmsPlane *primary_plane;
} MetaCrtcKms;

static GQuark kms_crtc_crtc_kms_quark;

gboolean
meta_crtc_kms_is_transform_handled (MetaCrtc             *crtc,
                                    MetaMonitorTransform  transform)
{
  MetaCrtcKms *crtc_kms = crtc->driver_private;

  if (!crtc_kms->primary_plane)
    return FALSE;

  return meta_kms_plane_is_transform_handled (crtc_kms->primary_plane,
                                              transform);
}

void
meta_crtc_kms_apply_transform (MetaCrtc               *crtc,
                               MetaKmsPlaneAssignment *kms_plane_assignment)
{
  MetaCrtcKms *crtc_kms = crtc->driver_private;
  MetaMonitorTransform hw_transform;

  hw_transform = crtc->config->transform;
  if (!meta_crtc_kms_is_transform_handled (crtc, hw_transform))
    hw_transform = META_MONITOR_TRANSFORM_NORMAL;
  if (!meta_crtc_kms_is_transform_handled (crtc, hw_transform))
    return;

  meta_kms_plane_update_set_rotation (crtc_kms->primary_plane,
                                      kms_plane_assignment,
                                      hw_transform);
}

void
meta_crtc_kms_assign_primary_plane (MetaCrtc      *crtc,
                                    uint32_t       fb_id,
                                    MetaKmsUpdate *kms_update)
{
  MetaCrtcConfig *crtc_config;
  MetaFixed16Rectangle src_rect;
  MetaFixed16Rectangle dst_rect;
  MetaKmsAssignPlaneFlag flags;
  MetaKmsCrtc *kms_crtc;
  MetaKmsDevice *kms_device;
  MetaKmsPlane *primary_kms_plane;
  MetaKmsPlaneAssignment *plane_assignment;

  crtc_config = crtc->config;


  src_rect = (MetaFixed16Rectangle) {
    .x = meta_fixed_16_from_int (0),
    .y = meta_fixed_16_from_int (0),
    .width = meta_fixed_16_from_int (crtc_config->mode->width),
    .height = meta_fixed_16_from_int (crtc_config->mode->height),
  };
  dst_rect = (MetaFixed16Rectangle) {
    .x = meta_fixed_16_from_int (0),
    .y = meta_fixed_16_from_int (0),
    .width = meta_fixed_16_from_int (crtc_config->mode->width),
    .height = meta_fixed_16_from_int (crtc_config->mode->height),
  };

  flags = META_KMS_ASSIGN_PLANE_FLAG_NONE;

  kms_crtc = meta_crtc_kms_get_kms_crtc (crtc);
  kms_device = meta_kms_crtc_get_device (kms_crtc);
  primary_kms_plane = meta_kms_device_get_primary_plane_for (kms_device,
                                                             kms_crtc);
  plane_assignment = meta_kms_update_assign_plane (kms_update,
                                                   kms_crtc,
                                                   primary_kms_plane,
                                                   fb_id,
                                                   src_rect,
                                                   dst_rect,
                                                   flags);
  meta_crtc_kms_apply_transform (crtc, plane_assignment);
}

static GList *
generate_crtc_connector_list (MetaGpu  *gpu,
                              MetaCrtc *crtc)
{
  GList *connectors = NULL;
  GList *l;

  for (l = meta_gpu_get_outputs (gpu); l; l = l->next)
    {
      MetaOutput *output = l->data;
      MetaCrtc *assigned_crtc;

      assigned_crtc = meta_output_get_assigned_crtc (output);
      if (assigned_crtc == crtc)
        {
          MetaKmsConnector *kms_connector =
            meta_output_kms_get_kms_connector (output);

          connectors = g_list_prepend (connectors, kms_connector);
        }
    }

  return connectors;
}

void
meta_crtc_kms_set_mode (MetaCrtc      *crtc,
                        MetaKmsUpdate *kms_update)
{
  MetaCrtcConfig *crtc_config = crtc->config;
  MetaGpu *gpu = meta_crtc_get_gpu (crtc);
  GList *connectors;
  drmModeModeInfo *mode;

  connectors = generate_crtc_connector_list (gpu, crtc);

  if (connectors)
    {
      mode = crtc_config->mode->driver_private;

      g_debug ("Setting CRTC (%ld) mode to %s", crtc->crtc_id, mode->name);
    }
  else
    {
      mode = NULL;

      g_debug ("Unsetting CRTC (%ld) mode", crtc->crtc_id);
    }

  meta_kms_update_mode_set (kms_update,
                            meta_crtc_kms_get_kms_crtc (crtc),
                            g_steal_pointer (&connectors),
                            mode);
}

void
meta_crtc_kms_page_flip (MetaCrtc                      *crtc,
                         const MetaKmsPageFlipFeedback *page_flip_feedback,
                         gpointer                       user_data,
                         MetaKmsUpdate                 *kms_update)
{
  meta_kms_update_page_flip (kms_update,
                             meta_crtc_kms_get_kms_crtc (crtc),
                             page_flip_feedback,
                             user_data);
}

MetaKmsCrtc *
meta_crtc_kms_get_kms_crtc (MetaCrtc *crtc)
{
  MetaCrtcKms *crtc_kms = crtc->driver_private;

  return crtc_kms->kms_crtc;
}

/**
 * meta_crtc_kms_get_modifiers:
 * @crtc: a #MetaCrtc object that has to be a #MetaCrtcKms
 * @format: a DRM pixel format
 *
 * Returns a pointer to a #GArray containing all the supported
 * modifiers for the given DRM pixel format on the CRTC's primary
 * plane. The array element type is uint64_t.
 *
 * The caller must not modify or destroy the array or its contents.
 *
 * Returns NULL if the modifiers are not known or the format is not
 * supported.
 */
GArray *
meta_crtc_kms_get_modifiers (MetaCrtc *crtc,
                             uint32_t  format)
{
  MetaCrtcKms *crtc_kms = crtc->driver_private;

  return meta_kms_plane_get_modifiers_for_format (crtc_kms->primary_plane,
                                                  format);
}

/**
 * meta_crtc_kms_copy_drm_format_list:
 * @crtc: a #MetaCrtc object that has to be a #MetaCrtcKms
 *
 * Returns a new #GArray that the caller must destroy. The array
 * contains all the DRM pixel formats the CRTC supports on
 * its primary plane. The array element type is uint32_t.
 */
GArray *
meta_crtc_kms_copy_drm_format_list (MetaCrtc *crtc)
{
  MetaCrtcKms *crtc_kms = crtc->driver_private;

  return meta_kms_plane_copy_drm_format_list (crtc_kms->primary_plane);
}

/**
 * meta_crtc_kms_supports_format:
 * @crtc: a #MetaCrtc object that has to be a #MetaCrtcKms
 * @drm_format: a DRM pixel format
 *
 * Returns true if the CRTC supports the format on its primary plane.
 */
gboolean
meta_crtc_kms_supports_format (MetaCrtc *crtc,
                               uint32_t  drm_format)
{
  MetaCrtcKms *crtc_kms = crtc->driver_private;

  return meta_kms_plane_is_format_supported (crtc_kms->primary_plane,
                                             drm_format);
}

MetaCrtc *
meta_crtc_kms_from_kms_crtc (MetaKmsCrtc *kms_crtc)
{
  return g_object_get_qdata (G_OBJECT (kms_crtc), kms_crtc_crtc_kms_quark);
}

static void
meta_crtc_destroy_notify (MetaCrtc *crtc)
{
  g_free (crtc->driver_private);
}

MetaCrtc *
meta_create_kms_crtc (MetaGpuKms  *gpu_kms,
                      MetaKmsCrtc *kms_crtc)
{
  MetaGpu *gpu = META_GPU (gpu_kms);
  MetaKmsDevice *kms_device;
  MetaCrtc *crtc;
  MetaCrtcKms *crtc_kms;
  MetaKmsPlane *primary_plane;

  kms_device = meta_gpu_kms_get_kms_device (gpu_kms);
  primary_plane = meta_kms_device_get_primary_plane_for (kms_device,
                                                         kms_crtc);
  crtc = g_object_new (META_TYPE_CRTC, NULL);
  crtc->gpu = gpu;
  crtc->crtc_id = meta_kms_crtc_get_id (kms_crtc);
  crtc->is_dirty = FALSE;
  crtc->all_transforms = ALL_TRANSFORMS_MASK;

  crtc_kms = g_new0 (MetaCrtcKms, 1);
  crtc_kms->kms_crtc = kms_crtc;
  crtc_kms->primary_plane = primary_plane;

  crtc->driver_private = crtc_kms;
  crtc->driver_notify = (GDestroyNotify) meta_crtc_destroy_notify;

  if (!kms_crtc_crtc_kms_quark)
    {
      kms_crtc_crtc_kms_quark =
        g_quark_from_static_string ("meta-kms-crtc-crtc-kms-quark");
    }

  g_object_set_qdata (G_OBJECT (kms_crtc), kms_crtc_crtc_kms_quark, crtc);

  return crtc;
}
