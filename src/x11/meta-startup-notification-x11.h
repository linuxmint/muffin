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

#include "meta-x11-display-private.h"

#ifndef META_X11_STARTUP_NOTIFICATION_H
#define META_X11_STARTUP_NOTIFICATION_H

typedef struct _MetaX11StartupNotification MetaX11StartupNotification;

#define META_TYPE_STARTUP_SEQUENCE_X11 (meta_startup_sequence_x11_get_type ())

G_DECLARE_FINAL_TYPE (MetaStartupSequenceX11,
                      meta_startup_sequence_x11,
                      META, STARTUP_SEQUENCE_X11,
                      MetaStartupSequence)

void     meta_x11_startup_notification_init    (MetaX11Display *x11_display);
void     meta_x11_startup_notification_release (MetaX11Display *x11_display);

gboolean meta_x11_startup_notification_handle_xevent (MetaX11Display *x11_display,
                                                      XEvent         *xevent);

gchar *  meta_x11_startup_notification_launch (MetaX11Display *x11_display,
                                               GAppInfo       *app_info,
                                               uint32_t        timestamp,
                                               int             workspace);

#endif /* META_X11_STARTUP_NOTIFICATION_H */
