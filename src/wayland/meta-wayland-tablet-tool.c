/*
 * Wayland Support
 *
 * Copyright (C) 2015 Red Hat
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "wayland/meta-wayland-tablet-tool.h"

#include <glib.h>
#include <wayland-server.h>

#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-tablet-cursor-surface.h"
#include "compositor/meta-surface-actor-wayland.h"
#include "wayland/meta-wayland-tablet.h"
#include "wayland/meta-wayland-tablet-seat.h"
#include "backends/meta-input-settings-private.h"
#include "backends/meta-logical-monitor.h"

#ifdef HAVE_NATIVE_BACKEND
#include <linux/input-event-codes.h>

#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-event-native.h"
#endif

#include "tablet-unstable-v2-server-protocol.h"

#define TABLET_AXIS_MAX 65535

static void
unbind_resource (struct wl_resource *resource)
{
  wl_list_remove (wl_resource_get_link (resource));
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
meta_wayland_tablet_tool_update_cursor_surface (MetaWaylandTabletTool *tool)
{
  MetaCursorSprite *cursor = NULL;

  if (tool->cursor_renderer == NULL)
    return;

  if (tool->current && tool->current_tablet)
    {
      if (tool->cursor_surface &&
          meta_wayland_surface_get_buffer (tool->cursor_surface))
        {
          MetaWaylandCursorSurface *cursor_surface =
            META_WAYLAND_CURSOR_SURFACE (tool->cursor_surface->role);

          cursor = meta_wayland_cursor_surface_get_sprite (cursor_surface);
        }
      else
        cursor = NULL;
    }
  else if (tool->current_tablet)
    cursor = META_CURSOR_SPRITE (tool->default_sprite);
  else
    cursor = NULL;

  meta_cursor_renderer_set_cursor (tool->cursor_renderer, cursor);
}

static void
meta_wayland_tablet_tool_set_cursor_surface (MetaWaylandTabletTool *tool,
                                             MetaWaylandSurface    *surface)
{
  if (tool->cursor_surface == surface)
    return;

  if (tool->cursor_surface)
    {
      MetaWaylandCursorSurface *cursor_surface;

      cursor_surface = META_WAYLAND_CURSOR_SURFACE (tool->cursor_surface->role);
      meta_wayland_cursor_surface_set_renderer (cursor_surface, NULL);

      meta_wayland_surface_update_outputs (tool->cursor_surface);
      wl_list_remove (&tool->cursor_surface_destroy_listener.link);
    }

  tool->cursor_surface = surface;

  if (tool->cursor_surface)
    {
      meta_wayland_surface_update_outputs (tool->cursor_surface);
      wl_resource_add_destroy_listener (tool->cursor_surface->resource,
                                        &tool->cursor_surface_destroy_listener);
    }

  meta_wayland_tablet_tool_update_cursor_surface (tool);
}

static uint32_t
input_device_get_capabilities (ClutterInputDevice *device)
{
  ClutterInputAxis axis;
  guint32 capabilities = 0, i;

  for (i = 0; i < clutter_input_device_get_n_axes (device); i++)
    {
      axis = clutter_input_device_get_axis (device, i);

      switch (axis)
        {
        case CLUTTER_INPUT_AXIS_PRESSURE:
          capabilities |= 1 << ZWP_TABLET_TOOL_V2_CAPABILITY_PRESSURE;
          break;
        case CLUTTER_INPUT_AXIS_DISTANCE:
          capabilities |= 1 << ZWP_TABLET_TOOL_V2_CAPABILITY_DISTANCE;
          break;
        case CLUTTER_INPUT_AXIS_XTILT:
        case CLUTTER_INPUT_AXIS_YTILT:
          capabilities |= 1 << ZWP_TABLET_TOOL_V2_CAPABILITY_TILT;
          break;
        case CLUTTER_INPUT_AXIS_ROTATION:
          capabilities |= 1 << ZWP_TABLET_TOOL_V2_CAPABILITY_ROTATION;
          break;
        case CLUTTER_INPUT_AXIS_WHEEL:
          capabilities |= 1 << ZWP_TABLET_TOOL_V2_CAPABILITY_WHEEL;
          break;
        case CLUTTER_INPUT_AXIS_SLIDER:
          capabilities |= 1 << ZWP_TABLET_TOOL_V2_CAPABILITY_SLIDER;
          break;
        default:
          break;
        }
    }

  return capabilities;
}

static enum zwp_tablet_tool_v2_type
input_device_tool_get_type (ClutterInputDeviceTool *device_tool)
{
  ClutterInputDeviceToolType tool_type;

  tool_type = clutter_input_device_tool_get_tool_type (device_tool);

  switch (tool_type)
    {
    case CLUTTER_INPUT_DEVICE_TOOL_NONE:
    case CLUTTER_INPUT_DEVICE_TOOL_PEN:
      return ZWP_TABLET_TOOL_V2_TYPE_PEN;
    case CLUTTER_INPUT_DEVICE_TOOL_ERASER:
      return ZWP_TABLET_TOOL_V2_TYPE_ERASER;
    case CLUTTER_INPUT_DEVICE_TOOL_BRUSH:
      return ZWP_TABLET_TOOL_V2_TYPE_BRUSH;
    case CLUTTER_INPUT_DEVICE_TOOL_PENCIL:
      return ZWP_TABLET_TOOL_V2_TYPE_PENCIL;
    case CLUTTER_INPUT_DEVICE_TOOL_AIRBRUSH:
      return ZWP_TABLET_TOOL_V2_TYPE_AIRBRUSH;
    case CLUTTER_INPUT_DEVICE_TOOL_MOUSE:
      return ZWP_TABLET_TOOL_V2_TYPE_MOUSE;
    case CLUTTER_INPUT_DEVICE_TOOL_LENS:
      return ZWP_TABLET_TOOL_V2_TYPE_LENS;
    }

  g_assert_not_reached ();
  return 0;
}

static void
meta_wayland_tablet_tool_notify_capabilities (MetaWaylandTabletTool *tool,
                                              struct wl_resource    *resource)
{
  uint32_t capabilities;

  capabilities = input_device_get_capabilities (tool->device);

  if (capabilities & (1 << ZWP_TABLET_TOOL_V2_CAPABILITY_PRESSURE))
    zwp_tablet_tool_v2_send_capability (resource,
                                        ZWP_TABLET_TOOL_V2_CAPABILITY_PRESSURE);
  if (capabilities & (1 << ZWP_TABLET_TOOL_V2_CAPABILITY_DISTANCE))
    zwp_tablet_tool_v2_send_capability (resource,
                                        ZWP_TABLET_TOOL_V2_CAPABILITY_DISTANCE);
  if (capabilities & (1 << ZWP_TABLET_TOOL_V2_CAPABILITY_TILT))
    zwp_tablet_tool_v2_send_capability (resource,
                                        ZWP_TABLET_TOOL_V2_CAPABILITY_TILT);
  if (capabilities & (1 << ZWP_TABLET_TOOL_V2_CAPABILITY_ROTATION))
    zwp_tablet_tool_v2_send_capability (resource,
                                        ZWP_TABLET_TOOL_V2_CAPABILITY_ROTATION);
  if (capabilities & (1 << ZWP_TABLET_TOOL_V2_CAPABILITY_SLIDER))
    zwp_tablet_tool_v2_send_capability (resource,
                                        ZWP_TABLET_TOOL_V2_CAPABILITY_SLIDER);
  if (capabilities & (1 << ZWP_TABLET_TOOL_V2_CAPABILITY_WHEEL))
    zwp_tablet_tool_v2_send_capability (resource,
                                        ZWP_TABLET_TOOL_V2_CAPABILITY_WHEEL);
}

static void
meta_wayland_tablet_tool_notify_details (MetaWaylandTabletTool *tool,
                                         struct wl_resource    *resource)
{
  guint64 serial, id;

  zwp_tablet_tool_v2_send_type (resource,
                                input_device_tool_get_type (tool->device_tool));

  serial = clutter_input_device_tool_get_serial (tool->device_tool);
  zwp_tablet_tool_v2_send_hardware_serial (resource, (uint32_t) (serial >> 32),
                                           (uint32_t) (serial & G_MAXUINT32));

  id = clutter_input_device_tool_get_id (tool->device_tool);
  zwp_tablet_tool_v2_send_hardware_id_wacom (resource, (uint32_t) (id >> 32),
                                             (uint32_t) (id & G_MAXUINT32));

  meta_wayland_tablet_tool_notify_capabilities (tool, resource);

  zwp_tablet_tool_v2_send_done (resource);
}

static void
meta_wayland_tablet_tool_ensure_resource (MetaWaylandTabletTool *tool,
                                          struct wl_client      *client)
{
  struct wl_resource *seat_resource, *tool_resource;

  seat_resource = meta_wayland_tablet_seat_lookup_resource (tool->seat, client);

  if (seat_resource &&
      !meta_wayland_tablet_tool_lookup_resource (tool, client))
    {
      tool_resource = meta_wayland_tablet_tool_create_new_resource (tool, client,
                                                                    seat_resource,
                                                                    0);

      meta_wayland_tablet_seat_notify_tool (tool->seat, tool, client);
      meta_wayland_tablet_tool_notify_details (tool, tool_resource);
    }
}

static void
broadcast_proximity_in (MetaWaylandTabletTool *tool)
{
  struct wl_resource *resource, *tablet_resource;
  struct wl_client *client;

  client = wl_resource_get_client (tool->focus_surface->resource);
  tablet_resource = meta_wayland_tablet_lookup_resource (tool->current_tablet,
                                                         client);

  wl_resource_for_each (resource, &tool->focus_resource_list)
    {
      zwp_tablet_tool_v2_send_proximity_in (resource, tool->proximity_serial,
                                            tablet_resource,
                                            tool->focus_surface->resource);
    }
}

static void
broadcast_proximity_out (MetaWaylandTabletTool *tool)
{
  struct wl_resource *resource;

  wl_resource_for_each (resource, &tool->focus_resource_list)
    {
      zwp_tablet_tool_v2_send_proximity_out (resource);
    }
}

static void
broadcast_frame (MetaWaylandTabletTool *tool,
                 const ClutterEvent    *event)
{
  struct wl_resource *resource;
  guint32 _time = event ? clutter_event_get_time (event) : CLUTTER_CURRENT_TIME;

  wl_resource_for_each (resource, &tool->focus_resource_list)
    {
      zwp_tablet_tool_v2_send_frame (resource, _time);
    }
}

static void
meta_wayland_tablet_tool_set_focus (MetaWaylandTabletTool *tool,
                                    MetaWaylandSurface    *surface,
                                    const ClutterEvent    *event)
{
  if (tool->focus_surface == surface)
    return;

  if (tool->focus_surface != NULL)
    {
      struct wl_list *l;

      l = &tool->focus_resource_list;
      if (!wl_list_empty (l))
        {
          broadcast_proximity_out (tool);
          broadcast_frame (tool, event);
          move_resources (&tool->resource_list, &tool->focus_resource_list);
        }

      wl_list_remove (&tool->focus_surface_destroy_listener.link);
      tool->focus_surface = NULL;
    }

  if (surface != NULL && tool->current_tablet)
    {
      struct wl_client *client;
      struct wl_list *l;

      tool->focus_surface = surface;
      client = wl_resource_get_client (tool->focus_surface->resource);
      wl_resource_add_destroy_listener (tool->focus_surface->resource,
                                        &tool->focus_surface_destroy_listener);

      move_resources_for_client (&tool->focus_resource_list,
                                 &tool->resource_list, client);
      meta_wayland_tablet_tool_ensure_resource (tool, client);

      l = &tool->focus_resource_list;

      if (!wl_list_empty (l))
        {
          struct wl_client *client = wl_resource_get_client (tool->focus_surface->resource);
          struct wl_display *display = wl_client_get_display (client);

          tool->proximity_serial = wl_display_next_serial (display);

          broadcast_proximity_in (tool);
          broadcast_frame (tool, event);
        }
    }

  meta_wayland_tablet_tool_update_cursor_surface (tool);
}

static void
tablet_tool_handle_focus_surface_destroy (struct wl_listener *listener,
                                          void               *data)
{
  MetaWaylandTabletTool *tool;

  tool = wl_container_of (listener, tool, focus_surface_destroy_listener);
  meta_wayland_tablet_tool_set_focus (tool, NULL, NULL);
}

static void
tablet_tool_handle_cursor_surface_destroy (struct wl_listener *listener,
                                           void               *data)
{
  MetaWaylandTabletTool *tool;

  tool = wl_container_of (listener, tool, cursor_surface_destroy_listener);
  meta_wayland_tablet_tool_set_cursor_surface (tool, NULL);
}

static void
tool_cursor_prepare_at (MetaCursorSpriteXcursor *sprite_xcursor,
                        int                      x,
                        int                      y,
                        MetaWaylandTabletTool   *tool)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaLogicalMonitor *logical_monitor;

  logical_monitor =
    meta_monitor_manager_get_logical_monitor_at (monitor_manager, x, y);

  /* Reload the cursor texture if the scale has changed. */
  if (logical_monitor)
    {
      MetaCursorSprite *cursor_sprite = META_CURSOR_SPRITE (sprite_xcursor);
      float ceiled_scale;

      ceiled_scale = ceilf (logical_monitor->scale);
      meta_cursor_sprite_xcursor_set_theme_scale (sprite_xcursor,
                                                  (int) ceiled_scale);

      if (meta_is_stage_views_scaled ())
        meta_cursor_sprite_set_texture_scale (cursor_sprite,
                                              1.0 / ceiled_scale);
      else
        meta_cursor_sprite_set_texture_scale (cursor_sprite, 1.0);
    }
}

