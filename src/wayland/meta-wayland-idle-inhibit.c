/*
 * Copyright (C) 2021 SUSE Software Solutions Germany GmbH
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
 */

#include "config.h"

#include "wayland/meta-wayland-idle-inhibit.h"

#include <wayland-server.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-settings-private.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-versions.h"

#include "idle-inhibit-unstable-v1-server-protocol.h"

typedef enum _IdleState
{
  IDLE_STATE_INITIALIZING,
  IDLE_STATE_UNINHIBITED,
  IDLE_STATE_INHIBITING,
  IDLE_STATE_INHIBITED,
  IDLE_STATE_UNINHIBITING,
} IdleState;

struct _MetaWaylandIdleInhibitor
{
  GDBusProxy *session_proxy;
  struct wl_resource *resource;

  MetaSurfaceActor *actor;
  gulong is_obscured_changed_handler;
  gulong actor_destroyed_handler_id;

  MetaWaylandSurface *surface;
  gulong surface_destroy_handler_id;
  gulong actor_changed_handler_id;

  uint32_t cookie;
  IdleState state;
};

typedef struct _MetaWaylandIdleInhibitor MetaWaylandIdleInhibitor;

static void update_inhibition (MetaWaylandIdleInhibitor *inhibitor);

static void
meta_wayland_inhibitor_free (MetaWaylandIdleInhibitor *inhibitor)
{
  g_clear_signal_handler (&inhibitor->is_obscured_changed_handler,
                          inhibitor->actor);
  g_clear_signal_handler (&inhibitor->actor_destroyed_handler_id,
                          inhibitor->actor);
  g_clear_signal_handler (&inhibitor->actor_changed_handler_id,
                          inhibitor->surface);
  g_clear_signal_handler (&inhibitor->surface_destroy_handler_id,
                          inhibitor->surface);

  g_free (inhibitor);
}

static void
inhibit_completed (GObject      *source,
                   GAsyncResult *res,
                   gpointer      user_data)
{
  MetaWaylandIdleInhibitor *inhibitor = user_data;
  g_autoptr (GVariant) ret = NULL;
  g_autoptr (GError) error = NULL;

  ret = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
  if (!ret)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to inhibit: %s", error->message);
      return;
    }

  g_warn_if_fail (inhibitor->state == IDLE_STATE_INHIBITING);

  g_variant_get (ret, "(u)", &inhibitor->cookie);
  inhibitor->state = IDLE_STATE_INHIBITED;

  update_inhibition (inhibitor);
}

static void
uninhibit_completed (GObject      *source,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  MetaWaylandIdleInhibitor *inhibitor = user_data;
  g_autoptr (GVariant) ret = NULL;
  g_autoptr (GError) error = NULL;

  ret = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
  if (!ret)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to uninhibit: %s", error->message);
      return;
    }

  if (!inhibitor)
    return;

  g_warn_if_fail (inhibitor->state == IDLE_STATE_UNINHIBITING);
  inhibitor->state = IDLE_STATE_UNINHIBITED;

  update_inhibition (inhibitor);
}

static void
update_inhibition (MetaWaylandIdleInhibitor *inhibitor)
{
  gboolean should_inhibit;

  if (!inhibitor->session_proxy)
    return;

  if (!inhibitor->surface ||
      !inhibitor->resource ||
      !inhibitor->actor)
    {
      should_inhibit = FALSE;
    }
  else
    {
      if (meta_surface_actor_is_effectively_obscured (inhibitor->actor))
        should_inhibit = FALSE;
      else
        should_inhibit = TRUE;
    }

  switch (inhibitor->state)
    {
    case IDLE_STATE_INITIALIZING:
    case IDLE_STATE_UNINHIBITED:
      if (!inhibitor->resource)
        {
          meta_wayland_inhibitor_free (inhibitor);
          return;
        }

      if (!should_inhibit)
        return;

      break;
    case IDLE_STATE_INHIBITED:
      if (should_inhibit)
        return;
      break;
    case IDLE_STATE_INHIBITING:
    case IDLE_STATE_UNINHIBITING:
      /* Update inhibition after current asynchronous call completes. */
      return;
    }

  if (should_inhibit)
    {
      g_dbus_proxy_call (G_DBUS_PROXY (inhibitor->session_proxy),
                         "Inhibit",
                         g_variant_new ("(ss)", "mutter", "idle-inhibit"),
                         G_DBUS_CALL_FLAGS_NONE,
                         -1,
                         NULL,
                         inhibit_completed,
                         inhibitor);
      inhibitor->state = IDLE_STATE_INHIBITING;
    }
  else
    {
      g_dbus_proxy_call (G_DBUS_PROXY (inhibitor->session_proxy),
                         "UnInhibit",
                         g_variant_new ("(u)", inhibitor->cookie),
                         G_DBUS_CALL_FLAGS_NONE,
                         -1,
                         NULL,
                         uninhibit_completed,
                         inhibitor);
      inhibitor->state = IDLE_STATE_UNINHIBITING;
    }
}

static void
is_obscured_changed (MetaSurfaceActor         *actor,
                     GParamSpec               *pspec,
                     MetaWaylandIdleInhibitor *inhibitor)
{
  update_inhibition (inhibitor);
}

static void
inhibitor_proxy_completed (GObject      *source,
                           GAsyncResult *res,
                           gpointer      user_data)
{
  MetaWaylandIdleInhibitor *inhibitor = user_data;
  GDBusProxy *proxy;
  g_autoptr (GError) error = NULL;

  proxy = g_dbus_proxy_new_finish (res, &error);
  if (!proxy)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          g_warning ("Failed to obtain org.freedesktop.ScreenSaver proxy: %s",
                     error->message);
        }
      return;
    }

  inhibitor->session_proxy = proxy;
  inhibitor->state = IDLE_STATE_UNINHIBITED;

  update_inhibition (inhibitor);
}

