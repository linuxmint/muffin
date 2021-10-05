/*
 * Copyright (C) 2019 Red Hat
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
 */

#ifndef META_WAYLAND_WINDOW_CONFIGURATION_H
#define META_WAYLAND_WINDOW_CONFIGURATION_H

#include <glib.h>
#include <stdint.h>

#include "core/window-private.h"
#include "wayland/meta-wayland-types.h"

struct _MetaWaylandWindowConfiguration
{
  uint32_t serial;

  gboolean has_position;
  int x;
  int y;

  gboolean has_relative_position;
  int rel_x;
  int rel_y;

  gboolean has_size;
  int width;
  int height;

  int scale;
  MetaGravity gravity;
  MetaMoveResizeFlags flags;
};

MetaWaylandWindowConfiguration * meta_wayland_window_configuration_new (int                 x,
                                                                        int                 y,
                                                                        int                 width,
                                                                        int                 height,
                                                                        int                 scale,
                                                                        MetaMoveResizeFlags flags,
                                                                        MetaGravity         gravity);

MetaWaylandWindowConfiguration * meta_wayland_window_configuration_new_relative (int rel_x,
                                                                                 int rel_y,
                                                                                 int width,
                                                                                 int height,
                                                                                 int scale);

MetaWaylandWindowConfiguration * meta_wayland_window_configuration_new_empty (void);

void meta_wayland_window_configuration_free (MetaWaylandWindowConfiguration *configuration);

#endif /* META_WAYLAND_WINDOW_CONFIGURATION_H */
