/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
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
 * Written by:
 *     Jason Francis <cycl0ps@tuta.io>
 */

 #ifndef META_WAYLAND_FOREIGN_PRIVATE_H
 #define META_WAYLAND_FOREIGN_PRIVATE_H
 
 #include <glib.h>
 
 #include <wayland-server.h>
 
 #include "wayland/meta-wayland-xdg-foreign.h"
 
 typedef struct _MetaWaylandXdgExported MetaWaylandXdgExported;
 typedef struct _MetaWaylandXdgImported MetaWaylandXdgImported;
 
 
 typedef void (* MetaWaylandResourceFunc) (struct wl_resource *resource);
 
 gboolean meta_wayland_xdg_foreign_is_valid_surface (MetaWaylandSurface *surface,
                                                     struct wl_resource *exporter);
 
 MetaWaylandXdgExported * meta_wayland_xdg_foreign_export (MetaWaylandXdgForeign *foreign,
                                                           struct wl_resource    *resource,
                                                           MetaWaylandSurface    *surface);
 
 const char * meta_wayland_xdg_exported_get_handle (MetaWaylandXdgExported *exported);
 
 void meta_wayland_xdg_exported_destroy (MetaWaylandXdgExported *exported);
 
 MetaWaylandXdgImported * meta_wayland_xdg_foreign_import (MetaWaylandXdgForeign   *foreign,
                                                           struct wl_resource      *resource,
                                                           const char              *handle,
                                                           MetaWaylandResourceFunc  send_destroyed_func);
 
 void meta_wayland_xdg_imported_set_parent_of (MetaWaylandXdgImported *imported,
                                               struct wl_resource     *surface_resource);
 
 void meta_wayland_xdg_imported_destroy (MetaWaylandXdgImported *imported);
 
 #endif /* META_WAYLAND_FOREIGN_PRIVATE_H */