/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2024 Linux Mint
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *   Michael Webster <miketwebster@gmail.com>
 */

#include "clutter-build-config.h"

#include "clutter-seat-wayland-client.h"
#include "clutter-keymap-wayland-client.h"

G_DEFINE_TYPE (ClutterSeatWaylandClient,
               clutter_seat_wayland_client,
               CLUTTER_TYPE_SEAT)

static ClutterInputDevice *
clutter_seat_wayland_client_get_pointer (ClutterSeat *seat)
{
    /* No pointer device yet - input handling not implemented */
    return NULL;
}

static ClutterInputDevice *
clutter_seat_wayland_client_get_keyboard (ClutterSeat *seat)
{
    /* No keyboard device yet - input handling not implemented */
    return NULL;
}

static GList *
clutter_seat_wayland_client_list_devices (ClutterSeat *seat)
{
    /* No devices yet - input handling not implemented */
    return NULL;
}

static void
clutter_seat_wayland_client_bell_notify (ClutterSeat *seat)
{
    /* No bell support */
}

static ClutterKeymap *
clutter_seat_wayland_client_get_keymap (ClutterSeat *seat)
{
    ClutterSeatWaylandClient *seat_wl = CLUTTER_SEAT_WAYLAND_CLIENT (seat);

    if (!seat_wl->keymap)
        seat_wl->keymap = clutter_keymap_wayland_client_new ();

    return seat_wl->keymap;
}

static void
clutter_seat_wayland_client_compress_motion (ClutterSeat        *seat,
                                             ClutterEvent       *event,
                                             const ClutterEvent *to_discard)
{
    /* No motion compression */
}

static gboolean
clutter_seat_wayland_client_handle_device_event (ClutterSeat  *seat,
                                                 ClutterEvent *event)
{
    return FALSE;
}

static void
clutter_seat_wayland_client_warp_pointer (ClutterSeat *seat,
                                          int          x,
                                          int          y)
{
    /* Cannot warp pointer as Wayland client */
}

static void
clutter_seat_wayland_client_copy_event_data (ClutterSeat        *seat,
                                             const ClutterEvent *src,
                                             ClutterEvent       *dest)
{
    /* No special event data to copy */
}

static void
clutter_seat_wayland_client_free_event_data (ClutterSeat  *seat,
                                             ClutterEvent *event)
{
    /* No special event data to free */
}

static void
clutter_seat_wayland_client_apply_kbd_a11y_settings (ClutterSeat            *seat,
                                                     ClutterKbdA11ySettings *settings)
{
    /* No keyboard a11y support yet */
}

static ClutterVirtualInputDevice *
clutter_seat_wayland_client_create_virtual_device (ClutterSeat            *seat,
                                                   ClutterInputDeviceType  device_type)
{
    /* No virtual input device support */
    return NULL;
}

static ClutterVirtualDeviceType
clutter_seat_wayland_client_get_supported_virtual_device_types (ClutterSeat *seat)
{
    return CLUTTER_VIRTUAL_DEVICE_TYPE_NONE;
}

static void
clutter_seat_wayland_client_finalize (GObject *object)
{
    ClutterSeatWaylandClient *seat_wl = CLUTTER_SEAT_WAYLAND_CLIENT (object);

    g_clear_object (&seat_wl->keymap);

    G_OBJECT_CLASS (clutter_seat_wayland_client_parent_class)->finalize (object);
}

static void
clutter_seat_wayland_client_class_init (ClutterSeatWaylandClientClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    ClutterSeatClass *seat_class = CLUTTER_SEAT_CLASS (klass);

    object_class->finalize = clutter_seat_wayland_client_finalize;

    seat_class->get_pointer = clutter_seat_wayland_client_get_pointer;
    seat_class->get_keyboard = clutter_seat_wayland_client_get_keyboard;
    seat_class->list_devices = clutter_seat_wayland_client_list_devices;
    seat_class->bell_notify = clutter_seat_wayland_client_bell_notify;
    seat_class->get_keymap = clutter_seat_wayland_client_get_keymap;
    seat_class->compress_motion = clutter_seat_wayland_client_compress_motion;
    seat_class->handle_device_event = clutter_seat_wayland_client_handle_device_event;
    seat_class->warp_pointer = clutter_seat_wayland_client_warp_pointer;
    seat_class->copy_event_data = clutter_seat_wayland_client_copy_event_data;
    seat_class->free_event_data = clutter_seat_wayland_client_free_event_data;
    seat_class->apply_kbd_a11y_settings = clutter_seat_wayland_client_apply_kbd_a11y_settings;
    seat_class->create_virtual_device = clutter_seat_wayland_client_create_virtual_device;
    seat_class->get_supported_virtual_device_types = clutter_seat_wayland_client_get_supported_virtual_device_types;
}

static void
clutter_seat_wayland_client_init (ClutterSeatWaylandClient *seat)
{
}

ClutterSeat *
clutter_seat_wayland_client_new (void)
{
    return g_object_new (CLUTTER_TYPE_SEAT_WAYLAND_CLIENT, NULL);
}
