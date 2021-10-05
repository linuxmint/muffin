/*
 * Copyright (C) 2018 Red Hat
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

#ifndef META_KMS_H
#define META_KMS_H

#include <glib-object.h>

#include "backends/meta-backend-private.h"
#include "backends/native/meta-kms-types.h"

#define META_TYPE_KMS (meta_kms_get_type ())
G_DECLARE_FINAL_TYPE (MetaKms, meta_kms, META, KMS, GObject)

MetaKmsUpdate * meta_kms_ensure_pending_update (MetaKms *kms);

MetaKmsUpdate * meta_kms_get_pending_update (MetaKms *kms);

MetaKmsFeedback * meta_kms_post_pending_update_sync (MetaKms *kms);

void meta_kms_discard_pending_page_flips (MetaKms *kms);

MetaBackend * meta_kms_get_backend (MetaKms *kms);

MetaKmsDevice * meta_kms_create_device (MetaKms            *kms,
                                        const char         *path,
                                        MetaKmsDeviceFlag   flags,
                                        GError            **error);

MetaKms * meta_kms_new (MetaBackend  *backend,
                        GError      **error);

#endif /* META_KMS_H */
