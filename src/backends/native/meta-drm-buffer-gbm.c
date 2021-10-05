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

G_DEFINE_TYPE (MetaDrmBufferGbm, meta_drm_buffer_gbm, META_TYPE_DRM_BUFFER)

struct gbm_bo *
meta_drm_buffer_gbm_get_bo (MetaDrmBufferGbm *buffer_gbm)
{
  return buffer_gbm->bo;
}

static gboolean
acquire_swapped_buffer (MetaDrmBufferGbm  *buffer_gbm,
                        gboolean           use_modifiers,
                        GError           **error)
{
  MetaGpuKmsFBArgs fb_args = { 0, };
  struct gbm_bo *bo;

  bo = gbm_surface_lock_front_buffer (buffer_gbm->surface);
  if (!bo)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "gbm_surface_lock_front_buffer failed");
      return FALSE;
    }

  if (gbm_bo_get_handle_for_plane (bo, 0).s32 == -1)
    {
      /* Failed to fetch handle to plane, falling back to old method */
      fb_args.strides[0] = gbm_bo_get_stride (bo);
      fb_args.handles[0] = gbm_bo_get_handle (bo).u32;
      fb_args.offsets[0] = 0;
      fb_args.modifiers[0] = DRM_FORMAT_MOD_INVALID;
    }
  else
    {
      int i;

      for (i = 0; i < gbm_bo_get_plane_count (bo); i++)
        {
          fb_args.strides[i] = gbm_bo_get_stride_for_plane (bo, i);
          fb_args.handles[i] = gbm_bo_get_handle_for_plane (bo, i).u32;
          fb_args.offsets[i] = gbm_bo_get_offset (bo, i);
          fb_args.modifiers[i] = gbm_bo_get_modifier (bo);
        }
     }

  fb_args.width = gbm_bo_get_width (bo);
  fb_args.height = gbm_bo_get_height (bo);
  fb_args.format = gbm_bo_get_format (bo);

  if (!meta_gpu_kms_add_fb (buffer_gbm->gpu_kms,
                            use_modifiers,
                            &fb_args,
                            &buffer_gbm->fb_id, error))
    {
      gbm_surface_release_buffer (buffer_gbm->surface, bo);
      return FALSE;
    }

  buffer_gbm->bo = bo;

  return TRUE;
}

MetaDrmBufferGbm *
meta_drm_buffer_gbm_new (MetaGpuKms          *gpu_kms,
                         struct gbm_surface  *gbm_surface,
                         gboolean             use_modifiers,
                         GError             **error)
{
  MetaDrmBufferGbm *buffer_gbm;

  buffer_gbm = g_object_new (META_TYPE_DRM_BUFFER_GBM, NULL);
  buffer_gbm->gpu_kms = gpu_kms;
  buffer_gbm->surface = gbm_surface;

  if (!acquire_swapped_buffer (buffer_gbm, use_modifiers, error))
    {
      g_object_unref (buffer_gbm);
      return NULL;
    }

  return buffer_gbm;
}

static uint32_t
meta_drm_buffer_gbm_get_fb_id (MetaDrmBuffer *buffer)
{
  return META_DRM_BUFFER_GBM (buffer)->fb_id;
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
    gbm_surface_release_buffer (buffer_gbm->surface, buffer_gbm->bo);

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
