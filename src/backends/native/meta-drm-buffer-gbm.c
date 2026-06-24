/*
 * Copyright (C) 2011 Intel Corporation.
 * Copyright (C) 2016 Red Hat
 * Copyright (C) 2018 DisplayLink (UK) Ltd.
 * Copyright (C) 2018 Canonical Ltd.
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

#include "backends/native/meta-drm-buffer-gbm.h"

#include <drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <gbm.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define INVALID_FB_ID 0U

struct _MetaDrmBufferGbm
{
  MetaDrmBuffer parent;

  MetaGpuKms *gpu_kms;

  struct gbm_surface *surface;

  struct gbm_bo *bo;
  uint32_t fb_id;
};

static void
cogl_scanout_iface_init (CoglScanoutInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaDrmBufferGbm, meta_drm_buffer_gbm, META_TYPE_DRM_BUFFER,
                         G_IMPLEMENT_INTERFACE (COGL_TYPE_SCANOUT,
                                                cogl_scanout_iface_init))

struct gbm_bo *
meta_drm_buffer_gbm_get_bo (MetaDrmBufferGbm *buffer_gbm)
{
  return buffer_gbm->bo;
}

static void
close_kms_handle (int kms_fd, uint32_t handle)
{
  struct drm_gem_close args = { .handle = handle };
  drmIoctl (kms_fd, DRM_IOCTL_GEM_CLOSE, &args);
}

static gboolean
init_fb_id (MetaDrmBufferGbm  *buffer_gbm,
            struct gbm_bo     *bo,
            gboolean           use_modifiers,
            GError           **error)
{
  MetaGpuKmsFBArgs fb_args = { 0, };
  int gbm_fd = gbm_device_get_fd (gbm_bo_get_device (bo));
  int kms_fd = meta_gpu_kms_get_fd (buffer_gbm->gpu_kms);
  gboolean need_prime = (gbm_fd != kms_fd);
  uint32_t imported_handles[4] = { 0, 0, 0, 0 };
  int n_imported = 0;
  int i;

  if (gbm_bo_get_handle_for_plane (bo, 0).s32 == -1)
    {
      /* Failed to fetch handle to plane, falling back to old method */
      fb_args.strides[0] = gbm_bo_get_stride (bo);
      fb_args.offsets[0] = 0;
      fb_args.modifiers[0] = DRM_FORMAT_MOD_INVALID;

      if (need_prime)
        {
          int prime_fd = -1;
          if (drmPrimeHandleToFD (gbm_fd, gbm_bo_get_handle (bo).u32,
                                   DRM_CLOEXEC, &prime_fd) != 0 ||
              drmPrimeFDToHandle (kms_fd, prime_fd, &imported_handles[0]) != 0)
            {
              if (prime_fd >= 0)
                close (prime_fd);
              g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                           "Failed to import GBM buffer via prime: %s",
                           g_strerror (errno));
              return FALSE;
            }
          close (prime_fd);
          n_imported = 1;
          fb_args.handles[0] = imported_handles[0];
        }
      else
        {
          fb_args.handles[0] = gbm_bo_get_handle (bo).u32;
        }
    }
  else
    {
      int n_planes = gbm_bo_get_plane_count (bo);

      for (i = 0; i < n_planes; i++)
        {
          fb_args.strides[i] = gbm_bo_get_stride_for_plane (bo, i);
          fb_args.offsets[i] = gbm_bo_get_offset (bo, i);
          fb_args.modifiers[i] = gbm_bo_get_modifier (bo);

          if (need_prime)
            {
              int prime_fd = -1;
              if (drmPrimeHandleToFD (gbm_fd,
                                       gbm_bo_get_handle_for_plane (bo, i).u32,
                                       DRM_CLOEXEC, &prime_fd) != 0 ||
                  drmPrimeFDToHandle (kms_fd, prime_fd,
                                       &imported_handles[i]) != 0)
                {
                  if (prime_fd >= 0)
                    close (prime_fd);
                  g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                               "Failed to import GBM buffer plane %d via prime: %s",
                               i, g_strerror (errno));
                  goto out_close_imported;
                }
              close (prime_fd);
              n_imported = i + 1;
              fb_args.handles[i] = imported_handles[i];
            }
          else
            {
              fb_args.handles[i] = gbm_bo_get_handle_for_plane (bo, i).u32;
            }
        }
    }

  fb_args.width = gbm_bo_get_width (bo);
  fb_args.height = gbm_bo_get_height (bo);
  fb_args.format = gbm_bo_get_format (bo);

  if (!meta_gpu_kms_add_fb (buffer_gbm->gpu_kms,
                            use_modifiers,
                            &fb_args,
                            &buffer_gbm->fb_id, error))
    goto out_close_imported;

  /* The kernel now holds a reference to the GEM objects via the FB;
   * close our local imported handles. */
  for (i = 0; i < n_imported; i++)
    close_kms_handle (kms_fd, imported_handles[i]);

  return TRUE;

