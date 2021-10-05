/*
 * Copyright © 2008 Kristian Høgsberg
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

#ifndef META_WAYLAND_DATA_DEVICE_H
#define META_WAYLAND_DATA_DEVICE_H

#include <glib-object.h>
#include <wayland-server.h>

#include "clutter/clutter.h"
#include "meta/meta-selection-source.h"
#include "wayland/meta-wayland-data-offer.h"
#include "wayland/meta-wayland-data-source.h"
#include "wayland/meta-wayland-types.h"

typedef struct _MetaWaylandDragGrab MetaWaylandDragGrab;
typedef struct _MetaWaylandDataSourceFuncs MetaWaylandDataSourceFuncs;

struct _MetaWaylandDataDevice
{
  uint32_t selection_serial;
  MetaWaylandDataSource *selection_data_source;
  MetaWaylandDataSource *dnd_data_source;
  struct wl_list resource_list;
  struct wl_list focus_resource_list;
  MetaWaylandDragGrab *current_grab;
  struct wl_client *focus_client;

  guint selection_owner_signal_id;

  MetaSelectionSource *owners[META_N_SELECTION_TYPES];
};

void meta_wayland_data_device_manager_init (MetaWaylandCompositor *compositor);

void meta_wayland_data_device_init (MetaWaylandDataDevice *data_device);

void meta_wayland_data_device_set_keyboard_focus (MetaWaylandDataDevice *data_device);

gboolean meta_wayland_data_device_is_dnd_surface (MetaWaylandDataDevice *data_device,
                                                  MetaWaylandSurface    *surface);

MetaWaylandDragGrab *
     meta_wayland_data_device_get_current_grab   (MetaWaylandDataDevice *data_device);

void meta_wayland_data_device_set_dnd_source     (MetaWaylandDataDevice *data_device,
                                                  MetaWaylandDataSource *source);
void meta_wayland_data_device_set_selection      (MetaWaylandDataDevice *data_device,
                                                  MetaWaylandDataSource *source,
                                                  guint32 serial);
void     meta_wayland_data_device_unset_dnd_selection (MetaWaylandDataDevice *data_device);

const MetaWaylandDragDestFuncs *
         meta_wayland_data_device_get_drag_dest_funcs (void);

void     meta_wayland_data_device_start_drag     (MetaWaylandDataDevice                 *data_device,
                                                  struct wl_client                      *client,
                                                  const MetaWaylandPointerGrabInterface *funcs,
                                                  MetaWaylandSurface                    *surface,
                                                  MetaWaylandDataSource                 *source,
                                                  MetaWaylandSurface                    *icon_surface);

void     meta_wayland_data_device_end_drag       (MetaWaylandDataDevice                 *data_device);

void     meta_wayland_drag_grab_set_focus        (MetaWaylandDragGrab             *drag_grab,
                                                  MetaWaylandSurface              *surface);
MetaWaylandSurface *
         meta_wayland_drag_grab_get_focus        (MetaWaylandDragGrab             *drag_grab);
void     meta_wayland_drag_grab_update_feedback_actor (MetaWaylandDragGrab *drag_grab,
                                                       ClutterEvent        *event);

#endif /* META_WAYLAND_DATA_DEVICE_H */
