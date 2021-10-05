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

#include "config.h"

#include "backends/native/meta-kms-page-flip-private.h"

#include "backends/native/meta-kms-impl.h"
#include "backends/native/meta-kms-private.h"
#include "backends/native/meta-kms-update.h"

struct _MetaKmsPageFlipData
{
  int ref_count;

  MetaKmsImpl *impl;
  MetaKmsCrtc *crtc;

  const MetaKmsPageFlipFeedback *feedback;
  gpointer user_data;

  unsigned int sequence;
  unsigned int sec;
  unsigned int usec;

  GError *error;
};

MetaKmsPageFlipData *
meta_kms_page_flip_data_new (MetaKmsImpl                   *impl,
                             MetaKmsCrtc                   *crtc,
                             const MetaKmsPageFlipFeedback *feedback,
                             gpointer                       user_data)
{
  MetaKmsPageFlipData *page_flip_data;

  page_flip_data = g_new0 (MetaKmsPageFlipData , 1);
  *page_flip_data = (MetaKmsPageFlipData) {
    .ref_count = 1,
    .impl = impl,
    .crtc = crtc,
    .feedback = feedback,
    .user_data = user_data,
  };

  return page_flip_data;
}

MetaKmsPageFlipData *
meta_kms_page_flip_data_ref (MetaKmsPageFlipData *page_flip_data)
{
  page_flip_data->ref_count++;

  return page_flip_data;
}

void
meta_kms_page_flip_data_unref (MetaKmsPageFlipData *page_flip_data)
{
  page_flip_data->ref_count--;

  if (page_flip_data->ref_count == 0)
    {
      g_clear_error (&page_flip_data->error);
      g_free (page_flip_data);
    }
}

MetaKmsImpl *
meta_kms_page_flip_data_get_kms_impl (MetaKmsPageFlipData *page_flip_data)
{
  return page_flip_data->impl;
}

static void
meta_kms_page_flip_data_flipped (MetaKms  *kms,
                                 gpointer  user_data)
{
  MetaKmsPageFlipData *page_flip_data = user_data;

  meta_assert_not_in_kms_impl (kms);

  page_flip_data->feedback->flipped (page_flip_data->crtc,
                                     page_flip_data->sequence,
                                     page_flip_data->sec,
                                     page_flip_data->usec,
                                     page_flip_data->user_data);
}

void
meta_kms_page_flip_data_set_timings_in_impl (MetaKmsPageFlipData *page_flip_data,
                                             unsigned int         sequence,
                                             unsigned int         sec,
                                             unsigned int         usec)
{
  MetaKms *kms = meta_kms_impl_get_kms (page_flip_data->impl);

  meta_assert_in_kms_impl (kms);

  page_flip_data->sequence = sequence;
  page_flip_data->sec = sec;
  page_flip_data->usec = usec;
}

void
meta_kms_page_flip_data_flipped_in_impl (MetaKmsPageFlipData *page_flip_data)
{
  MetaKms *kms = meta_kms_impl_get_kms (page_flip_data->impl);

  meta_assert_in_kms_impl (kms);

  meta_kms_queue_callback (kms,
                           meta_kms_page_flip_data_flipped,
                           meta_kms_page_flip_data_ref (page_flip_data),
                           (GDestroyNotify) meta_kms_page_flip_data_unref);
}

static void
meta_kms_page_flip_data_mode_set_fallback (MetaKms  *kms,
                                           gpointer  user_data)
{
  MetaKmsPageFlipData *page_flip_data = user_data;

  meta_assert_not_in_kms_impl (kms);

  page_flip_data->feedback->mode_set_fallback (page_flip_data->crtc,
                                               page_flip_data->user_data);
}

void
meta_kms_page_flip_data_mode_set_fallback_in_impl (MetaKmsPageFlipData *page_flip_data)
{
  MetaKms *kms = meta_kms_impl_get_kms (page_flip_data->impl);

  meta_assert_in_kms_impl (kms);

  meta_kms_queue_callback (kms,
                           meta_kms_page_flip_data_mode_set_fallback,
                           meta_kms_page_flip_data_ref (page_flip_data),
                           (GDestroyNotify) meta_kms_page_flip_data_unref);
}

static void
meta_kms_page_flip_data_discard (MetaKms  *kms,
                                 gpointer  user_data)
{
  MetaKmsPageFlipData *page_flip_data = user_data;

  meta_assert_not_in_kms_impl (kms);

  page_flip_data->feedback->discarded (page_flip_data->crtc,
                                       page_flip_data->user_data,
                                       page_flip_data->error);
}

void
meta_kms_page_flip_data_take_error (MetaKmsPageFlipData *page_flip_data,
                                    GError              *error)
{
  g_assert (!page_flip_data->error);

  page_flip_data->error = error;
}

void
meta_kms_page_flip_data_discard_in_impl (MetaKmsPageFlipData *page_flip_data,
                                         const GError        *error)
{
  MetaKms *kms = meta_kms_impl_get_kms (page_flip_data->impl);

  meta_assert_in_kms_impl (kms);

  if (error)
    meta_kms_page_flip_data_take_error (page_flip_data, g_error_copy (error));

  meta_kms_queue_callback (kms,
                           meta_kms_page_flip_data_discard,
                           meta_kms_page_flip_data_ref (page_flip_data),
                           (GDestroyNotify) meta_kms_page_flip_data_unref);
}
