/*
 * Wayland Support
 *
 * Copyright (C) 2013 Intel Corporation
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

#include "config.h"

#include "wayland/meta-wayland-seat.h"

#include "wayland/meta-wayland-data-device.h"
#include "wayland/meta-wayland-data-device-primary-legacy.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-tablet-seat.h"
#include "wayland/meta-wayland-versions.h"

#define CAPABILITY_ENABLED(prev, cur, capability) ((cur & (capability)) && !(prev & (capability)))
#define CAPABILITY_DISABLED(prev, cur, capability) ((prev & (capability)) && !(cur & (capability)))

static void
unbind_resource (struct wl_resource *resource)
{
  wl_list_remove (wl_resource_get_link (resource));
}

static void
seat_get_pointer (struct wl_client *client,
                  struct wl_resource *resource,
                  uint32_t id)
{
  MetaWaylandSeat *seat = wl_resource_get_user_data (resource);
  MetaWaylandPointer *pointer = seat->pointer;

  if (meta_wayland_seat_has_pointer (seat))
    meta_wayland_pointer_create_new_resource (pointer, client, resource, id);
}

static void
seat_get_keyboard (struct wl_client *client,
                   struct wl_resource *resource,
                   uint32_t id)
{
  MetaWaylandSeat *seat = wl_resource_get_user_data (resource);
  MetaWaylandKeyboard *keyboard = seat->keyboard;

  if (meta_wayland_seat_has_keyboard (seat))
    meta_wayland_keyboard_create_new_resource (keyboard, client, resource, id);
}

static void
seat_get_touch (struct wl_client *client,
                struct wl_resource *resource,
                uint32_t id)
{
  MetaWaylandSeat *seat = wl_resource_get_user_data (resource);
  MetaWaylandTouch *touch = seat->touch;

  if (meta_wayland_seat_has_touch (seat))
    meta_wayland_touch_create_new_resource (touch, client, resource, id);
}

static void
seat_release (struct wl_client   *client,
              struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct wl_seat_interface seat_interface = {
  seat_get_pointer,
  seat_get_keyboard,
  seat_get_touch,
  seat_release
};

static void
bind_seat (struct wl_client *client,
           void *data,
           guint32 version,
           guint32 id)
{
  MetaWaylandSeat *seat = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client, &wl_seat_interface, version, id);
  wl_resource_set_implementation (resource, &seat_interface, seat, unbind_resource);
  wl_list_insert (&seat->base_resource_list, wl_resource_get_link (resource));

  wl_seat_send_capabilities (resource, seat->capabilities);

  if (version >= WL_SEAT_NAME_SINCE_VERSION)
    wl_seat_send_name (resource, "seat0");
}

static uint32_t
lookup_device_capabilities (ClutterSeat *seat)
{
  GList *devices, *l;
  uint32_t capabilities = 0;

  devices = clutter_seat_list_devices (seat);

  for (l = devices; l; l = l->next)
    {
      ClutterInputDeviceType device_type;

      /* Only look for physical devices, master devices have rather generic
       * keyboard/pointer device types, which is not truly representative of
       * the slave devices connected to them.
       */
      if (clutter_input_device_get_device_mode (l->data) == CLUTTER_INPUT_MODE_MASTER)
        continue;

      device_type = clutter_input_device_get_device_type (l->data);

      switch (device_type)
        {
        case CLUTTER_TOUCHPAD_DEVICE:
        case CLUTTER_POINTER_DEVICE:
          capabilities |= WL_SEAT_CAPABILITY_POINTER;
          break;
        case CLUTTER_KEYBOARD_DEVICE:
          capabilities |= WL_SEAT_CAPABILITY_KEYBOARD;
          break;
        case CLUTTER_TOUCHSCREEN_DEVICE:
          capabilities |= WL_SEAT_CAPABILITY_TOUCH;
          break;
        default:
          g_debug ("Ignoring device '%s' with unhandled type %d",
                   clutter_input_device_get_device_name (l->data),
                   device_type);
          break;
        }
    }

  g_list_free (devices);

  return capabilities;
}

