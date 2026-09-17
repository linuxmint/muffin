/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

/**
 * SECTION:meta-backend
 * @title: MetaBackend
 * @short_description: Handles monitor config, modesetting, cursor sprites, ...
 *
 * MetaBackend is the abstraction that deals with several things like:
 * - Modesetting (depending on the backend, this can be done either by X or KMS)
 * - Initializing the #MetaSettings
 * - Setting up Monitor configuration
 * - Input device configuration (using the #ClutterDeviceManager)
 * - Creating the #MetaRenderer
 * - Setting up the stage of the scene graph (using #MetaStage)
 * - Creating the object that deals wih the cursor (using #MetaCursorTracker)
 *     and its possible pointer constraint (using #MetaPointerConstraint)
 * - Setting the cursor sprite (using #MetaCursorRenderer)
 * - Interacting with logind (using the appropriate D-Bus interface)
 * - Querying UPower (over D-Bus) to know when the lid is closed
 * - Setup Remote Desktop / Screencasting (#MetaRemoteDesktop)
 * - Setup the #MetaEgl object
 *
 * Note that the #MetaBackend is not a subclass of #ClutterBackend. It is
 * responsible for creating the correct one, based on the backend that is
 * used (#MetaBackendNative or #MetaBackendX11).
 */

#include "config.h"

#include "backends/meta-backend-private.h"

#include <stdlib.h>

#include "backends/meta-cursor-tracker-private.h"
#include "backends/meta-idle-monitor-private.h"
#include "backends/meta-input-settings-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor-manager-dummy.h"
#include "backends/meta-settings-private.h"
#include "backends/meta-stage-private.h"
#include "backends/x11/meta-backend-x11.h"
#include "clutter/clutter-muffin.h"
#include "meta/main.h"
#include "meta/meta-backend.h"
#include "meta/util.h"

#ifdef HAVE_PROFILER
#include "backends/meta-profiler.h"
#endif

#ifdef HAVE_REMOTE_DESKTOP
#include "backends/meta-dbus-session-watcher.h"
#include "backends/meta-remote-access-controller-private.h"
#include "backends/meta-remote-desktop.h"
#include "backends/meta-screen-cast.h"
#endif

#ifdef HAVE_NATIVE_BACKEND
#include "backends/native/meta-backend-native.h"
#endif

#ifdef HAVE_WAYLAND
#include "wayland/meta-wayland.h"
#endif

enum
{
  KEYMAP_CHANGED,
  KEYMAP_LAYOUT_GROUP_CHANGED,
  LAST_DEVICE_CHANGED,
  LID_IS_CLOSED_CHANGED,
  GPU_ADDED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

static MetaBackend *_backend;

static gboolean stage_views_disabled = FALSE;

/**
 * meta_get_backend:
 *
 * Accessor for the singleton MetaBackend.
 *
 * Returns: (transfer none): The only #MetaBackend there is.
 */
MetaBackend *
meta_get_backend (void)
{
  return _backend;
}

struct _MetaBackendPrivate
{
  MetaMonitorManager *monitor_manager;
  MetaOrientationManager *orientation_manager;
  MetaCursorTracker *cursor_tracker;
  MetaCursorRenderer *cursor_renderer;
  MetaInputSettings *input_settings;
  MetaRenderer *renderer;
#ifdef HAVE_EGL
  MetaEgl *egl;
#endif
  MetaSettings *settings;
#ifdef HAVE_REMOTE_DESKTOP
  MetaRemoteAccessController *remote_access_controller;
  MetaDbusSessionWatcher *dbus_session_watcher;
  MetaScreenCast *screen_cast;
  MetaRemoteDesktop *remote_desktop;
#endif

#ifdef HAVE_WAYLAND
  MetaWaylandCompositor *wayland_compositor;
#endif

#ifdef HAVE_PROFILER
  MetaProfiler *profiler;
#endif

#ifdef HAVE_LIBWACOM
  WacomDeviceDatabase *wacom_db;
#endif

  ClutterBackend *clutter_backend;
  ClutterActor *stage;

  GList *gpus;

  gboolean is_pointer_position_initialized;

  guint device_update_idle_id;
  gulong keymap_state_changed_id;

  GHashTable *device_monitors;

  ClutterInputDevice *current_device;

  MetaPointerConstraint *client_pointer_constraint;
  MetaDnd *dnd;

  guint upower_watch_id;
  GDBusProxy *upower_proxy;
  gboolean lid_is_closed;

  guint sleep_signal_id;
  GCancellable *cancellable;
  GDBusConnection *system_bus;

  gboolean was_headless;
};
typedef struct _MetaBackendPrivate MetaBackendPrivate;

static void
initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (MetaBackend, meta_backend, G_TYPE_OBJECT,
                                  G_ADD_PRIVATE (MetaBackend)
                                  G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                         initable_iface_init));

