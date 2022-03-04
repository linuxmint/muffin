/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter window groups */

/*
 * Copyright (C) 2002 Red Hat Inc.
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

#ifndef META_GROUP_H
#define META_GROUP_H

#include <X11/Xlib.h>
#include <glib.h>

#include <meta/common.h>
#include <meta/types.h>

/* note, can return NULL */
META_EXPORT
MetaGroup* meta_window_get_group       (MetaWindow *window);

META_EXPORT
void       meta_window_compute_group   (MetaWindow* window);

META_EXPORT
void       meta_window_shutdown_group  (MetaWindow *window);

META_EXPORT
void       meta_window_group_leader_changed (MetaWindow *window);

/* note, can return NULL */
META_EXPORT
MetaGroup *meta_x11_display_lookup_group (MetaX11Display *x11_display,
                                          Window          group_leader);

META_EXPORT
GSList*    meta_group_list_windows     (MetaGroup *group);

META_EXPORT
void       meta_group_update_layers    (MetaGroup *group);

META_EXPORT
const char* meta_group_get_startup_id  (MetaGroup *group);

META_EXPORT
int        meta_group_get_size         (MetaGroup *group);

META_EXPORT
gboolean meta_group_property_notify   (MetaGroup  *group,
                                       XEvent     *event);

#endif
