/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2026 Linux Mint
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "config.h"

#include "core/meta-window-debug-dbus.h"

#include <gio/gio.h>

#include "backends/meta-logical-monitor.h"
#include "core/window-private.h"
#include "meta/main.h"
#include "meta/workspace.h"

#ifdef HAVE_WAYLAND
#include "wayland/meta-wayland-surface.h"
#include "wayland/meta-window-wayland.h"
#endif

#define META_WINDOW_DEBUG_DBUS_SERVICE "org.cinnamon.Muffin.Debug"
#define META_WINDOW_DEBUG_DBUS_PATH "/org/cinnamon/Muffin/Debug"
#define META_WINDOW_DEBUG_DBUS_IFACE "org.cinnamon.Muffin.Debug"

typedef struct
{
  MetaDisplay *display;
  guint name_id;
  guint registration_id;
  GDBusConnection *connection;
  GDBusNodeInfo *introspection_data;
} MetaWindowDebugDbus;

static MetaWindowDebugDbus *debug_dbus;

static const char introspection_xml[] =
  "<node>"
  "  <interface name='org.cinnamon.Muffin.Debug'>"
  "    <method name='ListWindows'>"
  "      <arg name='windows' type='aa{sv}' direction='out'/>"
  "    </method>"
  "  </interface>"
  "</node>";

static const char *
safe_string (const char *str)
{
  return str ? str : "";
}

static void
add_rect (GVariantBuilder     *builder,
          const char          *name,
          const MetaRectangle *rect)
{
  g_variant_builder_add (builder, "{sv}", name,
                         g_variant_new ("(iiii)",
                                        rect->x,
                                        rect->y,
                                        rect->width,
                                        rect->height));
}

static const char *
client_type_to_string (MetaWindowClientType client_type)
{
  switch (client_type)
    {
    case META_WINDOW_CLIENT_TYPE_X11:
      return "x11";
    case META_WINDOW_CLIENT_TYPE_WAYLAND:
      return "wayland";
    }

  return "unknown";
}

static const char *
window_backend_to_string (MetaWindow *window)
{
  if (meta_window_get_client_type (window) == META_WINDOW_CLIENT_TYPE_X11 &&
      meta_is_wayland_compositor ())
    return "xwayland";

  return client_type_to_string (meta_window_get_client_type (window));
}

static int
get_surface_scale (MetaWindow *window)
{
#ifdef HAVE_WAYLAND
  if (window->surface)
    return window->surface->scale;
#endif

  return 1;
}

static int
get_geometry_scale (MetaWindow *window)
{
#ifdef HAVE_WAYLAND
  if (meta_window_get_client_type (window) == META_WINDOW_CLIENT_TYPE_WAYLAND)
    return meta_window_wayland_get_geometry_scale (window);
#endif

  return 1;
}

static double
get_monitor_scale (MetaWindow *window)
{
  if (!window->monitor)
    return 1.0;

  return meta_logical_monitor_get_scale (window->monitor);
}

static int
get_workspace_index (MetaWindow *window)
{
  MetaWorkspace *workspace;

  workspace = meta_window_get_workspace (window);
  if (!workspace)
    return -1;

  return meta_workspace_index (workspace);
}

