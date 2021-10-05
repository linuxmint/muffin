/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2013 Red Hat, Inc.
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
 *
 * Adapted from gnome-session/gnome-session/gs-idle-monitor.c and
 *         from gnome-desktop/libgnome-desktop/gnome-idle-monitor.c
 */

/**
 * SECTION:idle-monitor
 * @title: MetaIdleMonitor
 * @short_description: Mutter idle counter (similar to X's IDLETIME)
 */

#include "config.h"

#include <string.h>
#include <X11/Xlib.h>
#include <X11/extensions/sync.h>

#include "backends/gsm-inhibitor-flag.h"
#include "backends/meta-backend-private.h"
#include "backends/meta-idle-monitor-private.h"
#include "backends/meta-idle-monitor-dbus.h"
#include "clutter/clutter.h"
#include "meta/main.h"
#include "meta/meta-idle-monitor.h"
#include "meta/util.h"

G_STATIC_ASSERT(sizeof(unsigned long) == sizeof(gpointer));

enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_LAST,
};

static GParamSpec *obj_props[PROP_LAST];

G_DEFINE_TYPE (MetaIdleMonitor, meta_idle_monitor, G_TYPE_OBJECT)

static void
meta_idle_monitor_watch_fire (MetaIdleMonitorWatch *watch)
{
  MetaIdleMonitor *monitor;
  guint id;
  gboolean is_user_active_watch;

  monitor = watch->monitor;
  g_object_ref (monitor);

  g_clear_handle_id (&watch->idle_source_id, g_source_remove);

  id = watch->id;
  is_user_active_watch = (watch->timeout_msec == 0);

  if (watch->callback)
    watch->callback (monitor, id, watch->user_data);

  if (is_user_active_watch)
    meta_idle_monitor_remove_watch (monitor, id);

  g_object_unref (monitor);
}

static void
meta_idle_monitor_dispose (GObject *object)
{
  MetaIdleMonitor *monitor = META_IDLE_MONITOR (object);

  g_clear_pointer (&monitor->watches, g_hash_table_destroy);
  g_clear_object (&monitor->session_proxy);

  G_OBJECT_CLASS (meta_idle_monitor_parent_class)->dispose (object);
}

