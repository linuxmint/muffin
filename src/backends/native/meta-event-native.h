/*
 * Copyright (C) 2015 Red Hat
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
 * Authored by:
 *      Carlos Garnacho <carlosg@gnome.org>
 */

#ifndef META_EVENT_NATIVE_H
#define META_EVENT_NATIVE_H

#include "clutter/clutter.h"

typedef struct _MetaEventNative MetaEventNative;

MetaEventNative * meta_event_native_copy (MetaEventNative *event_evdev);
void              meta_event_native_free (MetaEventNative *event_evdev);

uint32_t          meta_event_native_get_event_code (const ClutterEvent *event);
void              meta_event_native_set_event_code (ClutterEvent *event,
                                                    uint32_t      evcode);
uint64_t          meta_event_native_get_time_usec  (const ClutterEvent *event);
void              meta_event_native_set_time_usec  (ClutterEvent *event,
                                                    uint64_t      time_usec);
void              meta_event_native_set_relative_motion (ClutterEvent *event,
                                                         double        dx,
                                                         double        dy,
                                                         double        dx_unaccel,
                                                         double        dy_unaccel);
gboolean          meta_event_native_get_relative_motion (const ClutterEvent *event,
                                                         double             *dx,
                                                         double             *dy,
                                                         double             *dx_unaccel,
                                                         double             *dy_unaccel);

int32_t           meta_event_native_sequence_get_slot (const ClutterEventSequence *sequence);

#endif /* META_EVENT_NATIVE_H */
