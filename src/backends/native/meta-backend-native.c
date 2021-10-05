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
 * SECTION:meta-backend-native
 * @title: MetaBackendNative
 * @short_description: A native (KMS/evdev) MetaBackend
 *
 * MetaBackendNative is an implementation of #MetaBackend that uses "native"
 * technologies like DRM/KMS and libinput/evdev to perform the necessary
 * functions.
 */

#include "config.h"

#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-backend-native-private.h"

#include <sched.h>
#include <stdlib.h>

#include "backends/meta-cursor-tracker-private.h"
#include "backends/meta-idle-monitor-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-pointer-constraint.h"
#include "backends/meta-settings-private.h"
#include "backends/meta-stage-private.h"
#include "backends/native/meta-barrier-native.h"
#include "backends/native/meta-clutter-backend-native.h"
#include "backends/native/meta-cursor-renderer-native.h"
#include "backends/native/meta-event-native.h"
#include "backends/native/meta-input-settings-native.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-launcher.h"
#include "backends/native/meta-monitor-manager-kms.h"
#include "backends/native/meta-renderer-native.h"
#include "backends/native/meta-seat-native.h"
#include "backends/native/meta-stage-native.h"
#include "cogl/cogl.h"
#include "core/meta-border.h"
#include "meta/main.h"

struct _MetaBackendNative
{
  MetaBackend parent;

  MetaLauncher *launcher;
  MetaUdev *udev;
  MetaKms *kms;
  MetaBarrierManagerNative *barrier_manager;

  gulong udev_device_added_handler_id;
};

static GInitableIface *initable_parent_iface;

static void
initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (MetaBackendNative, meta_backend_native, META_TYPE_BACKEND,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_iface_init))

static void
disconnect_udev_device_added_handler (MetaBackendNative *native);

static void
meta_backend_native_finalize (GObject *object)
{
  MetaBackendNative *native = META_BACKEND_NATIVE (object);

  if (native->udev_device_added_handler_id)
    disconnect_udev_device_added_handler (native);

  g_clear_object (&native->udev);
  g_clear_object (&native->kms);
  meta_launcher_free (native->launcher);

  G_OBJECT_CLASS (meta_backend_native_parent_class)->finalize (object);
}

static void
constrain_to_barriers (ClutterInputDevice *device,
                       guint32             time,
                       float              *new_x,
                       float              *new_y)
{
  MetaBackendNative *native = META_BACKEND_NATIVE (meta_get_backend ());

  meta_barrier_manager_native_process (native->barrier_manager,
                                       device,
                                       time,
                                       new_x, new_y);
}

static void
constrain_to_client_constraint (ClutterInputDevice *device,
                                guint32             time,
                                float               prev_x,
                                float               prev_y,
                                float              *x,
                                float              *y)
{
  MetaBackend *backend = meta_get_backend ();
  MetaPointerConstraint *constraint =
    meta_backend_get_client_pointer_constraint (backend);

  if (!constraint)
    return;

  meta_pointer_constraint_constrain (constraint, device,
                                     time, prev_x, prev_y, x, y);
}

/*
 * The pointer constrain code is mostly a rip-off of the XRandR code from Xorg.
 * (from xserver/randr/rrcrtc.c, RRConstrainCursorHarder)
 *
 * Copyright © 2006 Keith Packard
 * Copyright 2010 Red Hat, Inc
 *
 */

static void
constrain_all_screen_monitors (ClutterInputDevice *device,
                               MetaMonitorManager *monitor_manager,
                               float              *x,
                               float              *y)
{
  graphene_point_t current;
  float cx, cy;
  GList *logical_monitors, *l;

  clutter_input_device_get_coords (device, NULL, &current);

  cx = current.x;
  cy = current.y;

  /* if we're trying to escape, clamp to the CRTC we're coming from */

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      int left, right, top, bottom;

      left = logical_monitor->rect.x;
      right = left + logical_monitor->rect.width;
      top = logical_monitor->rect.y;
      bottom = top + logical_monitor->rect.height;

      if ((cx >= left) && (cx < right) && (cy >= top) && (cy < bottom))
        {
          if (*x < left)
            *x = left;
          if (*x >= right)
            *x = right - 1;
          if (*y < top)
            *y = top;
          if (*y >= bottom)
            *y = bottom - 1;

          return;
        }
    }
}

