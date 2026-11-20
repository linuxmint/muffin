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


#ifndef META_BACKEND_PRIVATE_H
#define META_BACKEND_PRIVATE_H

#include <glib-object.h>
#include <xkbcommon/xkbcommon.h>

#include "meta/meta-backend.h"
#include "meta/meta-idle-monitor.h"
#include "backends/meta-backend-types.h"
#include "backends/meta-cursor-renderer.h"
#include "backends/meta-egl.h"
#include "backends/meta-input-settings-private.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-orientation-manager.h"
#include "backends/meta-pointer-constraint.h"
#include "backends/meta-renderer.h"
#include "backends/meta-settings-private.h"
#include "core/util-private.h"

#ifdef HAVE_REMOTE_DESKTOP
#include "backends/meta-remote-desktop.h"
#endif

#define DEFAULT_XKB_RULES_FILE "evdev"
#define DEFAULT_XKB_MODEL "pc105+inet"

typedef enum
{
  META_SEQUENCE_NONE,
  META_SEQUENCE_ACCEPTED,
  META_SEQUENCE_REJECTED,
  META_SEQUENCE_PENDING_END
} MetaSequenceState;

struct _MetaBackendClass
{
  GObjectClass parent_class;

  ClutterBackend * (* create_clutter_backend) (MetaBackend *backend);

  void (* post_init) (MetaBackend *backend);

  MetaMonitorManager * (* create_monitor_manager) (MetaBackend *backend,
                                                   GError     **error);
  MetaCursorRenderer * (* create_cursor_renderer) (MetaBackend *backend);
  MetaRenderer * (* create_renderer) (MetaBackend *backend,
                                      GError     **error);
  MetaInputSettings * (* create_input_settings) (MetaBackend *backend);

  gboolean (* grab_device) (MetaBackend *backend,
                            int          device_id,
                            uint32_t     timestamp);
  gboolean (* ungrab_device) (MetaBackend *backend,
                              int          device_id,
                              uint32_t     timestamp);

  void (* finish_touch_sequence) (MetaBackend          *backend,
                                  ClutterEventSequence *sequence,
                                  MetaSequenceState     state);
  MetaLogicalMonitor * (* get_current_logical_monitor) (MetaBackend *backend);

  void (* set_keymap) (MetaBackend *backend,
                       const char  *layouts,
                       const char  *variants,
                       const char  *options);

  gboolean (* is_lid_closed) (MetaBackend *backend);

  struct xkb_keymap * (* get_keymap) (MetaBackend *backend);

  xkb_layout_index_t (* get_keymap_layout_group) (MetaBackend *backend);

  void (* lock_layout_group) (MetaBackend *backend,
                              guint        idx);

  void (* update_screen_size) (MetaBackend *backend, int width, int height);
  void (* select_stage_events) (MetaBackend *backend);

  void (* set_numlock) (MetaBackend *backend,
                        gboolean     numlock_state);

};

void meta_init_backend (GType backend_gtype);

#ifdef HAVE_WAYLAND
MetaWaylandCompositor * meta_backend_get_wayland_compositor (MetaBackend *backend);

void meta_backend_init_wayland_display (MetaBackend *backend);

void meta_backend_init_wayland (MetaBackend *backend);
#endif

ClutterBackend * meta_backend_get_clutter_backend (MetaBackend *backend);

MetaIdleMonitor * meta_backend_get_idle_monitor (MetaBackend        *backend,
                                                 ClutterInputDevice *device);
void meta_backend_foreach_device_monitor (MetaBackend *backend,
                                          GFunc        func,
                                          gpointer     user_data);

META_EXPORT_TEST
MetaMonitorManager * meta_backend_get_monitor_manager (MetaBackend *backend);
MetaOrientationManager * meta_backend_get_orientation_manager (MetaBackend *backend);
MetaCursorTracker * meta_backend_get_cursor_tracker (MetaBackend *backend);
MetaCursorRenderer * meta_backend_get_cursor_renderer (MetaBackend *backend);
META_EXPORT_TEST
MetaRenderer * meta_backend_get_renderer (MetaBackend *backend);
MetaEgl * meta_backend_get_egl (MetaBackend *backend);

#ifdef HAVE_REMOTE_DESKTOP
MetaRemoteDesktop * meta_backend_get_remote_desktop (MetaBackend *backend);
#endif

gboolean meta_backend_grab_device (MetaBackend *backend,
                                   int          device_id,
                                   uint32_t     timestamp);
gboolean meta_backend_ungrab_device (MetaBackend *backend,
                                     int          device_id,
                                     uint32_t     timestamp);

void meta_backend_finish_touch_sequence (MetaBackend          *backend,
                                         ClutterEventSequence *sequence,
                                         MetaSequenceState     state);

MetaLogicalMonitor * meta_backend_get_current_logical_monitor (MetaBackend *backend);

struct xkb_keymap * meta_backend_get_keymap (MetaBackend *backend);

xkb_layout_index_t meta_backend_get_keymap_layout_group (MetaBackend *backend);

gboolean meta_backend_is_lid_closed (MetaBackend *backend);

void meta_backend_update_last_device (MetaBackend        *backend,
                                      ClutterInputDevice *device);

MetaPointerConstraint * meta_backend_get_client_pointer_constraint (MetaBackend *backend);
void meta_backend_set_client_pointer_constraint (MetaBackend *backend,
                                                 MetaPointerConstraint *constraint);

void meta_backend_monitors_changed (MetaBackend *backend);

META_EXPORT_TEST
gboolean meta_is_stage_views_enabled (void);

gboolean meta_is_stage_views_scaled (void);

MetaInputSettings *meta_backend_get_input_settings (MetaBackend *backend);

void meta_backend_notify_keymap_changed (MetaBackend *backend);

void meta_backend_notify_keymap_layout_group_changed (MetaBackend *backend,
                                                      unsigned int locked_group);

void meta_backend_notify_ui_scaling_factor_changed (MetaBackend *backend);

META_EXPORT_TEST
void meta_backend_add_gpu (MetaBackend *backend,
                           MetaGpu     *gpu);

META_EXPORT_TEST
GList * meta_backend_get_gpus (MetaBackend *backend);

#ifdef HAVE_LIBWACOM
WacomDeviceDatabase * meta_backend_get_wacom_database (MetaBackend *backend);
#endif

#endif /* META_BACKEND_PRIVATE_H */
