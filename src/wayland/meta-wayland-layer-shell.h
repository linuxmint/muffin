/*
 * Copyright (C) 2024 Linux Mint
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

#ifndef META_WAYLAND_LAYER_SHELL_H
#define META_WAYLAND_LAYER_SHELL_H

#include "wayland/meta-wayland.h"
#include "wayland/meta-wayland-actor-surface.h"

/* Layer shell layer values - matches protocol enum */
typedef enum
{
  META_LAYER_SHELL_LAYER_BACKGROUND = 0,
  META_LAYER_SHELL_LAYER_BOTTOM = 1,
  META_LAYER_SHELL_LAYER_TOP = 2,
  META_LAYER_SHELL_LAYER_OVERLAY = 3,
} MetaLayerShellLayer;

#define META_TYPE_WAYLAND_LAYER_SHELL (meta_wayland_layer_shell_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandLayerShell, meta_wayland_layer_shell,
                      META, WAYLAND_LAYER_SHELL, GObject)

#define META_TYPE_WAYLAND_LAYER_SURFACE (meta_wayland_layer_surface_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandLayerSurface, meta_wayland_layer_surface,
                      META, WAYLAND_LAYER_SURFACE, MetaWaylandActorSurface)

void meta_wayland_init_layer_shell (MetaWaylandCompositor *compositor);

MetaLayerShellLayer meta_wayland_layer_surface_get_layer (MetaWaylandLayerSurface *layer_surface);
MetaWaylandOutput * meta_wayland_layer_surface_get_output (MetaWaylandLayerSurface *layer_surface);

void meta_wayland_layer_shell_update_struts (MetaWaylandCompositor *compositor);
void meta_wayland_layer_shell_on_workarea_changed (MetaWaylandCompositor *compositor);

#endif /* META_WAYLAND_LAYER_SHELL_H */
