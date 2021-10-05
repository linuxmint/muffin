/*
 * Copyright (C) 2017 Red Hat
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
 *     Olivier Fourdan <ofourdan@redhat.com>
 */

#ifndef META_WAYLAND_INHIBIT_SHORTCUTS_H
#define META_WAYLAND_INHIBIT_SHORTCUTS_H

#include <wayland-server.h>

#include "meta/window.h"
#include "wayland/meta-wayland-types.h"

#define META_TYPE_WAYLAND_KEYBOARD_SHORTCUTS_INHIBIT (meta_wayland_keyboard_shortcuts_inhibit_resource_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandKeyboardShotscutsInhibit,
                      meta_wayland_keyboard_shortcuts_inhibit_resource,
                      META, WAYLAND_KEYBOARD_SHORTCUTS_INHIBIT,
                      GObject);

gboolean meta_wayland_keyboard_shortcuts_inhibit_init (MetaWaylandCompositor *compositor);

#endif /* META_WAYLAND_INHIBIT_SHORTCUTS_H */
