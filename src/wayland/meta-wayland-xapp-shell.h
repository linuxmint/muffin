/*
 * Copyright (C) 2026 Linux Mint
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

#ifndef META_WAYLAND_XAPP_SHELL_H
#define META_WAYLAND_XAPP_SHELL_H

#include "meta/window.h"
#include "wayland/meta-wayland-types.h"

void meta_wayland_xapp_shell_init (MetaWaylandCompositor *compositor);

/* Applies any xapp-shell hints cached on @surface to @window. Called as a
 * window is managed, so the hints are in place before the window is announced
 * to the shell. */
void meta_wayland_xapp_shell_apply_to_window (MetaWaylandSurface *surface,
                                              MetaWindow         *window);

#endif
