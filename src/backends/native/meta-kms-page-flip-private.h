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

#ifndef META_KMS_PAGE_FLIP_H
#define META_KMS_PAGE_FLIP_H

#include <glib.h>

#include "backends/native/meta-kms-types.h"

typedef struct _MetaKmsPageFlipData MetaKmsPageFlipData;

typedef void (* MetaPageFlipDataFeedbackFunc) (MetaKmsPageFlipData *page_flip_data);

MetaKmsPageFlipData * meta_kms_page_flip_data_new (MetaKmsImpl                   *impl,
                                                   MetaKmsCrtc                   *crtc,
                                                   const MetaKmsPageFlipFeedback *feedback,
                                                   gpointer                       user_data);

MetaKmsPageFlipData * meta_kms_page_flip_data_ref (MetaKmsPageFlipData *page_flip_data);

void meta_kms_page_flip_data_unref (MetaKmsPageFlipData *page_flip_data);

MetaKmsImpl * meta_kms_page_flip_data_get_kms_impl (MetaKmsPageFlipData *page_flip_data);

void meta_kms_page_flip_data_set_timings_in_impl (MetaKmsPageFlipData *page_flip_data,
                                                  unsigned int         sequence,
                                                  unsigned int         sec,
                                                  unsigned int         usec);

void meta_kms_page_flip_data_flipped_in_impl (MetaKmsPageFlipData *page_flip_data);

void meta_kms_page_flip_data_mode_set_fallback_in_impl (MetaKmsPageFlipData *page_flip_data);

void meta_kms_page_flip_data_discard_in_impl (MetaKmsPageFlipData *page_flip_data,
                                              const GError        *error);

void meta_kms_page_flip_data_take_error (MetaKmsPageFlipData *page_flip_data,
                                         GError              *error);

#endif /* META_KMS_PAGE_FLIP_H */
