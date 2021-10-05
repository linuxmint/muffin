/*
 * Copyright (C) 2019 Red Hat, Inc.
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

#include "tests/wayland-unit-tests.h"

#include <gio/gio.h>
#include <wayland-server.h>

#include "wayland/meta-wayland.h"
#include "wayland/meta-wayland-actor-surface.h"
#include "wayland/meta-wayland-surface.h"
#include "wayland/meta-wayland-private.h"

#include "test-driver-server-protocol.h"

typedef struct _WaylandTestClient
{
  GSubprocess *subprocess;
  char *path;
  GMainLoop *main_loop;
} WaylandTestClient;

static char *
get_test_client_path (const char *test_client_name)
{
  return g_test_build_filename (G_TEST_BUILT,
                                "src",
                                "tests",
                                "wayland-test-clients",
                                test_client_name,
                                NULL);
}

static WaylandTestClient *
wayland_test_client_new (const char *test_client_name)
{
  MetaWaylandCompositor *compositor;
  const char *wayland_display_name;
  g_autofree char *test_client_path = NULL;
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  GSubprocess *subprocess;
  GError *error = NULL;
  WaylandTestClient *wayland_test_client;

  compositor = meta_wayland_compositor_get_default ();
  wayland_display_name = meta_wayland_get_wayland_display_name (compositor);
  test_client_path = get_test_client_path (test_client_name);

  launcher =  g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);
  g_subprocess_launcher_setenv (launcher,
                                "WAYLAND_DISPLAY", wayland_display_name,
                                TRUE);

  subprocess = g_subprocess_launcher_spawn (launcher,
                                            &error,
                                            test_client_path,
                                            NULL);
  if (!subprocess)
    {
      g_error ("Failed to launch Wayland test client '%s': %s",
               test_client_path, error->message);
    }

  wayland_test_client = g_new0 (WaylandTestClient, 1);
  wayland_test_client->subprocess = subprocess;
  wayland_test_client->path = g_strdup (test_client_name);
  wayland_test_client->main_loop = g_main_loop_new (NULL, FALSE);

  return wayland_test_client;
}

static void
wayland_test_client_finished (GObject      *source_object,
                              GAsyncResult *res,
                              gpointer      user_data)
{
  WaylandTestClient *wayland_test_client = user_data;
  GError *error = NULL;

  if (!g_subprocess_wait_finish (wayland_test_client->subprocess,
                                 res,
                                 &error))
    {
      g_error ("Failed to wait for Wayland test client '%s': %s",
               wayland_test_client->path, error->message);
    }

  g_main_loop_quit (wayland_test_client->main_loop);
}

static void
wayland_test_client_finish (WaylandTestClient *wayland_test_client)
{
  g_subprocess_wait_async (wayland_test_client->subprocess, NULL,
                           wayland_test_client_finished, wayland_test_client);

  g_main_loop_run (wayland_test_client->main_loop);

  g_assert_true (g_subprocess_get_successful (wayland_test_client->subprocess));

  g_main_loop_unref (wayland_test_client->main_loop);
  g_free (wayland_test_client->path);
  g_object_unref (wayland_test_client->subprocess);
  g_free (wayland_test_client);
}

static void
subsurface_remap_toplevel (void)
{
  WaylandTestClient *wayland_test_client;

  wayland_test_client = wayland_test_client_new ("subsurface-remap-toplevel");
  wayland_test_client_finish (wayland_test_client);
}

static void
on_actor_destroyed (ClutterActor       *actor,
                    struct wl_resource *callback)
{
  wl_callback_send_done (callback, 0);
  wl_resource_destroy (callback);
}

static void
sync_actor_destroy (struct wl_client   *client,
                    struct wl_resource *resource,
                    uint32_t            id,
                    struct wl_resource *surface_resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandActorSurface *actor_surface;
  MetaSurfaceActor *actor;
  struct wl_resource *callback;

  g_assert_nonnull (surface);

  actor_surface = (MetaWaylandActorSurface *) surface->role;
  g_assert_nonnull (actor_surface);

  actor = meta_wayland_actor_surface_get_actor (actor_surface);
  g_assert_nonnull (actor);

  callback = wl_resource_create (client, &wl_callback_interface, 1, id);

  g_signal_connect (actor, "destroy", G_CALLBACK (on_actor_destroyed),
                    callback);
}

static const struct test_driver_interface meta_test_driver_interface = {
  sync_actor_destroy,
};

static void
bind_test_driver (struct wl_client *client,
                  void             *data,
                  uint32_t          version,
                  uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client, &test_driver_interface,
                                 version, id);
  wl_resource_set_implementation (resource, &meta_test_driver_interface,
                                  NULL, NULL);
}

void
pre_run_wayland_tests (void)
{
  MetaWaylandCompositor *compositor;

  compositor = meta_wayland_compositor_get_default ();
  g_assert_nonnull (compositor);

  if (wl_global_create (compositor->wayland_display,
                        &test_driver_interface,
                        1,
                        NULL, bind_test_driver) == NULL)
    g_error ("Failed to register a global wl-subcompositor object");
}

void
init_wayland_tests (void)
{
  g_test_add_func ("/wayland/subsurface/remap-toplevel",
                   subsurface_remap_toplevel);
}
