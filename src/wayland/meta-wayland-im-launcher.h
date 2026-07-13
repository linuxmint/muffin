/*
 * Wayland Support
 *
 * Copyright (C) 2026 the Cinnamon team
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

#ifndef META_WAYLAND_IM_LAUNCHER_H
#define META_WAYLAND_IM_LAUNCHER_H

#include <wayland-server.h>

#include "wayland/meta-wayland-types.h"

MetaWaylandImLauncher * meta_wayland_im_launcher_new (MetaWaylandCompositor *compositor);

void meta_wayland_im_launcher_start (MetaWaylandImLauncher *launcher);

void meta_wayland_im_launcher_destroy (MetaWaylandImLauncher *launcher);

/* The wl_client of the compositor-spawned fcitx, or NULL if it is not
 * currently connected. The global filter uses this to advertise the
 * input-method-v2 / virtual-keyboard-v1 globals only to fcitx. */
struct wl_client * meta_wayland_im_launcher_get_client (MetaWaylandImLauncher *launcher);

#endif /* META_WAYLAND_IM_LAUNCHER_H */
