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

#include "clutter-build-config.h"

#include <string.h>
#include <errno.h>

#include <wayland-client.h>
#include <wayland-egl.h>

#include <cogl/cogl.h>
#include <cogl/cogl-egl.h>
#include <cogl/cogl-wayland-client.h>

#include "clutter-backend-wayland-client.h"
#include "clutter-stage-wayland-client.h"
#include "clutter-seat-wayland-client.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include "clutter-backend.h"
#include "clutter-debug.h"
#include "clutter-main.h"
#include "clutter-private.h"
#include "clutter-settings-private.h"
#include "clutter-stage-private.h"

G_DEFINE_TYPE (ClutterBackendWaylandClient,
               clutter_backend_wayland_client,
               CLUTTER_TYPE_BACKEND)

/* Registry handlers */
static void
registry_global (void *data,
                 struct wl_registry *registry,
                 uint32_t name,
                 const char *interface,
                 uint32_t version)
{
    ClutterBackendWaylandClient *backend = data;

    CLUTTER_NOTE (BACKEND, "Wayland registry: %s (v%d)", interface, version);

    if (strcmp (interface, wl_compositor_interface.name) == 0)
    {
        backend->wl_compositor = wl_registry_bind (registry, name,
                                                   &wl_compositor_interface,
                                                   MIN (version, 4));
    }
    else if (strcmp (interface, wl_shm_interface.name) == 0)
    {
        backend->wl_shm = wl_registry_bind (registry, name,
                                            &wl_shm_interface,
                                            MIN (version, 1));
    }
    else if (strcmp (interface, wl_seat_interface.name) == 0)
    {
        backend->wl_seat = wl_registry_bind (registry, name,
                                             &wl_seat_interface,
                                             MIN (version, 5));
    }
    else if (strcmp (interface, wl_output_interface.name) == 0)
    {
        if (!backend->wl_output)
        {
            backend->wl_output = wl_registry_bind (registry, name,
                                                   &wl_output_interface,
                                                   MIN (version, 2));
        }
    }
    else if (strcmp (interface, zwlr_layer_shell_v1_interface.name) == 0)
    {
        backend->layer_shell = wl_registry_bind (registry, name,
                                                 &zwlr_layer_shell_v1_interface,
                                                 MIN (version, 4));
    }
}

