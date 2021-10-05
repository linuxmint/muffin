/*
 * Copyright (C) 2013 Intel Corporation
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

#ifndef META_XWAYLAND_PRIVATE_H
#define META_XWAYLAND_PRIVATE_H

#include <glib.h>

#include "wayland/meta-wayland-private.h"

gboolean
meta_xwayland_init (MetaXWaylandManager *manager,
		    struct wl_display   *display);

void
meta_xwayland_complete_init (MetaDisplay *display,
                             Display     *xdisplay);

void
meta_xwayland_shutdown (MetaXWaylandManager *manager);

/* wl_data_device/X11 selection interoperation */
void     meta_xwayland_init_dnd         (Display *xdisplay);
void     meta_xwayland_shutdown_dnd     (Display *xdisplay);
gboolean meta_xwayland_dnd_handle_event (XEvent *xevent);

const MetaWaylandDragDestFuncs * meta_xwayland_selection_get_drag_dest_funcs (void);

void meta_xwayland_start_xserver (MetaXWaylandManager *manager,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data);
gboolean meta_xwayland_start_xserver_finish (MetaXWaylandManager  *manager,
                                             GAsyncResult         *result,
                                             GError              **error);

#endif /* META_XWAYLAND_PRIVATE_H */