MetaWaylandTabletTool *
meta_wayland_tablet_tool_new (MetaWaylandTabletSeat  *seat,
                              ClutterInputDevice     *device,
                              ClutterInputDeviceTool *device_tool)
{
  MetaWaylandTabletTool *tool;

  tool = g_slice_new0 (MetaWaylandTabletTool);
  tool->seat = seat;
  tool->device = device;
  tool->device_tool = device_tool;
  wl_list_init (&tool->resource_list);
  wl_list_init (&tool->focus_resource_list);

  tool->focus_surface_destroy_listener.notify = tablet_tool_handle_focus_surface_destroy;
  tool->cursor_surface_destroy_listener.notify = tablet_tool_handle_cursor_surface_destroy;

  tool->default_sprite = meta_cursor_sprite_xcursor_new (META_CURSOR_CROSSHAIR);
  tool->prepare_at_signal_id =
    g_signal_connect (tool->default_sprite, "prepare-at",
                      G_CALLBACK (tool_cursor_prepare_at), tool);

  return tool;
}

void
meta_wayland_tablet_tool_free (MetaWaylandTabletTool *tool)
{
  struct wl_resource *resource, *next;

  meta_wayland_tablet_tool_set_focus (tool, NULL, NULL);
  meta_wayland_tablet_tool_set_cursor_surface (tool, NULL);
  g_clear_object (&tool->cursor_renderer);

  wl_resource_for_each_safe (resource, next, &tool->resource_list)
    {
      zwp_tablet_tool_v2_send_removed (resource);
      wl_list_remove (wl_resource_get_link (resource));
      wl_list_init (wl_resource_get_link (resource));
    }

  g_clear_signal_handler (&tool->prepare_at_signal_id, tool->default_sprite);
  g_object_unref (tool->default_sprite);

  g_slice_free (MetaWaylandTabletTool, tool);
}