static void
registry_global_remove (void *data,
                        struct wl_registry *registry,
                        uint32_t name)
{
    /* TODO: Handle output removal, etc. */
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

/* GSource for Wayland display events */
typedef struct {
    GSource source;
    ClutterBackendWaylandClient *backend;
    GPollFD pfd;
} WaylandEventSource;

static gboolean
wayland_event_source_prepare (GSource *source,
                              gint    *timeout)
{
    WaylandEventSource *wl_source = (WaylandEventSource *) source;

    *timeout = -1;

    if (wl_source->backend->wl_display)
        wl_display_flush (wl_source->backend->wl_display);

    return FALSE;
}

static gboolean
wayland_event_source_check (GSource *source)
{
    WaylandEventSource *wl_source = (WaylandEventSource *) source;

    return (wl_source->pfd.revents & G_IO_IN) != 0;
}

static gboolean
wayland_event_source_dispatch (GSource     *source,
                               GSourceFunc  callback,
                               gpointer     user_data)
{
    WaylandEventSource *wl_source = (WaylandEventSource *) source;
    struct wl_display *display = wl_source->backend->wl_display;

    if (wl_source->pfd.revents & G_IO_IN)
    {
        if (wl_display_dispatch (display) == -1)
        {
            g_warning ("Wayland display dispatch failed: %s", strerror (errno));
            return G_SOURCE_REMOVE;
        }
    }

    if (wl_source->pfd.revents & (G_IO_ERR | G_IO_HUP))
    {
        g_warning ("Wayland display error");
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

static void
wayland_event_source_finalize (GSource *source)
{
}

static GSourceFuncs wayland_event_source_funcs = {
    .prepare = wayland_event_source_prepare,
    .check = wayland_event_source_check,
    .dispatch = wayland_event_source_dispatch,
    .finalize = wayland_event_source_finalize,
};

static GSource *
wayland_event_source_new (ClutterBackendWaylandClient *backend)
{
    GSource *source;
    WaylandEventSource *wl_source;

    source = g_source_new (&wayland_event_source_funcs, sizeof (WaylandEventSource));
    g_source_set_name (source, "Wayland Event Source");

    wl_source = (WaylandEventSource *) source;
    wl_source->backend = backend;
    wl_source->pfd.fd = wl_display_get_fd (backend->wl_display);
    wl_source->pfd.events = G_IO_IN | G_IO_ERR | G_IO_HUP;

    g_source_add_poll (source, &wl_source->pfd);

    return source;
}

/* Backend vfuncs */
static gboolean
clutter_backend_wayland_client_post_parse (ClutterBackend *backend,
                                           GError        **error)
{
    ClutterBackendWaylandClient *backend_wl = CLUTTER_BACKEND_WAYLAND_CLIENT (backend);
    const char *display_name;

    display_name = g_getenv ("WAYLAND_DISPLAY");
    if (!display_name)
        display_name = "wayland-0";

    CLUTTER_NOTE (BACKEND, "Connecting to Wayland display '%s'", display_name);

    backend_wl->wl_display = wl_display_connect (display_name);
    if (!backend_wl->wl_display)
    {
        g_set_error (error, CLUTTER_INIT_ERROR,
                     CLUTTER_INIT_ERROR_BACKEND,
                     "Failed to connect to Wayland display '%s': %s",
                     display_name, strerror (errno));
        return FALSE;
    }

    /* Get registry and bind globals */
    backend_wl->wl_registry = wl_display_get_registry (backend_wl->wl_display);
    wl_registry_add_listener (backend_wl->wl_registry, &registry_listener, backend_wl);
    wl_display_roundtrip (backend_wl->wl_display);

    /* Check required interfaces */
    if (!backend_wl->wl_compositor)
    {
        g_set_error_literal (error, CLUTTER_INIT_ERROR,
                             CLUTTER_INIT_ERROR_BACKEND,
                             "wl_compositor not available from Wayland compositor");
        return FALSE;
    }

    if (!backend_wl->layer_shell)
    {
        g_set_error_literal (error, CLUTTER_INIT_ERROR,
                             CLUTTER_INIT_ERROR_BACKEND,
                             "zwlr_layer_shell_v1 not available from Wayland compositor");
        return FALSE;
    }

    CLUTTER_NOTE (BACKEND, "Connected to Wayland display, protocols bound");

    /* Create event source for Wayland events */
    backend_wl->wayland_source = wayland_event_source_new (backend_wl);
    g_source_attach (backend_wl->wayland_source, NULL);

    return TRUE;
}

static CoglRenderer *
clutter_backend_wayland_client_get_renderer (ClutterBackend *backend,
                                             GError        **error)
{
    ClutterBackendWaylandClient *backend_wl = CLUTTER_BACKEND_WAYLAND_CLIENT (backend);
    CoglRenderer *renderer;

    CLUTTER_NOTE (BACKEND, "Creating Cogl renderer for Wayland EGL");

    renderer = cogl_renderer_new ();

    cogl_renderer_set_winsys_id (renderer, COGL_WINSYS_ID_EGL_WAYLAND);
    cogl_wayland_renderer_set_foreign_display (renderer, backend_wl->wl_display);

    if (!cogl_renderer_connect (renderer, error))
    {
        cogl_object_unref (renderer);
        return NULL;
    }

    return renderer;
}

static CoglDisplay *
clutter_backend_wayland_client_get_display (ClutterBackend *backend,
                                            CoglRenderer   *renderer,
                                            CoglSwapChain  *swap_chain,
                                            GError        **error)
{
    CoglOnscreenTemplate *onscreen_template;
    CoglDisplay *display;

    CLUTTER_NOTE (BACKEND, "Creating CoglDisplay for Wayland");

    onscreen_template = cogl_onscreen_template_new (swap_chain);
    cogl_swap_chain_set_has_alpha (swap_chain, TRUE);

    if (!cogl_renderer_check_onscreen_template (renderer, onscreen_template, error))
    {
        cogl_object_unref (onscreen_template);
        return NULL;
    }

    display = cogl_display_new (renderer, onscreen_template);
    cogl_object_unref (onscreen_template);

    return display;
}

static ClutterStageWindow *
clutter_backend_wayland_client_create_stage (ClutterBackend *backend,
                                             ClutterStage   *wrapper,
                                             GError        **error)
{
    CLUTTER_NOTE (BACKEND, "Creating Wayland client stage");

    return g_object_new (CLUTTER_TYPE_STAGE_WAYLAND_CLIENT,
                         "wrapper", wrapper,
                         "backend", backend,
                         NULL);
}

static ClutterFeatureFlags
clutter_backend_wayland_client_get_features (ClutterBackend *backend)
{
    ClutterFeatureFlags flags;

    flags = CLUTTER_BACKEND_CLASS (clutter_backend_wayland_client_parent_class)->get_features (backend);
    flags |= CLUTTER_FEATURE_STAGE_MULTIPLE;

    return flags;
}

static void
clutter_backend_wayland_client_init_events (ClutterBackend *backend)
{
    ClutterBackendWaylandClient *backend_wl = CLUTTER_BACKEND_WAYLAND_CLIENT (backend);

    CLUTTER_NOTE (BACKEND, "Initializing Wayland input events");

    /* Create stub seat for now - full input handling will come later */
    backend_wl->seat = clutter_seat_wayland_client_new ();
}

static ClutterSeat *
clutter_backend_wayland_client_get_default_seat (ClutterBackend *backend)
{
    ClutterBackendWaylandClient *backend_wl = CLUTTER_BACKEND_WAYLAND_CLIENT (backend);

    return backend_wl->seat;
}

static void
clutter_backend_wayland_client_dispose (GObject *object)
{
    ClutterBackendWaylandClient *backend_wl = CLUTTER_BACKEND_WAYLAND_CLIENT (object);

    g_clear_object (&backend_wl->seat);
    g_clear_object (&backend_wl->xsettings);

    G_OBJECT_CLASS (clutter_backend_wayland_client_parent_class)->dispose (object);
}

static void
clutter_backend_wayland_client_finalize (GObject *object)
{
    ClutterBackendWaylandClient *backend_wl = CLUTTER_BACKEND_WAYLAND_CLIENT (object);

    if (backend_wl->wayland_source)
    {
        g_source_destroy (backend_wl->wayland_source);
        g_source_unref (backend_wl->wayland_source);
    }

    if (backend_wl->layer_shell)
        zwlr_layer_shell_v1_destroy (backend_wl->layer_shell);

    if (backend_wl->wl_seat)
        wl_seat_destroy (backend_wl->wl_seat);

    if (backend_wl->wl_output)
        wl_output_destroy (backend_wl->wl_output);

    if (backend_wl->wl_shm)
        wl_shm_destroy (backend_wl->wl_shm);

    if (backend_wl->wl_compositor)
        wl_compositor_destroy (backend_wl->wl_compositor);

    if (backend_wl->wl_registry)
        wl_registry_destroy (backend_wl->wl_registry);

    if (backend_wl->wl_display)
        wl_display_disconnect (backend_wl->wl_display);

    G_OBJECT_CLASS (clutter_backend_wayland_client_parent_class)->finalize (object);
}

static void
clutter_backend_wayland_client_class_init (ClutterBackendWaylandClientClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    ClutterBackendClass *backend_class = CLUTTER_BACKEND_CLASS (klass);

    object_class->dispose = clutter_backend_wayland_client_dispose;
    object_class->finalize = clutter_backend_wayland_client_finalize;

    backend_class->post_parse = clutter_backend_wayland_client_post_parse;
    backend_class->get_renderer = clutter_backend_wayland_client_get_renderer;
    backend_class->get_display = clutter_backend_wayland_client_get_display;
    backend_class->create_stage = clutter_backend_wayland_client_create_stage;
    backend_class->get_features = clutter_backend_wayland_client_get_features;
    backend_class->init_events = clutter_backend_wayland_client_init_events;
    backend_class->get_default_seat = clutter_backend_wayland_client_get_default_seat;
}

static void
clutter_backend_wayland_client_init (ClutterBackendWaylandClient *backend)
{
}

ClutterBackend *
clutter_backend_wayland_client_new (void)
{
    return g_object_new (CLUTTER_TYPE_BACKEND_WAYLAND_CLIENT, NULL);
}

/* Accessors */
struct wl_display *
clutter_backend_wayland_client_get_wl_display (ClutterBackendWaylandClient *backend)
{
    g_return_val_if_fail (CLUTTER_IS_BACKEND_WAYLAND_CLIENT (backend), NULL);
    return backend->wl_display;
}

struct wl_compositor *
clutter_backend_wayland_client_get_compositor (ClutterBackendWaylandClient *backend)
{
    g_return_val_if_fail (CLUTTER_IS_BACKEND_WAYLAND_CLIENT (backend), NULL);
    return backend->wl_compositor;
}

struct zwlr_layer_shell_v1 *
clutter_backend_wayland_client_get_layer_shell (ClutterBackendWaylandClient *backend)
{
    g_return_val_if_fail (CLUTTER_IS_BACKEND_WAYLAND_CLIENT (backend), NULL);
    return backend->layer_shell;
}

struct wl_output *
clutter_backend_wayland_client_get_output (ClutterBackendWaylandClient *backend)
{
    g_return_val_if_fail (CLUTTER_IS_BACKEND_WAYLAND_CLIENT (backend), NULL);
    return backend->wl_output;
}
