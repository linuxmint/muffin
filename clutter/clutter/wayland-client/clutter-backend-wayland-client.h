/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2024 Linux Mint
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *   Michael Webster <miketwebster@gmail.com>
 */

#ifndef __CLUTTER_BACKEND_WAYLAND_CLIENT_H__
#define __CLUTTER_BACKEND_WAYLAND_CLIENT_H__

#include <glib-object.h>
#include <gio/gio.h>
#include <wayland-client.h>

#include <clutter/clutter-backend.h>
#include "clutter-backend-private.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_BACKEND_WAYLAND_CLIENT             (clutter_backend_wayland_client_get_type ())
#define CLUTTER_BACKEND_WAYLAND_CLIENT(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_BACKEND_WAYLAND_CLIENT, ClutterBackendWaylandClient))
#define CLUTTER_IS_BACKEND_WAYLAND_CLIENT(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_BACKEND_WAYLAND_CLIENT))
#define CLUTTER_BACKEND_WAYLAND_CLIENT_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), CLUTTER_TYPE_BACKEND_WAYLAND_CLIENT, ClutterBackendWaylandClientClass))
#define CLUTTER_IS_BACKEND_WAYLAND_CLIENT_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), CLUTTER_TYPE_BACKEND_WAYLAND_CLIENT))
#define CLUTTER_BACKEND_WAYLAND_CLIENT_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), CLUTTER_TYPE_BACKEND_WAYLAND_CLIENT, ClutterBackendWaylandClientClass))

typedef struct _ClutterBackendWaylandClient       ClutterBackendWaylandClient;
typedef struct _ClutterBackendWaylandClientClass  ClutterBackendWaylandClientClass;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterBackendWaylandClient, g_object_unref)

struct _ClutterBackendWaylandClient
{
    ClutterBackend parent_instance;

    /* Wayland connection */
    struct wl_display *wl_display;
    struct wl_registry *wl_registry;
    struct wl_compositor *wl_compositor;
    struct wl_shm *wl_shm;
    struct wl_seat *wl_seat;
    struct wl_output *wl_output;

    /* Layer shell protocol */
    struct zwlr_layer_shell_v1 *layer_shell;

    /* Event source for Wayland display */
    GSource *wayland_source;

    /* Input seat */
    ClutterSeat *seat;

    /* Settings (font options, etc.) */
    GSettings *xsettings;
};

struct _ClutterBackendWaylandClientClass
{
    ClutterBackendClass parent_class;
};

GType clutter_backend_wayland_client_get_type (void) G_GNUC_CONST;

ClutterBackend * clutter_backend_wayland_client_new (void);

/* Accessors for Wayland objects (for use by stage) */
struct wl_display *    clutter_backend_wayland_client_get_wl_display    (ClutterBackendWaylandClient *backend);
struct wl_compositor * clutter_backend_wayland_client_get_compositor    (ClutterBackendWaylandClient *backend);
struct zwlr_layer_shell_v1 * clutter_backend_wayland_client_get_layer_shell (ClutterBackendWaylandClient *backend);
struct wl_output *     clutter_backend_wayland_client_get_output        (ClutterBackendWaylandClient *backend);

G_END_DECLS

#endif /* __CLUTTER_BACKEND_WAYLAND_CLIENT_H__ */
