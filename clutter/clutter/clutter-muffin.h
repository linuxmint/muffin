/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2016 Red Hat Inc.
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
 */

#ifndef __CLUTTER_MUFFIN_H__
#define __CLUTTER_MUFFIN_H__

#define __CLUTTER_H_INSIDE__

#include "clutter-backend.h"
#include "clutter-macros.h"
#include "clutter-stage-view.h"
#include "cogl/clutter-stage-cogl.h"
#include "x11/clutter-stage-x11.h"

CLUTTER_AVAILABLE_IN_MUFFIN
void clutter_set_custom_backend_func (ClutterBackend *(* func) (void));

CLUTTER_AVAILABLE_IN_MUFFIN
gboolean        _clutter_get_sync_to_vblank     (void);

CLUTTER_AVAILABLE_IN_MUFFIN
void            _clutter_set_sync_to_vblank     (gboolean      sync_to_vblank);

CLUTTER_AVAILABLE_IN_MUFFIN
void clutter_master_clock_set_sync_method (gint state);

CLUTTER_AVAILABLE_IN_MUFFIN
void clutter_stage_x11_update_sync_state (ClutterStage *stage,
                                          gint          state);

CLUTTER_AVAILABLE_IN_MUFFIN
int64_t clutter_stage_get_frame_counter (ClutterStage *stage);

CLUTTER_AVAILABLE_IN_MUFFIN
void clutter_stage_capture_into (ClutterStage          *stage,
                                 gboolean               paint,
                                 cairo_rectangle_int_t *rect,
                                 uint8_t               *data);

#undef __CLUTTER_H_INSIDE__

#endif /* __CLUTTER_MUFFIN_H__ */
