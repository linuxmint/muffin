/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2015 Red Hat Inc.
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
 * Written by:
 *     Jonas Ådahl <jadahl@gmail.com>
 */

 #include "config.h"

 #include "wayland/meta-wayland-legacy-xdg-foreign.h"
 
 #include <wayland-server.h>
 
 #include "core/util-private.h"
 #include "wayland/meta-wayland-private.h"
 #include "wayland/meta-wayland-versions.h"
 #include "wayland/meta-wayland-xdg-foreign-private.h"
 
 #include "xdg-foreign-unstable-v1-server-protocol.h"
 
 static void
 xdg_exporter_v1_destroy (struct wl_client   *client,
                          struct wl_resource *resource)
 {
   wl_resource_destroy (resource);
 }
 
 static void
 xdg_exported_v1_destroy (struct wl_client   *client,
                          struct wl_resource *resource)
 {
   wl_resource_destroy (resource);
 }
 
 static const struct zxdg_exported_v1_interface meta_xdg_exported_v1_interface = {
   xdg_exported_v1_destroy,
 };
 
 static void
 xdg_exported_v1_destructor (struct wl_resource *resource)
 {
   MetaWaylandXdgExported *exported = wl_resource_get_user_data (resource);
 
   if (exported)
     meta_wayland_xdg_exported_destroy (exported);
 }
 
 static void
 xdg_exporter_v1_export (struct wl_client   *client,
                         struct wl_resource *resource,
                         uint32_t            id,
                         struct wl_resource *surface_resource)
 {
   MetaWaylandXdgForeign *foreign = wl_resource_get_user_data (resource);
   MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
   struct wl_resource *xdg_exported_resource;
   MetaWaylandXdgExported *exported;
   const char *handle;
 
   if (!meta_wayland_xdg_foreign_is_valid_surface (surface, resource))
     return;
 
   xdg_exported_resource =
     wl_resource_create (client,
                         &zxdg_exported_v1_interface,
                         wl_resource_get_version (resource),
                         id);
   if (!xdg_exported_resource)
     {
       wl_client_post_no_memory (client);
       return;
     }
 
   exported = meta_wayland_xdg_foreign_export (foreign, xdg_exported_resource, surface);
   if (!exported)
     return;
 
   wl_resource_set_implementation (xdg_exported_resource,
                                   &meta_xdg_exported_v1_interface,
                                   exported,
                                   xdg_exported_v1_destructor);
 
   handle = meta_wayland_xdg_exported_get_handle (exported);
 
   zxdg_exported_v1_send_handle (xdg_exported_resource, handle);
 }
 
 static const struct zxdg_exporter_v1_interface meta_xdg_exporter_v1_interface = {
   xdg_exporter_v1_destroy,
   xdg_exporter_v1_export,
 };
 
 static void
 bind_xdg_exporter_v1 (struct wl_client *client,
                       void             *data,
                       uint32_t          version,
                       uint32_t          id)
 {
   MetaWaylandXdgForeign *foreign = data;
   struct wl_resource *resource;
 
   resource = wl_resource_create (client,
                                  &zxdg_exporter_v1_interface,
                                  META_ZXDG_EXPORTER_V1_VERSION,
                                  id);
 
   if (resource == NULL)
     {
       wl_client_post_no_memory (client);
       return;
     }
 
   wl_resource_set_implementation (resource,
                                   &meta_xdg_exporter_v1_interface,
                                   foreign, NULL);
 }
 
 static void
 xdg_imported_v1_destroy (struct wl_client   *client,
                          struct wl_resource *resource)
 {
   wl_resource_destroy (resource);
 }
 
 static void
 xdg_imported_v1_set_parent_of (struct wl_client   *client,
                                struct wl_resource *resource,
                                struct wl_resource *surface_resource)
 {
   MetaWaylandXdgImported *imported = wl_resource_get_user_data (resource);
 
   meta_wayland_xdg_imported_set_parent_of (imported, surface_resource);
 }
 
 static const struct zxdg_imported_v1_interface meta_xdg_imported_v1_interface = {
   xdg_imported_v1_destroy,
   xdg_imported_v1_set_parent_of,
 };
 
 static void
 xdg_importer_v1_destroy (struct wl_client   *client,
                          struct wl_resource *resource)
 {
   wl_resource_destroy (resource);
 }
 
 static void
 xdg_imported_v1_destructor (struct wl_resource *resource)
 {
   MetaWaylandXdgImported *imported;
 
   imported = wl_resource_get_user_data (resource);
   if (!imported)
     return;
 
   meta_wayland_xdg_imported_destroy (imported);
 }
 
 static void
 xdg_importer_v1_import (struct wl_client   *client,
                         struct wl_resource *resource,
                         uint32_t            id,
                         const char         *handle)
 {
   MetaWaylandXdgForeign *foreign = wl_resource_get_user_data (resource);
   struct wl_resource *xdg_imported_resource;
   MetaWaylandXdgImported *imported;
 
   xdg_imported_resource =
     wl_resource_create (client,
                         &zxdg_imported_v1_interface,
                         wl_resource_get_version (resource),
                         id);
   if (!xdg_imported_resource)
     {
       wl_client_post_no_memory (client);
       return;
     }
 
   imported = meta_wayland_xdg_foreign_import (foreign, xdg_imported_resource,
                                               handle,
                                               zxdg_imported_v1_send_destroyed);
   if (!imported)
     {
       zxdg_imported_v1_send_destroyed (xdg_imported_resource);
       return;
     }
 
   wl_resource_set_implementation (xdg_imported_resource,
                                   &meta_xdg_imported_v1_interface,
                                   imported,
                                   xdg_imported_v1_destructor);
 }
 
 static const struct zxdg_importer_v1_interface meta_xdg_importer_v1_interface = {
   xdg_importer_v1_destroy,
   xdg_importer_v1_import,
 };
 
 static void
 bind_xdg_importer_v1 (struct wl_client *client,
                       void             *data,
                       uint32_t          version,
                       uint32_t          id)
 {
   MetaWaylandXdgForeign *foreign = data;
   struct wl_resource *resource;
 
   resource = wl_resource_create (client,
                                  &zxdg_importer_v1_interface,
                                  META_ZXDG_IMPORTER_V1_VERSION,
                                  id);
 
   if (resource == NULL)
     {
       wl_client_post_no_memory (client);
       return;
     }
 
   wl_resource_set_implementation (resource,
                                   &meta_xdg_importer_v1_interface,
                                   foreign,
                                   NULL);
 }
 
 gboolean
 meta_wayland_legacy_xdg_foreign_init (MetaWaylandCompositor *compositor)
 {
   if (wl_global_create (compositor->wayland_display,
                         &zxdg_exporter_v1_interface, 1,
                         compositor->foreign,
                         bind_xdg_exporter_v1) == NULL)
     return FALSE;
 
   if (wl_global_create (compositor->wayland_display,
                         &zxdg_importer_v1_interface, 1,
                         compositor->foreign,
                         bind_xdg_importer_v1) == NULL)
     return FALSE;
 
   return TRUE;
 }