static void
pointer_constrain_callback (ClutterInputDevice *device,
                            guint32             time,
                            float               prev_x,
                            float               prev_y,
                            float              *new_x,
                            float              *new_y,
                            gpointer            user_data)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  /* Constrain to barriers */
  constrain_to_barriers (device, time, new_x, new_y);

  /* Constrain to pointer lock */
  constrain_to_client_constraint (device, time, prev_x, prev_y, new_x, new_y);

  /* if we're moving inside a monitor, we're fine */
  if (meta_monitor_manager_get_logical_monitor_at (monitor_manager,
                                                   *new_x, *new_y))
    return;

  /* if we're trying to escape, clamp to the CRTC we're coming from */
  constrain_all_screen_monitors (device, monitor_manager, new_x, new_y);
}

static void
relative_motion_across_outputs (MetaMonitorManager *monitor_manager,
                                MetaLogicalMonitor *current,
                                float               cur_x,
                                float               cur_y,
                                float              *dx_inout,
                                float              *dy_inout)
{
  MetaLogicalMonitor *cur = current;
  float x = cur_x, y = cur_y;
  float target_x = cur_x, target_y = cur_y;
  float dx = *dx_inout, dy = *dy_inout;
  MetaDisplayDirection direction = -1;

  while (cur)
    {
      MetaLine2 left, right, top, bottom, motion;
      MetaVector2 intersection;

      motion = (MetaLine2) {
          .a = { x, y },
            .b = { x + (dx * cur->scale), y + (dy * cur->scale) }
      };
      left = (MetaLine2) {
            { cur->rect.x, cur->rect.y },
              { cur->rect.x, cur->rect.y + cur->rect.height }
      };
      right = (MetaLine2) {
            { cur->rect.x + cur->rect.width, cur->rect.y },
              { cur->rect.x + cur->rect.width, cur->rect.y + cur->rect.height }
      };
      top = (MetaLine2) {
            { cur->rect.x, cur->rect.y },
              { cur->rect.x + cur->rect.width, cur->rect.y }
      };
      bottom = (MetaLine2) {
            { cur->rect.x, cur->rect.y + cur->rect.height },
              { cur->rect.x + cur->rect.width, cur->rect.y + cur->rect.height }
      };

      target_x = motion.b.x;
      target_y = motion.b.y;

      if (direction != META_DISPLAY_RIGHT &&
          meta_line2_intersects_with (&motion, &left, &intersection))
        direction = META_DISPLAY_LEFT;
      else if (direction != META_DISPLAY_LEFT &&
               meta_line2_intersects_with (&motion, &right, &intersection))
        direction = META_DISPLAY_RIGHT;
      else if (direction != META_DISPLAY_DOWN &&
               meta_line2_intersects_with (&motion, &top, &intersection))
        direction = META_DISPLAY_UP;
      else if (direction != META_DISPLAY_UP &&
               meta_line2_intersects_with (&motion, &bottom, &intersection))
        direction = META_DISPLAY_DOWN;
      else
        /* We reached the dest logical monitor */
        break;

      x = intersection.x;
      y = intersection.y;
      dx -= intersection.x - motion.a.x;
      dy -= intersection.y - motion.a.y;

      cur = meta_monitor_manager_get_logical_monitor_neighbor (monitor_manager,
                                                               cur, direction);
    }

  *dx_inout = target_x - cur_x;
  *dy_inout = target_y - cur_y;
}