static void
add_window (GVariantBuilder *builder,
            MetaWindow      *window)
{
  GVariantBuilder window_builder;
  MetaRectangle frame_rect;
  MetaRectangle buffer_rect;
  MetaRectangle client_area_rect;
  MetaRectangle titlebar_rect;
  const char *app_id;

  app_id = meta_window_get_gtk_application_id (window);
  if (!app_id)
    app_id = meta_window_get_sandboxed_app_id (window);
  if (!app_id)
    app_id = meta_window_get_wm_class (window);
  if (!app_id)
    app_id = meta_window_get_wm_class_instance (window);

  meta_window_get_frame_rect (window, &frame_rect);
  meta_window_get_buffer_rect (window, &buffer_rect);
  meta_window_get_client_area_rect (window, &client_area_rect);
  meta_window_get_titlebar_rect (window, &titlebar_rect);

  g_variant_builder_init (&window_builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&window_builder, "{sv}", "id",
                         g_variant_new_uint64 (window->id));
  g_variant_builder_add (&window_builder, "{sv}", "stable-sequence",
                         g_variant_new_uint32 (meta_window_get_stable_sequence (window)));
  g_variant_builder_add (&window_builder, "{sv}", "title",
                         g_variant_new_string (safe_string (meta_window_get_title (window))));
  g_variant_builder_add (&window_builder, "{sv}", "app-id",
                         g_variant_new_string (safe_string (app_id)));
  g_variant_builder_add (&window_builder, "{sv}", "app-name",
                         g_variant_new_string (safe_string (app_id)));
  g_variant_builder_add (&window_builder, "{sv}", "gtk-application-id",
                         g_variant_new_string (safe_string (meta_window_get_gtk_application_id (window))));
  g_variant_builder_add (&window_builder, "{sv}", "sandboxed-app-id",
                         g_variant_new_string (safe_string (meta_window_get_sandboxed_app_id (window))));
  g_variant_builder_add (&window_builder, "{sv}", "wm-class",
                         g_variant_new_string (safe_string (meta_window_get_wm_class (window))));
  g_variant_builder_add (&window_builder, "{sv}", "wm-class-instance",
                         g_variant_new_string (safe_string (meta_window_get_wm_class_instance (window))));
  g_variant_builder_add (&window_builder, "{sv}", "backend",
                         g_variant_new_string (window_backend_to_string (window)));
  g_variant_builder_add (&window_builder, "{sv}", "client-type",
                         g_variant_new_string (client_type_to_string (meta_window_get_client_type (window))));
  g_variant_builder_add (&window_builder, "{sv}", "xwindow",
                         g_variant_new_uint64 (meta_window_get_xwindow (window)));
  g_variant_builder_add (&window_builder, "{sv}", "pid",
                         g_variant_new_int32 (meta_window_get_pid (window)));
  g_variant_builder_add (&window_builder, "{sv}", "client-pid",
                         g_variant_new_uint32 (meta_window_get_client_pid (window)));
  g_variant_builder_add (&window_builder, "{sv}", "monitor",
                         g_variant_new_int32 (meta_window_get_monitor (window)));
  g_variant_builder_add (&window_builder, "{sv}", "workspace",
                         g_variant_new_int32 (get_workspace_index (window)));
  g_variant_builder_add (&window_builder, "{sv}", "scale",
                         g_variant_new_int32 (get_surface_scale (window)));
  g_variant_builder_add (&window_builder, "{sv}", "surface-scale",
                         g_variant_new_int32 (get_surface_scale (window)));
  g_variant_builder_add (&window_builder, "{sv}", "geometry-scale",
                         g_variant_new_int32 (get_geometry_scale (window)));
  g_variant_builder_add (&window_builder, "{sv}", "monitor-scale",
                         g_variant_new_double (get_monitor_scale (window)));

  add_rect (&window_builder, "frame-rect", &frame_rect);
  add_rect (&window_builder, "buffer-rect", &buffer_rect);
  add_rect (&window_builder, "client-area-rect", &client_area_rect);
  add_rect (&window_builder, "titlebar-rect", &titlebar_rect);

  g_variant_builder_add (&window_builder, "{sv}", "mapped",
                         g_variant_new_boolean (window->mapped));
  g_variant_builder_add (&window_builder, "{sv}", "hidden",
                         g_variant_new_boolean (meta_window_is_hidden (window)));
  g_variant_builder_add (&window_builder, "{sv}", "focused",
                         g_variant_new_boolean (meta_window_appears_focused (window)));
  g_variant_builder_add (&window_builder, "{sv}", "demands-attention",
                         g_variant_new_boolean (window->wm_state_demands_attention));
  g_variant_builder_add (&window_builder, "{sv}", "decorated",
                         g_variant_new_boolean (window->decorated));
  g_variant_builder_add (&window_builder, "{sv}", "client-decorated",
                         g_variant_new_boolean (meta_window_is_client_decorated (window)));
  g_variant_builder_add (&window_builder, "{sv}", "override-redirect",
                         g_variant_new_boolean (meta_window_is_override_redirect (window)));
  g_variant_builder_add (&window_builder, "{sv}", "skip-taskbar",
                         g_variant_new_boolean (meta_window_is_skip_taskbar (window)));
  g_variant_builder_add (&window_builder, "{sv}", "on-all-workspaces",
                         g_variant_new_boolean (meta_window_is_on_all_workspaces (window)));
  g_variant_builder_add (&window_builder, "{sv}", "window-type",
                         g_variant_new_int32 (meta_window_get_window_type (window)));

  g_variant_builder_add_value (builder, g_variant_builder_end (&window_builder));
}

