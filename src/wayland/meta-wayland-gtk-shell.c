/*
 * Wayland Support
 *
 * Copyright (C) 2012,2013 Intel Corporation
 *               2013-2016 Red Hat, Inc.
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
 */

#include "config.h"

#include "wayland/meta-wayland-gtk-shell.h"

#include "core/bell.h"
#include "core/window-private.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-surface.h"
#include "wayland/meta-wayland-versions.h"
#include "wayland/meta-window-wayland.h"

#include "gtk-shell-server-protocol.h"

static GQuark quark_gtk_surface_data = 0;

typedef struct _MetaWaylandGtkSurface
{
  struct wl_resource *resource;
  MetaWaylandSurface *surface;
  gboolean is_modal;
  gulong configure_handler_id;
} MetaWaylandGtkSurface;

struct _MetaWaylandGtkShell
{
  GObject parent;

  GList *shell_resources;
  uint32_t capabilities;
};

G_DEFINE_TYPE (MetaWaylandGtkShell, meta_wayland_gtk_shell, G_TYPE_OBJECT)

static void
gtk_surface_destructor (struct wl_resource *resource)
{
  MetaWaylandGtkSurface *gtk_surface = wl_resource_get_user_data (resource);

  if (gtk_surface->surface)
    {
      g_object_steal_qdata (G_OBJECT (gtk_surface->surface),
                            quark_gtk_surface_data);
      g_clear_signal_handler (&gtk_surface->configure_handler_id,
                              gtk_surface->surface);
    }

  g_free (gtk_surface);
}

static void
gtk_surface_set_dbus_properties (struct wl_client   *client,
                                 struct wl_resource *resource,
                                 const char         *application_id,
                                 const char         *app_menu_path,
                                 const char         *menubar_path,
                                 const char         *window_object_path,
                                 const char         *application_object_path,
                                 const char         *unique_bus_name)
{
  MetaWaylandGtkSurface *gtk_surface = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = gtk_surface->surface;
  MetaWindow *window;

  window = meta_wayland_surface_get_window (surface);
  if (!window)
    return;

  meta_window_set_gtk_dbus_properties (window,
                                       application_id,
                                       unique_bus_name,
                                       app_menu_path,
                                       menubar_path,
                                       application_object_path,
                                       window_object_path);
}

static void
gtk_surface_set_modal (struct wl_client   *client,
                       struct wl_resource *resource)
{
  MetaWaylandGtkSurface *gtk_surface = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = gtk_surface->surface;
  MetaWindow *window;

  window = meta_wayland_surface_get_window (surface);
  if (!window)
    return;

  if (gtk_surface->is_modal)
    return;

  gtk_surface->is_modal = TRUE;
  meta_window_set_type (window, META_WINDOW_MODAL_DIALOG);
}

static void
gtk_surface_unset_modal (struct wl_client   *client,
                         struct wl_resource *resource)
{
  MetaWaylandGtkSurface *gtk_surface = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = gtk_surface->surface;
  MetaWindow *window;

  window = meta_wayland_surface_get_window (surface);
  if (!window)
    return;

  if (!gtk_surface->is_modal)
    return;

  gtk_surface->is_modal = FALSE;
  meta_window_set_type (window, META_WINDOW_NORMAL);
}

static void
gtk_surface_present (struct wl_client   *client,
                     struct wl_resource *resource,
                     uint32_t            timestamp)
{
  MetaWaylandGtkSurface *gtk_surface = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = gtk_surface->surface;
  MetaWindow *window;

  window = meta_wayland_surface_get_window (surface);
  if (!window)
    return;

  meta_window_activate_full (window, timestamp,
                             META_CLIENT_TYPE_APPLICATION, NULL);
}