static void
idle_inhibit_destroy (struct wl_client   *client,
                      struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
idle_inhibitor_destructor (struct wl_resource *resource)
{
  MetaWaylandIdleInhibitor *inhibitor = wl_resource_get_user_data (resource);

  switch (inhibitor->state)
    {
    case IDLE_STATE_UNINHIBITED:
      meta_wayland_inhibitor_free (inhibitor);
      return;
    case IDLE_STATE_INITIALIZING:
    case IDLE_STATE_INHIBITED:
    case IDLE_STATE_INHIBITING:
    case IDLE_STATE_UNINHIBITING:
      inhibitor->resource = NULL;
      break;
    }

  update_inhibition (inhibitor);
}

static void
on_surface_destroyed (MetaWaylandSurface       *surface,
                      MetaWaylandIdleInhibitor *inhibitor)
{
  g_clear_signal_handler (&inhibitor->is_obscured_changed_handler,
                          inhibitor->actor);
  g_clear_signal_handler (&inhibitor->actor_destroyed_handler_id,
                          inhibitor->actor);
  inhibitor->actor = NULL;
  g_clear_signal_handler (&inhibitor->actor_changed_handler_id,
                          inhibitor->surface);
  g_clear_signal_handler (&inhibitor->surface_destroy_handler_id,
                          inhibitor->surface);
  inhibitor->surface = NULL;
}

static void
on_actor_destroyed (MetaSurfaceActor         *actor,
                    MetaWaylandIdleInhibitor *inhibitor)
{
  g_warn_if_fail (actor == inhibitor->actor);

  g_clear_signal_handler (&inhibitor->is_obscured_changed_handler, actor);
  g_clear_signal_handler (&inhibitor->actor_destroyed_handler_id, actor);
  inhibitor->actor = NULL;
}

static void
attach_actor (MetaWaylandIdleInhibitor *inhibitor)
{
  inhibitor->actor = meta_wayland_surface_get_actor (inhibitor->surface);

  if (inhibitor->actor)
    {
      inhibitor->is_obscured_changed_handler =
        g_signal_connect (inhibitor->actor, "notify::is-obscured",
                          G_CALLBACK (is_obscured_changed), inhibitor);
      inhibitor->actor_destroyed_handler_id =
        g_signal_connect (inhibitor->actor, "destroy",
                          G_CALLBACK (on_actor_destroyed), inhibitor);
    }
}

static void
on_actor_changed (MetaWaylandSurface       *surface,
                  MetaWaylandIdleInhibitor *inhibitor)
{
  g_clear_signal_handler (&inhibitor->is_obscured_changed_handler,
                          inhibitor->actor);
  g_clear_signal_handler (&inhibitor->actor_destroyed_handler_id,
                          inhibitor->actor);
  attach_actor (inhibitor);
}

static const struct zwp_idle_inhibitor_v1_interface meta_wayland_idle_inhibitor_interface =
{
  idle_inhibit_destroy,
};

static void
idle_inhibit_manager_create_inhibitor (struct wl_client   *client,
                                       struct wl_resource *resource,
                                       uint32_t            id,
                                       struct wl_resource *surface_resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandIdleInhibitor *inhibitor;
  struct wl_resource *inhibitor_resource;

  inhibitor_resource = wl_resource_create (client,
                                           &zwp_idle_inhibitor_v1_interface,
                                           wl_resource_get_version (resource),
                                           id);

  inhibitor = g_new0 (MetaWaylandIdleInhibitor, 1);
  inhibitor->surface = surface;
  inhibitor->resource = inhibitor_resource;

  attach_actor (inhibitor);

  inhibitor->actor_changed_handler_id =
    g_signal_connect (surface, "actor-changed",
                      G_CALLBACK (on_actor_changed), inhibitor);
  inhibitor->surface_destroy_handler_id =
    g_signal_connect (surface, "destroy",
                      G_CALLBACK (on_surface_destroyed), inhibitor);

  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                            G_DBUS_PROXY_FLAGS_NONE,
                            NULL,
                            "org.freedesktop.ScreenSaver",
                            "/org/freedesktop/ScreenSaver",
                            "org.freedesktop.ScreenSaver",
                            NULL,
                            inhibitor_proxy_completed,
                            inhibitor);

  wl_resource_set_implementation (inhibitor_resource,
                                  &meta_wayland_idle_inhibitor_interface,
                                  inhibitor,
                                  idle_inhibitor_destructor);
}


static const struct zwp_idle_inhibit_manager_v1_interface meta_wayland_idle_inhibit_manager_interface =
{
  idle_inhibit_destroy,
  idle_inhibit_manager_create_inhibitor,
};

static void
bind_idle_inhibit (struct wl_client *client,
                   void             *data,
                   uint32_t          version,
                   uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &zwp_idle_inhibit_manager_v1_interface,
                                 version, id);

  wl_resource_set_implementation (resource,
                                  &meta_wayland_idle_inhibit_manager_interface,
                                  NULL, NULL);
}

gboolean
meta_wayland_idle_inhibit_init (MetaWaylandCompositor *compositor)
{
  if (wl_global_create (compositor->wayland_display,
                        &zwp_idle_inhibit_manager_v1_interface,
                        META_ZWP_IDLE_INHIBIT_V1_VERSION,
                        NULL,
                        bind_idle_inhibit) == NULL)
    return FALSE;
  return TRUE;
}
