/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2017 Red Hat
 * Copyright (c) 2018 DisplayLink (UK) Ltd.
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

#include "backends/native/meta-gpu-kms.h"

#include <drm.h>
#include <drm_fourcc.h>
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "backends/meta-crtc.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-output.h"
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-crtc-kms.h"
#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-update.h"
#include "backends/native/meta-kms-utils.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-launcher.h"
#include "backends/native/meta-output-kms.h"

#include "meta-default-modes.h"

struct _MetaGpuKms
{
  MetaGpu parent;

  MetaKmsDevice *kms_device;

  uint32_t id;
  int fd;

  clockid_t clock_id;

  gboolean resources_init_failed_before;
};

G_DEFINE_TYPE (MetaGpuKms, meta_gpu_kms, META_TYPE_GPU)

gboolean
meta_gpu_kms_add_fb (MetaGpuKms              *gpu_kms,
                     gboolean                 use_modifiers,
                     const MetaGpuKmsFBArgs  *args,
                     uint32_t                *fb_id_out,
                     GError                 **error)
{
  MetaDrmFormatBuf tmp;
  uint32_t fb_id;

  if (use_modifiers && args->modifiers[0] != DRM_FORMAT_MOD_INVALID)
    {
      if (drmModeAddFB2WithModifiers (gpu_kms->fd,
                                      args->width,
                                      args->height,
                                      args->format,
                                      args->handles,
                                      args->strides,
                                      args->offsets,
                                      args->modifiers,
                                      &fb_id,
                                      DRM_MODE_FB_MODIFIERS))
        {
          g_set_error (error,
                       G_IO_ERROR,
                       g_io_error_from_errno (errno),
                       "drmModeAddFB2WithModifiers failed: %s",
                       g_strerror (errno));
          return FALSE;
        }
    }
  else if (drmModeAddFB2 (gpu_kms->fd,
                          args->width,
                          args->height,
                          args->format,
                          args->handles,
                          args->strides,
                          args->offsets,
                          &fb_id,
                          0))
    {
      if (args->format != DRM_FORMAT_XRGB8888)
        {
          g_set_error (error,
                       G_IO_ERROR,
                       G_IO_ERROR_FAILED,
                       "drmModeAddFB does not support format '%s' (0x%x)",
                       meta_drm_format_to_string (&tmp, args->format),
                       args->format);
          return FALSE;
        }

      if (drmModeAddFB (gpu_kms->fd,
                        args->width,
                        args->height,
                        24,
                        32,
                        args->strides[0],
                        args->handles[0],
                        &fb_id))
        {
          g_set_error (error,
                       G_IO_ERROR,
                       g_io_error_from_errno (errno),
                       "drmModeAddFB failed: %s",
                       g_strerror (errno));
          return FALSE;
        }
    }

    *fb_id_out = fb_id;
    return TRUE;
}

gboolean
meta_gpu_kms_is_crtc_active (MetaGpuKms *gpu_kms,
                             MetaCrtc   *crtc)
{
  MetaGpu *gpu = META_GPU (gpu_kms);
  MetaBackend *backend = meta_gpu_get_backend (gpu);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GList *l;
  gboolean connected_crtc_found;

  g_assert (meta_crtc_get_gpu (crtc) == META_GPU (gpu_kms));

  if (meta_monitor_manager_get_power_save_mode (monitor_manager))
    return FALSE;

  connected_crtc_found = FALSE;
  for (l = meta_gpu_get_outputs (gpu); l; l = l->next)
    {
      MetaOutput *output = l->data;
      MetaCrtc *assigned_crtc;

      assigned_crtc = meta_output_get_assigned_crtc (output);
      if (assigned_crtc == crtc)
        {
          connected_crtc_found = TRUE;
          break;
        }
    }

  if (!connected_crtc_found)
    return FALSE;

  return TRUE;
}

static int64_t
timespec_to_nanoseconds (const struct timespec *ts)
{
  const int64_t one_billion = 1000000000;

  return ((int64_t) ts->tv_sec) * one_billion + ts->tv_nsec;
}

gboolean
meta_gpu_kms_wait_for_flip (MetaGpuKms *gpu_kms,
                            GError    **error)
{
  if (meta_kms_device_dispatch_sync (gpu_kms->kms_device, error) < 0)
    return FALSE;

  return TRUE;
}