static GVariant *
list_windows (MetaDisplay *display)
{
  GVariantBuilder builder;
  GSList *windows;
  GSList *l;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("aa{sv}"));

  windows = meta_display_list_windows (display,
                                       META_LIST_INCLUDE_OVERRIDE_REDIRECT |
                                       META_LIST_SORTED);

  for (l = windows; l; l = l->next)
    add_window (&builder, l->data);

  g_slist_free (windows);

  return g_variant_builder_end (&builder);
}

static void
handle_method_call (GDBusConnection       *connection,
                    const char            *sender,
                    const char            *object_path,
                    const char            *interface_name,
                    const char            *method_name,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               user_data)
{
  MetaWindowDebugDbus *dbus = user_data;

  if (g_strcmp0 (interface_name, META_WINDOW_DEBUG_DBUS_IFACE) != 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             G_DBUS_ERROR,
                                             G_DBUS_ERROR_UNKNOWN_INTERFACE,
                                             "Unknown interface %s",
                                             interface_name);
      return;
    }

  if (g_strcmp0 (method_name, "ListWindows") == 0)
    {
      GVariant *windows;

      windows = list_windows (dbus->display);
      g_dbus_method_invocation_return_value (invocation,
                                             g_variant_new ("(@aa{sv})",
                                                            windows));
      return;
    }

  g_dbus_method_invocation_return_error (invocation,
                                         G_DBUS_ERROR,
                                         G_DBUS_ERROR_UNKNOWN_METHOD,
                                         "Unknown method %s",
                                         method_name);
}

static const GDBusInterfaceVTable interface_vtable =
{
  handle_method_call,
  NULL,
  NULL
};

static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
  MetaWindowDebugDbus *dbus = user_data;
  GError *error = NULL;

  dbus->connection = g_object_ref (connection);
  dbus->registration_id =
    g_dbus_connection_register_object (connection,
                                       META_WINDOW_DEBUG_DBUS_PATH,
                                       dbus->introspection_data->interfaces[0],
                                       &interface_vtable,
                                       dbus,
                                       NULL,
                                       &error);

  if (!dbus->registration_id)
    {
      g_warning ("Failed to export window debug object: %s", error->message);
      g_error_free (error);
    }
}

static void
on_name_lost (GDBusConnection *connection,
              const char      *name,
              gpointer         user_data)
{
  g_warning ("Lost or failed to acquire name %s", name);
}

void
meta_window_debug_dbus_init (MetaDisplay *display)
{
  GError *error = NULL;

  g_return_if_fail (display != NULL);

  if (debug_dbus)
    return;

  debug_dbus = g_new0 (MetaWindowDebugDbus, 1);
  debug_dbus->display = display;
  debug_dbus->introspection_data =
    g_dbus_node_info_new_for_xml (introspection_xml, &error);

  if (!debug_dbus->introspection_data)
    {
      g_warning ("Failed to parse window debug introspection XML: %s",
                 error->message);
      g_error_free (error);
      g_clear_pointer (&debug_dbus, g_free);
      return;
    }

  debug_dbus->name_id =
    g_bus_own_name (G_BUS_TYPE_SESSION,
                    META_WINDOW_DEBUG_DBUS_SERVICE,
                    G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                    (meta_get_replace_current_wm () ?
                     G_BUS_NAME_OWNER_FLAGS_REPLACE : 0),
                    on_bus_acquired,
                    NULL,
                    on_name_lost,
                    debug_dbus,
                    NULL);
}

void
meta_window_debug_dbus_shutdown (void)
{
  if (!debug_dbus)
    return;

  if (debug_dbus->connection && debug_dbus->registration_id)
    g_dbus_connection_unregister_object (debug_dbus->connection,
                                         debug_dbus->registration_id);

  g_clear_object (&debug_dbus->connection);
  g_clear_handle_id (&debug_dbus->name_id, g_bus_unown_name);
  g_clear_pointer (&debug_dbus->introspection_data, g_dbus_node_info_unref);
  g_clear_pointer (&debug_dbus, g_free);
}
