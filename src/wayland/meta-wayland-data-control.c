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

#include "config.h"

#include "wayland/meta-wayland-data-control.h"

#include <gio/gunixoutputstream.h>
#include <glib-unix.h>
#include <unistd.h>

#include "meta/meta-selection-source-memory.h"
#include "wayland/meta-selection-source-wayland-private.h"
#include "wayland/meta-wayland-data-offer.h"
#include "wayland/meta-wayland-data-source.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-seat.h"

#include "ext-data-control-v1-server-protocol.h"

typedef struct _MetaWaylandDataSourceDataControl      MetaWaylandDataSourceDataControl;
typedef struct _MetaWaylandDataSourceDataControlClass MetaWaylandDataSourceDataControlClass;

struct _MetaWaylandDataSourceDataControl
{
  MetaWaylandDataSource parent;
};

struct _MetaWaylandDataSourceDataControlClass
{
  MetaWaylandDataSourceClass parent_class;
};

G_DEFINE_TYPE (MetaWaylandDataSourceDataControl,
               meta_wayland_data_source_data_control,
               META_TYPE_WAYLAND_DATA_SOURCE);

#define META_TYPE_WAYLAND_DATA_SOURCE_DATA_CONTROL (meta_wayland_data_source_data_control_get_type ())

static void
data_control_source_offer (struct wl_client   *client,
                           struct wl_resource *resource,
                           const char         *type)
{
  MetaWaylandDataSource *source = wl_resource_get_user_data (resource);

  if (!meta_wayland_data_source_add_mime_type (source, type))
    wl_resource_post_no_memory (resource);
}