static void
gtk_surface_request_focus (struct wl_client   *client,
                           struct wl_resource *resource,
                           const char         *startup_id)
{
  MetaWaylandGtkSurface *gtk_surface = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = gtk_surface->surface;
  MetaDisplay *display = meta_get_display ();
  MetaStartupSequence *sequence = NULL;
  MetaWindow *window;

  window = meta_wayland_surface_get_window (surface);
  if (!window)
    return;

  if (startup_id)
    sequence = meta_startup_notification_lookup_sequence (display->startup_notification,
                                                          startup_id);

  if (sequence)
    {
      uint32_t timestamp;
      int32_t workspace_idx;

      workspace_idx = meta_startup_sequence_get_workspace (sequence);
      timestamp = meta_startup_sequence_get_timestamp (sequence);

      meta_startup_sequence_complete (sequence);
      meta_startup_notification_remove_sequence (display->startup_notification,
                                                 sequence);
      if (workspace_idx >= 0)
        meta_window_change_workspace_by_index (window, workspace_idx, TRUE);

      meta_window_activate_full (window, timestamp,
                                 META_CLIENT_TYPE_APPLICATION, NULL);
    }
  else
    {
      meta_window_set_demands_attention (window);
    }
}

static const struct gtk_surface1_interface meta_wayland_gtk_surface_interface = {
  gtk_surface_set_dbus_properties,
  gtk_surface_set_modal,
  gtk_surface_unset_modal,
  gtk_surface_present,
  gtk_surface_request_focus,
};

static void
gtk_surface_surface_destroyed (MetaWaylandGtkSurface *gtk_surface)
{
  wl_resource_set_implementation (gtk_surface->resource,
                                  NULL, NULL, NULL);
  gtk_surface->surface = NULL;
}

static void
fill_edge_states (struct wl_array *states,
                  MetaWindow      *window)
{
  uint32_t *s;

  if (window->edge_constraints.top != META_EDGE_CONSTRAINT_MONITOR)
    {
      s = wl_array_add (states, sizeof *s);
      *s = GTK_SURFACE1_EDGE_CONSTRAINT_RESIZABLE_TOP;
    }

  if (window->edge_constraints.right != META_EDGE_CONSTRAINT_MONITOR)
    {
      s = wl_array_add (states, sizeof *s);
      *s = GTK_SURFACE1_EDGE_CONSTRAINT_RESIZABLE_RIGHT;
    }

  if (window->edge_constraints.bottom != META_EDGE_CONSTRAINT_MONITOR)
    {
      s = wl_array_add (states, sizeof *s);
      *s = GTK_SURFACE1_EDGE_CONSTRAINT_RESIZABLE_BOTTOM;
    }

  if (window->edge_constraints.left != META_EDGE_CONSTRAINT_MONITOR)
    {
      s = wl_array_add (states, sizeof *s);
      *s = GTK_SURFACE1_EDGE_CONSTRAINT_RESIZABLE_LEFT;
    }
}

static void
send_configure_edges (MetaWaylandGtkSurface *gtk_surface,
                      MetaWindow            *window)
{
  struct wl_array edge_states;

  wl_array_init (&edge_states);
  fill_edge_states (&edge_states, window);

  gtk_surface1_send_configure_edges (gtk_surface->resource, &edge_states);

  wl_array_release (&edge_states);
}

static void
add_state_value (struct wl_array         *states,
                 enum gtk_surface1_state  state)
{
  uint32_t *s;

  s = wl_array_add (states, sizeof *s);
  *s = state;
}

static void
fill_states (struct wl_array    *states,
             MetaWindow         *window,
             struct wl_resource *resource)
{
  int version;

  version = wl_resource_get_version (resource);

  if (version < GTK_SURFACE1_CONFIGURE_EDGES_SINCE_VERSION &&
      (window->tile_mode == META_TILE_LEFT ||
       window->tile_mode == META_TILE_RIGHT))
    add_state_value (states, GTK_SURFACE1_STATE_TILED);

  if (version >= GTK_SURFACE1_STATE_TILED_TOP_SINCE_VERSION &&
      window->edge_constraints.top != META_EDGE_CONSTRAINT_NONE)
    add_state_value (states, GTK_SURFACE1_STATE_TILED_TOP);

  if (version >= GTK_SURFACE1_STATE_TILED_RIGHT_SINCE_VERSION &&
      window->edge_constraints.right != META_EDGE_CONSTRAINT_NONE)
    add_state_value (states, GTK_SURFACE1_STATE_TILED_RIGHT);

