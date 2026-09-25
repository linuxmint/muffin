/*
 * Copyright (C) 2026 Linux Mint
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

#include "wayland/meta-wayland-idle-notify.h"

#include "backends/meta-idle-monitor-private.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-versions.h"

#include "ext-idle-notify-v1-server-protocol.h"

typedef struct _MetaWaylandIdleNotification
{
  struct wl_resource *resource;

  MetaIdleMonitor *monitor;

  guint idle_watch_id;
  guint active_watch_id;

  guint timeout;

  gboolean ignore_inhibitors;
} MetaWaylandIdleNotification;

static void idle_trigger_cb (MetaIdleMonitor *monitor,
                             guint            id,
                             gpointer         user_data);

static void
active_trigger_cb (MetaIdleMonitor *monitor,
                   guint            id,
                   gpointer         user_data)
{
  MetaWaylandIdleNotification *notification = user_data;

  notification->active_watch_id = 0;

  ext_idle_notification_v1_send_resumed (notification->resource);

  notification->idle_watch_id =
    meta_idle_monitor_add_idle_watch_full (notification->monitor,
                                           notification->timeout,
                                           notification->ignore_inhibitors,
                                           idle_trigger_cb,
                                           notification,
                                           NULL);
}

static void
idle_trigger_cb (MetaIdleMonitor *monitor,
                 guint            id,
                 gpointer         user_data)
{
  MetaWaylandIdleNotification *notification = user_data;

  meta_idle_monitor_remove_watch (notification->monitor, id);
  notification->idle_watch_id = 0;

  ext_idle_notification_v1_send_idled (notification->resource);

  notification->active_watch_id =
    meta_idle_monitor_add_user_active_watch (notification->monitor,
                                             active_trigger_cb,
                                             notification,
                                             NULL);
}

static void
ext_idle_notification_destructor (struct wl_resource *resource)
{
  MetaWaylandIdleNotification *notification = wl_resource_get_user_data (resource);

  if (notification->idle_watch_id)
      meta_idle_monitor_remove_watch (notification->monitor,
                                      notification->idle_watch_id);

  if (notification->active_watch_id)
      meta_idle_monitor_remove_watch (notification->monitor,
                                      notification->active_watch_id);

  g_free (notification);
}

static void
ext_idle_notification_destroy (struct wl_client   *client,
                               struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct ext_idle_notification_v1_interface ext_idle_notification_v1_impl = {
  ext_idle_notification_destroy,
};

static void
ext_idle_notification_create (struct wl_client   *client,
                              struct wl_resource *manager_resource,
                              uint32_t            id,
                              uint32_t            timeout,
                              struct wl_resource *seat_resource,
                              gboolean            ignore_inhibitors)
{
  MetaWaylandSeat *seat = wl_resource_get_user_data (seat_resource);
  MetaWaylandIdleNotification *notification;
  uint32_t version;
  ClutterInputDevice *pointer_device = NULL;

  notification = g_new0 (MetaWaylandIdleNotification, 1);
  notification->timeout = timeout;
  notification->ignore_inhibitors = ignore_inhibitors;

  if (seat && seat->pointer)
      pointer_device = seat->pointer->device;

  if (pointer_device)
    {
      MetaBackend *backend = meta_get_backend ();
      notification->monitor = meta_backend_get_idle_monitor (backend, pointer_device);
    }
  else
    {
      notification->monitor = meta_idle_monitor_get_core ();
    }

  version = wl_resource_get_version (manager_resource);
  notification->resource = wl_resource_create (client,
                                               &ext_idle_notification_v1_interface,
                                               version,
                                               id);

  wl_resource_set_implementation (notification->resource,
                                  &ext_idle_notification_v1_impl,
                                  notification,
                                  ext_idle_notification_destructor);

  notification->idle_watch_id =
  meta_idle_monitor_add_idle_watch_full (notification->monitor,
                                         notification->timeout,
                                         notification->ignore_inhibitors,
                                         idle_trigger_cb,
                                         notification,
                                         NULL);
}

static void
ext_idle_notifier_get_idle_notification (struct wl_client   *client,
                                         struct wl_resource *resource,
                                         uint32_t            id,
                                         uint32_t            timeout,
                                         struct wl_resource *seat)
{
  ext_idle_notification_create (client, resource,
                                id, timeout,
                                seat, FALSE);
}

static void
ext_idle_notifier_get_input_idle_notification (struct wl_client   *client,
                                               struct wl_resource *resource,
                                               uint32_t            id,
                                               uint32_t            timeout,
                                               struct wl_resource *seat)
{
  ext_idle_notification_create (client, resource,
                                id, timeout,
                                seat, TRUE);
}

static void
ext_idle_notifier_destroy (struct wl_client   *client,
                           struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct ext_idle_notifier_v1_interface ext_idle_notifier_v1_impl = {
  ext_idle_notifier_destroy,
  ext_idle_notifier_get_idle_notification,
  ext_idle_notifier_get_input_idle_notification,
};

static void
bind_ext_idle_notifier (struct wl_client *client,
                        void             *data,
                        uint32_t          version,
                        uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &ext_idle_notifier_v1_interface,
                                 version,
                                 id);
  wl_resource_set_implementation (resource,
                                  &ext_idle_notifier_v1_impl,
                                  NULL,
                                  NULL);
}

void
meta_wayland_idle_notify_init (MetaWaylandCompositor *compositor)
{
  if (wl_global_create (compositor->wayland_display,
      &ext_idle_notifier_v1_interface,
      META_EXT_IDLE_NOTIFY_V1_VERSION,
      NULL,
      bind_ext_idle_notifier) == NULL)
      g_error ("Failed to register a global ext-idle-notify object");
}
