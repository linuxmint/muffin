/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2002 Red Hat, Inc.
 * Copyright (C) 2003, 2004 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
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

#ifndef META_WINDOW_X11_H
#define META_WINDOW_X11_H

#include <X11/Xlib.h>

#include "meta/compositor.h"
#include "meta/window.h"

G_BEGIN_DECLS

#define META_TYPE_WINDOW_X11            (meta_window_x11_get_type())
#define META_WINDOW_X11(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_WINDOW_X11, MetaWindowX11))
#define META_WINDOW_X11_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_WINDOW_X11, MetaWindowX11Class))
#define META_IS_WINDOW_X11(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_WINDOW_X11))
#define META_IS_WINDOW_X11_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_WINDOW_X11))
#define META_WINDOW_X11_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_WINDOW_X11, MetaWindowX11Class))

GType meta_window_x11_get_type (void);

typedef struct _MetaWindowX11      MetaWindowX11;
typedef struct _MetaWindowX11Class MetaWindowX11Class;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaWindowX11, g_object_unref)

MetaWindow * meta_window_x11_new           (MetaDisplay        *display,
                                            Window              xwindow,
                                            gboolean            must_be_viewable,
                                            MetaCompEffect      effect);

void meta_window_x11_set_net_wm_state            (MetaWindow *window);
void meta_window_x11_set_wm_state                (MetaWindow *window);
void meta_window_x11_set_wm_take_focus           (MetaWindow *window,
                                                  gboolean    take_focus);
void meta_window_x11_set_wm_ping                 (MetaWindow *window,
                                                  gboolean    ping);
void meta_window_x11_set_wm_delete_window        (MetaWindow *window,
                                                  gboolean    delete_window);
void meta_window_x11_set_allowed_actions_hint    (MetaWindow *window);

void meta_window_x11_create_sync_request_alarm   (MetaWindow *window);
void meta_window_x11_destroy_sync_request_alarm  (MetaWindow *window);
void meta_window_x11_update_sync_request_counter (MetaWindow *window,
                                                  gint64      new_counter_value);

void meta_window_x11_update_input_region         (MetaWindow *window);
void meta_window_x11_update_shape_region         (MetaWindow *window);

void meta_window_x11_recalc_window_type          (MetaWindow *window);

gboolean meta_window_x11_configure_request       (MetaWindow *window,
                                                  XEvent     *event);
gboolean meta_window_x11_property_notify         (MetaWindow *window,
                                                  XEvent     *event);
gboolean meta_window_x11_client_message          (MetaWindow *window,
                                                  XEvent     *event);

void     meta_window_x11_configure_notify        (MetaWindow      *window,
                                                  XConfigureEvent *event);

Window   meta_window_x11_get_toplevel_xwindow    (MetaWindow *window);

void     meta_window_x11_freeze_commits          (MetaWindow *window);
void     meta_window_x11_thaw_commits            (MetaWindow *window);

void     meta_window_x11_set_thaw_after_paint    (MetaWindow *window,
                                                  gboolean    thaw_after_paint);
gboolean meta_window_x11_should_thaw_after_paint (MetaWindow *window);
gboolean meta_window_x11_always_update_shape     (MetaWindow *window);

void meta_window_x11_surface_rect_to_frame_rect  (MetaWindow    *window,
                                                  MetaRectangle *surface_rect,
                                                  MetaRectangle *frame_rect);
void meta_window_x11_surface_rect_to_client_rect (MetaWindow    *window,
                                                  MetaRectangle *surface_rect,
                                                  MetaRectangle *client_rect);
#endif