static void
tool_set_cursor (struct wl_client   *client,
                 struct wl_resource *resource,
                 uint32_t            serial,
                 struct wl_resource *surface_resource,
                 int32_t             hotspot_x,
                 int32_t             hotspot_y)
{
  MetaWaylandTabletTool *tool = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface;

  surface = (surface_resource ? wl_resource_get_user_data (surface_resource) : NULL);

  if (tool->focus_surface == NULL)
    return;
  if (tool->cursor_renderer == NULL)
    return;
  if (wl_resource_get_client (tool->focus_surface->resource) != client)
    return;
  if (tool->proximity_serial - serial > G_MAXUINT32 / 2)
    return;

  if (surface &&
      !meta_wayland_surface_assign_role (surface,
                                         META_TYPE_WAYLAND_TABLET_CURSOR_SURFACE,
                                         NULL))
    {
      wl_resource_post_error (resource, WL_POINTER_ERROR_ROLE,
                              "wl_surface@%d already has a different role",
                              wl_resource_get_id (surface_resource));
      return;
    }

  if (surface)
    {
      MetaWaylandCursorSurface *cursor_surface;

      cursor_surface = META_WAYLAND_CURSOR_SURFACE (surface->role);
      meta_wayland_cursor_surface_set_renderer (cursor_surface,
                                                tool->cursor_renderer);
      meta_wayland_cursor_surface_set_hotspot (cursor_surface,
                                               hotspot_x, hotspot_y);
    }

  meta_wayland_tablet_tool_set_cursor_surface (tool, surface);
}

