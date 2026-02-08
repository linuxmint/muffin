/*
 * Wayland Support
 *
 * Copyright (C) 2025 Red Hat Inc.
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "meta-wayland-pointer-warp.h"

#include "meta-wayland-private.h"
#include "meta-wayland-seat.h"
#include "meta-wayland-surface.h"

#include "pointer-warp-v1-server-protocol.h"

struct _MetaWaylandPointerWarp
{
  MetaWaylandSeat *seat;
  struct wl_list resource_list;
};

static void
pointer_warp_destroy (struct wl_client   *client,
                      struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
pointer_warp_perform (struct wl_client   *client,
                      struct wl_resource *resource,
                      struct wl_resource *surface_resource,
                      struct wl_resource *pointer_resource,
                      wl_fixed_t          x,
                      wl_fixed_t          y,
                      uint32_t            serial)
{
  ClutterSeat *seat;
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandPointer *pointer = wl_resource_get_user_data (pointer_resource);
  MetaSurfaceActor *surface_actor = meta_wayland_surface_get_actor (surface);
  graphene_point3d_t coords =
    GRAPHENE_POINT3D_INIT ((float) wl_fixed_to_double (x),
                            (float) wl_fixed_to_double (y),
                            0.0);

  /* Not focused and implicitly grabbed */
  if (!meta_wayland_pointer_get_grab_info (pointer, surface, serial, TRUE,
                                           NULL, NULL, NULL))
    return;

  /* Outside of actor */
  if (!surface_actor ||
      x < 0 || x > clutter_actor_get_width (CLUTTER_ACTOR (surface_actor)) ||
      y < 0 || y > clutter_actor_get_height (CLUTTER_ACTOR (surface_actor)))
    return;

  clutter_actor_apply_transform_to_point (CLUTTER_ACTOR (surface_actor),
                                          &coords, &coords);

  seat = clutter_backend_get_default_seat (clutter_get_default_backend ());

  clutter_seat_warp_pointer (seat, (int) coords.x, (int) coords.y);
}

static struct wp_pointer_warp_v1_interface pointer_warp_interface = {
  pointer_warp_destroy,
  pointer_warp_perform,
};

static void
unbind_resource (struct wl_resource *resource)
{
  wl_list_remove (wl_resource_get_link (resource));
}

static void
bind_pointer_warp (struct wl_client *client,
                   void             *data,
                   uint32_t          version,
                   uint32_t          id)
{
  MetaWaylandPointerWarp *pointer_warp = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client, &wp_pointer_warp_v1_interface,
                                 MIN (version, META_WP_POINTER_WARP_VERSION),
                                 id);
  wl_resource_set_implementation (resource, &pointer_warp_interface,
                                  pointer_warp, unbind_resource);
  wl_resource_set_user_data (resource, pointer_warp);
  wl_list_insert (&pointer_warp->resource_list,
                  wl_resource_get_link (resource));
}

MetaWaylandPointerWarp *
meta_wayland_pointer_warp_new (MetaWaylandSeat *seat)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaWaylandPointerWarp *pointer_warp;

  pointer_warp = g_new0 (MetaWaylandPointerWarp, 1);
  pointer_warp->seat = seat;
  wl_list_init (&pointer_warp->resource_list);

  wl_global_create (compositor->wayland_display,
                    &wp_pointer_warp_v1_interface,
                    META_WP_POINTER_WARP_VERSION,
                    pointer_warp, bind_pointer_warp);

  return pointer_warp;
}

void
meta_wayland_pointer_warp_destroy (MetaWaylandPointerWarp *pointer_warp)
{
  wl_list_remove (&pointer_warp->resource_list);
  g_free (pointer_warp);
}