static void
meta_backend_finalize (GObject *object)
{
  MetaBackend *backend = META_BACKEND (object);
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  if (priv->keymap_state_changed_id)
    {
      ClutterSeat *seat;
      ClutterKeymap *keymap;

      seat = clutter_backend_get_default_seat (priv->clutter_backend);
      keymap = clutter_seat_get_keymap (seat);
      g_clear_signal_handler (&priv->keymap_state_changed_id, keymap);
    }

  g_list_free_full (priv->gpus, g_object_unref);

  g_clear_object (&priv->current_device);
  g_clear_object (&priv->monitor_manager);
  g_clear_object (&priv->orientation_manager);
  g_clear_object (&priv->input_settings);
#ifdef HAVE_REMOTE_DESKTOP
  g_clear_object (&priv->remote_desktop);
  g_clear_object (&priv->screen_cast);
  g_clear_object (&priv->dbus_session_watcher);
  g_clear_object (&priv->remote_access_controller);
#endif

#ifdef HAVE_LIBWACOM
  g_clear_pointer (&priv->wacom_db, libwacom_database_destroy);
#endif

  if (priv->sleep_signal_id)
    g_dbus_connection_signal_unsubscribe (priv->system_bus, priv->sleep_signal_id);
  if (priv->upower_watch_id)
    g_bus_unwatch_name (priv->upower_watch_id);
  g_cancellable_cancel (priv->cancellable);
  g_clear_object (&priv->cancellable);
  g_clear_object (&priv->system_bus);
  g_clear_object (&priv->upower_proxy);

  g_clear_handle_id (&priv->device_update_idle_id, g_source_remove);

  g_hash_table_destroy (priv->device_monitors);

  g_clear_object (&priv->settings);

#ifdef HAVE_PROFILER
  g_clear_object (&priv->profiler);
#endif

  G_OBJECT_CLASS (meta_backend_parent_class)->finalize (object);
}

static void
meta_backend_sync_screen_size (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  int width, height;

  meta_monitor_manager_get_screen_size (priv->monitor_manager, &width, &height);

  META_BACKEND_GET_CLASS (backend)->update_screen_size (backend, width, height);
}

static void
reset_pointer_position (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  MetaMonitorManager *monitor_manager = priv->monitor_manager;
  ClutterSeat *seat = clutter_backend_get_default_seat (priv->clutter_backend);
  MetaLogicalMonitor *primary;

  primary =
    meta_monitor_manager_get_primary_logical_monitor (monitor_manager);

  /* Move the pointer out of the way to avoid hovering over reactive
   * elements (e.g. users list at login) causing undesired behaviour. */
  clutter_seat_warp_pointer (seat,
                             primary->rect.x + primary->rect.width * 0.9,
                             primary->rect.y + primary->rect.height * 0.9);
}

void
meta_backend_monitors_changed (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (priv->clutter_backend);
  ClutterInputDevice *device = clutter_seat_get_pointer (seat);
  graphene_point_t point;

  meta_backend_sync_screen_size (backend);

  if (clutter_input_device_get_coords (device, NULL, &point))
    {
      /* If we're outside all monitors, warp the pointer back inside */
      if ((!meta_monitor_manager_get_logical_monitor_at (monitor_manager,
                                                         point.x, point.y) ||
           !priv->is_pointer_position_initialized) &&
          !meta_monitor_manager_is_headless (monitor_manager))
        {
          reset_pointer_position (backend);
          priv->is_pointer_position_initialized = TRUE;
        }
    }

  meta_cursor_renderer_force_update (priv->cursor_renderer);

  if (meta_monitor_manager_is_headless (priv->monitor_manager) &&
      !priv->was_headless)
    {
      clutter_stage_freeze_updates (CLUTTER_STAGE (priv->stage));
      priv->was_headless = TRUE;
    }
  else if (!meta_monitor_manager_is_headless (priv->monitor_manager) &&
           priv->was_headless)
    {
      clutter_stage_thaw_updates (CLUTTER_STAGE (priv->stage));
      priv->was_headless = FALSE;
    }
}

void
meta_backend_foreach_device_monitor (MetaBackend *backend,
                                     GFunc        func,
                                     gpointer     user_data)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  GHashTableIter iter;
  gpointer value;

  g_hash_table_iter_init (&iter, priv->device_monitors);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      MetaIdleMonitor *device_monitor = META_IDLE_MONITOR (value);

      func (device_monitor, user_data);
    }
}

static MetaIdleMonitor *
meta_backend_create_idle_monitor (MetaBackend        *backend,
                                  ClutterInputDevice *device)
{
  return g_object_new (META_TYPE_IDLE_MONITOR,
                       "device", device,
                       NULL);
}

