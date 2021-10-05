/*
 * Copyright (C) 2010  Intel Corporation.
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

 * Authors:
 *  Damien Lespiau <damien.lespiau@intel.com>
 */

#ifndef META_XKB_UTILS_H
#define META_XKB_UTILS_H

#include <xkbcommon/xkbcommon.h>

#include "clutter/clutter.h"

ClutterEvent *    meta_key_event_new_from_evdev (ClutterInputDevice *device,
                                                 ClutterInputDevice *core_keyboard,
                                                 ClutterStage       *stage,
                                                 struct xkb_state   *xkb_state,
                                                 uint32_t            button_state,
                                                 uint32_t            _time,
                                                 uint32_t            key,
                                                 uint32_t            state);
void               meta_xkb_translate_state     (ClutterEvent       *event,
                                                 struct xkb_state   *xkb_state,
                                                 uint32_t            button_state);

#endif /* META_XKB_UTILS_H */
