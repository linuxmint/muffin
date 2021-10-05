/*
 * Copyright (C) 2016 Red Hat, Inc.
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

#ifndef META_WAYLAND_GTK_SHELL_H
#define META_WAYLAND_GTK_SHELL_H

#include "wayland/meta-wayland.h"

#define META_TYPE_WAYLAND_GTK_SHELL (meta_wayland_gtk_shell_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandGtkShell, meta_wayland_gtk_shell,
                      META, WAYLAND_GTK_SHELL, GObject)

void meta_wayland_init_gtk_shell (MetaWaylandCompositor *compositor);

#endif /* META_WAYLAND_GTK_SHELL_H */