MetaKmsDevice *
meta_gpu_kms_get_kms_device (MetaGpuKms *gpu_kms)
{
  return gpu_kms->kms_device;
}

int
meta_gpu_kms_get_fd (MetaGpuKms *gpu_kms)
{
  return gpu_kms->fd;
}

uint32_t
meta_gpu_kms_get_id (MetaGpuKms *gpu_kms)
{
  return gpu_kms->id;
}

const char *
meta_gpu_kms_get_file_path (MetaGpuKms *gpu_kms)
{
  return meta_kms_device_get_path (gpu_kms->kms_device);
}

int64_t
meta_gpu_kms_get_current_time_ns (MetaGpuKms *gpu_kms)
{
  struct timespec ts;

  if (clock_gettime (gpu_kms->clock_id, &ts))
    return 0;

  return timespec_to_nanoseconds (&ts);
}

void
meta_gpu_kms_set_power_save_mode (MetaGpuKms    *gpu_kms,
                                  uint64_t       state,
                                  MetaKmsUpdate *kms_update)
{
  MetaGpu *gpu = META_GPU (gpu_kms);
  GList *l;

  for (l = meta_gpu_get_outputs (gpu); l; l = l->next)
    {
      MetaOutput *output = l->data;

      meta_output_kms_set_power_save_mode (output, state, kms_update);
    }

  if (state != META_POWER_SAVE_ON)
    {
      /* Turn off CRTCs for DPMS */
      for (l = meta_gpu_get_crtcs (gpu); l; l = l->next)
        {
          MetaCrtc *crtc = META_CRTC (l->data);

          meta_kms_update_mode_set (kms_update,
                                    meta_crtc_kms_get_kms_crtc (crtc),
                                    NULL, NULL);
        }
    }
}

gboolean
meta_gpu_kms_is_boot_vga (MetaGpuKms *gpu_kms)
{
  MetaKmsDeviceFlag flags;

  flags = meta_kms_device_get_flags (gpu_kms->kms_device);
  return !!(flags & META_KMS_DEVICE_FLAG_BOOT_VGA);
}

gboolean
meta_gpu_kms_is_platform_device (MetaGpuKms *gpu_kms)
{
  MetaKmsDeviceFlag flags;

  flags = meta_kms_device_get_flags (gpu_kms->kms_device);
  return !!(flags & META_KMS_DEVICE_FLAG_PLATFORM_DEVICE);
}

static int
compare_outputs (gconstpointer one,
                 gconstpointer two)
{
  const MetaOutput *o_one = one, *o_two = two;

  return strcmp (o_one->name, o_two->name);
}

static void
meta_crtc_mode_destroy_notify (MetaCrtcMode *mode)
{
  g_slice_free (drmModeModeInfo, mode->driver_private);
}

gboolean
meta_drm_mode_equal (const drmModeModeInfo *one,
                     const drmModeModeInfo *two)
{
  return (one->clock == two->clock &&
          one->hdisplay == two->hdisplay &&
          one->hsync_start == two->hsync_start &&
          one->hsync_end == two->hsync_end &&
          one->htotal == two->htotal &&
          one->hskew == two->hskew &&
          one->vdisplay == two->vdisplay &&
          one->vsync_start == two->vsync_start &&
          one->vsync_end == two->vsync_end &&
          one->vtotal == two->vtotal &&
          one->vscan == two->vscan &&
          one->vrefresh == two->vrefresh &&
          one->flags == two->flags &&
          one->type == two->type &&
          strncmp (one->name, two->name, DRM_DISPLAY_MODE_LEN) == 0);
}

static guint
drm_mode_hash (gconstpointer ptr)
{
  const drmModeModeInfo *mode = ptr;
  guint hash = 0;

  /*
   * We don't include the name in the hash because it's generally
   * derived from the other fields (hdisplay, vdisplay and flags)
   */

  hash ^= mode->clock;
  hash ^= mode->hdisplay ^ mode->hsync_start ^ mode->hsync_end;
  hash ^= mode->vdisplay ^ mode->vsync_start ^ mode->vsync_end;
  hash ^= mode->vrefresh;
  hash ^= mode->flags ^ mode->type;

  return hash;
}

