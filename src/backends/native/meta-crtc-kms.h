/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2017 Red Hat
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

#ifndef META_CRTC_KMS_H
#define META_CRTC_KMS_H

#include <xf86drm.h>
#include <xf86drmMode.h>

#include "backends/meta-backend-types.h"
#include "backends/meta-crtc.h"
#include "backends/native/meta-gpu-kms.h"
#include "backends/native/meta-kms-crtc.h"

gboolean meta_crtc_kms_is_transform_handled (MetaCrtc             *crtc,
                                             MetaMonitorTransform  transform);

void meta_crtc_kms_apply_transform (MetaCrtc               *crtc,
                                    MetaKmsPlaneAssignment *kms_plane_assignment);

void meta_crtc_kms_assign_primary_plane (MetaCrtc      *crtc,
                                         uint32_t       fb_id,
                                         MetaKmsUpdate *kms_update);

void meta_crtc_kms_set_mode (MetaCrtc      *crtc,
                             MetaKmsUpdate *kms_update);

void meta_crtc_kms_page_flip (MetaCrtc                      *crtc,
                              const MetaKmsPageFlipFeedback *page_flip_feedback,
                              gpointer                       user_data,
                              MetaKmsUpdate                 *kms_update);

void meta_crtc_kms_set_is_underscanning (MetaCrtc *crtc,
                                         gboolean  is_underscanning);

MetaKmsCrtc * meta_crtc_kms_get_kms_crtc (MetaCrtc *crtc);

GArray * meta_crtc_kms_get_modifiers (MetaCrtc *crtc,
                                      uint32_t  format);

GArray *
meta_crtc_kms_copy_drm_format_list (MetaCrtc *crtc);

gboolean
meta_crtc_kms_supports_format (MetaCrtc *crtc,
                               uint32_t  drm_format);

MetaCrtc * meta_crtc_kms_from_kms_crtc (MetaKmsCrtc *kms_crtc);

MetaCrtc * meta_create_kms_crtc (MetaGpuKms  *gpu_kms,
                                 MetaKmsCrtc *kms_crtc);

#endif /* META_CRTC_KMS_H */
