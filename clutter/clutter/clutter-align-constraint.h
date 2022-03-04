/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
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
 *
 * Author:
 *   Emmanuele Bassi <ebassi@linux.intel.com>
 */

#ifndef __CLUTTER_ALIGN_CONSTRAINT_H__
#define __CLUTTER_ALIGN_CONSTRAINT_H__

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <clutter/clutter-constraint.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_ALIGN_CONSTRAINT           (clutter_align_constraint_get_type ())
#define CLUTTER_ALIGN_CONSTRAINT(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_ALIGN_CONSTRAINT, ClutterAlignConstraint))
#define CLUTTER_IS_ALIGN_CONSTRAINT(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_ALIGN_CONSTRAINT))

/**
 * ClutterAlignConstraint:
 *
 * #ClutterAlignConstraint is an opaque structure
 * whose members cannot be directly accesses
 *
 * Since: 1.4
 */
typedef struct _ClutterAlignConstraint          ClutterAlignConstraint;
typedef struct _ClutterAlignConstraintClass     ClutterAlignConstraintClass;

CLUTTER_EXPORT
GType clutter_align_constraint_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterConstraint *clutter_align_constraint_new            (ClutterActor           *source,
                                                            ClutterAlignAxis        axis,
                                                            gfloat                  factor);

CLUTTER_EXPORT
void               clutter_align_constraint_set_source     (ClutterAlignConstraint *align,
                                                            ClutterActor           *source);
CLUTTER_EXPORT
ClutterActor *     clutter_align_constraint_get_source     (ClutterAlignConstraint *align);
CLUTTER_EXPORT
void               clutter_align_constraint_set_align_axis (ClutterAlignConstraint *align,
                                                            ClutterAlignAxis        axis);
CLUTTER_EXPORT
ClutterAlignAxis   clutter_align_constraint_get_align_axis (ClutterAlignConstraint *align);
CLUTTER_EXPORT
void               clutter_align_constraint_set_factor     (ClutterAlignConstraint *align,
                                                            gfloat                  factor);
CLUTTER_EXPORT
gfloat             clutter_align_constraint_get_factor     (ClutterAlignConstraint *align);

G_END_DECLS

#endif /* __CLUTTER_ALIGN_CONSTRAINT_H__ */
