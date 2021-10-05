/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2015 Red Hat
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
 * Written by:
 *     Jonas Ådahl <jadahl@gmail.com>
 */

/**
 * SECTION:meta-pointer-constraint
 * @title: MetaPointerConstraint
 * @short_description: Pointer client constraints.
 *
 * A MetaPointerConstraint can be used to implement any kind of pointer
 * constraint as requested by a client, such as cursor lock.
 *
 * Examples of pointer constraints are "pointer confinement" and "pointer
 * locking" (as defined in the wayland pointer constraint protocol extension),
 * which restrict movement in relation to a given client.
 */

#include "config.h"

#include "backends/meta-pointer-constraint.h"

#include <glib-object.h>

G_DEFINE_TYPE (MetaPointerConstraint, meta_pointer_constraint, G_TYPE_OBJECT);

static void
meta_pointer_constraint_init (MetaPointerConstraint *constraint)
{
}

static void
meta_pointer_constraint_class_init (MetaPointerConstraintClass *klass)
{
}

/**
 * meta_pointer_constraint_constrain:
 * @constraint: a #MetaPointerConstraint.
 * @device; the device of the pointer.
 * @time: the timestamp (in ms) of the event.
 * @prev_x: X-coordinate of the previous pointer position.
 * @prev_y: Y-coordinate of the previous pointer position.
 * @x: The modifiable X-coordinate to which the pointer would like to go to.
 * @y: The modifiable Y-coordinate to which the pointer would like to go to.
 *
 * Constrains the pointer movement from point (@prev_x, @prev_y) to (@x, @y),
 * if needed.
 */
void
meta_pointer_constraint_constrain (MetaPointerConstraint *constraint,
                                   ClutterInputDevice    *device,
                                   guint32                time,
                                   float                  prev_x,
                                   float                  prev_y,
                                   float                  *x,
                                   float                  *y)
{
  META_POINTER_CONSTRAINT_GET_CLASS (constraint)->constrain (constraint,
                                                             device,
                                                             time,
                                                             prev_x, prev_y,
                                                             x, y);
}