out_close_imported:
  for (i = 0; i < n_imported; i++)
    close_kms_handle (kms_fd, imported_handles[i]);
  return FALSE;
}

static gboolean
lock_front_buffer (MetaDrmBufferGbm  *buffer_gbm,
                   gboolean           use_modifiers,
                   GError           **error)
{
  buffer_gbm->bo = gbm_surface_lock_front_buffer (buffer_gbm->surface);
  if (!buffer_gbm->bo)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "gbm_surface_lock_front_buffer failed");
      return FALSE;
    }
  return init_fb_id (buffer_gbm, buffer_gbm->bo, use_modifiers, error);
}

MetaDrmBufferGbm *
meta_drm_buffer_gbm_new_lock_front (MetaGpuKms          *gpu_kms,
                                    struct gbm_surface  *gbm_surface,
                                    gboolean             use_modifiers,
                                    GError             **error)
{
  MetaDrmBufferGbm *buffer_gbm;

  buffer_gbm = g_object_new (META_TYPE_DRM_BUFFER_GBM, NULL);
  buffer_gbm->gpu_kms = gpu_kms;
  buffer_gbm->surface = gbm_surface;

  if (!lock_front_buffer (buffer_gbm, use_modifiers, error))
    {
      g_object_unref (buffer_gbm);
      return NULL;
    }

  return buffer_gbm;
}

MetaDrmBufferGbm *
meta_drm_buffer_gbm_new_take (MetaGpuKms     *gpu_kms,
                              struct gbm_bo  *bo,
                              gboolean        use_modifiers,
                              GError        **error)
{
  MetaDrmBufferGbm *buffer_gbm;

  buffer_gbm = g_object_new (META_TYPE_DRM_BUFFER_GBM, NULL);
  buffer_gbm->gpu_kms = gpu_kms;

  if (!init_fb_id (buffer_gbm, bo, use_modifiers, error))
    {
      g_object_unref (buffer_gbm);
      return NULL;
    }

  buffer_gbm->bo = bo;

  return buffer_gbm;
}


static uint32_t
meta_drm_buffer_gbm_get_fb_id (MetaDrmBuffer *buffer)
{
  return META_DRM_BUFFER_GBM (buffer)->fb_id;
}

static void
cogl_scanout_iface_init (CoglScanoutInterface *iface)
{
}

static void
meta_drm_buffer_gbm_finalize (GObject *object)
{
  MetaDrmBufferGbm *buffer_gbm = META_DRM_BUFFER_GBM (object);

  if (buffer_gbm->fb_id != INVALID_FB_ID)
    {
      int kms_fd;

      kms_fd = meta_gpu_kms_get_fd (buffer_gbm->gpu_kms);
      drmModeRmFB (kms_fd, buffer_gbm->fb_id);
    }

  if (buffer_gbm->bo)
    {
      if (buffer_gbm->surface)
        gbm_surface_release_buffer (buffer_gbm->surface, buffer_gbm->bo);
      else
        gbm_bo_destroy (buffer_gbm->bo);
    }

  G_OBJECT_CLASS (meta_drm_buffer_gbm_parent_class)->finalize (object);
}

static void
meta_drm_buffer_gbm_init (MetaDrmBufferGbm *buffer_gbm)
{
}

static void
meta_drm_buffer_gbm_class_init (MetaDrmBufferGbmClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaDrmBufferClass *buffer_class = META_DRM_BUFFER_CLASS (klass);

  object_class->finalize = meta_drm_buffer_gbm_finalize;

  buffer_class->get_fb_id = meta_drm_buffer_gbm_get_fb_id;
}