static void
create_device_monitor (MetaBackend        *backend,
                       ClutterInputDevice *device)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  MetaIdleMonitor *idle_monitor;

  if (g_hash_table_contains (priv->device_monitors, device))
    return;

  idle_monitor = meta_backend_create_idle_monitor (backend, device);
  g_hash_table_insert (priv->device_monitors, device, idle_monitor);
}

static void
destroy_device_monitor (MetaBackend        *backend,
                        ClutterInputDevice *device)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  g_hash_table_remove (priv->device_monitors, device);
}

static void
meta_backend_monitor_device (MetaBackend        *backend,
                             ClutterInputDevice *device)
{
  create_device_monitor (backend, device);
}

static inline gboolean
device_is_slave_touchscreen (ClutterInputDevice *device)
{
  return (clutter_input_device_get_device_mode (device) != CLUTTER_INPUT_MODE_MASTER &&
          clutter_input_device_get_device_type (device) == CLUTTER_TOUCHSCREEN_DEVICE);
}

static inline gboolean
check_has_pointing_device (ClutterSeat *seat)
{
  GList *l, *devices;
  gboolean found = FALSE;

  devices = clutter_seat_list_devices (seat);

  for (l = devices; l; l = l->next)
    {
      ClutterInputDevice *device = l->data;

      if (clutter_input_device_get_device_mode (device) == CLUTTER_INPUT_MODE_MASTER)
        continue;
      if (clutter_input_device_get_device_type (device) == CLUTTER_TOUCHSCREEN_DEVICE ||
          clutter_input_device_get_device_type (device) == CLUTTER_KEYBOARD_DEVICE)
        continue;

      found = TRUE;
      break;
    }

  g_list_free (devices);

  return found;
}

static inline gboolean
check_has_slave_touchscreen (ClutterSeat *seat)
{
  GList *l, *devices;
  gboolean found = FALSE;

  devices = clutter_seat_list_devices (seat);

  for (l = devices; l; l = l->next)
    {
      ClutterInputDevice *device = l->data;

      if (clutter_input_device_get_device_mode (device) != CLUTTER_INPUT_MODE_MASTER &&
          clutter_input_device_get_device_type (device) == CLUTTER_TOUCHSCREEN_DEVICE)
        {
          found = TRUE;
          break;
        }
    }

  g_list_free (devices);

  return found;
}

static void
on_device_added (ClutterSeat        *seat,
                 ClutterInputDevice *device,
                 gpointer            user_data)
{
  MetaBackend *backend = META_BACKEND (user_data);
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  create_device_monitor (backend, device);

  if (device_is_slave_touchscreen (device))
    meta_cursor_tracker_set_pointer_visible (priv->cursor_tracker, FALSE);
}

static void
on_device_removed (ClutterSeat        *seat,
                   ClutterInputDevice *device,
                   gpointer            user_data)
{
  MetaBackend *backend = META_BACKEND (user_data);
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  destroy_device_monitor (backend, device);

  /* If the device the user last interacted goes away, check again pointer
   * visibility.
   */
  if (priv->current_device == device)
    {
      MetaCursorTracker *cursor_tracker = priv->cursor_tracker;
      gboolean has_touchscreen, has_pointing_device;
      ClutterInputDeviceType device_type;

      g_clear_object (&priv->current_device);
      g_clear_handle_id (&priv->device_update_idle_id, g_source_remove);

      device_type = clutter_input_device_get_device_type (device);
      has_touchscreen = check_has_slave_touchscreen (seat);

      if (device_type == CLUTTER_TOUCHSCREEN_DEVICE && has_touchscreen)
        {
          /* There's more touchscreens left, keep the pointer hidden */
          meta_cursor_tracker_set_pointer_visible (cursor_tracker, FALSE);
        }
      else if (device_type != CLUTTER_KEYBOARD_DEVICE)
        {
          has_pointing_device = check_has_pointing_device (seat);
          meta_cursor_tracker_set_pointer_visible (cursor_tracker,
                                                   has_pointing_device &&
                                                   !has_touchscreen);
        }
    }

  if (priv->current_device == device)
    meta_backend_update_last_device (backend, NULL);
}

static void
create_device_monitors (MetaBackend *backend,
                        ClutterSeat *seat)
{
  GList *l, *devices;

  create_device_monitor (backend, clutter_seat_get_pointer (seat));
  create_device_monitor (backend, clutter_seat_get_keyboard (seat));

  devices = clutter_seat_list_devices (seat);
  for (l = devices; l; l = l->next)
    {
      ClutterInputDevice *device = l->data;

      meta_backend_monitor_device (backend, device);
    }

  g_list_free (devices);
}

static MetaInputSettings *
meta_backend_create_input_settings (MetaBackend *backend)
{
  return META_BACKEND_GET_CLASS (backend)->create_input_settings (backend);
}