static void
relative_motion_filter (ClutterInputDevice *device,
                        float               x,
                        float               y,
                        float              *dx,
                        float              *dy,
                        gpointer            user_data)
{
  MetaMonitorManager *monitor_manager = user_data;
  MetaLogicalMonitor *logical_monitor, *dest_logical_monitor;
  float new_dx, new_dy;

  if (meta_is_stage_views_scaled ())
    return;

  logical_monitor = meta_monitor_manager_get_logical_monitor_at (monitor_manager,
                                                                 x, y);
  if (!logical_monitor)
    return;

  new_dx = (*dx) * logical_monitor->scale;
  new_dy = (*dy) * logical_monitor->scale;

  dest_logical_monitor = meta_monitor_manager_get_logical_monitor_at (monitor_manager,
                                                                      x + new_dx,
                                                                      y + new_dy);
  if (dest_logical_monitor &&
      dest_logical_monitor != logical_monitor)
    {
      /* If we are crossing monitors, attempt to bisect the distance on each
       * axis and apply the relative scale for each of them.
       */
      new_dx = *dx;
      new_dy = *dy;
      relative_motion_across_outputs (monitor_manager, logical_monitor,
                                      x, y, &new_dx, &new_dy);
    }

  *dx = new_dx;
  *dy = new_dy;
}

static ClutterBackend *
meta_backend_native_create_clutter_backend (MetaBackend *backend)
{
  return g_object_new (META_TYPE_CLUTTER_BACKEND_NATIVE, NULL);
}

static void
meta_backend_native_post_init (MetaBackend *backend)
{
  ClutterBackend *clutter_backend = clutter_get_default_backend ();
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
  MetaSettings *settings = meta_backend_get_settings (backend);

  meta_seat_native_set_pointer_constrain_callback (META_SEAT_NATIVE (seat),
                                                   pointer_constrain_callback,
                                                   NULL, NULL);
  meta_seat_native_set_relative_motion_filter (META_SEAT_NATIVE (seat),
                                               relative_motion_filter,
                                               meta_backend_get_monitor_manager (backend));

  META_BACKEND_CLASS (meta_backend_native_parent_class)->post_init (backend);

  if (meta_settings_is_experimental_feature_enabled (settings,
                                                     META_EXPERIMENTAL_FEATURE_RT_SCHEDULER))
    {
      int retval;
      struct sched_param sp = {
        .sched_priority = sched_get_priority_min (SCHED_RR)
      };

      retval = sched_setscheduler (0, SCHED_RR | SCHED_RESET_ON_FORK, &sp);

      if (retval != 0)
        g_warning ("Failed to set RT scheduler: %m");
    }

#ifdef HAVE_WAYLAND
  meta_backend_init_wayland (backend);
#endif
}

static MetaMonitorManager *
meta_backend_native_create_monitor_manager (MetaBackend *backend,
                                            GError     **error)
{
  return g_initable_new (META_TYPE_MONITOR_MANAGER_KMS, NULL, error,
                         "backend", backend,
                         NULL);
}

static MetaCursorRenderer *
meta_backend_native_create_cursor_renderer (MetaBackend *backend)
{
  return META_CURSOR_RENDERER (meta_cursor_renderer_native_new (backend));
}

static MetaRenderer *
meta_backend_native_create_renderer (MetaBackend *backend,
                                     GError     **error)
{
  MetaBackendNative *native = META_BACKEND_NATIVE (backend);
  MetaRendererNative *renderer_native;

  renderer_native = meta_renderer_native_new (native, error);
  if (!renderer_native)
    return NULL;

  return META_RENDERER (renderer_native);
}

static MetaInputSettings *
meta_backend_native_create_input_settings (MetaBackend *backend)
{
  return g_object_new (META_TYPE_INPUT_SETTINGS_NATIVE, NULL);
}

static MetaLogicalMonitor *
meta_backend_native_get_current_logical_monitor (MetaBackend *backend)
{
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  int x, y;

  meta_cursor_tracker_get_pointer (cursor_tracker, &x, &y, NULL);
  return meta_monitor_manager_get_logical_monitor_at (monitor_manager, x, y);
}

