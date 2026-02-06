/*
 * Copyright (C) 2024 Linux Mint
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

#include "wayland/meta-wayland-foreign-toplevel.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor-manager-private.h"
#include "core/display-private.h"
#include "core/window-private.h"
#include "wayland/meta-wayland-outputs.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-versions.h"

#include "wlr-foreign-toplevel-management-unstable-v1-server-protocol.h"

typedef struct _MetaForeignToplevelManager MetaForeignToplevelManager;
typedef struct _MetaForeignToplevelHandle MetaForeignToplevelHandle;

struct _MetaForeignToplevelManager
{
    MetaWaylandCompositor *compositor;
    GList *manager_resources;
    GList *handles;

    gulong window_created_handler_id;
    gulong window_entered_monitor_handler_id;
    gulong window_left_monitor_handler_id;
};

struct _MetaForeignToplevelHandle
{
    MetaForeignToplevelManager *manager;
    MetaWindow *window;
    GList *handle_resources;

    gulong title_handler_id;
    gulong wm_class_handler_id;
    gulong minimized_handler_id;
    gulong maximized_h_handler_id;
    gulong maximized_v_handler_id;
    gulong fullscreen_handler_id;
    gulong appears_focused_handler_id;
    gulong skip_taskbar_handler_id;
    gulong unmanaging_handler_id;

    gboolean closed;
};

static struct wl_resource *
find_output_resource_for_client (MetaWaylandOutput *wayland_output,
                                 struct wl_client   *client)
{
    GList *l;

    if (!wayland_output)
        return NULL;

    for (l = wayland_output->resources; l; l = l->next)
    {
        struct wl_resource *resource = l->data;

        if (wl_resource_get_client (resource) == client)
            return resource;
    }

    return NULL;
}

static MetaWaylandOutput *
find_wayland_output_for_monitor (MetaWaylandCompositor *compositor,
                                 int                    monitor_index)
{
    MetaBackend *backend = meta_get_backend ();
    MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
    GList *logical_monitors;
    GList *l;

    logical_monitors = meta_monitor_manager_get_logical_monitors (monitor_manager);

    for (l = logical_monitors; l; l = l->next)
    {
        MetaLogicalMonitor *logical_monitor = l->data;

        if (logical_monitor->number == monitor_index)
        {
            GHashTableIter iter;
            gpointer key, value;

            g_hash_table_iter_init (&iter, compositor->outputs);
            while (g_hash_table_iter_next (&iter, &key, &value))
            {
                MetaWaylandOutput *wayland_output = value;

                if (wayland_output->logical_monitor == logical_monitor)
                    return wayland_output;
            }

            break;
        }
    }

    return NULL;
}

static void
send_state_to_resource (struct wl_resource          *resource,
                        MetaForeignToplevelHandle   *handle)
{
    MetaWindow *window = handle->window;
    struct wl_array states;
    uint32_t *s;

    wl_array_init (&states);

    if (META_WINDOW_MAXIMIZED (window))
    {
        s = wl_array_add (&states, sizeof (uint32_t));
        *s = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED;
    }

    if (window->minimized)
    {
        s = wl_array_add (&states, sizeof (uint32_t));
        *s = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED;
    }

    if (meta_window_appears_focused (window))
    {
        s = wl_array_add (&states, sizeof (uint32_t));
        *s = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED;
    }

    if (window->fullscreen)
    {
        s = wl_array_add (&states, sizeof (uint32_t));
        *s = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN;
    }

    zwlr_foreign_toplevel_handle_v1_send_state (resource, &states);
    wl_array_release (&states);
}

static void
send_title_to_resource (struct wl_resource *resource,
                        MetaWindow         *window)
{
    const char *title = meta_window_get_title (window);

    if (title)
        zwlr_foreign_toplevel_handle_v1_send_title (resource, title);
}

static void
send_app_id_to_resource (struct wl_resource *resource,
                         MetaWindow         *window)
{
    const char *wm_class = meta_window_get_wm_class (window);
    const char *sandboxed_app_id;

    sandboxed_app_id = meta_window_get_sandboxed_app_id (window);
    if (sandboxed_app_id)
        zwlr_foreign_toplevel_handle_v1_send_app_id (resource, sandboxed_app_id);
    else if (wm_class)
        zwlr_foreign_toplevel_handle_v1_send_app_id (resource, wm_class);
}

static void
send_done_to_resource (struct wl_resource *resource)
{
    zwlr_foreign_toplevel_handle_v1_send_done (resource);
}

static void
send_output_enter (MetaForeignToplevelHandle *handle,
                   int                        monitor_index)
{
    MetaWaylandOutput *wayland_output;
    GList *l;

    wayland_output = find_wayland_output_for_monitor (handle->manager->compositor,
                                                       monitor_index);
    if (!wayland_output)
        return;

    for (l = handle->handle_resources; l; l = l->next)
    {
        struct wl_resource *handle_resource = l->data;
        struct wl_client *client = wl_resource_get_client (handle_resource);
        struct wl_resource *output_resource;

        output_resource = find_output_resource_for_client (wayland_output, client);
        if (output_resource)
            zwlr_foreign_toplevel_handle_v1_send_output_enter (handle_resource,
                                                                output_resource);
    }
}

static void
send_output_leave (MetaForeignToplevelHandle *handle,
                   int                        monitor_index)
{
    MetaWaylandOutput *wayland_output;
    GList *l;

    wayland_output = find_wayland_output_for_monitor (handle->manager->compositor,
                                                       monitor_index);
    if (!wayland_output)
        return;

    for (l = handle->handle_resources; l; l = l->next)
    {
        struct wl_resource *handle_resource = l->data;
        struct wl_client *client = wl_resource_get_client (handle_resource);
        struct wl_resource *output_resource;

        output_resource = find_output_resource_for_client (wayland_output, client);
        if (output_resource)
            zwlr_foreign_toplevel_handle_v1_send_output_leave (handle_resource,
                                                                output_resource);
    }
}

static void
on_title_changed (GObject    *object,
                  GParamSpec *pspec,
                  gpointer    user_data)
{
    MetaForeignToplevelHandle *handle = user_data;
    GList *l;

    if (handle->closed)
        return;

    for (l = handle->handle_resources; l; l = l->next)
    {
        send_title_to_resource (l->data, handle->window);
        send_done_to_resource (l->data);
    }
}

static void
on_wm_class_changed (GObject    *object,
                     GParamSpec *pspec,
                     gpointer    user_data)
{
    MetaForeignToplevelHandle *handle = user_data;
    GList *l;

    if (handle->closed)
        return;

    for (l = handle->handle_resources; l; l = l->next)
    {
        send_app_id_to_resource (l->data, handle->window);
        send_done_to_resource (l->data);
    }
}

static void
on_state_changed (GObject    *object,
                  GParamSpec *pspec,
                  gpointer    user_data)
{
    MetaForeignToplevelHandle *handle = user_data;
    GList *l;

    if (handle->closed)
        return;

    for (l = handle->handle_resources; l; l = l->next)
    {
        send_state_to_resource (l->data, handle);
        send_done_to_resource (l->data);
    }
}

static void
on_skip_taskbar_changed (GObject    *object,
                         GParamSpec *pspec,
                         gpointer    user_data)
{
    MetaForeignToplevelHandle *handle = user_data;
    MetaWindow *window = handle->window;

    if (handle->closed)
        return;

    if (meta_window_is_skip_taskbar (window))
    {
        GList *l;

        for (l = handle->handle_resources; l; l = l->next)
            zwlr_foreign_toplevel_handle_v1_send_closed (l->data);

        handle->closed = TRUE;
    }
}

static void handle_destroy (MetaForeignToplevelHandle *handle);

static void
on_unmanaging (MetaWindow *window,
               gpointer    user_data)
{
    MetaForeignToplevelHandle *handle = user_data;
    GList *l;

    if (!handle->closed)
    {
        for (l = handle->handle_resources; l; l = l->next)
            zwlr_foreign_toplevel_handle_v1_send_closed (l->data);

        handle->closed = TRUE;
    }

    handle_destroy (handle);
}

static void
disconnect_window_signals (MetaForeignToplevelHandle *handle)
{
    if (handle->window == NULL)
        return;

    if (handle->title_handler_id)
        g_signal_handler_disconnect (handle->window, handle->title_handler_id);
    if (handle->wm_class_handler_id)
        g_signal_handler_disconnect (handle->window, handle->wm_class_handler_id);
    if (handle->minimized_handler_id)
        g_signal_handler_disconnect (handle->window, handle->minimized_handler_id);
    if (handle->maximized_h_handler_id)
        g_signal_handler_disconnect (handle->window, handle->maximized_h_handler_id);
    if (handle->maximized_v_handler_id)
        g_signal_handler_disconnect (handle->window, handle->maximized_v_handler_id);
    if (handle->fullscreen_handler_id)
        g_signal_handler_disconnect (handle->window, handle->fullscreen_handler_id);
    if (handle->appears_focused_handler_id)
        g_signal_handler_disconnect (handle->window, handle->appears_focused_handler_id);
    if (handle->skip_taskbar_handler_id)
        g_signal_handler_disconnect (handle->window, handle->skip_taskbar_handler_id);
    if (handle->unmanaging_handler_id)
        g_signal_handler_disconnect (handle->window, handle->unmanaging_handler_id);

    handle->title_handler_id = 0;
    handle->wm_class_handler_id = 0;
    handle->minimized_handler_id = 0;
    handle->maximized_h_handler_id = 0;
    handle->maximized_v_handler_id = 0;
    handle->fullscreen_handler_id = 0;
    handle->appears_focused_handler_id = 0;
    handle->skip_taskbar_handler_id = 0;
    handle->unmanaging_handler_id = 0;
    handle->window = NULL;
}

static void
handle_destroy (MetaForeignToplevelHandle *handle)
{
    GList *l;

    disconnect_window_signals (handle);

    for (l = handle->handle_resources; l; l = l->next)
        wl_resource_set_user_data (l->data, NULL);

    g_list_free (handle->handle_resources);
    handle->handle_resources = NULL;

    if (handle->manager)
    {
        handle->manager->handles = g_list_remove (handle->manager->handles,
                                                   handle);
    }

    g_free (handle);
}

/* Handle requests from clients */
static void
handle_set_maximized (struct wl_client   *client,
                      struct wl_resource *resource)
{
    MetaForeignToplevelHandle *handle = wl_resource_get_user_data (resource);

