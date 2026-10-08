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

#ifndef META_IDLE_MONITOR_PRIVATE_H
#define META_IDLE_MONITOR_PRIVATE_H

#include "core/display-private.h"
#include "meta/meta-idle-monitor.h"

typedef struct
{
  MetaIdleMonitor          *monitor;
  guint	                    id;
  MetaIdleMonitorWatchFunc  callback;
  gpointer		    user_data;
  GDestroyNotify            notify;
  guint64                   timeout_msec;
  int                       idle_source_id;
  GSource                  *timeout_source;
  gboolean                ignore_inhibitors;
} MetaIdleMonitorWatch;

struct _MetaIdleMonitor
{
  GObject parent_instance;

  GDBusProxy *session_proxy;
  gboolean inhibited;
  GHashTable *watches;
  ClutterInputDevice *device;
  guint64 last_event_time;
};

struct _MetaIdleMonitorClass
{
  GObjectClass parent_class;
};

void meta_idle_monitor_reset_idletime (MetaIdleMonitor *monitor);

guint meta_idle_monitor_add_idle_watch_full (MetaIdleMonitor          *monitor,
                                             guint64                   interval_msec,
                                             gboolean                  ignore_inhibitors,
                                             MetaIdleMonitorWatchFunc  callback,
                                             gpointer                  user_data,
                                             GDestroyNotify            notify);

#endif /* META_IDLE_MONITOR_PRIVATE_H */