  if (version >= GTK_SURFACE1_STATE_TILED_BOTTOM_SINCE_VERSION &&
      window->edge_constraints.bottom != META_EDGE_CONSTRAINT_NONE)
    add_state_value (states, GTK_SURFACE1_STATE_TILED_BOTTOM);

  if (version >= GTK_SURFACE1_STATE_TILED_LEFT_SINCE_VERSION &&
      window->edge_constraints.left != META_EDGE_CONSTRAINT_NONE)
    add_state_value (states, GTK_SURFACE1_STATE_TILED_LEFT);
}

static void
send_configure (MetaWaylandGtkSurface *gtk_surface,
                MetaWindow            *window)
{
  struct wl_array states;

  wl_array_init (&states);
  fill_states (&states, window, gtk_surface->resource);

  gtk_surface1_send_configure (gtk_surface->resource, &states);

  wl_array_release (&states);
}

static void
on_configure (MetaWaylandSurface    *surface,
              MetaWaylandGtkSurface *gtk_surface)
{
  MetaWindow *window;

  window = meta_wayland_surface_get_window (surface);
  send_configure (gtk_surface, window);

  if (wl_resource_get_version (gtk_surface->resource) >=
      GTK_SURFACE1_CONFIGURE_EDGES_SINCE_VERSION)
    send_configure_edges (gtk_surface, window);
}

static void
gtk_shell_get_gtk_surface (struct wl_client   *client,
                           struct wl_resource *resource,
                           guint32             id,
                           struct wl_resource *surface_resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandGtkSurface *gtk_surface;

  gtk_surface = g_object_get_qdata (G_OBJECT (surface), quark_gtk_surface_data);
  if (gtk_surface)
    {
      wl_resource_post_error (surface_resource,
                              WL_DISPLAY_ERROR_INVALID_OBJECT,
                              "gtk_shell::get_gtk_surface already requested");
      return;
    }

  gtk_surface = g_new0 (MetaWaylandGtkSurface, 1);
  gtk_surface->surface = surface;
  gtk_surface->resource = wl_resource_create (client,
                                              &gtk_surface1_interface,
                                              wl_resource_get_version (resource),
                                              id);
  wl_resource_set_implementation (gtk_surface->resource,
                                  &meta_wayland_gtk_surface_interface,
                                  gtk_surface, gtk_surface_destructor);

  gtk_surface->configure_handler_id = g_signal_connect (surface,
                                                        "configure",
                                                        G_CALLBACK (on_configure),
                                                        gtk_surface);

  g_object_set_qdata_full (G_OBJECT (surface),
                           quark_gtk_surface_data,
                           gtk_surface,
                           (GDestroyNotify) gtk_surface_surface_destroyed);
}

static void
gtk_shell_set_startup_id (struct wl_client   *client,
                          struct wl_resource *resource,
                          const char         *startup_id)
{
  MetaStartupSequence *sequence;
  MetaDisplay *display;

  display = meta_get_display ();

  sequence = meta_startup_notification_lookup_sequence (display->startup_notification,
                                                        startup_id);
  if (sequence)
    meta_startup_sequence_complete (sequence);
}

static void
gtk_shell_system_bell (struct wl_client   *client,
                       struct wl_resource *resource,
                       struct wl_resource *gtk_surface_resource)
{
  MetaDisplay *display = meta_get_display ();

  if (gtk_surface_resource)
    {
      MetaWaylandGtkSurface *gtk_surface =
        wl_resource_get_user_data (gtk_surface_resource);
      MetaWaylandSurface *surface = gtk_surface->surface;
      MetaWindow *window;

      window = meta_wayland_surface_get_window (surface);
      if (!window)
        return;

      meta_bell_notify (display, window);
    }
  else
    {
      meta_bell_notify (display, NULL);
    }
}