static void
meta_backend_native_set_keymap (MetaBackend *backend,
                                const char  *layouts,
                                const char  *variants,
                                const char  *options)
{
  struct xkb_rule_names names;
  struct xkb_keymap *keymap;
  struct xkb_context *context;
  ClutterSeat *seat;

  names.rules = DEFAULT_XKB_RULES_FILE;
  names.model = DEFAULT_XKB_MODEL;
  names.layout = layouts;
  names.variant = variants;
  names.options = options;

  context = xkb_context_new (XKB_CONTEXT_NO_FLAGS);
  keymap = xkb_keymap_new_from_names (context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
  xkb_context_unref (context);

  seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  meta_seat_native_set_keyboard_map (META_SEAT_NATIVE (seat), keymap);

  meta_backend_notify_keymap_changed (backend);

  xkb_keymap_unref (keymap);
}

static struct xkb_keymap *
meta_backend_native_get_keymap (MetaBackend *backend)
{
  ClutterSeat *seat;

  seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  return meta_seat_native_get_keyboard_map (META_SEAT_NATIVE (seat));
}

static xkb_layout_index_t
meta_backend_native_get_keymap_layout_group (MetaBackend *backend)
{
  ClutterSeat *seat;

  seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  return meta_seat_native_get_keyboard_layout_index (META_SEAT_NATIVE (seat));
}

static void
meta_backend_native_lock_layout_group (MetaBackend *backend,
                                       guint        idx)
{
  xkb_layout_index_t old_idx;
  ClutterSeat *seat;

  old_idx = meta_backend_native_get_keymap_layout_group (backend);
  if (old_idx == idx)
    return;

  seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  meta_seat_native_set_keyboard_layout_index (META_SEAT_NATIVE (seat), idx);
  meta_backend_notify_keymap_layout_group_changed (backend, idx);
}

static void
meta_backend_native_set_numlock (MetaBackend *backend,
                                 gboolean     numlock_state)
{
  ClutterSeat *seat;

  seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  meta_seat_native_set_keyboard_numlock (META_SEAT_NATIVE (seat),
                                         numlock_state);
}

static void
meta_backend_native_update_screen_size (MetaBackend *backend,
                                        int width, int height)
{
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  MetaStageNative *stage_native;
  ClutterActor *stage = meta_backend_get_stage (backend);

  stage_native = meta_clutter_backend_native_get_stage_native (clutter_backend);
  meta_stage_native_rebuild_views (stage_native);

  clutter_actor_set_size (stage, width, height);
}

static MetaGpuKms *
create_gpu_from_udev_device (MetaBackendNative  *native,
                             GUdevDevice        *device,
                             GError            **error)
{
  MetaKmsDeviceFlag flags = META_KMS_DEVICE_FLAG_NONE;
  const char *device_path;
  MetaKmsDevice *kms_device;

  if (meta_is_udev_device_platform_device (device))
    flags |= META_KMS_DEVICE_FLAG_PLATFORM_DEVICE;

  if (meta_is_udev_device_boot_vga (device))
    flags |= META_KMS_DEVICE_FLAG_BOOT_VGA;

  device_path = g_udev_device_get_device_file (device);

  kms_device = meta_kms_create_device (native->kms, device_path, flags,
                                       error);
  if (!kms_device)
    return NULL;

  return meta_gpu_kms_new (native, kms_device, error);
}

static void
on_udev_device_added (MetaUdev          *udev,
                      GUdevDevice       *device,
                      MetaBackendNative *native)
{
  MetaBackend *backend = META_BACKEND (native);
  g_autoptr (GError) error = NULL;
  const char *device_path;
  MetaGpuKms *new_gpu_kms;
  GList *gpus, *l;

  if (!meta_udev_is_drm_device (udev, device))
    return;

  device_path = g_udev_device_get_device_file (device);

  gpus = meta_backend_get_gpus (backend);;
  for (l = gpus; l; l = l->next)
    {
      MetaGpuKms *gpu_kms = l->data;

      if (!g_strcmp0 (device_path, meta_gpu_kms_get_file_path (gpu_kms)))
        {
          g_warning ("Failed to hotplug secondary gpu '%s': %s",
                     device_path, "device already present");
          return;
        }
    }

  new_gpu_kms = create_gpu_from_udev_device (native, device, &error);
  if (!new_gpu_kms)
    {
      g_warning ("Failed to hotplug secondary gpu '%s': %s",
                 device_path, error->message);
      return;
    }

  meta_backend_add_gpu (backend, META_GPU (new_gpu_kms));
}

static void
connect_udev_device_added_handler (MetaBackendNative *native)
{
  native->udev_device_added_handler_id =
    g_signal_connect (native->udev, "device-added",
                      G_CALLBACK (on_udev_device_added), native);
}

static void
disconnect_udev_device_added_handler (MetaBackendNative *native)
{
  g_clear_signal_handler (&native->udev_device_added_handler_id, native->udev);
}

static gboolean
init_gpus (MetaBackendNative  *native,
           GError            **error)
{
  MetaBackend *backend = META_BACKEND (native);
  MetaUdev *udev = meta_backend_native_get_udev (native);
  GList *devices;
  GList *l;

  devices = meta_udev_list_drm_devices (udev, error);
  if (!devices)
    return FALSE;

  for (l = devices; l; l = l->next)
    {
      GUdevDevice *device = l->data;
      MetaGpuKms *gpu_kms;
      GError *local_error = NULL;

      gpu_kms = create_gpu_from_udev_device (native, device, &local_error);

      if (!gpu_kms)
        {
          g_warning ("Failed to open gpu '%s': %s",
                     g_udev_device_get_device_file (device),
                     local_error->message);
          g_clear_error (&local_error);
          continue;
        }

      meta_backend_add_gpu (backend, META_GPU (gpu_kms));
    }

  g_list_free_full (devices, g_object_unref);

  if (g_list_length (meta_backend_get_gpus (backend)) == 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "No GPUs found");
      return FALSE;
    }

  connect_udev_device_added_handler (native);

  return TRUE;
}

