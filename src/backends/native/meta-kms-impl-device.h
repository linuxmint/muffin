/*
 * Copyright (C) 2019 Red Hat
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

#ifndef META_KMS_IMPL_DEVICE_H
#define META_KMS_IMPL_DEVICE_H

#include <glib-object.h>
#include <stdint.h>
#include <xf86drmMode.h>

#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-types.h"
#include "backends/native/meta-kms-update.h"

typedef struct _MetaKmsDeviceCaps
{
  gboolean has_cursor_size;
  uint64_t cursor_width;
  uint64_t cursor_height;
} MetaKmsDeviceCaps;

#define META_TYPE_KMS_IMPL_DEVICE (meta_kms_impl_device_get_type ())
G_DECLARE_FINAL_TYPE (MetaKmsImplDevice, meta_kms_impl_device,
                      META, KMS_IMPL_DEVICE,
                      GObject)

MetaKmsDevice * meta_kms_impl_device_get_device (MetaKmsImplDevice *impl_device);

GList * meta_kms_impl_device_copy_connectors (MetaKmsImplDevice *impl_device);

GList * meta_kms_impl_device_copy_crtcs (MetaKmsImplDevice *impl_device);

GList * meta_kms_impl_device_copy_planes (MetaKmsImplDevice *impl_device);

const MetaKmsDeviceCaps * meta_kms_impl_device_get_caps (MetaKmsImplDevice *impl_device);

gboolean meta_kms_impl_device_dispatch (MetaKmsImplDevice  *impl_device,
                                        GError            **error);

drmModePropertyPtr meta_kms_impl_device_find_property (MetaKmsImplDevice       *impl_device,
                                                       drmModeObjectProperties *props,
                                                       const char              *prop_name,
                                                       int                     *idx);

int meta_kms_impl_device_get_fd (MetaKmsImplDevice *impl_device);

int meta_kms_impl_device_leak_fd (MetaKmsImplDevice *impl_device);

void meta_kms_impl_device_update_states (MetaKmsImplDevice *impl_device);

void meta_kms_impl_device_predict_states (MetaKmsImplDevice *impl_device,
                                          MetaKmsUpdate     *update);

MetaKmsPlane * meta_kms_impl_device_add_fake_plane (MetaKmsImplDevice *impl_device,
                                                    MetaKmsPlaneType   plane_type,
                                                    MetaKmsCrtc       *crtc);

int meta_kms_impl_device_close (MetaKmsImplDevice *impl_device);

MetaKmsImplDevice * meta_kms_impl_device_new (MetaKmsDevice  *device,
                                              MetaKmsImpl    *kms_impl,
                                              int             fd,
                                              GError        **error);

#endif /* META_KMS_IMPL_DEVICE_H */