    if (!handle || !handle->window)
        return;

    if (!meta_window_can_maximize (handle->window))
        return;

    meta_window_maximize (handle->window, META_MAXIMIZE_BOTH);
}

static void
handle_unset_maximized (struct wl_client   *client,
                        struct wl_resource *resource)
{
    MetaForeignToplevelHandle *handle = wl_resource_get_user_data (resource);

    if (!handle || !handle->window)
        return;

    meta_window_unmaximize (handle->window, META_MAXIMIZE_BOTH);
}

static void
handle_set_minimized (struct wl_client   *client,
                      struct wl_resource *resource)
{
    MetaForeignToplevelHandle *handle = wl_resource_get_user_data (resource);

    if (!handle || !handle->window)
        return;

    if (!meta_window_can_minimize (handle->window))
        return;

    meta_window_minimize (handle->window);
}

static void
handle_unset_minimized (struct wl_client   *client,
                        struct wl_resource *resource)
{
    MetaForeignToplevelHandle *handle = wl_resource_get_user_data (resource);

    if (!handle || !handle->window)
        return;

    meta_window_unminimize (handle->window);
}

static void
handle_activate (struct wl_client   *client,
                 struct wl_resource *resource,
                 struct wl_resource *seat_resource)
{
    MetaForeignToplevelHandle *handle = wl_resource_get_user_data (resource);
    uint32_t timestamp;