static void
tool_destroy (struct wl_client   *client,
              struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct zwp_tablet_tool_v2_interface tool_interface = {
  tool_set_cursor,
  tool_destroy
};

static void
emit_proximity_in (MetaWaylandTabletTool *tool,
                   struct wl_resource    *resource)
{
  struct wl_resource *tablet_resource;
  struct wl_client *client;

  if (!tool->focus_surface)
    return;

  client = wl_resource_get_client (resource);
  tablet_resource = meta_wayland_tablet_lookup_resource (tool->current_tablet,
                                                         client);

  zwp_tablet_tool_v2_send_proximity_in (resource, tool->proximity_serial,
                                        tablet_resource, tool->focus_surface->resource);
}

struct wl_resource *
meta_wayland_tablet_tool_create_new_resource (MetaWaylandTabletTool *tool,
                                              struct wl_client      *client,
                                              struct wl_resource    *seat_resource,
                                              uint32_t               id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client, &zwp_tablet_tool_v2_interface,
                                 wl_resource_get_version (seat_resource), id);
  wl_resource_set_implementation (resource, &tool_interface,
                                  tool, unbind_resource);
  wl_resource_set_user_data (resource, tool);

  if (tool->focus_surface &&
      wl_resource_get_client (tool->focus_surface->resource) == client)
    {
      wl_list_insert (&tool->focus_resource_list, wl_resource_get_link (resource));
      emit_proximity_in (tool, resource);
    }
  else
    {
      wl_list_insert (&tool->resource_list, wl_resource_get_link (resource));
    }

  return resource;
}

