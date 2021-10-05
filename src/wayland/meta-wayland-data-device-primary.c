/*
 * Copyright © 2011 Kristian Høgsberg
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

/* The file is based on src/data-device.c from Weston */

#include "config.h"

#include "wayland/meta-wayland-data-device-primary.h"

#include "compositor/meta-dnd-actor-private.h"
#include "meta/meta-selection-source-memory.h"
#include "wayland/meta-selection-source-wayland-private.h"
#include "wayland/meta-wayland-data-offer-primary.h"
#include "wayland/meta-wayland-data-source-primary.h"
#include "wayland/meta-wayland-dnd-surface.h"
#include "wayland/meta-wayland-pointer.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-seat.h"

#include "primary-selection-unstable-v1-server-protocol.h"

static struct wl_resource * create_and_send_primary_offer   (MetaWaylandDataDevicePrimary *data_device,
                                                             struct wl_resource           *target);

static void
move_resources (struct wl_list *destination,
                struct wl_list *source)
{
  wl_list_insert_list (destination, source);
  wl_list_init (source);
}

static void
move_resources_for_client (struct wl_list   *destination,
			   struct wl_list   *source,
			   struct wl_client *client)
{
  struct wl_resource *resource, *tmp;
  wl_resource_for_each_safe (resource, tmp, source)
    {
      if (wl_resource_get_client (resource) == client)
        {
          wl_list_remove (wl_resource_get_link (resource));
          wl_list_insert (destination, wl_resource_get_link (resource));
        }
    }
}

static void
unbind_resource (struct wl_resource *resource)
{
  wl_list_remove (wl_resource_get_link (resource));
}

static void
default_destructor (struct wl_client   *client,
                    struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
set_selection_source (MetaWaylandDataDevicePrimary *data_device,
                      MetaSelectionSource          *selection_source)

{
  MetaDisplay *display = meta_get_display ();

  meta_selection_set_owner (meta_display_get_selection (display),
                            META_SELECTION_PRIMARY,
                            selection_source);
  g_set_object (&data_device->owner, selection_source);
}

static void
unset_selection_source (MetaWaylandDataDevicePrimary *data_device)
{
  MetaDisplay *display = meta_get_display ();

  if (!data_device->owner)
    return;

  meta_selection_unset_owner (meta_display_get_selection (display),
                              META_SELECTION_PRIMARY,
                              data_device->owner);
  g_clear_object (&data_device->owner);
}

static void
primary_source_destroyed (gpointer  data,
                          GObject  *object_was_here)
{
  MetaWaylandDataDevicePrimary *data_device = data;

  data_device->data_source = NULL;
  unset_selection_source (data_device);
}

static void
meta_wayland_data_device_primary_set_selection (MetaWaylandDataDevicePrimary *data_device,
                                                MetaWaylandDataSource        *source,
                                                uint32_t                      serial)
{
  MetaWaylandSeat *seat = wl_container_of (data_device, seat, primary_data_device);
  MetaSelectionSource *selection_source;

  g_assert (!source || META_IS_WAYLAND_DATA_SOURCE_PRIMARY (source));

  if (data_device->data_source &&
      data_device->serial - serial < UINT32_MAX / 2)
    return;

  if (data_device->data_source)
    {
      g_object_weak_unref (G_OBJECT (data_device->data_source),
                           primary_source_destroyed,
                           data_device);
    }

  data_device->data_source = source;
  data_device->serial = serial;

  if (source)
    {
      meta_wayland_data_source_set_seat (source, seat);
      g_object_weak_ref (G_OBJECT (source),
                         primary_source_destroyed,
                         data_device);

      selection_source = meta_selection_source_wayland_new (source);
    }
  else
    {
      selection_source = g_object_new (META_TYPE_SELECTION_SOURCE_MEMORY, NULL);
    }

  set_selection_source (data_device, selection_source);
  g_object_unref (selection_source);
}

static void
primary_device_set_selection (struct wl_client   *client,
                              struct wl_resource *resource,
                              struct wl_resource *source_resource,
                              uint32_t            serial)
{
  MetaWaylandDataDevicePrimary *data_device = wl_resource_get_user_data (resource);
  MetaWaylandSeat *seat = wl_container_of (data_device, seat, primary_data_device);
  MetaWaylandDataSource *source = NULL;

  if (source_resource)
    source = wl_resource_get_user_data (source_resource);

  if (wl_resource_get_client (resource) !=
      meta_wayland_keyboard_get_focus_client (seat->keyboard))
    return;

  meta_wayland_data_device_primary_set_selection (data_device, source, serial);
}

static const struct zwp_primary_selection_device_v1_interface primary_device_interface = {
  primary_device_set_selection,
  default_destructor,
};

static void
owner_changed_cb (MetaSelection                *selection,
                  MetaSelectionType             selection_type,
                  MetaSelectionSource          *new_owner,
                  MetaWaylandDataDevicePrimary *data_device)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaWaylandSeat *seat = compositor->seat;
  struct wl_resource *data_device_resource;
  struct wl_client *focus_client;

  focus_client = meta_wayland_keyboard_get_focus_client (seat->keyboard);
  if (!focus_client)
    return;

  if (selection_type == META_SELECTION_PRIMARY)
    {
      wl_resource_for_each (data_device_resource, &data_device->focus_resource_list)
        {
          struct wl_resource *offer = NULL;

          if (new_owner)
            {
              offer = create_and_send_primary_offer (data_device,
                                                     data_device_resource);
            }

          zwp_primary_selection_device_v1_send_selection (data_device_resource,
                                                          offer);
        }
    }
}

