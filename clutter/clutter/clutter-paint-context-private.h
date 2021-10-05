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
 */

#ifndef CLUTTER_PAINT_CONTEXT_PRIVATE_H
#define CLUTTER_PAINT_CONTEXT_PRIVATE_H

#include "clutter-paint-context.h"

typedef enum _ClutterPaintFlag
{
  CLUTTER_PAINT_FLAG_NONE = 0,
  CLUTTER_PAINT_FLAG_NO_CURSORS = 1 << 0,
  CLUTTER_PAINT_FLAG_NO_PAINT_SIGNAL = 1 << 1,
} ClutterPaintFlag;

ClutterPaintContext * clutter_paint_context_new_for_view (ClutterStageView     *view,
                                                          const cairo_region_t *redraw_clip,
                                                          ClutterPaintFlag      paint_flags);

gboolean clutter_paint_context_is_drawing_off_stage (ClutterPaintContext *paint_context);

CoglFramebuffer * clutter_paint_context_get_base_framebuffer (ClutterPaintContext *paint_context);

CLUTTER_EXPORT
ClutterPaintFlag clutter_paint_context_get_paint_flags (ClutterPaintContext *paint_context);

#endif /* CLUTTER_PAINT_CONTEXT_PRIVATE_H */
