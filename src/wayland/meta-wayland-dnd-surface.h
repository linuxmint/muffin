/*
 * Copyright (C) 2015-2019 Red Hat, Inc.
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

#ifndef META_WAYLAND_DND_SURFACE_H
#define META_WAYLAND_DND_SURFACE_H

#include "wayland/meta-wayland-actor-surface.h"

#define META_TYPE_WAYLAND_SURFACE_ROLE_DND (meta_wayland_surface_role_dnd_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandSurfaceRoleDND,
                      meta_wayland_surface_role_dnd,
                      META, WAYLAND_SURFACE_ROLE_DND,
                      MetaWaylandActorSurface)

#endif /* META_WAYLAND_DND_SURFACE_H */
