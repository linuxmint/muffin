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

#ifndef META_KMS_DEVICE_H
#define META_KMS_DEVICE_H

#include <glib-object.h>

#include "backends/native/meta-kms-types.h"

#define META_TYPE_KMS_DEVICE (meta_kms_device_get_type ())
G_DECLARE_FINAL_TYPE (MetaKmsDevice, meta_kms_device,
                      META, KMS_DEVICE,
                      GObject)

int meta_kms_device_leak_fd (MetaKmsDevice *device);

const char * meta_kms_device_get_path (MetaKmsDevice *device);

MetaKmsDeviceFlag meta_kms_device_get_flags (MetaKmsDevice *device);

gboolean meta_kms_device_get_cursor_size (MetaKmsDevice *device,
                                          uint64_t      *out_cursor_width,
                                          uint64_t      *out_cursor_height);

GList * meta_kms_device_get_connectors (MetaKmsDevice *device);

GList * meta_kms_device_get_crtcs (MetaKmsDevice *device);

MetaKmsPlane * meta_kms_device_get_primary_plane_for (MetaKmsDevice *device,
                                                      MetaKmsCrtc   *crtc);

MetaKmsPlane * meta_kms_device_get_cursor_plane_for (MetaKmsDevice *device,
                                                     MetaKmsCrtc   *crtc);

int meta_kms_device_dispatch_sync (MetaKmsDevice  *device,
                                   GError        **error);

MetaKmsDevice * meta_kms_device_new (MetaKms            *kms,
                                     const char         *path,
                                     MetaKmsDeviceFlag   flags,
                                     GError            **error);

#endif /* META_KMS_DEVICE_H */