static void
meta_backend_real_post_init (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (priv->clutter_backend);
  ClutterKeymap *keymap = clutter_seat_get_keymap (seat);

  priv->stage = meta_stage_new (backend);
  clutter_actor_realize (priv->stage);
  META_BACKEND_GET_CLASS (backend)->select_stage_events (backend);

  meta_monitor_manager_setup (priv->monitor_manager);

  meta_backend_sync_screen_size (backend);

  priv->cursor_renderer = META_BACKEND_GET_CLASS (backend)->create_cursor_renderer (backend);

  priv->device_monitors =
    g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify) g_object_unref);

  create_device_monitors (backend, seat);

  g_signal_connect_object (seat, "device-added",
                           G_CALLBACK (on_device_added), backend, 0);
  g_signal_connect_object (seat, "device-removed",
                           G_CALLBACK (on_device_removed), backend,
                           G_CONNECT_AFTER);

  priv->input_settings = meta_backend_create_input_settings (backend);

  if (priv->input_settings)
    {
      priv->keymap_state_changed_id =
        g_signal_connect_swapped (keymap, "state-changed",
                                  G_CALLBACK (meta_input_settings_maybe_save_numlock_state),
                                  priv->input_settings);
      meta_input_settings_maybe_restore_numlock_state (priv->input_settings);
    }

#ifdef HAVE_REMOTE_DESKTOP
  priv->remote_access_controller =
    g_object_new (META_TYPE_REMOTE_ACCESS_CONTROLLER, NULL);
  priv->dbus_session_watcher = g_object_new (META_TYPE_DBUS_SESSION_WATCHER, NULL);
  priv->screen_cast = meta_screen_cast_new (backend,
                                            priv->dbus_session_watcher);
  priv->remote_desktop = meta_remote_desktop_new (priv->dbus_session_watcher);
#endif /* HAVE_REMOTE_DESKTOP */

  if (!meta_monitor_manager_is_headless (priv->monitor_manager))
    {
      reset_pointer_position (backend);
      priv->is_pointer_position_initialized = TRUE;
    }
}

static gboolean
meta_backend_real_grab_device (MetaBackend *backend,
                               int          device_id,
                               uint32_t     timestamp)
{
  /* Do nothing */
  return TRUE;
}

static gboolean
meta_backend_real_ungrab_device (MetaBackend *backend,
                                 int          device_id,
                                 uint32_t     timestamp)
{
  /* Do nothing */
  return TRUE;
}

static void
meta_backend_real_select_stage_events (MetaBackend *backend)
{
  /* Do nothing */
}

static gboolean
meta_backend_real_is_lid_closed (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->lid_is_closed;
}

gboolean
meta_backend_is_lid_closed (MetaBackend *backend)
{
  return META_BACKEND_GET_CLASS (backend)->is_lid_closed (backend);
}

static void
upower_properties_changed (GDBusProxy *proxy,
                           GVariant   *changed_properties,
                           GStrv       invalidated_properties,
                           gpointer    user_data)
{
  MetaBackend *backend = user_data;
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  GVariant *v;
  gboolean lid_is_closed;

  v = g_variant_lookup_value (changed_properties,
                              "LidIsClosed",
                              G_VARIANT_TYPE_BOOLEAN);
  if (!v)
    return;

  lid_is_closed = g_variant_get_boolean (v);
  g_variant_unref (v);

  if (lid_is_closed == priv->lid_is_closed)
    return;

  priv->lid_is_closed = lid_is_closed;
  g_signal_emit (backend, signals[LID_IS_CLOSED_CHANGED], 0,
                 priv->lid_is_closed);

  if (lid_is_closed)
    return;

  meta_idle_monitor_reset_idletime (meta_idle_monitor_get_core ());
}

static void
upower_ready_cb (GObject      *source_object,
                 GAsyncResult *res,
                 gpointer      user_data)
{
  MetaBackend *backend;
  MetaBackendPrivate *priv;
  GDBusProxy *proxy;
  GError *error = NULL;
  GVariant *v;

  proxy = g_dbus_proxy_new_finish (res, &error);
  if (!proxy)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to create UPower proxy: %s", error->message);
      g_error_free (error);
      return;
    }

  backend = META_BACKEND (user_data);
  priv = meta_backend_get_instance_private (backend);

  priv->upower_proxy = proxy;
  g_signal_connect (proxy, "g-properties-changed",
                    G_CALLBACK (upower_properties_changed), backend);

  v = g_dbus_proxy_get_cached_property (proxy, "LidIsClosed");
  if (!v)
    return;
  priv->lid_is_closed = g_variant_get_boolean (v);
  g_variant_unref (v);

  if (priv->lid_is_closed)
    {
      g_signal_emit (backend, signals[LID_IS_CLOSED_CHANGED], 0,
                     priv->lid_is_closed);
    }
}