    if (!handle || !handle->window)
        return;

    timestamp = meta_display_get_current_time_roundtrip (meta_get_display ());
    meta_window_activate (handle->window, timestamp);
}

static void
handle_close (struct wl_client   *client,
              struct wl_resource *resource)
{
    MetaForeignToplevelHandle *handle = wl_resource_get_user_data (resource);

    if (!handle || !handle->window)
        return;

    if (!meta_window_can_close (handle->window))
        return;

    meta_window_delete (handle->window, META_CURRENT_TIME);
}

static void
handle_set_rectangle (struct wl_client   *client,
                      struct wl_resource *resource,
                      struct wl_resource *surface_resource,
                      int32_t             x,
                      int32_t             y,
                      int32_t             width,
                      int32_t             height)
{
    MetaForeignToplevelHandle *handle = wl_resource_get_user_data (resource);

    if (!handle || !handle->window)
        return;

    if (width > 0 && height > 0)
    {
        MetaRectangle rect = { x, y, width, height };
        meta_window_set_icon_geometry (handle->window, &rect);
    }
    else
    {
        meta_window_set_icon_geometry (handle->window, NULL);
    }
}

static void
handle_resource_destroy (struct wl_client   *client,
                         struct wl_resource *resource)
{
    wl_resource_destroy (resource);
}

