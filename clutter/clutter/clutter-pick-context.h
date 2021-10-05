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

#ifndef CLUTTER_PICK_CONTEXT_H
#define CLUTTER_PICK_CONTEXT_H

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <glib-object.h>

#include "clutter-macros.h"
#include "clutter-stage-view.h"

typedef struct _ClutterPickContext ClutterPickContext;

#define CLUTTER_TYPE_PICK_CONTEXT (clutter_pick_context_get_type ())

CLUTTER_EXPORT
GType clutter_pick_context_get_type (void);

CLUTTER_EXPORT
ClutterPickContext * clutter_pick_context_ref (ClutterPickContext *pick_context);

CLUTTER_EXPORT
void clutter_pick_context_unref (ClutterPickContext *pick_context);

CLUTTER_EXPORT
void clutter_pick_context_destroy (ClutterPickContext *pick_context);

CLUTTER_EXPORT
CoglFramebuffer * clutter_pick_context_get_framebuffer (ClutterPickContext *pick_context);

#endif /* CLUTTER_PICK_CONTEXT_H */