static void
upower_appeared (GDBusConnection *connection,
                 const gchar     *name,
                 const gchar     *name_owner,
                 gpointer         user_data)
{
  MetaBackend *backend = META_BACKEND (user_data);
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  g_dbus_proxy_new (connection,
                    G_DBUS_PROXY_FLAGS_NONE,
                    NULL,
                    "org.freedesktop.UPower",
                    "/org/freedesktop/UPower",
                    "org.freedesktop.UPower",
                    priv->cancellable,
                    upower_ready_cb,
                    backend);
}

static void
upower_vanished (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
  MetaBackend *backend = META_BACKEND (user_data);
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  g_clear_object (&priv->upower_proxy);
}

static void
meta_backend_constructed (GObject *object)
{
  MetaBackend *backend = META_BACKEND (object);
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  MetaBackendClass *backend_class =
   META_BACKEND_GET_CLASS (backend);

#ifdef HAVE_LIBWACOM
  priv->wacom_db = libwacom_database_new ();
  if (!priv->wacom_db)
    {
      g_warning ("Could not create database of Wacom devices, "
                 "expect tablets to misbehave");
    }
#endif

  if (backend_class->is_lid_closed != meta_backend_real_is_lid_closed)
    return;

  priv->upower_watch_id = g_bus_watch_name (G_BUS_TYPE_SYSTEM,
                                            "org.freedesktop.UPower",
                                            G_BUS_NAME_WATCHER_FLAGS_NONE,
                                            upower_appeared,
                                            upower_vanished,
                                            backend,
                                            NULL);
}

