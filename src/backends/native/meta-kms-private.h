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

#ifndef META_KMS_PRIVATE_H
#define META_KMS_PRIVATE_H

#include "backends/native/meta-kms.h"

#include "backends/native/meta-kms-types.h"

typedef void (* MetaKmsCallback) (MetaKms  *kms,
                                  gpointer  user_data);

typedef gpointer (* MetaKmsImplTaskFunc) (MetaKmsImpl  *impl,
                                          gpointer      user_data,
                                          GError      **error);

void meta_kms_queue_callback (MetaKms         *kms,
                              MetaKmsCallback  callback,
                              gpointer         user_data,
                              GDestroyNotify   user_data_destroy);

int meta_kms_flush_callbacks (MetaKms *kms);

gpointer meta_kms_run_impl_task_sync (MetaKms              *kms,
                                      MetaKmsImplTaskFunc   func,
                                      gpointer              user_data,
                                      GError              **error);

GSource * meta_kms_add_source_in_impl (MetaKms        *kms,
                                       GSourceFunc     func,
                                       gpointer        user_data,
                                       GDestroyNotify  user_data_destroy);

GSource * meta_kms_register_fd_in_impl (MetaKms             *kms,
                                        int                  fd,
                                        MetaKmsImplTaskFunc  dispatch,
                                        gpointer             user_data);

gboolean meta_kms_in_impl_task (MetaKms *kms);

gboolean meta_kms_is_waiting_for_impl_task (MetaKms *kms);

#define meta_assert_in_kms_impl(kms) \
  g_assert (meta_kms_in_impl_task (kms))
#define meta_assert_not_in_kms_impl(kms) \
  g_assert (!meta_kms_in_impl_task (kms))
#define meta_assert_is_waiting_for_kms_impl_task(kms) \
  g_assert (meta_kms_is_waiting_for_impl_task (kms))

#endif /* META_KMS_PRIVATE_H */
