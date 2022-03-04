/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2008 Iain Holmes
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

#ifndef META_X11_DISPLAY_H
#define META_X11_DISPLAY_H

#include <glib-object.h>
#include <X11/Xlib.h>

#include <meta/common.h>
#include <meta/prefs.h>
#include <meta/types.h>

#define META_TYPE_X11_DISPLAY (meta_x11_display_get_type ())

META_EXPORT
G_DECLARE_FINAL_TYPE (MetaX11Display, meta_x11_display, META, X11_DISPLAY, GObject)

META_EXPORT
gboolean meta_x11_init_gdk_display (GError **error);

META_EXPORT
int      meta_x11_display_get_screen_number (MetaX11Display *x11_display);

META_EXPORT
Display *meta_x11_display_get_xdisplay      (MetaX11Display *x11_display);

META_EXPORT
Window   meta_x11_display_get_xroot         (MetaX11Display *x11_display);

META_EXPORT
int      meta_x11_display_get_xinput_opcode     (MetaX11Display *x11_display);

META_EXPORT
int      meta_x11_display_get_damage_event_base (MetaX11Display *x11_display);

META_EXPORT
int      meta_x11_display_get_shape_event_base  (MetaX11Display *x11_display);

META_EXPORT
gboolean meta_x11_display_has_shape             (MetaX11Display *x11_display);

META_EXPORT
void meta_x11_display_set_cm_selection (MetaX11Display *x11_display);

META_EXPORT
gboolean meta_x11_display_xwindow_is_a_no_focus_window (MetaX11Display *x11_display,
                                                        Window xwindow);

META_EXPORT
void     meta_x11_display_set_stage_input_region (MetaX11Display *x11_display,
                                                  XserverRegion   region);

META_EXPORT
void     meta_x11_display_clear_stage_input_region (MetaX11Display *x11_display);

#endif /* META_X11_DISPLAY_H */
