/*
 * Copyright (C) 2019 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */
#ifndef META_SEAT_X11_H
#define META_SEAT_X11_H

#include "clutter/clutter.h"

G_BEGIN_DECLS

#define META_TYPE_SEAT_X11 meta_seat_x11_get_type ()
G_DECLARE_FINAL_TYPE (MetaSeatX11, meta_seat_x11, META, SEAT_X11, ClutterSeat)

MetaSeatX11 * meta_seat_x11_new (int opcode,
                                 int master_pointer,
				 int master_keyboard);
gboolean meta_seat_x11_translate_event (MetaSeatX11  *seat,
					XEvent       *xevent,
					ClutterEvent *event);
ClutterInputDevice * meta_seat_x11_lookup_device_id (MetaSeatX11 *seat_x11,
                                                     int          device_id);
void meta_seat_x11_select_stage_events (MetaSeatX11  *seat,
                                        ClutterStage *stage);
void meta_seat_x11_notify_devices (MetaSeatX11  *seat_x11,
                                   ClutterStage *stage);

G_END_DECLS

#endif /* META_SEAT_X11_H */
