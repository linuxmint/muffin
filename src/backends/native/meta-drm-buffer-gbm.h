/*
 * Copyright (C) 2018 Canonical Ltd.
 * Copyright (C) 2019 Red Hat Inc.
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

#ifndef META_DRM_BUFFER_GBM_H
#define META_DRM_BUFFER_GBM_H

#include <gbm.h>

#include "backends/native/meta-drm-buffer.h"
#include "backends/native/meta-gpu-kms.h"

#define META_TYPE_DRM_BUFFER_GBM (meta_drm_buffer_gbm_get_type ())
G_DECLARE_FINAL_TYPE (MetaDrmBufferGbm,
                      meta_drm_buffer_gbm,
                      META, DRM_BUFFER_GBM,
                      MetaDrmBuffer)

MetaDrmBufferGbm * meta_drm_buffer_gbm_new (MetaGpuKms          *gpu_kms,
                                            struct gbm_surface  *gbm_surface,
                                            gboolean             use_modifiers,
                                            GError             **error);

struct gbm_bo * meta_drm_buffer_gbm_get_bo (MetaDrmBufferGbm *buffer_gbm);

#endif /* META_DRM_BUFFER_GBM_H */
