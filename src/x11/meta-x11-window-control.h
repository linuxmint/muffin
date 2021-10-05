/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter interface used by GTK+ UI to talk to core */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2005 Elijah Newren
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

#ifndef META_X11_WINDOW__CONTROL_H
#define META_X11_WINDOW__CONTROL_H

#include <gdk/gdkx.h>

#include "meta/boxes.h"
#include "meta/common.h"
#include "x11/meta-x11-display-private.h"

void meta_x11_wm_queue_frame_resize (MetaX11Display *x11_display,
                                     Window          frame_xwindow);

void meta_x11_wm_user_lower_and_unfocus (MetaX11Display *x11_display,
                                         Window          frame_xwindow,
                                         uint32_t        timestamp);

void meta_x11_wm_toggle_maximize (MetaX11Display *x11_display,
                                  Window          frame_xwindow);
void meta_x11_wm_toggle_maximize_horizontally (MetaX11Display *xdisplay,
                                               Window          frame_xwindow);
void meta_x11_wm_toggle_maximize_vertically (MetaX11Display *x11_display,
                                             Window          frame_xwindow);

void meta_x11_wm_show_window_menu (MetaX11Display     *x11_xdisplay,
                                   Window              frame_xwindow,
                                   MetaWindowMenuType  menu,
                                   int                 root_x,
                                   int                 root_y,
                                   uint32_t            timestamp);

void meta_x11_wm_show_window_menu_for_rect (MetaX11Display     *x11_display,
                                            Window              frame_xwindow,
                                            MetaWindowMenuType  menu,
                                            MetaRectangle      *rect,
                                            uint32_t            timestamp);

gboolean meta_x11_wm_begin_grab_op (MetaX11Display *x11_display,
                                    Window          frame_xwindow,
                                    MetaGrabOp      op,
                                    gboolean        pointer_already_grabbed,
                                    gboolean        frame_action,
                                    int             button,
                                    gulong          modmask,
                                    uint32_t        timestamp,
                                    int             root_x,
                                    int             root_y);
void meta_x11_wm_end_grab_op (MetaX11Display *x11_display,
                              uint32_t        timestamp);
MetaGrabOp meta_x11_wm_get_grab_op (MetaX11Display *x11_display);


void meta_x11_wm_grab_buttons  (MetaX11Display *x11_display,
                                Window          frame_xwindow);

void meta_x11_wm_set_screen_cursor (MetaX11Display *x11_display,
                                    Window          frame_on_screen,
                                    MetaCursor      cursor);

#endif /* META_X11_WINDOW_CONTROL_H */
