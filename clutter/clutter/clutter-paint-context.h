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

#ifndef CLUTTER_PAINT_CONTEXT_H
#define CLUTTER_PAINT_CONTEXT_H

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <glib-object.h>

#include "clutter-macros.h"
#include "clutter-stage-view.h"

typedef struct _ClutterPaintContext ClutterPaintContext;

#define CLUTTER_TYPE_PAINT_CONTEXT (clutter_paint_context_get_type ())

CLUTTER_EXPORT
GType clutter_paint_context_get_type (void);

CLUTTER_EXPORT
ClutterPaintContext * clutter_paint_context_new_for_framebuffer (CoglFramebuffer *framebuffer);

CLUTTER_EXPORT
ClutterPaintContext * clutter_paint_context_ref (ClutterPaintContext *paint_context);

CLUTTER_EXPORT
void clutter_paint_context_unref (ClutterPaintContext *paint_context);

CLUTTER_EXPORT
void clutter_paint_context_destroy (ClutterPaintContext *paint_context);

CLUTTER_EXPORT
CoglFramebuffer * clutter_paint_context_get_framebuffer (ClutterPaintContext *paint_context);

CLUTTER_EXPORT
ClutterStageView * clutter_paint_context_get_stage_view (ClutterPaintContext *paint_context);

CLUTTER_EXPORT
void clutter_paint_context_push_framebuffer (ClutterPaintContext *paint_context,
                                             CoglFramebuffer     *framebuffer);

CLUTTER_EXPORT
void clutter_paint_context_pop_framebuffer (ClutterPaintContext *paint_context);

CLUTTER_EXPORT
const cairo_region_t * clutter_paint_context_get_redraw_clip (ClutterPaintContext *paint_context);

#endif /* CLUTTER_PAINT_CONTEXT_H */
