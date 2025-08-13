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

#include "config.h"

#include "wayland/meta-wayland-window-configuration.h"

static uint32_t global_serial_counter = 0;

static gboolean
is_window_size_fixed (MetaWindow *window)
{
  if (meta_window_is_fullscreen (window))
    return TRUE;

  if (meta_window_get_maximized (window) |
      (META_MAXIMIZE_VERTICAL | META_MAXIMIZE_VERTICAL))
    return TRUE;

  if (meta_window_get_tile_mode (window) != META_TILE_NONE)
    return TRUE;

  return FALSE;
}

MetaWaylandWindowConfiguration *
meta_wayland_window_configuration_new (MetaWindow          *window,
                                       MetaRectangle        rect,
                                       int                  bounds_width,
                                       int                  bounds_height,
                                       int                  scale,
                                       MetaMoveResizeFlags  flags,
                                       MetaGravity          gravity)
{
  MetaWaylandWindowConfiguration *configuration;

  configuration = g_new0 (MetaWaylandWindowConfiguration, 1);
  *configuration = (MetaWaylandWindowConfiguration) {
    .serial = ++global_serial_counter,

    .bounds_height = bounds_height,
    .bounds_width = bounds_width,

    .scale = scale,
    .gravity = gravity,
    .flags = flags,
  };

  if (flags & META_MOVE_RESIZE_MOVE_ACTION ||
      window->rect.x != rect.x ||
      window->rect.y != rect.y)
    {
      configuration->has_position = TRUE;
      configuration->x = rect.x;
      configuration->y = rect.y;
    }

  if (flags & META_MOVE_RESIZE_RESIZE_ACTION ||
      is_window_size_fixed (window) ||
      window->rect.width != rect.width ||
      window->rect.height != rect.height)
    {
      configuration->has_size = TRUE;
      configuration->width = rect.width;
      configuration->height = rect.height;
    }

  return configuration;
}

MetaWaylandWindowConfiguration *
meta_wayland_window_configuration_new_relative (int rel_x,
                                                int rel_y,
                                                int width,
                                                int height,
                                                int scale)
{
  MetaWaylandWindowConfiguration *configuration;

  configuration = g_new0 (MetaWaylandWindowConfiguration, 1);
  *configuration = (MetaWaylandWindowConfiguration) {
    .serial = ++global_serial_counter,

    .has_relative_position = TRUE,
    .rel_x = rel_x,
    .rel_y = rel_y,

    .has_size = TRUE,
    .width = width,
    .height = height,

    .scale = scale,
  };

  return configuration;
}

MetaWaylandWindowConfiguration *
meta_wayland_window_configuration_new_empty (int bounds_width,
                                             int bounds_height)
{
  MetaWaylandWindowConfiguration *configuration;

  configuration = g_new0 (MetaWaylandWindowConfiguration, 1);
  *configuration = (MetaWaylandWindowConfiguration) {
    .serial = ++global_serial_counter,
    .scale = 1,
    .bounds_width = bounds_width,
    .bounds_height = bounds_height,
  };

  return configuration;
}

void
meta_wayland_window_configuration_free (MetaWaylandWindowConfiguration *configuration)
{
  g_free (configuration);
}