struct wl_resource *
meta_wayland_tablet_tool_lookup_resource (MetaWaylandTabletTool *tool,
                                          struct wl_client      *client)
{
  struct wl_resource *resource = NULL;

  if (!wl_list_empty (&tool->resource_list))
    resource = wl_resource_find_for_client (&tool->resource_list, client);

  if (!wl_list_empty (&tool->focus_resource_list))
    resource = wl_resource_find_for_client (&tool->focus_resource_list, client);

  return resource;
}

static void
meta_wayland_tablet_tool_account_button (MetaWaylandTabletTool *tool,
                                         const ClutterEvent    *event)
{
  if (event->type == CLUTTER_BUTTON_PRESS)
    {
      tool->pressed_buttons |= 1 << (event->button.button - 1);
      tool->button_count++;
    }
  else if (event->type == CLUTTER_BUTTON_RELEASE)
    {
      tool->pressed_buttons &= ~(1 << (event->button.button - 1));
      tool->button_count--;
    }
}

static void
sync_focus_surface (MetaWaylandTabletTool *tool,
                    const ClutterEvent    *event)
{
  MetaDisplay *display = meta_get_display ();

  switch (display->event_route)
    {
    case META_EVENT_ROUTE_WINDOW_OP:
    case META_EVENT_ROUTE_COMPOSITOR_GRAB:
    case META_EVENT_ROUTE_FRAME_BUTTON:
      /* The compositor has a grab, so remove our focus */
      meta_wayland_tablet_tool_set_focus (tool, NULL, event);
      break;

    case META_EVENT_ROUTE_NORMAL:
    case META_EVENT_ROUTE_WAYLAND_POPUP:
      meta_wayland_tablet_tool_set_focus (tool, tool->current, event);
      break;

    default:
      g_assert_not_reached ();
    }
}