static void
handle_set_fullscreen (struct wl_client   *client,
                       struct wl_resource *resource,
                       struct wl_resource *output_resource)
{
    MetaForeignToplevelHandle *handle = wl_resource_get_user_data (resource);

    if (!handle || !handle->window)
        return;

    if (!handle->window->has_fullscreen_func)
        return;

    meta_window_make_fullscreen (handle->window);
}

static void
handle_unset_fullscreen (struct wl_client   *client,
                         struct wl_resource *resource)
{
    MetaForeignToplevelHandle *handle = wl_resource_get_user_data (resource);

    if (!handle || !handle->window)
        return;

    meta_window_unmake_fullscreen (handle->window);
}

static const struct zwlr_foreign_toplevel_handle_v1_interface toplevel_handle_interface = {
    handle_set_maximized,
    handle_unset_maximized,
    handle_set_minimized,
    handle_unset_minimized,
    handle_activate,
    handle_close,
    handle_set_rectangle,
    handle_resource_destroy,
    handle_set_fullscreen,
    handle_unset_fullscreen,
};

static void
handle_resource_destroyed (struct wl_resource *resource)
{
    MetaForeignToplevelHandle *handle = wl_resource_get_user_data (resource);

    if (!handle)
        return;

    handle->handle_resources = g_list_remove (handle->handle_resources,
                                               resource);

    if (handle->handle_resources == NULL && handle->closed)
        handle_destroy (handle);
}

static MetaForeignToplevelHandle *
find_handle_for_window (MetaForeignToplevelManager *manager,
                        MetaWindow                 *window)
{
    GList *l;

    for (l = manager->handles; l; l = l->next)
    {
        MetaForeignToplevelHandle *handle = l->data;

        if (handle->window == window)
            return handle;
    }

    return NULL;
}

static void
send_initial_state_for_resource (struct wl_resource        *handle_resource,
                                 MetaForeignToplevelHandle *handle)
{
    MetaWindow *window = handle->window;
    MetaWaylandOutput *wayland_output;
    struct wl_client *client;
    struct wl_resource *output_resource;
    int monitor;

    send_title_to_resource (handle_resource, window);
    send_app_id_to_resource (handle_resource, window);
    send_state_to_resource (handle_resource, handle);

    client = wl_resource_get_client (handle_resource);
    monitor = meta_window_get_monitor (window);
    if (monitor >= 0)
    {
        wayland_output = find_wayland_output_for_monitor (handle->manager->compositor,
                                                           monitor);
        if (wayland_output)
        {
            output_resource = find_output_resource_for_client (wayland_output,
                                                                client);
            if (output_resource)
                zwlr_foreign_toplevel_handle_v1_send_output_enter (handle_resource,
                                                                    output_resource);
        }
    }

    /* Send parent if version >= 3 */
    if (wl_resource_get_version (handle_resource) >= 3)
    {
        MetaWindow *transient_for = meta_window_get_transient_for (window);

        if (transient_for)
        {
            MetaForeignToplevelHandle *parent_handle;
            parent_handle = find_handle_for_window (handle->manager, transient_for);

            if (parent_handle && parent_handle->handle_resources)
            {
                struct wl_resource *parent_resource = NULL;
                GList *pl;

                for (pl = parent_handle->handle_resources; pl; pl = pl->next)
                {
                    if (wl_resource_get_client (pl->data) == client)
                    {
                        parent_resource = pl->data;
                        break;
                    }
                }

                if (parent_resource)
                {
                    zwlr_foreign_toplevel_handle_v1_send_parent (handle_resource,
                                                                  parent_resource);
                }
            }
        }
        else
        {
            zwlr_foreign_toplevel_handle_v1_send_parent (handle_resource, NULL);
        }
    }

    send_done_to_resource (handle_resource);
}