static void
gtk_shell_notify_launch (struct wl_client   *client,
                         struct wl_resource *resource,
                         const char         *startup_id)
{
  MetaDisplay *display = meta_get_display ();
  MetaStartupSequence *sequence;
  uint32_t timestamp;

  sequence = meta_startup_notification_lookup_sequence (display->startup_notification,
                                                        startup_id);
  if (sequence)
    {
      g_warning ("Naughty client notified launch with duplicate startup_id '%s'",
                 startup_id);
      return;
    }

  timestamp = meta_display_get_current_time_roundtrip (display);
  sequence = g_object_new (META_TYPE_STARTUP_SEQUENCE,
                           "id", startup_id,
                           "timestamp", timestamp,
                           NULL);

  meta_startup_notification_add_sequence (display->startup_notification,
                                          sequence);
  g_object_unref (sequence);
}

static const struct gtk_shell1_interface meta_wayland_gtk_shell_interface = {
  gtk_shell_get_gtk_surface,
  gtk_shell_set_startup_id,
  gtk_shell_system_bell,
  gtk_shell_notify_launch,
};

static void
gtk_shell_destructor (struct wl_resource *resource)
{
  MetaWaylandGtkShell *gtk_shell = wl_resource_get_user_data (resource);

  gtk_shell->shell_resources = g_list_remove (gtk_shell->shell_resources,
                                              resource);
}

static void
bind_gtk_shell (struct wl_client *client,
                void             *data,
                guint32           version,
                guint32           id)
{
  MetaWaylandGtkShell *gtk_shell = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client, &gtk_shell1_interface, version, id);
  wl_resource_set_implementation (resource, &meta_wayland_gtk_shell_interface,
                                  data, gtk_shell_destructor);

  gtk_shell->shell_resources = g_list_prepend (gtk_shell->shell_resources,
                                               resource);

  gtk_shell1_send_capabilities (resource, gtk_shell->capabilities);
}

static void
meta_wayland_gtk_shell_init (MetaWaylandGtkShell *gtk_shell)
{
}

static void
meta_wayland_gtk_shell_class_init (MetaWaylandGtkShellClass *klass)
{
  quark_gtk_surface_data =
    g_quark_from_static_string ("-meta-wayland-gtk-shell-surface-data");
}

static uint32_t
calculate_capabilities (void)
{
  uint32_t capabilities = 0;

  if (!meta_prefs_get_show_fallback_app_menu ())
    capabilities = GTK_SHELL1_CAPABILITY_GLOBAL_APP_MENU;

  return capabilities;
}

static void
prefs_changed (MetaPreference pref,
               gpointer       user_data)
{
  MetaWaylandGtkShell *gtk_shell = user_data;
  uint32_t new_capabilities;
  GList *l;

  if (pref != META_PREF_BUTTON_LAYOUT)
    return;

  new_capabilities = calculate_capabilities ();
  if (gtk_shell->capabilities == new_capabilities)
    return;
  gtk_shell->capabilities = new_capabilities;

  for (l = gtk_shell->shell_resources; l; l = l->next)
    {
      struct wl_resource *resource = l->data;

      gtk_shell1_send_capabilities (resource, gtk_shell->capabilities);
    }
}

static MetaWaylandGtkShell *
meta_wayland_gtk_shell_new (MetaWaylandCompositor *compositor)
{
  MetaWaylandGtkShell *gtk_shell;

  gtk_shell = g_object_new (META_TYPE_WAYLAND_GTK_SHELL, NULL);

  if (wl_global_create (compositor->wayland_display,
                        &gtk_shell1_interface,
                        META_GTK_SHELL1_VERSION,
                        gtk_shell, bind_gtk_shell) == NULL)
    g_error ("Failed to register a global gtk-shell object");

  gtk_shell->capabilities = calculate_capabilities ();

  meta_prefs_add_listener (prefs_changed, gtk_shell);

  return gtk_shell;
}

void
meta_wayland_init_gtk_shell (MetaWaylandCompositor *compositor)
{
  g_object_set_data_full (G_OBJECT (compositor), "-meta-wayland-gtk-shell",
                          meta_wayland_gtk_shell_new (compositor),
                          g_object_unref);
}
