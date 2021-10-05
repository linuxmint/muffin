/*
 * Copyright (C) 2019 Red Hat
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
 * Author: Olivier Fourdan <ofourdan@redhat.com>
 */

#ifndef __CLUTTER_INPUT_POINTER_A11Y_H__
#define __CLUTTER_INPUT_POINTER_A11Y_H__

#include <clutter/clutter-types.h>
#include "clutter-enum-types.h"

G_BEGIN_DECLS

CLUTTER_EXPORT
void _clutter_input_pointer_a11y_add_device      (ClutterInputDevice   *device);
CLUTTER_EXPORT
void _clutter_input_pointer_a11y_remove_device   (ClutterInputDevice   *device);
CLUTTER_EXPORT
void _clutter_input_pointer_a11y_on_motion_event (ClutterInputDevice   *device,
                                                  float                 x,
                                                  float                 y);
CLUTTER_EXPORT
void _clutter_input_pointer_a11y_on_button_event (ClutterInputDevice   *device,
                                                  int                   button,
                                                  gboolean              pressed);
CLUTTER_EXPORT
gboolean _clutter_is_input_pointer_a11y_enabled  (ClutterInputDevice     *device);

G_END_DECLS

#endif /* __CLUTTER_INPUT_POINTER_A11Y_H__ */
