/*
 * Wayland Support
 *
 * Copyright (C) 2022 Robert Mader <robert.mader@posteo.de>
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

#include "wayland/meta-wayland-fractional-scale.h"

#include <float.h>
#include <glib.h>
#include <math.h>

#include "wayland/meta-wayland-outputs.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-surface.h"
#include "wayland/meta-wayland-versions.h"

#include "fractional-scale-v1-server-protocol.h"

static void
wp_fractional_scale_destructor (struct wl_resource *resource)
{
  MetaWaylandSurface *surface;

  surface = wl_resource_get_user_data (resource);
  if (!surface)
    return;

  g_clear_signal_handler (&surface->fractional_scale.destroy_handler_id,
                          surface);
  surface->fractional_scale.resource = NULL;

  /* The cached scale only tracks what the destroyed object was told. Keeping
   * it would make a replacement object skip its initial preferred_scale. */
  surface->fractional_scale.sent_scale = 0.0;
}

static void
on_surface_destroyed (MetaWaylandSurface *surface)
{
  wl_resource_set_user_data (surface->fractional_scale.resource, NULL);
}

static void
wp_fractional_scale_destroy (struct wl_client   *client,
                             struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct wp_fractional_scale_v1_interface meta_wayland_fractional_scale_interface = {
  wp_fractional_scale_destroy,
};

static void
wp_fractional_scale_manager_destroy (struct wl_client   *client,
                                     struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
wp_fractional_scale_manager_get_fractional_scale (struct wl_client   *client,
                                                  struct wl_resource *resource,
                                                  uint32_t            fractional_scale_id,
                                                  struct wl_resource *surface_resource)
{
  MetaWaylandSurface *surface;
  struct wl_resource *fractional_scale_resource;
  MetaLogicalMonitor *logical_monitor;

  surface = wl_resource_get_user_data (surface_resource);
  if (surface->fractional_scale.resource)
    {
      wl_resource_post_error (resource,
                              WP_FRACTIONAL_SCALE_MANAGER_V1_ERROR_FRACTIONAL_SCALE_EXISTS,
                              "fractional scale resource already exists on surface");
      return;
    }

  fractional_scale_resource = wl_resource_create (client,
                                                  &wp_fractional_scale_v1_interface,
                                                  wl_resource_get_version (resource),
                                                  fractional_scale_id);
  wl_resource_set_implementation (fractional_scale_resource,
                                  &meta_wayland_fractional_scale_interface,
                                  surface,
                                  wp_fractional_scale_destructor);

  surface->fractional_scale.resource = fractional_scale_resource;
  surface->fractional_scale.destroy_handler_id =
    g_signal_connect (surface,
                      "destroy",
                      G_CALLBACK (on_surface_destroyed),
                      NULL);

  logical_monitor = meta_wayland_surface_get_preferred_scale_monitor (surface);
  if (logical_monitor)
    {
      double scale;

      scale = meta_logical_monitor_get_scale (logical_monitor);
      meta_wayland_fractional_scale_maybe_send_preferred_scale (surface, scale);
    }
}

static const struct wp_fractional_scale_manager_v1_interface meta_wayland_fractional_scale_manager_interface = {
  wp_fractional_scale_manager_destroy,
  wp_fractional_scale_manager_get_fractional_scale,
};

static void
wp_fractional_scale_bind (struct wl_client *client,
                          void             *data,
                          uint32_t          version,
                          uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &wp_fractional_scale_manager_v1_interface,
                                 version,
                                 id);
  wl_resource_set_implementation (resource,
                                  &meta_wayland_fractional_scale_manager_interface,
                                  data,
                                  NULL);
}

void
meta_wayland_init_fractional_scale (MetaWaylandCompositor *compositor)
{
  if (g_getenv ("MUFFIN_DEBUG_DISABLE_FRACTIONAL_SCALE"))
    {
      g_message ("wp_fractional_scale_v1 disabled by "
                 "MUFFIN_DEBUG_DISABLE_FRACTIONAL_SCALE");
      return;
    }

  if (wl_global_create (compositor->wayland_display,
                        &wp_fractional_scale_manager_v1_interface,
                        META_WP_FRACTIONAL_SCALE_V1_VERSION,
                        compositor,
                        wp_fractional_scale_bind) == NULL)
    g_error ("Failed to register a global wp_fractional_scale object");
}

void
meta_wayland_fractional_scale_maybe_send_preferred_scale (MetaWaylandSurface *surface,
                                                          double              scale)
{
  uint32_t wire_scale;

  if (!surface->fractional_scale.resource)
    return;

  if (G_APPROX_VALUE (scale, 0.0, FLT_EPSILON) ||
      G_APPROX_VALUE (scale, surface->fractional_scale.sent_scale, FLT_EPSILON))
    return;

  wire_scale = (int) round (scale * 120);
  wp_fractional_scale_v1_send_preferred_scale (surface->fractional_scale.resource,
                                               wire_scale);
  surface->fractional_scale.sent_scale = scale;
}