static void
meta_wayland_seat_set_capabilities (MetaWaylandSeat *seat,
                                    uint32_t         flags)
{
  struct wl_resource *resource;
  uint32_t prev_flags;

  prev_flags = seat->capabilities;

  if (prev_flags == flags)
    return;

  seat->capabilities = flags;

  if (CAPABILITY_ENABLED (prev_flags, flags, WL_SEAT_CAPABILITY_POINTER))
    meta_wayland_pointer_enable (seat->pointer);
  else if (CAPABILITY_DISABLED (prev_flags, flags, WL_SEAT_CAPABILITY_POINTER))
    meta_wayland_pointer_disable (seat->pointer);

  if (CAPABILITY_ENABLED (prev_flags, flags, WL_SEAT_CAPABILITY_KEYBOARD))
    {
      MetaDisplay *display;

      meta_wayland_keyboard_enable (seat->keyboard);
      display = meta_get_display ();

      /* Post-initialization, ensure the input focus is in sync */
      if (display)
        meta_display_sync_wayland_input_focus (display);
    }
  else if (CAPABILITY_DISABLED (prev_flags, flags, WL_SEAT_CAPABILITY_KEYBOARD))
    meta_wayland_keyboard_disable (seat->keyboard);

  if (CAPABILITY_ENABLED (prev_flags, flags, WL_SEAT_CAPABILITY_TOUCH))
    meta_wayland_touch_enable (seat->touch);
  else if (CAPABILITY_DISABLED (prev_flags, flags, WL_SEAT_CAPABILITY_TOUCH))
    meta_wayland_touch_disable (seat->touch);

  /* Broadcast capability changes */
  wl_resource_for_each (resource, &seat->base_resource_list)
    {
      wl_seat_send_capabilities (resource, flags);
    }
}

static void
meta_wayland_seat_update_capabilities (MetaWaylandSeat *seat,
				       ClutterSeat     *clutter_seat)
{
  uint32_t capabilities;

  capabilities = lookup_device_capabilities (clutter_seat);
  meta_wayland_seat_set_capabilities (seat, capabilities);
}

static void
meta_wayland_seat_devices_updated (ClutterSeat        *clutter_seat,
                                   ClutterInputDevice *input_device,
                                   MetaWaylandSeat    *seat)
{
  meta_wayland_seat_update_capabilities (seat, clutter_seat);
}

static MetaWaylandSeat *
meta_wayland_seat_new (MetaWaylandCompositor *compositor,
                       struct wl_display     *display)
{
  MetaWaylandSeat *seat = g_new0 (MetaWaylandSeat, 1);
  ClutterSeat *clutter_seat;

  wl_list_init (&seat->base_resource_list);
  seat->wl_display = display;

  seat->pointer = g_object_new (META_TYPE_WAYLAND_POINTER,
                                "seat", seat,
                                NULL);
  seat->keyboard = g_object_new (META_TYPE_WAYLAND_KEYBOARD,
                                 "seat", seat,
                                 NULL);
  seat->touch = g_object_new (META_TYPE_WAYLAND_TOUCH,
                              "seat", seat,
                              NULL);

  seat->text_input = meta_wayland_text_input_new (seat);
  seat->gtk_text_input = meta_wayland_gtk_text_input_new (seat);

  meta_wayland_data_device_init (&seat->data_device);
  meta_wayland_data_device_primary_init (&seat->primary_data_device);
  meta_wayland_data_device_primary_legacy_init (&seat->primary_legacy_data_device);

  clutter_seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  meta_wayland_seat_update_capabilities (seat, clutter_seat);
  g_signal_connect (clutter_seat, "device-added",
                    G_CALLBACK (meta_wayland_seat_devices_updated), seat);
  g_signal_connect (clutter_seat, "device-removed",
                    G_CALLBACK (meta_wayland_seat_devices_updated), seat);

  wl_global_create (display, &wl_seat_interface, META_WL_SEAT_VERSION, seat, bind_seat);

  meta_wayland_tablet_manager_ensure_seat (compositor->tablet_manager, seat);

  return seat;
}

void
meta_wayland_seat_init (MetaWaylandCompositor *compositor)
{
  compositor->seat = meta_wayland_seat_new (compositor,
                                            compositor->wayland_display);
}

void
meta_wayland_seat_free (MetaWaylandSeat *seat)
{
  ClutterSeat *clutter_seat;

  clutter_seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  g_signal_handlers_disconnect_by_data (clutter_seat, seat);
  meta_wayland_seat_set_capabilities (seat, 0);

  g_object_unref (seat->pointer);
  g_object_unref (seat->keyboard);
  g_object_unref (seat->touch);
  meta_wayland_gtk_text_input_destroy (seat->gtk_text_input);
  meta_wayland_text_input_destroy (seat->text_input);

  g_free (seat);
}

