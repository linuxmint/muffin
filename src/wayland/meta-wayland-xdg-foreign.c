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

#include "wayland/meta-wayland-xdg-foreign.h"

#include <wayland-server.h>

#include "core/util-private.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-versions.h"
#include "wayland/meta-wayland-xdg-foreign-private.h"
#include "wayland/meta-wayland-xdg-shell.h"

#include "xdg-foreign-unstable-v2-server-protocol.h"

#define META_XDG_FOREIGN_HANDLE_LENGTH 32

typedef struct _MetaWaylandXdgExported MetaWaylandXdgExported;
typedef struct _MetaWaylandXdgImported MetaWaylandXdgImported;

typedef struct _MetaWaylandXdgForeign
{
  MetaWaylandCompositor *compositor;
  GRand *rand;

  GHashTable *exported_surfaces;
} MetaWaylandXdgForeign;

struct _MetaWaylandXdgExported
{
  MetaWaylandXdgForeign *foreign;
  struct wl_resource *resource;

  MetaWaylandSurface *surface;
  gulong surface_unmapped_handler_id;
  char *handle;

  GList *imported;
};

struct _MetaWaylandXdgImported
{
  MetaWaylandXdgForeign *foreign;
  struct wl_resource *resource;
  MetaWaylandResourceFunc send_destroyed_func;

  MetaWaylandSurface *parent_of;
  gulong parent_of_unmapped_handler_id;

  MetaWaylandXdgExported *exported;
};

static void
xdg_exporter_destroy (struct wl_client   *client,
                      struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
xdg_exported_destroy (struct wl_client   *client,
                      struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct zxdg_exported_v2_interface meta_xdg_exported_interface = {
  xdg_exported_destroy,
};

void
meta_wayland_xdg_exported_destroy (MetaWaylandXdgExported *exported)
{
  MetaWaylandXdgForeign *foreign = exported->foreign;

  while (exported->imported)
    {
      MetaWaylandXdgImported *imported = exported->imported->data;

      imported->send_destroyed_func (imported->resource);
      meta_wayland_xdg_imported_destroy (imported);
    }

  g_clear_signal_handler (&exported->surface_unmapped_handler_id,
                          exported->surface);
  wl_resource_set_user_data (exported->resource, NULL);

  g_hash_table_remove (foreign->exported_surfaces, exported->handle);

  g_free (exported->handle);
  g_free (exported);
}

static void
xdg_exported_destructor (struct wl_resource *resource)
{
  MetaWaylandXdgExported *exported = wl_resource_get_user_data (resource);

  if (exported)
    meta_wayland_xdg_exported_destroy (exported);
}

static void
exported_surface_unmapped (MetaWaylandSurface     *surface,
                           MetaWaylandXdgExported *exported)
{
  meta_wayland_xdg_exported_destroy (exported);
}

gboolean
meta_wayland_xdg_foreign_is_valid_surface (MetaWaylandSurface *surface,
                                           struct wl_resource *exporter)
{
  if (!surface->role ||
      !meta_wayland_surface_get_window (surface) || 
      !META_IS_WAYLAND_XDG_SURFACE (surface->role))
    {
      wl_resource_post_error (exporter,
                              WL_DISPLAY_ERROR_INVALID_OBJECT,
                              "exported surface had an invalid role");
      return FALSE;
    }
  return TRUE;
}

MetaWaylandXdgExported *
meta_wayland_xdg_foreign_export (MetaWaylandXdgForeign *foreign,
                                 struct wl_resource    *resource,
                                 MetaWaylandSurface    *surface)
{
  MetaWaylandXdgExported *exported;
  char *handle;

  exported = g_new0 (MetaWaylandXdgExported, 1);
  exported->foreign = foreign;
  exported->surface = surface;
  exported->resource = resource;

  exported->surface_unmapped_handler_id =
    g_signal_connect (surface, "unmapped",
                      G_CALLBACK (exported_surface_unmapped),
                      exported);

  wl_resource_set_implementation (resource,
                                  &meta_xdg_exported_interface,
                                  exported,
                                  xdg_exported_destructor);

  while (TRUE)
    {
      handle = meta_generate_random_id (foreign->rand,
                                        META_XDG_FOREIGN_HANDLE_LENGTH);

      if (!g_hash_table_contains (foreign->exported_surfaces, handle))
        {
          g_hash_table_insert (foreign->exported_surfaces, handle, exported);
          break;
        }

      g_free (handle);
    }

  exported->handle = handle;

  return exported;
}

static void
xdg_exporter_export (struct wl_client   *client,
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
                        &zxdg_exported_v2_interface,
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
                                  &meta_xdg_exported_interface,
                                  exported,
                                  xdg_exported_destructor);

  handle = meta_wayland_xdg_exported_get_handle (exported);

  zxdg_exported_v2_send_handle (xdg_exported_resource, handle);
}

static const struct zxdg_exporter_v2_interface meta_xdg_exporter_interface = {
  xdg_exporter_destroy,
  xdg_exporter_export,
};

static void
bind_xdg_exporter (struct wl_client *client,
                   void             *data,
                   uint32_t          version,
                   uint32_t          id)
{
  MetaWaylandXdgForeign *foreign = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &zxdg_exporter_v2_interface,
                                 META_ZXDG_EXPORTER_V2_VERSION,
                                 id);

  if (resource == NULL)
    {
      wl_client_post_no_memory (client);
      return;
    }

  wl_resource_set_implementation (resource,
                                  &meta_xdg_exporter_interface,
                                  foreign, NULL);
}