static void
repick_for_event (MetaWaylandTabletTool *tool,
                  const ClutterEvent    *for_event)
{
  ClutterActor *actor = NULL;

  actor = clutter_event_get_source (for_event);

  if (META_IS_SURFACE_ACTOR_WAYLAND (actor))
    tool->current = meta_surface_actor_wayland_get_surface (META_SURFACE_ACTOR_WAYLAND (actor));
  else
    tool->current = NULL;

  sync_focus_surface (tool, for_event);
  meta_wayland_tablet_tool_update_cursor_surface (tool);
}

static void
meta_wayland_tablet_tool_get_relative_coordinates (MetaWaylandTabletTool *tool,
                                                   const ClutterEvent    *event,
                                                   MetaWaylandSurface    *surface,
                                                   wl_fixed_t            *sx,
                                                   wl_fixed_t            *sy)
{
  float xf, yf;

  clutter_event_get_coords (event, &xf, &yf);
  meta_wayland_surface_get_relative_coordinates (surface, xf, yf, &xf, &yf);

  *sx = wl_fixed_from_double (xf);
  *sy = wl_fixed_from_double (yf);
}

static void
broadcast_motion (MetaWaylandTabletTool *tool,
                  const ClutterEvent    *event)
{
  struct wl_resource *resource;
  wl_fixed_t sx, sy;

  meta_wayland_tablet_tool_get_relative_coordinates (tool, event,
                                                     tool->focus_surface,
                                                     &sx, &sy);

  wl_resource_for_each (resource, &tool->focus_resource_list)
    {
      zwp_tablet_tool_v2_send_motion (resource, sx, sy);
    }
}

static void
broadcast_down (MetaWaylandTabletTool *tool,
                const ClutterEvent    *event)
{
  struct wl_resource *resource;

  tool->down_serial = wl_display_next_serial (tool->seat->manager->wl_display);

  wl_resource_for_each (resource, &tool->focus_resource_list)
    {
      zwp_tablet_tool_v2_send_down (resource, tool->down_serial);
    }
}

static void
broadcast_up (MetaWaylandTabletTool *tool,
              const ClutterEvent    *event)
{
  struct wl_resource *resource;

  wl_resource_for_each (resource, &tool->focus_resource_list)
    {
      zwp_tablet_tool_v2_send_up (resource);
    }
}

static void
broadcast_button (MetaWaylandTabletTool *tool,
                  const ClutterEvent    *event)
{
  struct wl_resource *resource;
  guint32 button;

#ifdef HAVE_NATIVE_BACKEND
  MetaBackend *backend = meta_get_backend ();
  if (META_IS_BACKEND_NATIVE (backend))
    {
      button = meta_event_native_get_event_code (event);
    }
  else
#endif
    {
      /* We can't do much better here, there's several
       * different BTN_ ranges to cover.
       */
      button = event->button.button;
    }

  tool->button_serial = wl_display_next_serial (tool->seat->manager->wl_display);

  wl_resource_for_each (resource, &tool->focus_resource_list)
    {
      zwp_tablet_tool_v2_send_button (resource, tool->button_serial, button,
                                      event->type == CLUTTER_BUTTON_PRESS ?
                                      ZWP_TABLET_TOOL_V2_BUTTON_STATE_PRESSED :
                                      ZWP_TABLET_TOOL_V2_BUTTON_STATE_RELEASED);
    }
}

static void
broadcast_axis (MetaWaylandTabletTool *tool,
                const ClutterEvent    *event,
                ClutterInputAxis       axis)
{
  struct wl_resource *resource;
  ClutterInputDevice *source;
  uint32_t value;
  gdouble val;

  source = clutter_event_get_source_device (event);

  if (!clutter_input_device_get_axis_value (source, event->motion.axes, axis, &val))
    return;

  value = val * TABLET_AXIS_MAX;

  wl_resource_for_each (resource, &tool->focus_resource_list)
    {
      switch (axis)
        {
        case CLUTTER_INPUT_AXIS_PRESSURE:
          zwp_tablet_tool_v2_send_pressure (resource, value);
          break;
        case CLUTTER_INPUT_AXIS_DISTANCE:
          zwp_tablet_tool_v2_send_distance (resource, value);
          break;
        case CLUTTER_INPUT_AXIS_SLIDER:
          zwp_tablet_tool_v2_send_slider (resource, value);
          break;
        default:
          break;
        }
    }
}