MetaCrtcMode *
meta_gpu_kms_get_mode_from_drm_mode (MetaGpuKms            *gpu_kms,
                                     const drmModeModeInfo *drm_mode)
{
  MetaGpu *gpu = META_GPU (gpu_kms);
  GList *l;

  for (l = meta_gpu_get_modes (gpu); l; l = l->next)
    {
      MetaCrtcMode *mode = l->data;

      if (meta_drm_mode_equal (drm_mode, mode->driver_private))
        return mode;
    }

  g_assert_not_reached ();
  return NULL;
}

static MetaCrtcMode *
create_mode (const drmModeModeInfo *drm_mode,
             long                   mode_id)
{
  MetaCrtcMode *mode;

  mode = g_object_new (META_TYPE_CRTC_MODE, NULL);
  mode->mode_id = mode_id;
  mode->name = g_strndup (drm_mode->name, DRM_DISPLAY_MODE_LEN);
  mode->width = drm_mode->hdisplay;
  mode->height = drm_mode->vdisplay;
  mode->flags = drm_mode->flags;
  mode->refresh_rate = meta_calculate_drm_mode_refresh_rate (drm_mode);
  mode->driver_private = g_slice_dup (drmModeModeInfo, drm_mode);
  mode->driver_notify = (GDestroyNotify) meta_crtc_mode_destroy_notify;

  return mode;
}

static MetaOutput *
find_output_by_connector_id (GList    *outputs,
                             uint32_t  connector_id)
{
  GList *l;

  for (l = outputs; l; l = l->next)
    {
      MetaOutput *output = l->data;

      if (meta_output_kms_get_connector_id (output) == connector_id)
        return output;
    }

  return NULL;
}

static void
setup_output_clones (MetaGpu *gpu)
{
  GList *l;

  for (l = meta_gpu_get_outputs (gpu); l; l = l->next)
    {
      MetaOutput *output = l->data;
      GList *k;

      for (k = meta_gpu_get_outputs (gpu); k; k = k->next)
        {
          MetaOutput *other_output = k->data;

          if (other_output == output)
            continue;

          if (meta_output_kms_can_clone (output, other_output))
            {
              output->n_possible_clones++;
              output->possible_clones = g_renew (MetaOutput *,
                                                 output->possible_clones,
                                                 output->n_possible_clones);
              output->possible_clones[output->n_possible_clones - 1] =
                other_output;
            }
        }
    }
}

static void
init_modes (MetaGpuKms *gpu_kms)
{
  MetaGpu *gpu = META_GPU (gpu_kms);
  GHashTable *modes_table;
  GList *l;
  GList *modes;
  GHashTableIter iter;
  drmModeModeInfo *drm_mode;
  int i;
  long mode_id;

  /*
   * Gather all modes on all connected connectors.
   */
  modes_table = g_hash_table_new (drm_mode_hash, (GEqualFunc) meta_drm_mode_equal);
  for (l = meta_kms_device_get_connectors (gpu_kms->kms_device); l; l = l->next)
    {
      MetaKmsConnector *kms_connector = l->data;
      const MetaKmsConnectorState *state;

      state = meta_kms_connector_get_current_state (kms_connector);
      if (!state)
        continue;

      for (i = 0; i < state->n_modes; i++)
        g_hash_table_add (modes_table, &state->modes[i]);
    }

  modes = NULL;

  g_hash_table_iter_init (&iter, modes_table);
  mode_id = 0;
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &drm_mode))
    {
      MetaCrtcMode *mode;

      mode = create_mode (drm_mode, (long) mode_id);
      modes = g_list_append (modes, mode);

      mode_id++;
    }

  g_hash_table_destroy (modes_table);

  for (i = 0; i < G_N_ELEMENTS (meta_default_landscape_drm_mode_infos); i++)
    {
      MetaCrtcMode *mode;

      mode = create_mode (&meta_default_landscape_drm_mode_infos[i], mode_id);
      modes = g_list_append (modes, mode);

      mode_id++;
    }

  for (i = 0; i < G_N_ELEMENTS (meta_default_portrait_drm_mode_infos); i++)
    {
      MetaCrtcMode *mode;

      mode = create_mode (&meta_default_portrait_drm_mode_infos[i], mode_id);
      modes = g_list_append (modes, mode);

      mode_id++;
    }

  meta_gpu_take_modes (gpu, modes);
}