static gboolean
event_is_synthesized_crossing (const ClutterEvent *event)
{
  ClutterInputDevice *device;

  if (event->type != CLUTTER_ENTER && event->type != CLUTTER_LEAVE)
    return FALSE;

  device = clutter_event_get_source_device (event);
  return clutter_input_device_get_device_mode (device) == CLUTTER_INPUT_MODE_MASTER;
}

static gboolean
event_from_supported_hardware_device (MetaWaylandSeat    *seat,
                                      const ClutterEvent *event)
{
  ClutterInputDevice     *input_device;
  ClutterInputMode        input_mode;
  ClutterInputDeviceType  device_type;
  gboolean                hardware_device = FALSE;
  gboolean                supported_device = FALSE;

  input_device = clutter_event_get_source_device (event);

  if (input_device == NULL)
    goto out;

  input_mode = clutter_input_device_get_device_mode (input_device);

  if (input_mode != CLUTTER_INPUT_MODE_SLAVE)
    goto out;

  hardware_device = TRUE;

  device_type = clutter_input_device_get_device_type (input_device);

  switch (device_type)
    {
    case CLUTTER_TOUCHPAD_DEVICE:
    case CLUTTER_POINTER_DEVICE:
    case CLUTTER_KEYBOARD_DEVICE:
    case CLUTTER_TOUCHSCREEN_DEVICE:
      supported_device = TRUE;
      break;

    default:
      supported_device = FALSE;
      break;
    }

out:
  return hardware_device && supported_device;
}

void
meta_wayland_seat_update (MetaWaylandSeat    *seat,
                          const ClutterEvent *event)
{
  if (!(clutter_event_get_flags (event) & CLUTTER_EVENT_FLAG_INPUT_METHOD) &&
      !event_from_supported_hardware_device (seat, event) &&
      !event_is_synthesized_crossing (event))
    return;

  switch (event->type)
    {
    case CLUTTER_MOTION:
    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_BUTTON_RELEASE:
    case CLUTTER_SCROLL:
    case CLUTTER_ENTER:
    case CLUTTER_LEAVE:
      if (meta_wayland_seat_has_pointer (seat))
        meta_wayland_pointer_update (seat->pointer, event);
      break;

    case CLUTTER_KEY_PRESS:
    case CLUTTER_KEY_RELEASE:
      if (meta_wayland_seat_has_keyboard (seat))
        meta_wayland_keyboard_update (seat->keyboard, (const ClutterKeyEvent *) event);
      break;

    case CLUTTER_TOUCH_BEGIN:
    case CLUTTER_TOUCH_UPDATE:
    case CLUTTER_TOUCH_END:
      if (meta_wayland_seat_has_touch (seat))
        meta_wayland_touch_update (seat->touch, event);
      break;

    default:
      break;
    }
}

gboolean
meta_wayland_seat_handle_event (MetaWaylandSeat *seat,
                                const ClutterEvent *event)
{
  if (!(clutter_event_get_flags (event) & CLUTTER_EVENT_FLAG_INPUT_METHOD) &&
      !event_from_supported_hardware_device (seat, event))
    return FALSE;

  switch (event->type)
    {
    case CLUTTER_MOTION:
    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_BUTTON_RELEASE:
    case CLUTTER_SCROLL:
    case CLUTTER_TOUCHPAD_SWIPE:
    case CLUTTER_TOUCHPAD_PINCH:
      if (meta_wayland_seat_has_pointer (seat))
        return meta_wayland_pointer_handle_event (seat->pointer, event);

      break;
    case CLUTTER_KEY_PRESS:
    case CLUTTER_KEY_RELEASE:
      if (meta_wayland_text_input_handle_event (seat->text_input, event))
        return TRUE;

      if (meta_wayland_gtk_text_input_handle_event (seat->gtk_text_input,
                                                    event))
        return TRUE;

      if (meta_wayland_seat_has_keyboard (seat))
        return meta_wayland_keyboard_handle_event (seat->keyboard,
                                                   (const ClutterKeyEvent *) event);
      break;
    case CLUTTER_TOUCH_BEGIN:
    case CLUTTER_TOUCH_UPDATE:
    case CLUTTER_TOUCH_END:
      if (meta_wayland_seat_has_touch (seat))
        return meta_wayland_touch_handle_event (seat->touch, event);

      break;
    case CLUTTER_IM_COMMIT:
    case CLUTTER_IM_DELETE:
    case CLUTTER_IM_PREEDIT:
      if (meta_wayland_text_input_handle_event (seat->text_input, event))
        return TRUE;
      if (meta_wayland_gtk_text_input_handle_event (seat->gtk_text_input,
                                                    event))
        return TRUE;

      break;
    default:
      break;
    }

  return FALSE;
}