static void
broadcast_tilt (MetaWaylandTabletTool *tool,
                const ClutterEvent    *event)
{
  struct wl_resource *resource;
  ClutterInputDevice *source;
  gdouble xtilt, ytilt;

  source = clutter_event_get_source_device (event);

  if (!clutter_input_device_get_axis_value (source, event->motion.axes,
                                            CLUTTER_INPUT_AXIS_XTILT, &xtilt) ||
      !clutter_input_device_get_axis_value (source, event->motion.axes,
                                            CLUTTER_INPUT_AXIS_YTILT, &ytilt))
    return;

  wl_resource_for_each (resource, &tool->focus_resource_list)
    {
      zwp_tablet_tool_v2_send_tilt (resource,
                                    wl_fixed_from_double (xtilt),
                                    wl_fixed_from_double (ytilt));
    }
}

static void
broadcast_rotation (MetaWaylandTabletTool *tool,
                    const ClutterEvent    *event)
{
  struct wl_resource *resource;
  ClutterInputDevice *source;
  gdouble rotation;

  source = clutter_event_get_source_device (event);

  if (!clutter_input_device_get_axis_value (source, event->motion.axes,
                                            CLUTTER_INPUT_AXIS_ROTATION,
                                            &rotation))
    return;

  wl_resource_for_each (resource, &tool->focus_resource_list)
    {
      zwp_tablet_tool_v2_send_rotation (resource,
                                        wl_fixed_from_double (rotation));
    }
}

static void
broadcast_wheel (MetaWaylandTabletTool *tool,
                 const ClutterEvent    *event)
{
  struct wl_resource *resource;
  ClutterInputDevice *source;
  gdouble angle;
  gint32 clicks = 0;

  source = clutter_event_get_source_device (event);

  if (!clutter_input_device_get_axis_value (source, event->motion.axes,
                                            CLUTTER_INPUT_AXIS_WHEEL,
                                            &angle))
    return;

  /* FIXME: Perform proper angle-to-clicks accumulation elsewhere */
  if (angle > 0.01)
    clicks = 1;
  else if (angle < -0.01)
    clicks = -1;
  else
    return;

  wl_resource_for_each (resource, &tool->focus_resource_list)
    {
      zwp_tablet_tool_v2_send_wheel (resource,
                                     wl_fixed_from_double (angle),
                                     clicks);
    }
}

static void
broadcast_axes (MetaWaylandTabletTool *tool,
                const ClutterEvent    *event)
{
  ClutterInputDevice *device;
  guint32 capabilities;

  if (!event->motion.axes)
    return;

  device = clutter_event_get_source_device (event);
  capabilities = input_device_get_capabilities (device);

  if (capabilities & (1 << ZWP_TABLET_TOOL_V2_CAPABILITY_PRESSURE))
    broadcast_axis (tool, event, CLUTTER_INPUT_AXIS_PRESSURE);
  if (capabilities & (1 << ZWP_TABLET_TOOL_V2_CAPABILITY_DISTANCE))
    broadcast_axis (tool, event, CLUTTER_INPUT_AXIS_DISTANCE);
  if (capabilities & (1 << ZWP_TABLET_TOOL_V2_CAPABILITY_TILT))
    broadcast_tilt (tool, event);
  if (capabilities & (1 << ZWP_TABLET_TOOL_V2_CAPABILITY_ROTATION))
    broadcast_rotation (tool, event);
  if (capabilities & (1 << ZWP_TABLET_TOOL_V2_CAPABILITY_SLIDER))
    broadcast_axis (tool, event, CLUTTER_INPUT_AXIS_SLIDER);
  if (capabilities & (1 << ZWP_TABLET_TOOL_V2_CAPABILITY_WHEEL))
    broadcast_wheel (tool, event);
}

