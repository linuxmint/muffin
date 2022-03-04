/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#ifndef META_XWAYLAND_H
#define META_XWAYLAND_H

#include <glib.h>

#include "core/util-private.h"
#include "meta/types.h"
#include "wayland/meta-wayland-types.h"

META_EXPORT_TEST
void
meta_xwayland_override_display_number (int number);

void
meta_xwayland_handle_wl_surface_id (MetaWindow *window,
                                    guint32     surface_id);

gboolean
meta_xwayland_is_xwayland_surface (MetaWaylandSurface *surface);

void
meta_xwayland_handle_xwayland_grab (MetaWindow *window,
                                    gboolean    allow);

void
meta_xwayland_associate_window_with_surface (MetaWindow          *window,
                                             MetaWaylandSurface  *surface);

#endif /* META_XWAYLAND_H */