static void
init_crtcs (MetaGpuKms *gpu_kms)
{
  MetaGpu *gpu = META_GPU (gpu_kms);
  MetaKmsDevice *kms_device = gpu_kms->kms_device;
  GList *l;
  GList *crtcs;

  crtcs = NULL;

  for (l = meta_kms_device_get_crtcs (kms_device); l; l = l->next)
    {
      MetaKmsCrtc *kms_crtc = l->data;
      MetaCrtc *crtc;

      crtc = meta_create_kms_crtc (gpu_kms, kms_crtc);

      crtcs = g_list_append (crtcs, crtc);
    }

  meta_gpu_take_crtcs (gpu, crtcs);
}

static void
init_frame_clock (MetaGpuKms *gpu_kms)
{
  uint64_t uses_monotonic;

  if (drmGetCap (gpu_kms->fd, DRM_CAP_TIMESTAMP_MONOTONIC, &uses_monotonic) != 0)
    uses_monotonic = 0;

  gpu_kms->clock_id = uses_monotonic ? CLOCK_MONOTONIC : CLOCK_REALTIME;
}

static void
init_outputs (MetaGpuKms *gpu_kms)
{
  MetaGpu *gpu = META_GPU (gpu_kms);
  GList *old_outputs;
  GList *outputs;
  GList *l;

  old_outputs = meta_gpu_get_outputs (gpu);

  outputs = NULL;

  for (l = meta_kms_device_get_connectors (gpu_kms->kms_device); l; l = l->next)
    {
      MetaKmsConnector *kms_connector = l->data;
      const MetaKmsConnectorState *connector_state;
      MetaOutput *output;
      MetaOutput *old_output;
      GError *error = NULL;

      connector_state = meta_kms_connector_get_current_state (kms_connector);
      if (!connector_state)
        continue;

      old_output =
        find_output_by_connector_id (old_outputs,
                                     meta_kms_connector_get_id (kms_connector));
      output = meta_create_kms_output (gpu_kms,
                                       kms_connector,
                                       old_output,
                                       &error);
      if (!output)
        {
          g_warning ("Failed to create KMS output: %s", error->message);
          g_error_free (error);
        }
      else
        {
          outputs = g_list_prepend (outputs, output);
        }
    }


  /* Sort the outputs for easier handling in MetaMonitorConfig */
  outputs = g_list_sort (outputs, compare_outputs);
  meta_gpu_take_outputs (gpu, outputs);

  setup_output_clones (gpu);
}

static gboolean
meta_gpu_kms_read_current (MetaGpu  *gpu,
                           GError  **error)
{
  MetaGpuKms *gpu_kms = META_GPU_KMS (gpu);

  /* Note: we must not free the public structures (output, crtc, monitor
     mode and monitor info) here, they must be kept alive until the API
     users are done with them after we emit monitors-changed, and thus
     are freed by the platform-independent layer. */

  init_modes (gpu_kms);
  init_crtcs (gpu_kms);
  init_outputs (gpu_kms);
  init_frame_clock (gpu_kms);

  return TRUE;
}

gboolean
meta_gpu_kms_can_have_outputs (MetaGpuKms *gpu_kms)
{
  GList *l;
  int n_connected_connectors = 0;

  for (l = meta_kms_device_get_connectors (gpu_kms->kms_device); l; l = l->next)
    {
      MetaKmsConnector *kms_connector = l->data;

      if (meta_kms_connector_get_current_state (kms_connector))
        n_connected_connectors++;
    }

  return n_connected_connectors > 0;
}

MetaGpuKms *
meta_gpu_kms_new (MetaBackendNative  *backend_native,
                  MetaKmsDevice      *kms_device,
                  GError            **error)
{
  MetaGpuKms *gpu_kms;
  int kms_fd;

  kms_fd = meta_kms_device_leak_fd (kms_device);

  gpu_kms = g_object_new (META_TYPE_GPU_KMS,
                          "backend", backend_native,
                          NULL);

  gpu_kms->kms_device = kms_device;
  gpu_kms->fd = kms_fd;

  meta_gpu_kms_read_current (META_GPU (gpu_kms), NULL);

  return gpu_kms;
}

static void
meta_gpu_kms_init (MetaGpuKms *gpu_kms)
{
  static uint32_t id = 0;

  gpu_kms->fd = -1;
  gpu_kms->id = ++id;
}

static void
meta_gpu_kms_class_init (MetaGpuKmsClass *klass)
{
  MetaGpuClass *gpu_class = META_GPU_CLASS (klass);

  gpu_class->read_current = meta_gpu_kms_read_current;
}