static void
data_control_source_destroy (struct wl_client   *client,
                             struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct ext_data_control_source_v1_interface data_control_source_interface = {
  data_control_source_offer,
  data_control_source_destroy,
};

static void
destroy_data_control_source (struct wl_resource *resource)
{
  MetaWaylandDataSource *source = wl_resource_get_user_data (resource);

  meta_wayland_data_source_set_resource (source, NULL);
  g_object_unref (source);
}

static void
meta_wayland_data_source_data_control_send (MetaWaylandDataSource *source,
                                            const gchar           *mime_type,
                                            gint                   fd)
{
  struct wl_resource *resource = meta_wayland_data_source_get_resource (source);

  ext_data_control_source_v1_send_send (resource, mime_type, fd);
  close (fd);
}

static void
meta_wayland_data_source_data_control_cancel (MetaWaylandDataSource *source)
{
  struct wl_resource *resource = meta_wayland_data_source_get_resource (source);

  if (resource)
    ext_data_control_source_v1_send_cancelled (resource);
}

static void
meta_wayland_data_source_data_control_init (MetaWaylandDataSourceDataControl *source)
{
}

static void
meta_wayland_data_source_data_control_class_init (MetaWaylandDataSourceDataControlClass *klass)
{
  MetaWaylandDataSourceClass *data_source_class =
    META_WAYLAND_DATA_SOURCE_CLASS (klass);

  data_source_class->send = meta_wayland_data_source_data_control_send;
  data_source_class->cancel = meta_wayland_data_source_data_control_cancel;
}

static MetaWaylandDataSource *
meta_wayland_data_source_data_control_new (struct wl_resource *resource)
{
  MetaWaylandDataSource *source =
    g_object_new (META_TYPE_WAYLAND_DATA_SOURCE_DATA_CONTROL, NULL);

  meta_wayland_data_source_set_resource (source, resource);
  wl_resource_set_implementation (resource, &data_control_source_interface,
                                  source, destroy_data_control_source);

  return source;
}

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
clipboard_source_destroyed (gpointer  data,
                            GObject  *object_was_here)
{
  MetaWaylandDataControlDevice *data_control = data;
  MetaDisplay *display = meta_get_display ();

  data_control->clipboard_data_source = NULL;

  if (data_control->clipboard_owner)
    {
      meta_selection_unset_owner (meta_display_get_selection (display),
                                  META_SELECTION_CLIPBOARD,
                                  data_control->clipboard_owner);
      g_clear_object (&data_control->clipboard_owner);
    }
}

static void
primary_source_destroyed (gpointer  data,
                          GObject  *object_was_here)
{
  MetaWaylandDataControlDevice *data_control = data;
  MetaDisplay *display = meta_get_display ();

  data_control->primary_data_source = NULL;

  if (data_control->primary_owner)
    {
      meta_selection_unset_owner (meta_display_get_selection (display),
                                  META_SELECTION_PRIMARY,
                                  data_control->primary_owner);
      g_clear_object (&data_control->primary_owner);
    }
}

static struct wl_resource *
create_and_send_offer (MetaWaylandDataControlDevice *data_control,
                       struct wl_resource           *target,
                       MetaSelectionType             selection_type)
{
  MetaWaylandDataOffer *offer;
  MetaDisplay *display = meta_get_display ();
  struct wl_resource *resource;
  GList *mimetypes, *l;

  mimetypes = meta_selection_get_mimetypes (meta_display_get_selection (display),
                                            selection_type);
  if (!mimetypes)
    return NULL;

  offer = g_slice_new0 (MetaWaylandDataOffer);
  offer->selection_type = selection_type;
  offer->resource = wl_resource_create (wl_resource_get_client (target),
                                        &ext_data_control_offer_v1_interface,
                                        wl_resource_get_version (target), 0);

  {
    static const struct ext_data_control_offer_v1_interface offer_interface = {
      NULL, /* receive - handled below */
      default_destructor,
    };
    wl_resource_set_implementation (offer->resource,
                                    &offer_interface,
                                    offer, NULL);
  }

  resource = offer->resource;

  ext_data_control_device_v1_send_data_offer (target, resource);

  for (l = mimetypes; l; l = l->next)
    ext_data_control_offer_v1_send_offer (resource, l->data);

  g_list_free_full (mimetypes, g_free);

  return resource;
}

static void
clipboard_owner_changed_cb (MetaSelection                *selection,
                            MetaSelectionType             selection_type,
                            MetaSelectionSource          *new_owner,
                            MetaWaylandDataControlDevice *data_control)
{
  struct wl_resource *resource;

  if (selection_type != META_SELECTION_CLIPBOARD)
    return;

  wl_resource_for_each (resource, &data_control->focus_resource_list)
    {
      struct wl_resource *offer = NULL;

      if (new_owner)
        {
          offer = create_and_send_offer (data_control, resource,
                                         META_SELECTION_CLIPBOARD);
        }

      ext_data_control_device_v1_send_selection (resource, offer);
    }
}

static void
primary_owner_changed_cb (MetaSelection                *selection,
                          MetaSelectionType             selection_type,
                          MetaSelectionSource          *new_owner,
                          MetaWaylandDataControlDevice *data_control)
{
  struct wl_resource *resource;

  if (selection_type != META_SELECTION_PRIMARY)
    return;

  wl_resource_for_each (resource, &data_control->focus_resource_list)
    {
      struct wl_resource *offer = NULL;

      if (new_owner)
        {
          offer = create_and_send_offer (data_control, resource,
                                         META_SELECTION_PRIMARY);
        }

      ext_data_control_device_v1_send_primary_selection (resource, offer);
    }
}

static void
ensure_owners_changed_handler_connected (MetaWaylandDataControlDevice *data_control)
{
  if (data_control->clipboard_selection_owner_signal_id != 0)
    return;

  data_control->clipboard_selection_owner_signal_id =
    g_signal_connect (meta_display_get_selection (meta_get_display ()),
                      "owner-changed",
                      G_CALLBACK (clipboard_owner_changed_cb), data_control);
}

static void
ensure_primary_owners_changed_handler_connected (MetaWaylandDataControlDevice *data_control)
{
  if (data_control->primary_selection_owner_signal_id != 0)
    return;

  data_control->primary_selection_owner_signal_id =
    g_signal_connect (meta_display_get_selection (meta_get_display ()),
                      "owner-changed",
                      G_CALLBACK (primary_owner_changed_cb), data_control);
}

static void
data_control_device_set_selection (struct wl_client   *client,
                                   struct wl_resource *resource,
                                   struct wl_resource *source_resource)
{
  MetaWaylandDataControlDevice *data_control =
    wl_resource_get_user_data (resource);
  MetaWaylandSeat *seat = wl_container_of (data_control, seat, data_control_device);
  MetaWaylandDataSource *source = NULL;
  MetaDisplay *display = meta_get_display ();

  if (source_resource)
    source = wl_resource_get_user_data (source_resource);

  if (wl_resource_get_client (resource) !=
      meta_wayland_keyboard_get_focus_client (seat->keyboard))
    return;

  if (data_control->clipboard_data_source)
    {
      g_object_weak_unref (G_OBJECT (data_control->clipboard_data_source),
                           clipboard_source_destroyed,
                           data_control);
      data_control->clipboard_data_source = NULL;
    }

  if (data_control->clipboard_owner)
    {
      meta_selection_unset_owner (meta_display_get_selection (display),
                                  META_SELECTION_CLIPBOARD,
                                  data_control->clipboard_owner);
      g_clear_object (&data_control->clipboard_owner);
    }

  data_control->clipboard_data_source = source;

  if (source)
    {
      meta_wayland_data_source_set_seat (source, seat);
      g_object_weak_ref (G_OBJECT (source),
                         clipboard_source_destroyed,
                         data_control);

      data_control->clipboard_owner = meta_selection_source_wayland_new (source);
      meta_selection_set_owner (meta_display_get_selection (display),
                                META_SELECTION_CLIPBOARD,
                                data_control->clipboard_owner);
    }
  else
    {
      data_control->clipboard_owner =
        g_object_new (META_TYPE_SELECTION_SOURCE_MEMORY, NULL);
      meta_selection_set_owner (meta_display_get_selection (display),
                                META_SELECTION_CLIPBOARD,
                                data_control->clipboard_owner);
      g_clear_object (&data_control->clipboard_owner);
    }

  ensure_owners_changed_handler_connected (data_control);
}

static void
data_control_device_set_primary_selection (struct wl_client   *client,
                                           struct wl_resource *resource,
                                           struct wl_resource *source_resource)
{
  MetaWaylandDataControlDevice *data_control =
    wl_resource_get_user_data (resource);
  MetaWaylandSeat *seat = wl_container_of (data_control, seat, data_control_device);
  MetaWaylandDataSource *source = NULL;
  MetaDisplay *display = meta_get_display ();

  if (source_resource)
    source = wl_resource_get_user_data (source_resource);

  if (wl_resource_get_client (resource) !=
      meta_wayland_keyboard_get_focus_client (seat->keyboard))
    return;

  if (data_control->primary_data_source)
    {
      g_object_weak_unref (G_OBJECT (data_control->primary_data_source),
                           primary_source_destroyed,
                           data_control);
      data_control->primary_data_source = NULL;
    }

  if (data_control->primary_owner)
    {
      meta_selection_unset_owner (meta_display_get_selection (display),
                                  META_SELECTION_PRIMARY,
                                  data_control->primary_owner);
      g_clear_object (&data_control->primary_owner);
    }

  data_control->primary_data_source = source;

  if (source)
    {
      meta_wayland_data_source_set_seat (source, seat);
      g_object_weak_ref (G_OBJECT (source),
                         primary_source_destroyed,
                         data_control);

      data_control->primary_owner = meta_selection_source_wayland_new (source);
      meta_selection_set_owner (meta_display_get_selection (display),
                                META_SELECTION_PRIMARY,
                                data_control->primary_owner);
    }
  else
    {
      data_control->primary_owner =
        g_object_new (META_TYPE_SELECTION_SOURCE_MEMORY, NULL);
      meta_selection_set_owner (meta_display_get_selection (display),
                                META_SELECTION_PRIMARY,
                                data_control->primary_owner);
      g_clear_object (&data_control->primary_owner);
    }

  ensure_primary_owners_changed_handler_connected (data_control);
}

static const struct ext_data_control_device_v1_interface data_control_device_interface = {
  data_control_device_set_selection,
  default_destructor,
  data_control_device_set_primary_selection,
};

static void
data_control_manager_create_source (struct wl_client   *client,
                                    struct wl_resource *manager_resource,
                                    guint32             id)
{
  struct wl_resource *source_resource;

  source_resource =
    wl_resource_create (client, &ext_data_control_source_v1_interface,
                        wl_resource_get_version (manager_resource), id);
  meta_wayland_data_source_data_control_new (source_resource);
}

static void
data_control_manager_get_device (struct wl_client   *client,
                                 struct wl_resource *manager_resource,
                                 guint32             id,
                                 struct wl_resource *seat_resource)
{
  MetaWaylandSeat *seat = wl_resource_get_user_data (seat_resource);
  struct wl_resource *cr;

  cr = wl_resource_create (client, &ext_data_control_device_v1_interface,
                           wl_resource_get_version (manager_resource), id);
  wl_resource_set_implementation (cr, &data_control_device_interface,
                                  &seat->data_control_device, unbind_resource);
  wl_list_insert (&seat->data_control_device.resource_list,
                  wl_resource_get_link (cr));

  ensure_owners_changed_handler_connected (&seat->data_control_device);
  ensure_primary_owners_changed_handler_connected (&seat->data_control_device);
}

static const struct ext_data_control_manager_v1_interface data_control_manager_interface = {
  data_control_manager_create_source,
  data_control_manager_get_device,
  default_destructor,
};

static void
bind_data_control_manager (struct wl_client *client,
                           void             *data,
                           uint32_t          version,
                           uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &ext_data_control_manager_v1_interface,
                                 version, id);
  wl_resource_set_implementation (resource, &data_control_manager_interface,
                                  NULL, NULL);
}

