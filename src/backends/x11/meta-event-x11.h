/* Copyright (C) 2006, 2007, 2008  OpenedHand Ltd
 * Copyright (C) 2009, 2010  Intel Corp.
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
 *
 *
 * Authored by:
 *      Matthew Allum <mallum@openedhand.com>
 *      Emmanuele Bassi <ebassi@linux.intel.com>
 */

#ifndef META_EVENT_X11_H
#define META_EVENT_X11_H

#include <X11/Xlib.h>

#include "clutter/x11/clutter-x11.h"

typedef struct _MetaEventX11         MetaEventX11;

struct _MetaEventX11
{
  /* additional fields for Key events */
  gint key_group;

  guint key_is_modifier : 1;
  guint num_lock_set    : 1;
  guint caps_lock_set   : 1;
};

MetaEventX11 *       meta_event_x11_new          (void);
MetaEventX11 *       meta_event_x11_copy         (MetaEventX11 *event_x11);
void                 meta_event_x11_free         (MetaEventX11 *event_x11);

Time  meta_x11_get_current_event_time (void);

gint  meta_x11_event_get_key_group (const ClutterEvent *event);

guint meta_x11_event_sequence_get_touch_detail (const ClutterEventSequence *sequence);

ClutterX11FilterReturn meta_x11_handle_event (XEvent *xevent);

#endif /* META_EVENT_X11_H */
