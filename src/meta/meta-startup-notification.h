/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2018 Red Hat, Inc
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef META_STARTUP_NOTIFICATION_H
#define META_STARTUP_NOTIFICATION_H

#include <meta/meta-launch-context.h>

#define META_TYPE_STARTUP_SEQUENCE (meta_startup_sequence_get_type ())
#define META_TYPE_STARTUP_NOTIFICATION (meta_startup_notification_get_type ())

META_EXPORT
G_DECLARE_FINAL_TYPE (MetaStartupNotification,
                      meta_startup_notification,
                      META, STARTUP_NOTIFICATION,
                      GObject)

META_EXPORT
G_DECLARE_DERIVABLE_TYPE (MetaStartupSequence,
                          meta_startup_sequence,
                          META, STARTUP_SEQUENCE,
                          GObject)

/**
 * meta_startup_notification_get_sequences: (skip)
 */
META_EXPORT
GSList *      meta_startup_notification_get_sequences (MetaStartupNotification *sn);

META_EXPORT
MetaLaunchContext *
             meta_startup_notification_create_launcher (MetaStartupNotification *sn);

META_EXPORT
const char * meta_startup_sequence_get_id             (MetaStartupSequence *sequence);

META_EXPORT
gboolean     meta_startup_sequence_get_completed      (MetaStartupSequence *sequence);

META_EXPORT
const char * meta_startup_sequence_get_name           (MetaStartupSequence *sequence);

META_EXPORT
int          meta_startup_sequence_get_workspace      (MetaStartupSequence *sequence);

META_EXPORT
uint64_t     meta_startup_sequence_get_timestamp      (MetaStartupSequence *sequence);

META_EXPORT
const char * meta_startup_sequence_get_icon_name      (MetaStartupSequence *sequence);

META_EXPORT
const char * meta_startup_sequence_get_application_id (MetaStartupSequence *sequence);

META_EXPORT
const char * meta_startup_sequence_get_wmclass        (MetaStartupSequence *sequence);

META_EXPORT
void        meta_startup_sequence_complete           (MetaStartupSequence *sequence);

#endif /* META_STARTUP_NOTIFICATION_H */