static void
meta_backend_class_init (MetaBackendClass *klass)
{
  const gchar *muffin_stage_views;
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_backend_finalize;
  object_class->constructed = meta_backend_constructed;

  klass->post_init = meta_backend_real_post_init;
  klass->grab_device = meta_backend_real_grab_device;
  klass->ungrab_device = meta_backend_real_ungrab_device;
  klass->select_stage_events = meta_backend_real_select_stage_events;
  klass->is_lid_closed = meta_backend_real_is_lid_closed;

  signals[KEYMAP_CHANGED] =
    g_signal_new ("keymap-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
  signals[KEYMAP_LAYOUT_GROUP_CHANGED] =
    g_signal_new ("keymap-layout-group-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, G_TYPE_UINT);
  signals[LAST_DEVICE_CHANGED] =
    g_signal_new ("last-device-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, CLUTTER_TYPE_INPUT_DEVICE);
  signals[LID_IS_CLOSED_CHANGED] =
    g_signal_new ("lid-is-closed-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  /**
   * MetaBackend::gpu-added: (skip)
   * @backend: the #MetaBackend
   * @gpu: the #MetaGpu
   */
  signals[GPU_ADDED] =
    g_signal_new ("gpu-added",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, META_TYPE_GPU);

  muffin_stage_views = g_getenv ("MUFFIN_STAGE_VIEWS");
  stage_views_disabled = g_strcmp0 (muffin_stage_views, "0") == 0;
}

static MetaMonitorManager *
meta_backend_create_monitor_manager (MetaBackend *backend,
                                     GError     **error)
{
  if (g_getenv ("META_DUMMY_MONITORS"))
    return g_object_new (META_TYPE_MONITOR_MANAGER_DUMMY, NULL);

  return META_BACKEND_GET_CLASS (backend)->create_monitor_manager (backend,
                                                                   error);
}

static MetaRenderer *
meta_backend_create_renderer (MetaBackend *backend,
                              GError     **error)
{
  return META_BACKEND_GET_CLASS (backend)->create_renderer (backend, error);
}

static void
prepare_for_sleep_cb (GDBusConnection *connection,
                      const gchar     *sender_name,
                      const gchar     *object_path,
                      const gchar     *interface_name,
                      const gchar     *signal_name,
                      GVariant        *parameters,
                      gpointer         user_data)
{
  gboolean suspending;

  g_variant_get (parameters, "(b)", &suspending);
  if (suspending)
    return;
  meta_idle_monitor_reset_idletime (meta_idle_monitor_get_core ());
}

static void
system_bus_gotten_cb (GObject      *object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  MetaBackendPrivate *priv;
  GDBusConnection *bus;

  bus = g_bus_get_finish (res, NULL);
  if (!bus)
    return;

  priv = meta_backend_get_instance_private (user_data);
  priv->system_bus = bus;
  priv->sleep_signal_id =
    g_dbus_connection_signal_subscribe (priv->system_bus,
                                        "org.freedesktop.login1",
                                        "org.freedesktop.login1.Manager",
                                        "PrepareForSleep",
                                        "/org/freedesktop/login1",
                                        NULL,
                                        G_DBUS_SIGNAL_FLAGS_NONE,
                                        prepare_for_sleep_cb,
                                        NULL,
                                        NULL);
}

#ifdef HAVE_WAYLAND
MetaWaylandCompositor *
meta_backend_get_wayland_compositor (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->wayland_compositor;
}

void
meta_backend_init_wayland_display (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  priv->wayland_compositor = meta_wayland_compositor_new (backend);
}

void
meta_backend_init_wayland (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  meta_wayland_compositor_setup (priv->wayland_compositor);
}
#endif

/* Mutter is responsible for pulling events off the X queue, so Clutter
 * doesn't need (and shouldn't) run its normal event source which polls
 * the X fd, but we do have to deal with dispatching events that accumulate
 * in the clutter queue. This happens, for example, when clutter generate
 * enter/leave events on mouse motion - several events are queued in the
 * clutter queue but only one dispatched. It could also happen because of
 * explicit calls to clutter_event_put(). We add a very simple custom
 * event loop source which is simply responsible for pulling events off
 * of the queue and dispatching them before we block for new events.
 */

static gboolean
clutter_source_prepare (GSource *source,
                        int     *timeout)
{
  *timeout = -1;

  return clutter_events_pending ();
}

static gboolean
clutter_source_check (GSource *source)
{
  return clutter_events_pending ();
}

static gboolean
clutter_source_dispatch (GSource     *source,
                         GSourceFunc  callback,
                         gpointer     user_data)
{
  ClutterEvent *event = clutter_event_get ();

  if (event)
    {
      clutter_do_event (event);
      clutter_event_free (event);
    }

  return TRUE;
}

static GSourceFuncs clutter_source_funcs = {
  clutter_source_prepare,
  clutter_source_check,
  clutter_source_dispatch
};

static ClutterBackend *
meta_get_clutter_backend (void)
{
  MetaBackend *backend = meta_get_backend ();

  return meta_backend_get_clutter_backend (backend);
}

static gboolean
init_clutter (MetaBackend  *backend,
              GError      **error)
{
  GSource *source;

  clutter_set_custom_backend_func (meta_get_clutter_backend);

  if (clutter_init (NULL, NULL) != CLUTTER_INIT_SUCCESS)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Unable to initialize Clutter");
      return FALSE;
    }

  source = g_source_new (&clutter_source_funcs, sizeof (GSource));
  g_source_attach (source, NULL);
  g_source_unref (source);

  return TRUE;
}

static void
meta_backend_post_init (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  META_BACKEND_GET_CLASS (backend)->post_init (backend);

  meta_settings_post_init (priv->settings);
}

static gboolean
meta_backend_initable_init (GInitable     *initable,
                            GCancellable  *cancellable,
                            GError       **error)
{
  MetaBackend *backend = META_BACKEND (initable);
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  priv->settings = meta_settings_new (backend);

#ifdef HAVE_EGL
  priv->egl = g_object_new (META_TYPE_EGL, NULL);
#endif

  priv->orientation_manager = g_object_new (META_TYPE_ORIENTATION_MANAGER, NULL);

  priv->monitor_manager = meta_backend_create_monitor_manager (backend, error);
  if (!priv->monitor_manager)
    return FALSE;

  priv->renderer = meta_backend_create_renderer (backend, error);
  if (!priv->renderer)
    return FALSE;

  priv->cursor_tracker = g_object_new (META_TYPE_CURSOR_TRACKER, NULL);

  priv->dnd = g_object_new (META_TYPE_DND, NULL);

  priv->cancellable = g_cancellable_new ();
  g_bus_get (G_BUS_TYPE_SYSTEM,
             priv->cancellable,
             system_bus_gotten_cb,
             backend);

#ifdef HAVE_PROFILER
  priv->profiler = meta_profiler_new ();
#endif

  if (!init_clutter (backend, error))
    return FALSE;

  meta_backend_post_init (backend);

  return TRUE;
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_iface->init = meta_backend_initable_init;
}

static void
meta_backend_init (MetaBackend *backend)
{
  _backend = backend;
}

/**
 * meta_backend_get_idle_monitor: (skip)
 */
MetaIdleMonitor *
meta_backend_get_idle_monitor (MetaBackend        *backend,
                               ClutterInputDevice *device)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return g_hash_table_lookup (priv->device_monitors, device);
}

/**
 * meta_backend_get_monitor_manager: (skip)
 */
MetaMonitorManager *
meta_backend_get_monitor_manager (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->monitor_manager;
}

/**
 * meta_backend_get_orientation_manager: (skip)
 */
MetaOrientationManager *
meta_backend_get_orientation_manager (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->orientation_manager;
}

MetaCursorTracker *
meta_backend_get_cursor_tracker (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->cursor_tracker;
}

/**
 * meta_backend_get_cursor_renderer: (skip)
 */
MetaCursorRenderer *
meta_backend_get_cursor_renderer (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->cursor_renderer;
}

/**
 * meta_backend_get_renderer: (skip)
 */
MetaRenderer *
meta_backend_get_renderer (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->renderer;
}

#ifdef HAVE_EGL
/**
 * meta_backend_get_egl: (skip)
 */
MetaEgl *
meta_backend_get_egl (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->egl;
}
#endif /* HAVE_EGL */

/**
 * meta_backend_get_settings: (skip)
 */
MetaSettings *
meta_backend_get_settings (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->settings;
}

#ifdef HAVE_REMOTE_DESKTOP
/**
 * meta_backend_get_remote_desktop: (skip)
 */
MetaRemoteDesktop *
meta_backend_get_remote_desktop (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->remote_desktop;
}
#endif /* HAVE_REMOTE_DESKTOP */

/**
 * meta_backend_get_remote_access_controller:
 * @backend: A #MetaBackend
 *
 * Return Value: (transfer none): The #MetaRemoteAccessController
 */
MetaRemoteAccessController *
meta_backend_get_remote_access_controller (MetaBackend *backend)
{
#ifdef HAVE_REMOTE_DESKTOP
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->remote_access_controller;
#else
  return NULL;
#endif
}

/**
 * meta_backend_is_rendering_hardware_accelerated:
 * @backend: A #MetaBackend
 *
 * Returns: %TRUE if the rendering is hardware accelerated, otherwise
 * %FALSE.
 */
gboolean
meta_backend_is_rendering_hardware_accelerated (MetaBackend *backend)
{
  MetaRenderer *renderer = meta_backend_get_renderer (backend);

  return meta_renderer_is_hardware_accelerated (renderer);
}

/**
 * meta_backend_grab_device: (skip)
 */
gboolean
meta_backend_grab_device (MetaBackend *backend,
                          int          device_id,
                          uint32_t     timestamp)
{
  return META_BACKEND_GET_CLASS (backend)->grab_device (backend, device_id, timestamp);
}

/**
 * meta_backend_ungrab_device: (skip)
 */
gboolean
meta_backend_ungrab_device (MetaBackend *backend,
                            int          device_id,
                            uint32_t     timestamp)
{
  return META_BACKEND_GET_CLASS (backend)->ungrab_device (backend, device_id, timestamp);
}

/**
 * meta_backend_finish_touch_sequence: (skip)
 */
void
meta_backend_finish_touch_sequence (MetaBackend          *backend,
                                    ClutterEventSequence *sequence,
                                    MetaSequenceState     state)
{
  if (META_BACKEND_GET_CLASS (backend)->finish_touch_sequence)
    META_BACKEND_GET_CLASS (backend)->finish_touch_sequence (backend,
                                                             sequence,
                                                             state);
}

MetaLogicalMonitor *
meta_backend_get_current_logical_monitor (MetaBackend *backend)
{
  return META_BACKEND_GET_CLASS (backend)->get_current_logical_monitor (backend);
}

void
meta_backend_set_keymap (MetaBackend *backend,
                         const char  *layouts,
                         const char  *variants,
                         const char  *options)
{
  META_BACKEND_GET_CLASS (backend)->set_keymap (backend, layouts, variants, options);
}

/**
 * meta_backend_get_keymap: (skip)
 */
struct xkb_keymap *
meta_backend_get_keymap (MetaBackend *backend)

{
  return META_BACKEND_GET_CLASS (backend)->get_keymap (backend);
}

xkb_layout_index_t
meta_backend_get_keymap_layout_group (MetaBackend *backend)
{
  return META_BACKEND_GET_CLASS (backend)->get_keymap_layout_group (backend);
}

void
meta_backend_lock_layout_group (MetaBackend *backend,
                                guint idx)
{
  META_BACKEND_GET_CLASS (backend)->lock_layout_group (backend, idx);
}

void
meta_backend_set_numlock (MetaBackend *backend,
                          gboolean     numlock_state)
{
  META_BACKEND_GET_CLASS (backend)->set_numlock (backend, numlock_state);
}


/**
 * meta_backend_get_stage:
 * @backend: A #MetaBackend
 *
 * Gets the global #ClutterStage that's managed by this backend.
 *
 * Returns: (transfer none): the #ClutterStage
 */
ClutterActor *
meta_backend_get_stage (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  return priv->stage;
}

static gboolean
update_last_device (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);
  MetaCursorTracker *cursor_tracker = priv->cursor_tracker;
  ClutterInputDeviceType device_type;

  priv->device_update_idle_id = 0;
  device_type = clutter_input_device_get_device_type (priv->current_device);

  g_signal_emit (backend, signals[LAST_DEVICE_CHANGED], 0,
                 priv->current_device);

  switch (device_type)
    {
    case CLUTTER_KEYBOARD_DEVICE:
      break;
    case CLUTTER_TOUCHSCREEN_DEVICE:
      meta_cursor_tracker_set_pointer_visible (cursor_tracker, FALSE);
      break;
    default:
      meta_cursor_tracker_set_pointer_visible (cursor_tracker, TRUE);
      break;
    }

  return G_SOURCE_REMOVE;
}

void
meta_backend_update_last_device (MetaBackend        *backend,
                                 ClutterInputDevice *device)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  if (priv->current_device == device)
    return;

  if (!device ||
      clutter_input_device_get_device_mode (device) == CLUTTER_INPUT_MODE_MASTER)
    return;

  g_set_object (&priv->current_device, device);

  if (priv->device_update_idle_id == 0)
    {
      priv->device_update_idle_id =
        g_idle_add ((GSourceFunc) update_last_device, backend);
      g_source_set_name_by_id (priv->device_update_idle_id,
                               "[muffin] update_last_device");
    }
}

