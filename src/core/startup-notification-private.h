/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001, 2002 Havoc Pennington
 * Copyright (C) 2002, 2003 Red Hat Inc.
 * Some ICCCM manager selection code derived from fvwm2,
 * Copyright (C) 2001 Dominik Vogt, Matthias Clasen, and fvwm2 team
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
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

#ifndef META_STARTUP_NOTIFICATION_PRIVATE_H
#define META_STARTUP_NOTIFICATION_PRIVATE_H

#include "core/display-private.h"
#include "meta/meta-startup-notification.h"

struct _MetaStartupSequenceClass
{
  GObjectClass parent_class;

  void (* complete) (MetaStartupSequence *sequence);
};

MetaStartupNotification *
         meta_startup_notification_new             (MetaDisplay             *display);

gboolean meta_startup_notification_handle_xevent   (MetaStartupNotification *sn,
                                                    XEvent                  *xevent);

void     meta_startup_notification_add_sequence    (MetaStartupNotification *sn,
                                                    MetaStartupSequence     *seq);
void     meta_startup_notification_remove_sequence (MetaStartupNotification *sn,
                                                    MetaStartupSequence     *seq);
MetaStartupSequence *
         meta_startup_notification_lookup_sequence (MetaStartupNotification *sn,
                                                    const gchar             *id);

#endif /* META_STARTUP_NOTIFICATION_PRIVATE_H */
