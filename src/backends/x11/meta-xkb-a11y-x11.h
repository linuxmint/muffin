/*
 *
 * Copyright © 2001 Ximian, Inc.
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2017 Red Hat
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

#ifndef META_XKB_A11Y_X11_H
#define META_XKB_A11Y_X11_H

#include <X11/Xlib.h>

#include "clutter/clutter.h"

void
meta_seat_x11_apply_kbd_a11y_settings (ClutterSeat            *seat,
                                       ClutterKbdA11ySettings *kbd_a11y_settings);

gboolean
meta_seat_x11_a11y_init               (ClutterSeat            *seat);

#endif /* META_XKB_A11Y_X11_H */