MetaPointerConstraint *
meta_backend_get_client_pointer_constraint (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->client_pointer_constraint;
}

/**
 * meta_backend_set_client_pointer_constraint:
 * @backend: a #MetaBackend object.
 * @constraint: (nullable): the client constraint to follow.
 *
 * Sets the current pointer constraint and removes (and unrefs) the previous
 * one. If @constrant is %NULL, this means that there is no
 * #MetaPointerConstraint active.
 */
void
meta_backend_set_client_pointer_constraint (MetaBackend           *backend,
                                            MetaPointerConstraint *constraint)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  g_assert (!constraint || !priv->client_pointer_constraint);

  g_clear_object (&priv->client_pointer_constraint);
  if (constraint)
    priv->client_pointer_constraint = g_object_ref (constraint);
}

ClutterBackend *
meta_backend_get_clutter_backend (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  if (!priv->clutter_backend)
    {
      priv->clutter_backend =
        META_BACKEND_GET_CLASS (backend)->create_clutter_backend (backend);
    }

  return priv->clutter_backend;
}

void
meta_init_backend (GType backend_gtype)
{
  MetaBackend *backend;
  GError *error = NULL;

  /* meta_backend_init() above install the backend globally so
   * so meta_get_backend() works even during initialization. */
  backend = g_object_new (backend_gtype, NULL);
  if (!g_initable_init (G_INITABLE (backend), NULL, &error))
    {
      g_warning ("Failed to create backend: %s", error->message);
      meta_exit (META_EXIT_ERROR);
    }
}

