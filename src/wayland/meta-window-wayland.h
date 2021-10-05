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

#ifndef META_WINDOW_WAYLAND_H
#define META_WINDOW_WAYLAND_H

#include "core/window-private.h"
#include "meta/window.h"
#include "wayland/meta-wayland-types.h"

G_BEGIN_DECLS

#define META_TYPE_WINDOW_WAYLAND (meta_window_wayland_get_type())
G_DECLARE_FINAL_TYPE (MetaWindowWayland, meta_window_wayland,
                      META, WINDOW_WAYLAND,
                      MetaWindow)

MetaWindow * meta_window_wayland_new       (MetaDisplay        *display,
                                            MetaWaylandSurface *surface);

void meta_window_wayland_finish_move_resize (MetaWindow              *window,
                                             MetaRectangle            new_geom,
                                             MetaWaylandSurfaceState *pending);

int meta_window_wayland_get_geometry_scale (MetaWindow *window);

void meta_window_wayland_place_relative_to (MetaWindow *window,
                                            MetaWindow *other,
                                            int         x,
                                            int         y);

void meta_window_place_with_placement_rule (MetaWindow        *window,
                                            MetaPlacementRule *placement_rule);

void meta_window_update_placement_rule (MetaWindow        *window,
                                        MetaPlacementRule *placement_rule);

MetaWaylandWindowConfiguration *
  meta_window_wayland_peek_configuration (MetaWindowWayland *wl_window,
                                          uint32_t           serial);

void meta_window_wayland_set_min_size (MetaWindow *window,
                                       int         width,
                                       int         height);

void meta_window_wayland_set_max_size (MetaWindow *window,
                                       int         width,
                                       int         height);

void meta_window_wayland_get_min_size (MetaWindow *window,
                                       int        *width,
                                       int        *height);


void meta_window_wayland_get_max_size (MetaWindow *window,
                                       int        *width,
                                       int        *height);

#endif