void
meta_wayland_data_control_manager_init (MetaWaylandCompositor *compositor)
{
  if (wl_global_create (compositor->wayland_display,
                        &ext_data_control_manager_v1_interface,
                        META_EXT_DATA_CONTROL_MANAGER_V1_VERSION,
                        NULL, bind_data_control_manager) == NULL)
    g_error ("Could not create ext_data_control_manager");
}

void
meta_wayland_data_control_device_init (MetaWaylandDataControlDevice *data_control)
{
  wl_list_init (&data_control->resource_list);
  wl_list_init (&data_control->focus_resource_list);
}

void
meta_wayland_data_control_device_set_keyboard_focus (MetaWaylandDataControlDevice *data_control)
{
  MetaWaylandSeat *seat = wl_container_of (data_control, seat, data_control_device);
  struct wl_client *focus_client;
  struct wl_resource *resource;

  focus_client = meta_wayland_keyboard_get_focus_client (seat->keyboard);

  if (focus_client == data_control->focus_client)
    return;

  data_control->focus_client = focus_client;
  move_resources (&data_control->resource_list,
                  &data_control->focus_resource_list);

  if (!focus_client)
    return;

  move_resources_for_client (&data_control->focus_resource_list,
                             &data_control->resource_list,
                             focus_client);

  wl_resource_for_each (resource, &data_control->focus_resource_list)
    {
      struct wl_resource *clipboard_offer = NULL;
      struct wl_resource *primary_offer = NULL;
      GList *mimetypes;

      mimetypes = meta_selection_get_mimetypes (meta_display_get_selection (meta_get_display ()),
                                                META_SELECTION_CLIPBOARD);
      if (mimetypes)
        {
          clipboard_offer = create_and_send_offer (data_control, resource,
                                                   META_SELECTION_CLIPBOARD);
          g_list_free_full (mimetypes, g_free);
        }

      ext_data_control_device_v1_send_selection (resource, clipboard_offer);

      mimetypes = meta_selection_get_mimetypes (meta_display_get_selection (meta_get_display ()),
                                                META_SELECTION_PRIMARY);
      if (mimetypes)
        {
          primary_offer = create_and_send_offer (data_control, resource,
                                                 META_SELECTION_PRIMARY);
          g_list_free_full (mimetypes, g_free);
        }

      ext_data_control_device_v1_send_primary_selection (resource, primary_offer);
    }
}