static void
handle_motion_event (MetaWaylandTabletTool *tool,
                     const ClutterEvent    *event)
{
  if (!tool->focus_surface)
    return;

  broadcast_motion (tool, event);
  broadcast_axes (tool, event);
  broadcast_frame (tool, event);
}

static void
handle_button_event (MetaWaylandTabletTool *tool,
                     const ClutterEvent    *event)
{
  if (!tool->focus_surface)
    return;

  if (event->type == CLUTTER_BUTTON_PRESS && tool->button_count == 1)
    clutter_event_get_coords (event, &tool->grab_x, &tool->grab_y);

  if (event->type == CLUTTER_BUTTON_PRESS && event->button.button == 1)
    broadcast_down (tool, event);
  else if (event->type == CLUTTER_BUTTON_RELEASE && event->button.button == 1)
    broadcast_up (tool, event);
  else
    broadcast_button (tool, event);

  broadcast_frame (tool, event);
}

void
meta_wayland_tablet_tool_update (MetaWaylandTabletTool *tool,
                                 const ClutterEvent    *event)
{
  switch (event->type)
    {
    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_BUTTON_RELEASE:
      meta_wayland_tablet_tool_account_button (tool, event);
      break;
    case CLUTTER_MOTION:
      if (!tool->pressed_buttons)
        repick_for_event (tool, event);
      break;
    case CLUTTER_PROXIMITY_IN:
      if (!tool->cursor_renderer)
        tool->cursor_renderer = meta_cursor_renderer_new ();
      tool->current_tablet =
        meta_wayland_tablet_seat_lookup_tablet (tool->seat,
                                                clutter_event_get_source_device (event));
      break;
    case CLUTTER_PROXIMITY_OUT:
      tool->current_tablet = NULL;
      meta_wayland_tablet_tool_set_cursor_surface (tool, NULL);
      meta_wayland_tablet_tool_update_cursor_surface (tool);
      g_clear_object (&tool->cursor_renderer);
      break;
    default:
      break;
    }
}

gboolean
meta_wayland_tablet_tool_handle_event (MetaWaylandTabletTool *tool,
                                       const ClutterEvent    *event)
{
  switch (event->type)
    {
    case CLUTTER_PROXIMITY_IN:
      /* We don't have much info here to make anything useful out of it,
       * wait until the first motion event so we have both coordinates
       * and tool.
       */
      break;
    case CLUTTER_PROXIMITY_OUT:
      meta_wayland_tablet_tool_set_focus (tool, NULL, event);
      break;
    case CLUTTER_MOTION:
      handle_motion_event (tool, event);
      break;
    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_BUTTON_RELEASE:
      handle_button_event (tool, event);
      break;
    default:
      return CLUTTER_EVENT_PROPAGATE;
    }

  return CLUTTER_EVENT_STOP;
}

void
meta_wayland_tablet_tool_set_cursor_position (MetaWaylandTabletTool *tool,
                                              float                  new_x,
                                              float                  new_y)
{
  if (tool->cursor_renderer)
    meta_cursor_renderer_set_position (tool->cursor_renderer, new_x, new_y);
}

static gboolean
tablet_tool_can_grab_surface (MetaWaylandTabletTool *tool,
                              MetaWaylandSurface    *surface)
{
  MetaWaylandSurface *subsurface;

  if (tool->focus_surface == surface)
    return TRUE;

  META_WAYLAND_SURFACE_FOREACH_SUBSURFACE (surface, subsurface)
    {
      if (tablet_tool_can_grab_surface (tool, subsurface))
        return TRUE;
    }

  return FALSE;
}

gboolean
meta_wayland_tablet_tool_can_grab_surface (MetaWaylandTabletTool *tool,
                                           MetaWaylandSurface    *surface,
                                           uint32_t               serial)
{
  return ((tool->down_serial == serial || tool->button_serial == serial) &&
          tablet_tool_can_grab_surface (tool, surface));
}

gboolean
meta_wayland_tablet_tool_can_popup (MetaWaylandTabletTool *tool,
                                    uint32_t               serial)
{
  return tool->down_serial == serial || tool->button_serial == serial;
}