static gboolean
meta_backend_native_initable_init (GInitable     *initable,
                                   GCancellable  *cancellable,
                                   GError       **error)
{
  MetaBackendNative *native = META_BACKEND_NATIVE (initable);

  if (!meta_is_stage_views_enabled ())
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "The native backend requires stage views");
      return FALSE;
    }

  native->launcher = meta_launcher_new (error);
  if (!native->launcher)
    return FALSE;

#ifdef HAVE_WAYLAND
  meta_backend_init_wayland_display (META_BACKEND (native));
#endif

  native->udev = meta_udev_new (native);
  native->barrier_manager = meta_barrier_manager_native_new ();

  native->kms = meta_kms_new (META_BACKEND (native), error);
  if (!native->kms)
    return FALSE;

  if (!init_gpus (native, error))
    return FALSE;

  return initable_parent_iface->init (initable, cancellable, error);
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_parent_iface = g_type_interface_peek_parent (initable_iface);

  initable_iface->init = meta_backend_native_initable_init;
}

static void
meta_backend_native_class_init (MetaBackendNativeClass *klass)
{
  MetaBackendClass *backend_class = META_BACKEND_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_backend_native_finalize;

  backend_class->create_clutter_backend = meta_backend_native_create_clutter_backend;

  backend_class->post_init = meta_backend_native_post_init;

  backend_class->create_monitor_manager = meta_backend_native_create_monitor_manager;
  backend_class->create_cursor_renderer = meta_backend_native_create_cursor_renderer;
  backend_class->create_renderer = meta_backend_native_create_renderer;
  backend_class->create_input_settings = meta_backend_native_create_input_settings;

  backend_class->get_current_logical_monitor = meta_backend_native_get_current_logical_monitor;

  backend_class->set_keymap = meta_backend_native_set_keymap;
  backend_class->get_keymap = meta_backend_native_get_keymap;
  backend_class->get_keymap_layout_group = meta_backend_native_get_keymap_layout_group;
  backend_class->lock_layout_group = meta_backend_native_lock_layout_group;
  backend_class->update_screen_size = meta_backend_native_update_screen_size;
  backend_class->set_numlock = meta_backend_native_set_numlock;
}