static void
ensure_owners_changed_handler_connected (MetaWaylandDataDevicePrimary *data_device)
{
  if (data_device->selection_owner_signal_id != 0)
    return;

  data_device->selection_owner_signal_id =
    g_signal_connect (meta_display_get_selection (meta_get_display ()),
                      "owner-changed",
                      G_CALLBACK (owner_changed_cb), data_device);
}

static void
primary_device_manager_create_source (struct wl_client   *client,
                                      struct wl_resource *manager_resource,
                                      guint32             id)
{
  struct wl_resource *source_resource;

  source_resource =
    wl_resource_create (client, &zwp_primary_selection_source_v1_interface,
                        wl_resource_get_version (manager_resource),
                        id);
  meta_wayland_data_source_primary_new (source_resource);
}

static void
primary_device_manager_get_device (struct wl_client   *client,
                                   struct wl_resource *manager_resource,
                                   guint32             id,
                                   struct wl_resource *seat_resource)
{
  MetaWaylandSeat *seat = wl_resource_get_user_data (seat_resource);
  struct wl_resource *cr;

  cr = wl_resource_create (client, &zwp_primary_selection_device_v1_interface,
                           wl_resource_get_version (manager_resource), id);
  wl_resource_set_implementation (cr, &primary_device_interface,
                                  &seat->primary_data_device, unbind_resource);
  wl_list_insert (&seat->primary_data_device.resource_list, wl_resource_get_link (cr));

  ensure_owners_changed_handler_connected (&seat->primary_data_device);
}

static const struct zwp_primary_selection_device_manager_v1_interface primary_manager_interface = {
  primary_device_manager_create_source,
  primary_device_manager_get_device,
  default_destructor,
};

static void
bind_primary_manager (struct wl_client *client,
                      void             *data,
                      uint32_t          version,
                      uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client, &zwp_primary_selection_device_manager_v1_interface,
                                 version, id);
  wl_resource_set_implementation (resource, &primary_manager_interface, NULL, NULL);
}

void
meta_wayland_data_device_primary_manager_init (MetaWaylandCompositor *compositor)
{
  if (wl_global_create (compositor->wayland_display,
			&zwp_primary_selection_device_manager_v1_interface,
			1, NULL, bind_primary_manager) == NULL)
    g_error ("Could not create data_device");
}

void
meta_wayland_data_device_primary_init (MetaWaylandDataDevicePrimary *data_device)
{
  wl_list_init (&data_device->resource_list);
  wl_list_init (&data_device->focus_resource_list);
}

static struct wl_resource *
create_and_send_primary_offer (MetaWaylandDataDevicePrimary *data_device,
                               struct wl_resource           *target)
{
  MetaWaylandDataOffer *offer;
  MetaDisplay *display = meta_get_display ();
  struct wl_resource *resource;
  GList *mimetypes, *l;

  mimetypes = meta_selection_get_mimetypes (meta_display_get_selection (display),
                                            META_SELECTION_PRIMARY);
  if (!mimetypes)
    return NULL;

  offer = meta_wayland_data_offer_primary_new (target);
  resource = meta_wayland_data_offer_get_resource (offer);

  zwp_primary_selection_device_v1_send_data_offer (target, resource);

  for (l = mimetypes; l; l = l->next)
    zwp_primary_selection_offer_v1_send_offer (resource, l->data);

  g_list_free_full (mimetypes, g_free);

  return resource;
}

void
meta_wayland_data_device_primary_set_keyboard_focus (MetaWaylandDataDevicePrimary *data_device)
{
  MetaWaylandSeat *seat = wl_container_of (data_device, seat, primary_data_device);
  struct wl_client *focus_client;
  struct wl_resource *data_device_resource;

  focus_client = meta_wayland_keyboard_get_focus_client (seat->keyboard);

  if (focus_client == data_device->focus_client)
    return;

  data_device->focus_client = focus_client;
  move_resources (&data_device->resource_list,
                  &data_device->focus_resource_list);

  if (!focus_client)
    return;

  move_resources_for_client (&data_device->focus_resource_list,
                             &data_device->resource_list,
                             focus_client);

  wl_resource_for_each (data_device_resource, &data_device->focus_resource_list)
    {
      struct wl_resource *offer;
      offer = create_and_send_primary_offer (data_device, data_device_resource);
      zwp_primary_selection_device_v1_send_selection (data_device_resource, offer);
    }
}