/**
 * meta_is_stage_views_enabled:
 *
 * Returns whether the #ClutterStage can be rendered using multiple stage views.
 * In practice, this means we can define a separate framebuffer for each
 * #MetaLogicalMonitor, rather than rendering everything into a single
 * framebuffer. For example: in X11, onle one single framebuffer is allowed.
 */
gboolean
meta_is_stage_views_enabled (void)
{
  if (!meta_is_wayland_compositor ())
    return FALSE;

  return !stage_views_disabled;
}

gboolean
meta_is_stage_views_scaled (void)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaLogicalMonitorLayoutMode layout_mode;

  if (!meta_is_stage_views_enabled ())
    return FALSE;

  layout_mode = monitor_manager->layout_mode;

  return layout_mode == META_LOGICAL_MONITOR_LAYOUT_MODE_LOGICAL;
}

MetaInputSettings *
meta_backend_get_input_settings (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->input_settings;
}

/**
 * meta_backend_get_dnd:
 * @backend: A #MetaDnd
 *
 * Gets the global #MetaDnd that's managed by this backend.
 *
 * Returns: (transfer none): the #MetaDnd
 */
MetaDnd *
meta_backend_get_dnd (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->dnd;
}

void
meta_backend_notify_keymap_changed (MetaBackend *backend)
{
  g_signal_emit (backend, signals[KEYMAP_CHANGED], 0);
}

void
meta_backend_notify_keymap_layout_group_changed (MetaBackend *backend,
                                                 unsigned int locked_group)
{
  g_signal_emit (backend, signals[KEYMAP_LAYOUT_GROUP_CHANGED], 0,
                 locked_group);
}

void
meta_backend_add_gpu (MetaBackend *backend,
                      MetaGpu     *gpu)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  priv->gpus = g_list_append (priv->gpus, gpu);

  g_signal_emit (backend, signals[GPU_ADDED], 0, gpu);
}

GList *
meta_backend_get_gpus (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->gpus;
}

#ifdef HAVE_LIBWACOM
WacomDeviceDatabase *
meta_backend_get_wacom_database (MetaBackend *backend)
{
  MetaBackendPrivate *priv = meta_backend_get_instance_private (backend);

  return priv->wacom_db;
}
#endif
