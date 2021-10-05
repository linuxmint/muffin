/*
 * Wayland Support
 *
 * Copyright (C) 2018-2019 Robert Mader <robert.mader@posteo.de>
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

#include "meta-wayland-viewporter.h"

#include <glib.h>

#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-subsurface.h"
#include "wayland/meta-wayland-surface.h"
#include "wayland/meta-wayland-versions.h"

#include "viewporter-server-protocol.h"

static void
wp_viewport_destructor (struct wl_resource *resource)
{
  MetaWaylandSurface *surface;
  MetaWaylandSurfaceState *pending;

  surface = wl_resource_get_user_data (resource);
  if (!surface)
    return;

  g_clear_signal_handler (&surface->viewport.destroy_handler_id, surface);

  pending = meta_wayland_surface_get_pending_state (surface);
  pending->viewport_src_rect.size.width = -1;
  pending->viewport_dst_width = -1;
  pending->has_new_viewport_src_rect = TRUE;
  pending->has_new_viewport_dst_size = TRUE;

  surface->viewport.resource = NULL;
}

static void
on_surface_destroyed (MetaWaylandSurface *surface)
{
  wl_resource_set_user_data (surface->viewport.resource, NULL);
}

static void
wp_viewport_destroy (struct wl_client   *client,
                     struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
wp_viewport_set_source (struct wl_client   *client,
                        struct wl_resource *resource,
                        wl_fixed_t          src_x,
                        wl_fixed_t          src_y,
                        wl_fixed_t          src_width,
                        wl_fixed_t          src_height)
{
  MetaWaylandSurface *surface;
  float new_x;
  float new_y;
  float new_width;
  float new_height;

  surface = wl_resource_get_user_data (resource);
  if (!surface)
    {
      wl_resource_post_error (resource,
                              WP_VIEWPORT_ERROR_NO_SURFACE,
                              "wl_surface for this viewport no longer exists");
      return;
    }

  new_x = wl_fixed_to_double (src_x);
  new_y = wl_fixed_to_double (src_y);
  new_width = wl_fixed_to_double (src_width);
  new_height = wl_fixed_to_double (src_height);

  if ((new_x >= 0 && new_y >= 0 &&
       new_width > 0 && new_height > 0) ||
      (new_x == -1 && new_y == -1 &&
       new_width == -1 && new_height == -1))
    {
      MetaWaylandSurfaceState *pending;

      pending = meta_wayland_surface_get_pending_state (surface);
      pending->viewport_src_rect.origin.x = new_x;
      pending->viewport_src_rect.origin.y = new_y;
      pending->viewport_src_rect.size.width = new_width;
      pending->viewport_src_rect.size.height = new_height;
      pending->has_new_viewport_src_rect = TRUE;
    }
  else
    {
      wl_resource_post_error (resource,
                              WP_VIEWPORT_ERROR_BAD_VALUE,
                              "x and y values must be zero or positive and "
                              "width and height valuest must be positive or "
                              "all values must be -1 to unset the viewport");
    }
}

static void
wp_viewport_set_destination (struct wl_client   *client,
                             struct wl_resource *resource,
                             int                 dst_width,
                             int                 dst_height)
{
  MetaWaylandSurface *surface;

  surface = wl_resource_get_user_data (resource);
  if (!surface)
    {
      wl_resource_post_error (resource,
                              WP_VIEWPORT_ERROR_NO_SURFACE,
                              "wl_surface for this viewport no longer exists");
      return;
    }

  if ((dst_width > 0 && dst_height > 0) ||
      (dst_width == -1 && dst_height == -1))
    {
      MetaWaylandSurfaceState *pending;

      pending = meta_wayland_surface_get_pending_state (surface);
      pending->viewport_dst_width = dst_width;
      pending->viewport_dst_height = dst_height;
      pending->has_new_viewport_dst_size = TRUE;
    }
  else
    {
      wl_resource_post_error (resource,
                              WP_VIEWPORT_ERROR_BAD_VALUE,
                              "all values must be either positive or -1");
    }
}

static const struct wp_viewport_interface meta_wayland_viewport_interface = {
  wp_viewport_destroy,
  wp_viewport_set_source,
  wp_viewport_set_destination,
};

static void
wp_viewporter_destroy (struct wl_client   *client,
                       struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
wp_viewporter_get_viewport (struct wl_client   *client,
                            struct wl_resource *resource,
                            uint32_t            viewport_id,
                            struct wl_resource *surface_resource)
{
  MetaWaylandSurface *surface;
  struct wl_resource *viewport_resource;

  surface = wl_resource_get_user_data (surface_resource);
  if (surface->viewport.resource)
    {
      wl_resource_post_error (resource,
                              WP_VIEWPORTER_ERROR_VIEWPORT_EXISTS,
                              "viewport already exists on surface");
      return;
    }

  viewport_resource = wl_resource_create (client,
                                          &wp_viewport_interface,
                                          wl_resource_get_version (resource),
                                          viewport_id);
  wl_resource_set_implementation (viewport_resource,
                                  &meta_wayland_viewport_interface,
                                  surface,
                                  wp_viewport_destructor);

  surface->viewport.resource = viewport_resource;
  surface->viewport.destroy_handler_id =
    g_signal_connect (surface,
                      "destroy",
                      G_CALLBACK (on_surface_destroyed),
                      NULL);
}

static const struct wp_viewporter_interface meta_wayland_viewporter_interface = {
  wp_viewporter_destroy,
  wp_viewporter_get_viewport,
};

static void
wp_viewporter_bind (struct wl_client *client,
                    void             *data,
                    uint32_t          version,
                    uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &wp_viewporter_interface,
                                 version,
                                 id);
  wl_resource_set_implementation (resource,
                                  &meta_wayland_viewporter_interface,
                                  data,
                                  NULL);
}

void
meta_wayland_init_viewporter (MetaWaylandCompositor *compositor)
{
  if (wl_global_create (compositor->wayland_display,
                        &wp_viewporter_interface,
                        META_WP_VIEWPORTER_VERSION,
                        compositor,
                        wp_viewporter_bind) == NULL)
    g_error ("Failed to register a global wl-viewporter object");
}