static MetaForeignToplevelHandle *
create_handle_for_window (MetaForeignToplevelManager *manager,
                          MetaWindow                 *window)
{
    MetaForeignToplevelHandle *handle;

    handle = g_new0 (MetaForeignToplevelHandle, 1);
    handle->manager = manager;
    handle->window = window;
    handle->closed = FALSE;

    handle->title_handler_id =
        g_signal_connect (window, "notify::title",
                          G_CALLBACK (on_title_changed), handle);
    handle->wm_class_handler_id =
        g_signal_connect (window, "notify::wm-class",
                          G_CALLBACK (on_wm_class_changed), handle);
    handle->minimized_handler_id =
        g_signal_connect (window, "notify::minimized",
                          G_CALLBACK (on_state_changed), handle);
    handle->maximized_h_handler_id =
        g_signal_connect (window, "notify::maximized-horizontally",
                          G_CALLBACK (on_state_changed), handle);
    handle->maximized_v_handler_id =
        g_signal_connect (window, "notify::maximized-vertically",
                          G_CALLBACK (on_state_changed), handle);
    handle->fullscreen_handler_id =
        g_signal_connect (window, "notify::fullscreen",
                          G_CALLBACK (on_state_changed), handle);
    handle->appears_focused_handler_id =
        g_signal_connect (window, "notify::appears-focused",
                          G_CALLBACK (on_state_changed), handle);
    handle->skip_taskbar_handler_id =
        g_signal_connect (window, "notify::skip-taskbar",
                          G_CALLBACK (on_skip_taskbar_changed), handle);
    handle->unmanaging_handler_id =
        g_signal_connect (window, "unmanaging",
                          G_CALLBACK (on_unmanaging), handle);

    manager->handles = g_list_prepend (manager->handles, handle);

    return handle;
}

static gboolean
should_expose_window (MetaWindow *window)
{
    MetaWindowType type = meta_window_get_window_type (window);

    if (meta_window_is_skip_taskbar (window))
        return FALSE;

    if (meta_window_is_override_redirect (window))
        return FALSE;

    switch (type)
    {
    case META_WINDOW_NORMAL:
    case META_WINDOW_DIALOG:
    case META_WINDOW_MODAL_DIALOG:
    case META_WINDOW_UTILITY:
        return TRUE;
    default:
        return FALSE;
    }
}

static void
create_handle_resource_for_manager_resource (MetaForeignToplevelHandle *handle,
                                             struct wl_resource        *manager_resource)
{
    struct wl_client *client = wl_resource_get_client (manager_resource);
    uint32_t version = wl_resource_get_version (manager_resource);
    struct wl_resource *handle_resource;

    handle_resource = wl_resource_create (client,
                                          &zwlr_foreign_toplevel_handle_v1_interface,
                                          version, 0);

    wl_resource_set_implementation (handle_resource,
                                    &toplevel_handle_interface,
                                    handle,
                                    handle_resource_destroyed);

    handle->handle_resources = g_list_prepend (handle->handle_resources,
                                                handle_resource);

    zwlr_foreign_toplevel_manager_v1_send_toplevel (manager_resource,
                                                     handle_resource);
    send_initial_state_for_resource (handle_resource, handle);
}

static void
on_window_created (MetaDisplay *display,
                   MetaWindow  *window,
                   gpointer     user_data)
{
    MetaForeignToplevelManager *manager = user_data;
    MetaForeignToplevelHandle *handle;
    GList *l;

    if (!should_expose_window (window))
        return;

    handle = create_handle_for_window (manager, window);

    for (l = manager->manager_resources; l; l = l->next)
        create_handle_resource_for_manager_resource (handle, l->data);
}

static void
on_window_entered_monitor (MetaDisplay *display,
                           int          monitor_index,
                           MetaWindow  *window,
                           gpointer     user_data)
{
    MetaForeignToplevelManager *manager = user_data;
    MetaForeignToplevelHandle *handle;

    handle = find_handle_for_window (manager, window);
    if (!handle || handle->closed)
        return;

    send_output_enter (handle, monitor_index);

    {
        GList *l;
        for (l = handle->handle_resources; l; l = l->next)
            send_done_to_resource (l->data);
    }
}