static void
meta_idle_monitor_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  MetaIdleMonitor *monitor = META_IDLE_MONITOR (object);

  switch (prop_id)
    {
    case PROP_DEVICE:
      g_value_set_object (value, monitor->device);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_idle_monitor_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  MetaIdleMonitor *monitor = META_IDLE_MONITOR (object);
  switch (prop_id)
    {
    case PROP_DEVICE:
      monitor->device = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_idle_monitor_class_init (MetaIdleMonitorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_idle_monitor_dispose;
  object_class->get_property = meta_idle_monitor_get_property;
  object_class->set_property = meta_idle_monitor_set_property;

  /**
   * MetaIdleMonitor:device:
   *
   * The device to listen to idletime on.
   */
  obj_props[PROP_DEVICE] =
    g_param_spec_object ("device",
                         "Device",
                         "The device to listen to idletime on",
                         CLUTTER_TYPE_INPUT_DEVICE,
                         G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_property (object_class, PROP_DEVICE, obj_props[PROP_DEVICE]);
}

static void
free_watch (gpointer data)
{
  MetaIdleMonitorWatch *watch = (MetaIdleMonitorWatch *) data;
  MetaIdleMonitor *monitor = watch->monitor;

  g_object_ref (monitor);

  g_clear_handle_id (&watch->idle_source_id, g_source_remove);

  if (watch->notify != NULL)
    watch->notify (watch->user_data);

  if (watch->timeout_source != NULL)
    g_source_destroy (watch->timeout_source);

  g_object_unref (monitor);
  g_slice_free (MetaIdleMonitorWatch, watch);
}

static void
update_inhibited_watch (gpointer key,
                        gpointer value,
                        gpointer user_data)
{
  MetaIdleMonitor *monitor = user_data;
  MetaIdleMonitorWatch *watch = value;

  if (!watch->timeout_source)
    return;

  if (monitor->inhibited)
    {
      g_source_set_ready_time (watch->timeout_source, -1);
    }
  else
    {
      g_source_set_ready_time (watch->timeout_source,
                               monitor->last_event_time +
                               watch->timeout_msec * 1000);
    }
}

static void
update_inhibited (MetaIdleMonitor *monitor,
                  gboolean         inhibited)
{
  if (inhibited == monitor->inhibited)
    return;

  monitor->inhibited = inhibited;

  g_hash_table_foreach (monitor->watches,
                        update_inhibited_watch,
                        monitor);
}

static void
meta_idle_monitor_inhibited_actions_changed (GDBusProxy  *session,
                                             GVariant    *changed,
                                             char       **invalidated,
                                             gpointer     user_data)
{
  MetaIdleMonitor *monitor = user_data;
  GVariant *v;

  v = g_variant_lookup_value (changed, "InhibitedActions", G_VARIANT_TYPE_UINT32);
  if (v)
    {
      gboolean inhibited;

      inhibited = !!(g_variant_get_uint32 (v) & GSM_INHIBITOR_FLAG_IDLE);
      g_variant_unref (v);

      if (!inhibited)
        monitor->last_event_time = g_get_monotonic_time ();
      update_inhibited (monitor, inhibited);
    }
}

static void
meta_idle_monitor_init (MetaIdleMonitor *monitor)
{
  GVariant *v;

  monitor->watches = g_hash_table_new_full (NULL, NULL, NULL, free_watch);
  monitor->last_event_time = g_get_monotonic_time ();

  /* Monitor inhibitors */
  monitor->session_proxy =
    g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                   G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS |
                                   G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                   NULL,
                                   "org.gnome.SessionManager",
                                   "/org/gnome/SessionManager",
                                   "org.gnome.SessionManager",
                                   NULL,
                                   NULL);
  if (!monitor->session_proxy)
    return;

  g_signal_connect (monitor->session_proxy, "g-properties-changed",
                    G_CALLBACK (meta_idle_monitor_inhibited_actions_changed),
                    monitor);

  v = g_dbus_proxy_get_cached_property (monitor->session_proxy,
                                        "InhibitedActions");
  if (v)
    {
      monitor->inhibited = !!(g_variant_get_uint32 (v) &
                              GSM_INHIBITOR_FLAG_IDLE);
      g_variant_unref (v);
    }
}

/**
 * meta_idle_monitor_get_core:
 *
 * Returns: (transfer none): the #MetaIdleMonitor that tracks the server-global
 * idletime for all devices.
 */
MetaIdleMonitor *
meta_idle_monitor_get_core (void)
{
  MetaBackend *backend = meta_get_backend ();
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);

  return meta_backend_get_idle_monitor (backend, clutter_seat_get_pointer (seat));
}

static guint32
get_next_watch_serial (void)
{
  static guint32 serial = 0;

  g_atomic_int_inc (&serial);

  return serial;
}

static gboolean
idle_monitor_dispatch_timeout (GSource     *source,
                               GSourceFunc  callback,
                               gpointer     user_data)
{
  MetaIdleMonitorWatch *watch = (MetaIdleMonitorWatch *) user_data;
  int64_t now;
  int64_t ready_time;

  now = g_source_get_time (source);
  ready_time = g_source_get_ready_time (source);
  if (ready_time > now)
    return G_SOURCE_CONTINUE;

  g_source_set_ready_time (watch->timeout_source, -1);

  meta_idle_monitor_watch_fire (watch);

  return G_SOURCE_CONTINUE;
}

static GSourceFuncs idle_monitor_source_funcs = {
  .prepare = NULL,
  .check = NULL,
  .dispatch = idle_monitor_dispatch_timeout,
  .finalize = NULL,
};

static MetaIdleMonitorWatch *
make_watch (MetaIdleMonitor           *monitor,
            guint64                    timeout_msec,
            MetaIdleMonitorWatchFunc   callback,
            gpointer                   user_data,
            GDestroyNotify             notify)
{
  MetaIdleMonitorWatch *watch;

  watch = g_slice_new0 (MetaIdleMonitorWatch);

  watch->monitor = monitor;
  watch->id = get_next_watch_serial ();
  watch->callback = callback;
  watch->user_data = user_data;
  watch->notify = notify;
  watch->timeout_msec = timeout_msec;

  if (timeout_msec != 0)
    {
      GSource *source = g_source_new (&idle_monitor_source_funcs,
                                      sizeof (GSource));

      g_source_set_callback (source, NULL, watch, NULL);
      if (!monitor->inhibited)
        {
          g_source_set_ready_time (source,
                                   monitor->last_event_time +
                                   timeout_msec * 1000);
        }
      g_source_attach (source, NULL);
      g_source_unref (source);

      watch->timeout_source = source;
    }

  g_hash_table_insert (monitor->watches,
                       GUINT_TO_POINTER (watch->id),
                       watch);
  return watch;
}

/**
 * meta_idle_monitor_add_idle_watch:
 * @monitor: A #MetaIdleMonitor
 * @interval_msec: The idletime interval, in milliseconds
 * @callback: (nullable): The callback to call when the user has
 *     accumulated @interval_msec milliseconds of idle time.
 * @user_data: (nullable): The user data to pass to the callback
 * @notify: A #GDestroyNotify
 *
 * Returns: a watch id
 *
 * Adds a watch for a specific idle time. The callback will be called
 * when the user has accumulated @interval_msec milliseconds of idle time.
 * This function will return an ID that can either be passed to
 * meta_idle_monitor_remove_watch(), or can be used to tell idle time
 * watches apart if you have more than one.
 *
 * Also note that this function will only care about positive transitions
 * (user's idle time exceeding a certain time). If you want to know about
 * when the user has become active, use
 * meta_idle_monitor_add_user_active_watch().
 */
guint
meta_idle_monitor_add_idle_watch (MetaIdleMonitor	       *monitor,
                                  guint64	                interval_msec,
                                  MetaIdleMonitorWatchFunc      callback,
                                  gpointer			user_data,
                                  GDestroyNotify		notify)
{
  MetaIdleMonitorWatch *watch;

  g_return_val_if_fail (META_IS_IDLE_MONITOR (monitor), 0);
  g_return_val_if_fail (interval_msec > 0, 0);

  watch = make_watch (monitor,
                      interval_msec,
                      callback,
                      user_data,
                      notify);

  return watch->id;
}

/**
 * meta_idle_monitor_add_user_active_watch:
 * @monitor: A #MetaIdleMonitor
 * @callback: (nullable): The callback to call when the user is
 *     active again.
 * @user_data: (nullable): The user data to pass to the callback
 * @notify: A #GDestroyNotify
 *
 * Returns: a watch id
 *
 * Add a one-time watch to know when the user is active again.
 * Note that this watch is one-time and will de-activate after the
 * function is called, for efficiency purposes. It's most convenient
 * to call this when an idle watch, as added by
 * meta_idle_monitor_add_idle_watch(), has triggered.
 */
guint
meta_idle_monitor_add_user_active_watch (MetaIdleMonitor          *monitor,
                                         MetaIdleMonitorWatchFunc  callback,
                                         gpointer		   user_data,
                                         GDestroyNotify	           notify)
{
  MetaIdleMonitorWatch *watch;

  g_return_val_if_fail (META_IS_IDLE_MONITOR (monitor), 0);

  watch = make_watch (monitor,
                      0,
                      callback,
                      user_data,
                      notify);

  return watch->id;
}

/**
 * meta_idle_monitor_remove_watch:
 * @monitor: A #MetaIdleMonitor
 * @id: A watch ID
 *
 * Removes an idle time watcher, previously added by
 * meta_idle_monitor_add_idle_watch() or
 * meta_idle_monitor_add_user_active_watch().
 */
void
meta_idle_monitor_remove_watch (MetaIdleMonitor *monitor,
                                guint	         id)
{
  g_return_if_fail (META_IS_IDLE_MONITOR (monitor));

  g_object_ref (monitor);
  g_hash_table_remove (monitor->watches,
                       GUINT_TO_POINTER (id));
  g_object_unref (monitor);
}

/**
 * meta_idle_monitor_get_idletime:
 * @monitor: A #MetaIdleMonitor
 *
 * Returns: The current idle time, in milliseconds, or -1 for not supported
 */
gint64
meta_idle_monitor_get_idletime (MetaIdleMonitor *monitor)
{
  return (g_get_monotonic_time () - monitor->last_event_time) / 1000;
}

void
meta_idle_monitor_reset_idletime (MetaIdleMonitor *monitor)
{
  GList *node, *watch_ids;

  monitor->last_event_time = g_get_monotonic_time ();

  watch_ids = g_hash_table_get_keys (monitor->watches);

  for (node = watch_ids; node != NULL; node = node->next)
    {
      guint watch_id = GPOINTER_TO_UINT (node->data);
      MetaIdleMonitorWatch *watch;

      watch = g_hash_table_lookup (monitor->watches,
                                   GUINT_TO_POINTER (watch_id));
      if (!watch)
        continue;

      if (watch->timeout_msec == 0)
        {
          meta_idle_monitor_watch_fire (watch);
        }
      else
        {
          if (monitor->inhibited)
            {
              g_source_set_ready_time (watch->timeout_source, -1);
            }
          else
            {
              g_source_set_ready_time (watch->timeout_source,
                                       monitor->last_event_time +
                                       watch->timeout_msec * 1000);
            }
        }
    }

  g_list_free (watch_ids);
}
