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

#include "wayland/meta-wayland-xapp-shell.h"

#include <wayland-server.h>

#include "core/window-private.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-surface.h"
#include "wayland/meta-wayland-versions.h"

#include "xapp-shell-server-protocol.h"

#define XAPP_SURFACE_DATA_KEY "meta-wayland-xapp-surface"

typedef struct _MetaWaylandXAppSurface
{
  struct wl_resource *resource;
  MetaWaylandSurface *surface;

  /* The hints are cached here and applied to the window whenever it exists. A
   * client typically sets them on its wl_surface before that surface has been
   * given a toplevel role - and thus before it has a MetaWindow - so applying
   * them immediately would drop them. Caching, then applying as the window is
   * managed (see meta_wayland_xapp_shell_apply_to_window), mirrors the X11
   * _NET_WM_XAPP_* properties, which sit on the window before the WM reads
   * them - so the shell sees the right icon the moment the window appears. */
  char    *icon_name;
  gboolean icon_name_set;
  int      progress;
  gboolean progress_set;
  gboolean progress_pulse;
  gboolean progress_pulse_set;
} MetaWaylandXAppSurface;

static void
apply_hints_to_window (MetaWaylandXAppSurface *xapp_surface,
                       MetaWindow             *window)
{
  if (xapp_surface->icon_name_set)
    meta_window_set_icon_name (window, xapp_surface->icon_name);
  if (xapp_surface->progress_set)
    meta_window_set_progress (window, xapp_surface->progress);
  if (xapp_surface->progress_pulse_set)
    meta_window_set_progress_pulse (window, xapp_surface->progress_pulse);
}

/* Apply immediately when the surface already has a window (the client changed a
 * hint on an already-mapped window); otherwise the hints wait on the surface
 * until the window is managed. */
static void
apply_pending_hints (MetaWaylandXAppSurface *xapp_surface)
{
  MetaWindow *window;

  if (!xapp_surface->surface)
    return;

  window = meta_wayland_surface_get_window (xapp_surface->surface);
  if (window)
    apply_hints_to_window (xapp_surface, window);
}

void
meta_wayland_xapp_shell_apply_to_window (MetaWaylandSurface *surface,
                                         MetaWindow         *window)
{
  MetaWaylandXAppSurface *xapp_surface;

  xapp_surface = g_object_get_data (G_OBJECT (surface), XAPP_SURFACE_DATA_KEY);
  if (xapp_surface)
    apply_hints_to_window (xapp_surface, window);
}

static void
xapp_surface_set_icon_name (struct wl_client   *client,
                            struct wl_resource *resource,
                            const char         *icon_name)
{
  MetaWaylandXAppSurface *xapp_surface = wl_resource_get_user_data (resource);

  g_free (xapp_surface->icon_name);
  xapp_surface->icon_name = g_strdup (icon_name);
  xapp_surface->icon_name_set = TRUE;

  apply_pending_hints (xapp_surface);
}

static void
xapp_surface_set_progress (struct wl_client   *client,
                           struct wl_resource *resource,
                           int32_t             progress)
{
  MetaWaylandXAppSurface *xapp_surface = wl_resource_get_user_data (resource);

  xapp_surface->progress = progress < 0 ? 0 : progress;
  xapp_surface->progress_set = TRUE;

  /* Setting an explicit progress value also clears the pulsing state. */
  xapp_surface->progress_pulse = FALSE;
  xapp_surface->progress_pulse_set = TRUE;

  apply_pending_hints (xapp_surface);
}

static void
xapp_surface_set_progress_pulse (struct wl_client   *client,
                                 struct wl_resource *resource,
                                 uint32_t            pulse)
{
  MetaWaylandXAppSurface *xapp_surface = wl_resource_get_user_data (resource);

  xapp_surface->progress_pulse = pulse != 0;
  xapp_surface->progress_pulse_set = TRUE;

  apply_pending_hints (xapp_surface);
}

static void
xapp_surface_destroy (struct wl_client   *client,
                      struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct xapp_surface_v1_interface xapp_surface_implementation = {
  xapp_surface_set_icon_name,
  xapp_surface_set_progress,
  xapp_surface_set_progress_pulse,
  xapp_surface_destroy,
};

static void
xapp_surface_destructor (struct wl_resource *resource)
{
  MetaWaylandXAppSurface *xapp_surface = wl_resource_get_user_data (resource);

  if (xapp_surface->surface)
    {
      if (g_object_get_data (G_OBJECT (xapp_surface->surface),
                             XAPP_SURFACE_DATA_KEY) == xapp_surface)
        g_object_set_data (G_OBJECT (xapp_surface->surface),
                           XAPP_SURFACE_DATA_KEY, NULL);

      g_object_remove_weak_pointer (G_OBJECT (xapp_surface->surface),
                                    (gpointer *) &xapp_surface->surface);
    }

  g_free (xapp_surface->icon_name);
  g_free (xapp_surface);
}

static void
xapp_shell_get_xapp_surface (struct wl_client   *client,
                             struct wl_resource *resource,
                             uint32_t            id,
                             struct wl_resource *surface_resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandXAppSurface *xapp_surface;

  if (surface == NULL)
    g_warning ("xapp-shell: wl_surface has no associated MetaWaylandSurface; "
               "the xapp_surface_v1 will not affect any window");

  xapp_surface = g_new0 (MetaWaylandXAppSurface, 1);
  xapp_surface->surface = surface;
  if (surface)
    {
      g_object_add_weak_pointer (G_OBJECT (surface),
                                 (gpointer *) &xapp_surface->surface);
      /* Let the window-managed path find these cached hints (see
       * meta_wayland_xapp_shell_apply_to_window). */
      g_object_set_data (G_OBJECT (surface), XAPP_SURFACE_DATA_KEY, xapp_surface);
    }

  xapp_surface->resource = wl_resource_create (client,
                                               &xapp_surface_v1_interface,
                                               wl_resource_get_version (resource),
                                               id);
  wl_resource_set_implementation (xapp_surface->resource,
                                  &xapp_surface_implementation,
                                  xapp_surface, xapp_surface_destructor);
}

static void
xapp_shell_destroy (struct wl_client   *client,
                    struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct xapp_shell_v1_interface xapp_shell_implementation = {
  xapp_shell_get_xapp_surface,
  xapp_shell_destroy,
};

static void
bind_xapp_shell (struct wl_client *client,
                 void             *data,
                 uint32_t          version,
                 uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client, &xapp_shell_v1_interface, version, id);
  wl_resource_set_implementation (resource, &xapp_shell_implementation,
                                  NULL, NULL);

  xapp_shell_v1_send_capabilities (resource,
                                 XAPP_SHELL_V1_CAPABILITY_ICON_NAME |
                                 XAPP_SHELL_V1_CAPABILITY_PROGRESS);
}

void
meta_wayland_xapp_shell_init (MetaWaylandCompositor *compositor)
{
  if (wl_global_create (compositor->wayland_display,
                        &xapp_shell_v1_interface,
                        META_XAPP_SHELL_V1_VERSION,
                        NULL,
                        bind_xapp_shell) == NULL)
    g_error ("Failed to register a global xapp-shell object");
}
