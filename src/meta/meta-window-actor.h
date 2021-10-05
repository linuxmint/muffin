/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2008 Matthew Allum
 * Copyright (C) 2007 Iain Holmes
 * Based on xcompmgr - (c) 2003 Keith Packard
 *          xfwm4    - (c) 2005-2007 Olivier Fourdan
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef META_WINDOW_ACTOR_H_
#define META_WINDOW_ACTOR_H_

#include "clutter/clutter.h"
#include "meta/compositor.h"
#include "meta/meta-shaped-texture.h"

#define META_TYPE_WINDOW_ACTOR (meta_window_actor_get_type ())

META_EXPORT
G_DECLARE_DERIVABLE_TYPE (MetaWindowActor,
                          meta_window_actor,
                          META, WINDOW_ACTOR,
                          ClutterActor)

META_EXPORT
MetaWindow *       meta_window_actor_get_meta_window      (MetaWindowActor *self);

META_EXPORT
MetaShapedTexture *meta_window_actor_get_texture          (MetaWindowActor *self);

META_EXPORT
void               meta_window_actor_sync_visibility      (MetaWindowActor *self);

META_EXPORT
gboolean       meta_window_actor_is_destroyed (MetaWindowActor *self);

META_EXPORT
cairo_surface_t * meta_window_actor_get_image (MetaWindowActor       *self,
                                               cairo_rectangle_int_t *clip);

typedef enum
{
  META_SHADOW_MODE_AUTO,
  META_SHADOW_MODE_FORCED_OFF,
  META_SHADOW_MODE_FORCED_ON,
} MetaShadowMode;

#endif /* META_WINDOW_ACTOR_H */