void
meta_wayland_seat_repick (MetaWaylandSeat *seat)
{
  if (!meta_wayland_seat_has_pointer (seat))
    return;

  meta_wayland_pointer_repick (seat->pointer);
}

void
meta_wayland_seat_set_input_focus (MetaWaylandSeat    *seat,
                                   MetaWaylandSurface *surface)
{
  MetaWaylandTabletSeat *tablet_seat;
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();

  if (meta_wayland_seat_has_keyboard (seat))
    {
      meta_wayland_keyboard_set_focus (seat->keyboard, surface);
      meta_wayland_data_device_set_keyboard_focus (&seat->data_device);
      meta_wayland_data_device_primary_set_keyboard_focus (&seat->primary_data_device);
      meta_wayland_data_device_primary_legacy_set_keyboard_focus (&seat->primary_legacy_data_device);
    }

  tablet_seat = meta_wayland_tablet_manager_ensure_seat (compositor->tablet_manager, seat);
  meta_wayland_tablet_seat_set_pad_focus (tablet_seat, surface);

  meta_wayland_text_input_set_focus (seat->text_input, surface);
  meta_wayland_gtk_text_input_set_focus (seat->gtk_text_input, surface);
}

gboolean
meta_wayland_seat_get_grab_info (MetaWaylandSeat    *seat,
                                 MetaWaylandSurface *surface,
                                 uint32_t            serial,
                                 gboolean            require_pressed,
                                 gfloat             *x,
                                 gfloat             *y)
{
  MetaWaylandCompositor *compositor;
  MetaWaylandTabletSeat *tablet_seat;
  GList *tools, *l;

  compositor = meta_wayland_compositor_get_default ();
  tablet_seat = meta_wayland_tablet_manager_ensure_seat (compositor->tablet_manager, seat);
  tools = g_hash_table_get_values (tablet_seat->tools);

  if (meta_wayland_seat_has_touch (seat))
    {
      ClutterEventSequence *sequence;
      sequence = meta_wayland_touch_find_grab_sequence (seat->touch,
                                                        surface,
                                                        serial);
      if (sequence)
        {
          meta_wayland_touch_get_press_coords (seat->touch, sequence, x, y);
          return TRUE;
        }
    }

  if (meta_wayland_seat_has_pointer (seat))
    {
      if ((!require_pressed || seat->pointer->button_count > 0) &&
          meta_wayland_pointer_can_grab_surface (seat->pointer, surface, serial))
        {
          if (x)
            *x = seat->pointer->grab_x;
          if (y)
            *y = seat->pointer->grab_y;

          return TRUE;
        }
    }

  for (l = tools; l; l = l->next)
    {
      MetaWaylandTabletTool *tool = l->data;

      if ((!require_pressed || tool->button_count > 0) &&
          meta_wayland_tablet_tool_can_grab_surface (tool, surface, serial))
        {
          if (x)
            *x = tool->grab_x;
          if (y)
            *y = tool->grab_y;

          return TRUE;
        }
    }

  return FALSE;
}

gboolean
meta_wayland_seat_can_popup (MetaWaylandSeat *seat,
                             uint32_t         serial)
{
  MetaWaylandCompositor *compositor;
  MetaWaylandTabletSeat *tablet_seat;

  compositor = meta_wayland_compositor_get_default ();
  tablet_seat =
    meta_wayland_tablet_manager_ensure_seat (compositor->tablet_manager, seat);

  return (meta_wayland_pointer_can_popup (seat->pointer, serial) ||
          meta_wayland_keyboard_can_popup (seat->keyboard, serial) ||
          meta_wayland_touch_can_popup (seat->touch, serial) ||
          meta_wayland_tablet_seat_can_popup (tablet_seat, serial));
}

gboolean
meta_wayland_seat_has_keyboard (MetaWaylandSeat *seat)
{
  return (seat->capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
}

gboolean
meta_wayland_seat_has_pointer (MetaWaylandSeat *seat)
{
  return (seat->capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
}

gboolean
meta_wayland_seat_has_touch (MetaWaylandSeat *seat)
{
  return (seat->capabilities & WL_SEAT_CAPABILITY_TOUCH) != 0;
}
