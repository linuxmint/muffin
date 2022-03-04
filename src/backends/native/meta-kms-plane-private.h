/*
 * Copyright (C) 2018-2019 Red Hat
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

#ifndef META_KMS_PLANE_PRIVATE_H
#define META_KMS_PLANE_PRIVATE_H

#include <xf86drm.h>
#include <xf86drmMode.h>

#include "backends/native/meta-kms-plane.h"
#include "backends/native/meta-kms-types.h"

MetaKmsPlane * meta_kms_plane_new (MetaKmsPlaneType         type,
                                   MetaKmsImplDevice       *impl_device,
                                   drmModePlane            *drm_plane,
                                   drmModeObjectProperties *drm_plane_props);

MetaKmsPlane * meta_kms_plane_new_fake (MetaKmsPlaneType  type,
                                        MetaKmsCrtc      *crtc);

#endif /* META_KMS_PLANE_PRIVATE_H */
