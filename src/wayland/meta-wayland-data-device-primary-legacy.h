/*
 * Copyright © 2008 Kristian Høgsberg
 *             2020 Red Hat Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifndef META_WAYLAND_DATA_DEVICE_PRIMARY_LEGACY_H
#define META_WAYLAND_DATA_DEVICE_PRIMARY_LEGACY_H

#include <glib-object.h>
#include <wayland-server.h>

#include "clutter/clutter.h"
#include "meta/meta-selection-source.h"
#include "wayland/meta-wayland-data-offer.h"
#include "wayland/meta-wayland-data-source.h"
#include "wayland/meta-wayland-types.h"

struct _MetaWaylandDataDevicePrimaryLegacy
{
  uint32_t serial;
  MetaWaylandDataSource *data_source;
  struct wl_list resource_list;
  struct wl_list focus_resource_list;
  struct wl_client *focus_client;

  guint selection_owner_signal_id;

  MetaSelectionSource *owner;
};

void meta_wayland_data_device_primary_legacy_manager_init (MetaWaylandCompositor *compositor);

void meta_wayland_data_device_primary_legacy_init (MetaWaylandDataDevicePrimaryLegacy *data_device);

void meta_wayland_data_device_primary_legacy_set_keyboard_focus (MetaWaylandDataDevicePrimaryLegacy *data_device);

#endif /* META_WAYLAND_DATA_DEVICE_PRIMARY_LEGACY_H */
