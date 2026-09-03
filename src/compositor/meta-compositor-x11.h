/*
 * Copyright (C) 2019 Red Hat Inc.
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
 */

#ifndef META_COMPOSITOR_X11_H
#define META_COMPOSITOR_X11_H

#include "compositor/compositor-private.h"

#define META_TYPE_COMPOSITOR_X11 (meta_compositor_x11_get_type ())
G_DECLARE_FINAL_TYPE (MetaCompositorX11, meta_compositor_x11,
                      META, COMPOSITOR_X11, MetaCompositor)

MetaCompositorX11 * meta_compositor_x11_new (MetaDisplay *display,
                                             MetaBackend *backend);

void meta_compositor_x11_process_xevent (MetaCompositorX11 *compositor_x11,
                                         XEvent            *xevent,
                                         MetaWindow        *window);

Window meta_compositor_x11_get_output_xwindow (MetaCompositorX11 *compositor_x11);

#endif /* META_COMPOSITOR_X11_H */
