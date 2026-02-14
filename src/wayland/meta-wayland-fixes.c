/*
 * Copyright (C) 2024 Red Hat, Inc.
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

#include "wayland/meta-wayland-fixes.h"

#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-versions.h"

static void
wl_fixes_destroy (struct wl_client   *client,
                  struct wl_resource *resource)
{
    wl_resource_destroy (resource);
}

static void
wl_fixes_destroy_registry (struct wl_client   *client,
                           struct wl_resource *resource,
                           struct wl_resource *registry_resource)
{
    wl_resource_destroy (registry_resource);
}

static const struct wl_fixes_interface meta_wayland_fixes_interface = {
    wl_fixes_destroy,
    wl_fixes_destroy_registry,
};

static void
bind_wl_fixes (struct wl_client *client,
               void             *data,
               uint32_t          version,
               uint32_t          id)
{
    MetaWaylandCompositor *compositor = data;
    struct wl_resource *resource;

    resource = wl_resource_create (client, &wl_fixes_interface, version, id);
    wl_resource_set_implementation (resource,
                                    &meta_wayland_fixes_interface,
                                    compositor,
                                    NULL);
}

void
meta_wayland_init_fixes (MetaWaylandCompositor *compositor)
{
    if (wl_global_create (compositor->wayland_display,
        &wl_fixes_interface,
        META_WL_FIXES_VERSION,
        compositor,
        bind_wl_fixes) == NULL)
        g_error ("Failed to register a global wl_fixes object");
}