static void
on_window_left_monitor (MetaDisplay *display,
                        int          monitor_index,
                        MetaWindow  *window,
                        gpointer     user_data)
{
    MetaForeignToplevelManager *manager = user_data;
    MetaForeignToplevelHandle *handle;

    handle = find_handle_for_window (manager, window);
    if (!handle || handle->closed)
        return;

    send_output_leave (handle, monitor_index);

    {
        GList *l;
        for (l = handle->handle_resources; l; l = l->next)
            send_done_to_resource (l->data);
    }
}

/* Manager protocol requests */
static void
manager_stop (struct wl_client   *client,
              struct wl_resource *resource)
{
    zwlr_foreign_toplevel_manager_v1_send_finished (resource);
}

static const struct zwlr_foreign_toplevel_manager_v1_interface manager_interface = {
    manager_stop,
};

static void
manager_resource_destroyed (struct wl_resource *resource)
{
    MetaForeignToplevelManager *manager = wl_resource_get_user_data (resource);

    if (manager)
    {
        manager->manager_resources = g_list_remove (manager->manager_resources,
                                                     resource);
    }
}

static void
send_existing_windows (MetaForeignToplevelManager *manager,
                       struct wl_resource          *manager_resource)
{
    MetaDisplay *display = meta_get_display ();
    GSList *windows;
    GSList *l;

    if (!display)
        return;

    windows = meta_display_list_windows (display, META_LIST_DEFAULT);

    for (l = windows; l; l = l->next)
    {
        MetaWindow *window = l->data;
        MetaForeignToplevelHandle *handle;

        if (!should_expose_window (window))
            continue;

        handle = find_handle_for_window (manager, window);
        if (!handle)
            handle = create_handle_for_window (manager, window);

        create_handle_resource_for_manager_resource (handle, manager_resource);
    }

    g_slist_free (windows);
}

static void
ensure_display_signals_connected (MetaForeignToplevelManager *manager)
{
    MetaDisplay *display;

    if (manager->window_created_handler_id != 0)
        return;

    display = meta_get_display ();
    if (!display)
        return;

    manager->window_created_handler_id =
        g_signal_connect (display, "window-created",
                          G_CALLBACK (on_window_created), manager);
    manager->window_entered_monitor_handler_id =
        g_signal_connect (display, "window-entered-monitor",
                          G_CALLBACK (on_window_entered_monitor), manager);
    manager->window_left_monitor_handler_id =
        g_signal_connect (display, "window-left-monitor",
                          G_CALLBACK (on_window_left_monitor), manager);
}

static void
bind_manager (struct wl_client *client,
              void             *data,
              uint32_t          version,
              uint32_t          id)
{
    MetaForeignToplevelManager *manager = data;
    struct wl_resource *resource;

    resource = wl_resource_create (client,
                                   &zwlr_foreign_toplevel_manager_v1_interface,
                                   version, id);

    wl_resource_set_implementation (resource,
                                    &manager_interface,
                                    manager,
                                    manager_resource_destroyed);

    manager->manager_resources = g_list_prepend (manager->manager_resources,
                                                  resource);

    ensure_display_signals_connected (manager);
    send_existing_windows (manager, resource);
}

void
meta_wayland_init_foreign_toplevel (MetaWaylandCompositor *compositor)
{
    MetaForeignToplevelManager *manager;

    manager = g_new0 (MetaForeignToplevelManager, 1);
    manager->compositor = compositor;

    if (wl_global_create (compositor->wayland_display,
                          &zwlr_foreign_toplevel_manager_v1_interface,
                          META_ZWLR_FOREIGN_TOPLEVEL_MANAGER_V1_VERSION,
                          manager, bind_manager) == NULL)
    {
        g_warning ("Failed to register zwlr_foreign_toplevel_manager_v1 global");
        g_free (manager);
        return;
    }

    ensure_display_signals_connected (manager);

    g_object_set_data (G_OBJECT (compositor), "-meta-wayland-foreign-toplevel",
                       manager);

    g_debug ("Foreign toplevel management protocol initialized (version %d)",
             META_ZWLR_FOREIGN_TOPLEVEL_MANAGER_V1_VERSION);
}