const char *
meta_wayland_xdg_exported_get_handle (MetaWaylandXdgExported *exported)
{
  return exported->handle;
}

static void
xdg_imported_destroy (struct wl_client   *client,
                      struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
imported_parent_of_unmapped (MetaWaylandSurface     *surface,
                             MetaWaylandXdgImported *imported)
{
  imported->parent_of = NULL;
}

static gboolean
is_valid_child (MetaWaylandSurface *surface)
{
  if (!surface)
    return TRUE;

  if (!surface->role)
    return FALSE;

  if (!META_IS_WAYLAND_XDG_TOPLEVEL (surface->role))
    return FALSE;

  if (!meta_wayland_surface_get_window (surface))
    return FALSE;

  return TRUE;
}

void
meta_wayland_xdg_imported_set_parent_of (MetaWaylandXdgImported *imported,
                                         struct wl_resource     *surface_resource)
{
  MetaWaylandSurface *surface;

  if (!imported)
    return;

  if (surface_resource)
    surface = wl_resource_get_user_data (surface_resource);
  else
    surface = NULL;

  if (!is_valid_child (surface))
    {
      wl_resource_post_error (imported->resource,
                              WL_DISPLAY_ERROR_INVALID_OBJECT,
                              "set_parent_of was called with an invalid child");
      return;
    }

  if (imported->parent_of)
   g_clear_signal_handler (&imported->parent_of_unmapped_handler_id,
                           imported->parent_of);

  imported->parent_of = surface;

  if (surface)
    {
      MetaWindow *window;
      MetaWindow *exported_window;

      imported->parent_of_unmapped_handler_id =
        g_signal_connect (surface, "unmapped",
                          G_CALLBACK (imported_parent_of_unmapped),
                          imported);

      window = meta_wayland_surface_get_window (surface);
      exported_window =
        meta_wayland_surface_get_window (imported->exported->surface);
      meta_window_set_transient_for (window, exported_window);
    }
}

static void
xdg_imported_set_parent_of (struct wl_client   *client,
                            struct wl_resource *resource,
                            struct wl_resource *surface_resource)
{
  MetaWaylandXdgImported *imported = wl_resource_get_user_data (resource);

  meta_wayland_xdg_imported_set_parent_of (imported, surface_resource);
}

static const struct zxdg_imported_v2_interface meta_xdg_imported_interface = {
  xdg_imported_destroy,
  xdg_imported_set_parent_of,
};

static void
xdg_importer_destroy (struct wl_client   *client,
                      struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

void
meta_wayland_xdg_imported_destroy (MetaWaylandXdgImported *imported)
{
  MetaWaylandXdgExported *exported = imported->exported;

  exported->imported = g_list_remove (exported->imported, imported);

  if (imported->parent_of)
    {
      MetaWindow *window;

      g_clear_signal_handler (&imported->parent_of_unmapped_handler_id,
                              imported->parent_of);

      window = meta_wayland_surface_get_window (imported->parent_of);
      if (window)
        meta_window_set_transient_for (window, NULL);
    }

  wl_resource_set_user_data (imported->resource, NULL);

  g_free (imported);
}

static void
xdg_imported_destructor (struct wl_resource *resource)
{
  MetaWaylandXdgImported *imported;

  imported = wl_resource_get_user_data (resource);
  if (!imported)
    return;

  meta_wayland_xdg_imported_destroy (imported);
}

MetaWaylandXdgImported *
meta_wayland_xdg_foreign_import (MetaWaylandXdgForeign   *foreign,
                                 struct wl_resource      *resource,
                                 const char              *handle,
                                 MetaWaylandResourceFunc  send_destroyed_func)
{
  MetaWaylandXdgImported *imported;
  MetaWaylandXdgExported *exported;

  exported = g_hash_table_lookup (foreign->exported_surfaces, handle);
  if (!exported ||
      !META_IS_WAYLAND_XDG_SURFACE (exported->surface->role))
    return NULL;

  imported = g_new0 (MetaWaylandXdgImported, 1);
  imported->foreign = foreign;
  imported->exported = exported;
  imported->resource = resource;
  imported->send_destroyed_func = send_destroyed_func;

  exported->imported = g_list_prepend (exported->imported, imported);

  return imported;
}

static void
xdg_importer_import (struct wl_client   *client,
                     struct wl_resource *resource,
                     uint32_t            id,
                     const char         *handle)
{
  MetaWaylandXdgForeign *foreign = wl_resource_get_user_data (resource);
  struct wl_resource *xdg_imported_resource;
  MetaWaylandXdgImported *imported;

  xdg_imported_resource =
    wl_resource_create (client,
                        &zxdg_imported_v2_interface,
                        wl_resource_get_version (resource),
                        id);
  if (!xdg_imported_resource)
    {
      wl_client_post_no_memory (client);
      return;
    }

  imported = meta_wayland_xdg_foreign_import (foreign, xdg_imported_resource,
                                              handle,
                                              zxdg_imported_v2_send_destroyed);
  if (!imported)
    {
      zxdg_imported_v2_send_destroyed (xdg_imported_resource);
      return;
    }

    wl_resource_set_implementation (xdg_imported_resource,
                                    &meta_xdg_imported_interface,
                                    imported,
                                    xdg_imported_destructor);
}

static const struct zxdg_importer_v2_interface meta_xdg_importer_interface = {
  xdg_importer_destroy,
  xdg_importer_import,
};

static void
bind_xdg_importer (struct wl_client *client,
                   void             *data,
                   uint32_t          version,
                   uint32_t          id)
{
  MetaWaylandXdgForeign *foreign = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &zxdg_importer_v2_interface,
                                 META_ZXDG_IMPORTER_V2_VERSION,
                                 id);

  if (resource == NULL)
    {
      wl_client_post_no_memory (client);
      return;
    }

  wl_resource_set_implementation (resource,
                                  &meta_xdg_importer_interface,
                                  foreign,
                                  NULL);
}

gboolean
meta_wayland_xdg_foreign_init (MetaWaylandCompositor *compositor)
{
  MetaWaylandXdgForeign *foreign;

  foreign = g_new0 (MetaWaylandXdgForeign, 1);

  foreign->compositor = compositor;
  foreign->rand = g_rand_new ();
  foreign->exported_surfaces = g_hash_table_new ((GHashFunc) g_str_hash,
                                                 (GEqualFunc) g_str_equal);

  compositor->foreign = foreign;

  if (wl_global_create (compositor->wayland_display,
                        &zxdg_exporter_v2_interface, 1,
                        foreign,
                        bind_xdg_exporter) == NULL)
    return FALSE;

  if (wl_global_create (compositor->wayland_display,
                        &zxdg_importer_v2_interface, 1,
                        foreign,
                        bind_xdg_importer) == NULL)
    return FALSE;

  return TRUE;
}
