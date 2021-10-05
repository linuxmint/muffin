/*
 * Copyright (C) 2012 Intel Corporation
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
 */

#ifndef META_WAYLAND_PRIVATE_H
#define META_WAYLAND_PRIVATE_H

#include <glib.h>
#include <wayland-server.h>

#include "clutter/clutter.h"
#include "core/window-private.h"
#include "meta/meta-cursor-tracker.h"
#include "wayland/meta-wayland-pointer-gestures.h"
#include "wayland/meta-wayland-seat.h"
#include "wayland/meta-wayland-surface.h"
#include "wayland/meta-wayland-tablet-manager.h"
#include "wayland/meta-wayland-versions.h"
#include "wayland/meta-wayland.h"

typedef struct _MetaXWaylandDnd MetaXWaylandDnd;

typedef struct
{
  struct wl_list link;
  struct wl_resource *resource;
  MetaWaylandSurface *surface;
} MetaWaylandFrameCallback;

typedef struct
{
  int display_index;
  char *lock_file;
  int abstract_fd;
  int unix_fd;
  char *name;
} MetaXWaylandConnection;

typedef struct
{
  MetaXWaylandConnection private_connection;
  MetaXWaylandConnection public_connection;

  guint xserver_grace_period_id;
  struct wl_display *wayland_display;
  struct wl_client *client;
  struct wl_resource *xserver_resource;
  char *auth_file;

  GCancellable *xserver_died_cancellable;
  GSubprocess *proc;

  GList *x11_windows;

  MetaXWaylandDnd *dnd;
} MetaXWaylandManager;

struct _MetaWaylandCompositor
{
  GObject parent;

  MetaBackend *backend;

  struct wl_display *wayland_display;
  char *display_name;
  GHashTable *outputs;
  GList *frame_callback_surfaces;

  MetaXWaylandManager xwayland_manager;

  MetaWaylandSeat *seat;
  MetaWaylandTabletManager *tablet_manager;

  GHashTable *scheduled_surface_associations;
};

#define META_TYPE_WAYLAND_COMPOSITOR (meta_wayland_compositor_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandCompositor, meta_wayland_compositor,
                      META, WAYLAND_COMPOSITOR, GObject)

#endif /* META_WAYLAND_PRIVATE_H */