static void
meta_backend_native_init (MetaBackendNative *native)
{
}

MetaLauncher *
meta_backend_native_get_launcher (MetaBackendNative *native)
{
  return native->launcher;
}

MetaUdev *
meta_backend_native_get_udev (MetaBackendNative *native)
{
  return native->udev;
}

MetaKms *
meta_backend_native_get_kms (MetaBackendNative *native)
{
  return native->kms;
}

gboolean
meta_activate_vt (int vt, GError **error)
{
  MetaBackend *backend = meta_get_backend ();
  MetaBackendNative *native = META_BACKEND_NATIVE (backend);
  MetaLauncher *launcher = meta_backend_native_get_launcher (native);

  return meta_launcher_activate_vt (launcher, vt, error);
}

MetaBarrierManagerNative *
meta_backend_native_get_barrier_manager (MetaBackendNative *native)
{
  return native->barrier_manager;
}

/**
 * meta_activate_session:
 *
 * Tells mutter to activate the session. When mutter is a
 * display server, this tells logind to switch over to
 * the new session.
 */
gboolean
meta_activate_session (void)
{
  GError *error = NULL;
  MetaBackend *backend = meta_get_backend ();

  /* Do nothing. */
  if (!META_IS_BACKEND_NATIVE (backend))
    return TRUE;

  MetaBackendNative *native = META_BACKEND_NATIVE (backend);

  if (!meta_launcher_activate_session (native->launcher, &error))
    {
      g_warning ("Could not activate session: %s\n", error->message);
      g_error_free (error);
      return FALSE;
    }

  return TRUE;
}

void
meta_backend_native_pause (MetaBackendNative *native)
{
  MetaBackend *backend = META_BACKEND (native);
  ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerKms *monitor_manager_kms =
    META_MONITOR_MANAGER_KMS (monitor_manager);
  ClutterBackend *clutter_backend = clutter_get_default_backend ();
  MetaSeatNative *seat =
    META_SEAT_NATIVE (clutter_backend_get_default_seat (clutter_backend));

  COGL_TRACE_BEGIN_SCOPED (MetaBackendNativePause,
                           "Backend (pause)");

  meta_seat_native_release_devices (seat);
  clutter_stage_freeze_updates (stage);

  disconnect_udev_device_added_handler (native);

  meta_monitor_manager_kms_pause (monitor_manager_kms);
}

void meta_backend_native_resume (MetaBackendNative *native)
{
  MetaBackend *backend = META_BACKEND (native);
  ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerKms *monitor_manager_kms =
    META_MONITOR_MANAGER_KMS (monitor_manager);
  MetaInputSettings *input_settings;
  MetaIdleMonitor *idle_monitor;
  ClutterBackend *clutter_backend = clutter_get_default_backend ();
  MetaSeatNative *seat =
    META_SEAT_NATIVE (clutter_backend_get_default_seat (clutter_backend));

  COGL_TRACE_BEGIN_SCOPED (MetaBackendNativeResume,
                           "Backend (resume)");

  meta_monitor_manager_kms_resume (monitor_manager_kms);

  connect_udev_device_added_handler (native);

  meta_seat_native_reclaim_devices (seat);
  clutter_stage_thaw_updates (stage);

  clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));

  idle_monitor = meta_idle_monitor_get_core ();
  meta_idle_monitor_reset_idletime (idle_monitor);

  input_settings = meta_backend_get_input_settings (backend);
  meta_input_settings_maybe_restore_numlock_state (input_settings);

  clutter_seat_ensure_a11y_state (CLUTTER_SEAT (seat));
}
