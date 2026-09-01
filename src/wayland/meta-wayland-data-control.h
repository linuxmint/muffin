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

#ifndef META_WAYLAND_DATA_CONTROL_H
#define META_WAYLAND_DATA_CONTROL_H

#include <glib-object.h>
#include <wayland-server.h>

#include "meta/meta-selection-source.h"
#include "wayland/meta-wayland-data-offer.h"
#include "wayland/meta-wayland-data-source.h"
#include "wayland/meta-wayland-types.h"

struct _MetaWaylandDataControlDevice
{
  struct wl_list resource_list;
  struct wl_list focus_resource_list;
  struct wl_client *focus_client;

  MetaWaylandDataSource *clipboard_data_source;
  guint clipboard_selection_owner_signal_id;
  MetaSelectionSource *clipboard_owner;

  MetaWaylandDataSource *primary_data_source;
  guint primary_selection_owner_signal_id;
  MetaSelectionSource *primary_owner;
};

void meta_wayland_data_control_manager_init (MetaWaylandCompositor *compositor);

void meta_wayland_data_control_device_init (MetaWaylandDataControlDevice *data_control);

void meta_wayland_data_control_device_set_keyboard_focus (MetaWaylandDataControlDevice *data_control);

#endif /* META_WAYLAND_DATA_CONTROL_H */
