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

#ifndef META_WAYLAND_H
#define META_WAYLAND_H

#include "clutter/clutter.h"
#include "core/util-private.h"
#include "meta/types.h"
#include "wayland/meta-wayland-types.h"

META_EXPORT_TEST
void                    meta_wayland_override_display_name (const char *display_name);

void                    meta_wayland_pre_clutter_init           (void);

void                    meta_wayland_init                       (void);

void                    meta_wayland_finalize                   (void);

MetaWaylandCompositor * meta_wayland_compositor_new             (MetaBackend *backend);

void                    meta_wayland_compositor_setup           (MetaWaylandCompositor *compositor);

META_EXPORT_TEST
MetaWaylandCompositor  *meta_wayland_compositor_get_default     (void);

void                    meta_wayland_compositor_update          (MetaWaylandCompositor *compositor,
                                                                 const ClutterEvent    *event);

gboolean                meta_wayland_compositor_handle_event    (MetaWaylandCompositor *compositor,
                                                                 const ClutterEvent    *event);

void                    meta_wayland_compositor_update_key_state (MetaWaylandCompositor *compositor,
                                                                 char                  *key_vector,
                                                                  int                    key_vector_len,
                                                                  int                    offset);

void                    meta_wayland_compositor_repick          (MetaWaylandCompositor *compositor);

void                    meta_wayland_compositor_set_input_focus (MetaWaylandCompositor *compositor,
                                                                 MetaWindow            *window);

void                    meta_wayland_compositor_paint_finished  (MetaWaylandCompositor *compositor);

void                    meta_wayland_compositor_add_frame_callback_surface (MetaWaylandCompositor *compositor,
                                                                            MetaWaylandSurface    *surface);

void                    meta_wayland_compositor_remove_frame_callback_surface (MetaWaylandCompositor *compositor,
                                                                               MetaWaylandSurface    *surface);

META_EXPORT_TEST
const char             *meta_wayland_get_wayland_display_name   (MetaWaylandCompositor *compositor);

META_EXPORT_TEST
const char             *meta_wayland_get_xwayland_display_name  (MetaWaylandCompositor *compositor);

void                    meta_wayland_compositor_restore_shortcuts      (MetaWaylandCompositor *compositor,
                                                                        ClutterInputDevice    *source);

gboolean                meta_wayland_compositor_is_shortcuts_inhibited (MetaWaylandCompositor *compositor,
                                                                        ClutterInputDevice    *source);

void                    meta_wayland_compositor_flush_clients (MetaWaylandCompositor *compositor);

void                    meta_wayland_compositor_schedule_surface_association (MetaWaylandCompositor *compositor,
                                                                              int                    id,
                                                                              MetaWindow            *window);

void                    meta_wayland_compositor_notify_surface_id (MetaWaylandCompositor *compositor,
                                                                   int                    id,
                                                                   MetaWaylandSurface    *surface);

#endif

