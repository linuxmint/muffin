/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *
 * Copyright (C) 2006, 2007, 2008 OpenedHand Ltd
 * Copyright (C) 2009, 2010 Intel Corp
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

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#ifndef __CLUTTER_ACTOR_DEPRECATED_H__
#define __CLUTTER_ACTOR_DEPRECATED_H__

#include <clutter/clutter-types.h>

G_BEGIN_DECLS

CLUTTER_DEPRECATED
ClutterActor *  clutter_get_actor_by_gid                        (guint32                id_);

CLUTTER_DEPRECATED_FOR(clutter_actor_add_child)
void            clutter_actor_set_parent                        (ClutterActor          *self,
                                                                 ClutterActor          *parent);

CLUTTER_DEPRECATED_FOR(clutter_actor_remove_child)
void            clutter_actor_unparent                          (ClutterActor          *self);

CLUTTER_DEPRECATED
void            clutter_actor_push_internal                     (ClutterActor          *self);

CLUTTER_DEPRECATED
void            clutter_actor_pop_internal                      (ClutterActor          *self);

CLUTTER_DEPRECATED
void            clutter_actor_show_all                          (ClutterActor          *self);

CLUTTER_DEPRECATED_FOR(clutter_actor_set_z_position)
void            clutter_actor_set_depth                         (ClutterActor          *self,
                                                                 gfloat                 depth);

CLUTTER_DEPRECATED_FOR(clutter_actor_get_z_position)
gfloat          clutter_actor_get_depth                         (ClutterActor          *self);

CLUTTER_DEPRECATED_FOR(clutter_actor_set_rotation_angle)
void            clutter_actor_set_rotation                      (ClutterActor          *self,
                                                                 ClutterRotateAxis      axis,
                                                                 gdouble                angle,
                                                                 gfloat                 x,
                                                                 gfloat                 y,
                                                                 gfloat                 z);
CLUTTER_DEPRECATED_FOR(clutter_actor_set_rotation_angle and clutter_actor_set_pivot_point)
void            clutter_actor_set_z_rotation_from_gravity       (ClutterActor          *self,
                                                                 gdouble                angle,
                                                                 ClutterGravity         gravity);
CLUTTER_DEPRECATED_FOR(clutter_actor_get_rotation_angle)
gdouble         clutter_actor_get_rotation                      (ClutterActor          *self,
                                                                 ClutterRotateAxis      axis,
                                                                 gfloat                *x,
                                                                 gfloat                *y,
                                                                 gfloat                *z);
CLUTTER_DEPRECATED
ClutterGravity  clutter_actor_get_z_rotation_gravity            (ClutterActor          *self);

CLUTTER_DEPRECATED_FOR(clutter_actor_set_scale and clutter_actor_set_pivot_point)
void            clutter_actor_set_scale_full                    (ClutterActor          *self,
                                                                 gdouble                scale_x,
                                                                 gdouble                scale_y,
                                                                 gfloat                 center_x,
                                                                 gfloat                 center_y);
CLUTTER_DEPRECATED_FOR(clutter_actor_get_pivot_point)
void            clutter_actor_get_scale_center                  (ClutterActor          *self,
                                                                 gfloat                *center_x,
                                                                 gfloat                *center_y);
CLUTTER_DEPRECATED_FOR(clutter_actor_get_pivot_point)
ClutterGravity  clutter_actor_get_scale_gravity                 (ClutterActor          *self);

CLUTTER_DEPRECATED
void            clutter_actor_set_anchor_point                  (ClutterActor          *self,
                                                                 gfloat                 anchor_x,
                                                                 gfloat                 anchor_y);
CLUTTER_DEPRECATED
void            clutter_actor_move_anchor_point                 (ClutterActor          *self,
                                                                 gfloat                 anchor_x,
                                                                 gfloat                 anchor_y);
CLUTTER_DEPRECATED
ClutterGravity  clutter_actor_get_anchor_point_gravity          (ClutterActor          *self);
CLUTTER_DEPRECATED
void            clutter_actor_set_anchor_point_from_gravity     (ClutterActor          *self,
                                                                 ClutterGravity         gravity);
CLUTTER_DEPRECATED
void            clutter_actor_move_anchor_point_from_gravity    (ClutterActor          *self,
                                                                 ClutterGravity         gravity);

G_END_DECLS

#endif /* __CLUTTER_ACTOR_DEPRECATED_H__ */
