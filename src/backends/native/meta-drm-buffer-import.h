/*
 * Copyright (C) 2018 Canonical Ltd.
 * Copyright (C) 2019 Red Hat Inc.
 * Copyright (C) 2019 DisplayLink (UK) Ltd.
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

#ifndef META_DRM_BUFFER_IMPORT_H
#define META_DRM_BUFFER_IMPORT_H

#include <gbm.h>

#include "backends/native/meta-drm-buffer.h"
#include "backends/native/meta-drm-buffer-gbm.h"
#include "backends/native/meta-gpu-kms.h"

#define META_TYPE_DRM_BUFFER_IMPORT (meta_drm_buffer_import_get_type ())
G_DECLARE_FINAL_TYPE (MetaDrmBufferImport,
                      meta_drm_buffer_import,
                      META, DRM_BUFFER_IMPORT,
                      MetaDrmBuffer)

/*
 * MetaDrmBufferImport is a buffer that refers to the storage of a
 * MetaDrmBufferGbm buffer on another MetaGpuKms.
 *
 * When creating an imported buffer, the given GBM buffer is exported
 * as a dma_buf and then imported to the given MetaGpuKms. A reference
 * is kept to the GBM buffer so that it won't disappear while the
 * imported buffer exists.
 *
 * The import has a high chance of failing under normal operating
 * conditions and needs to be handled with fallbacks to something else.
 */
MetaDrmBufferImport * meta_drm_buffer_import_new (MetaGpuKms        *gpu_kms,
                                                  MetaDrmBufferGbm  *buffer_gbm,
                                                  GError           **error);

#endif /* META_DRM_BUFFER_IMPORT_H */
