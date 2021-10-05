/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2013 Red Hat
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
 *     Owen Taylor <otaylor@redhat.com>
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#ifndef __META_SURFACE_ACTOR_X11_H__
#define __META_SURFACE_ACTOR_X11_H__

#include <glib-object.h>

#include <X11/extensions/Xdamage.h>

#include "compositor/meta-surface-actor.h"
#include "meta/display.h"
#include "meta/window.h"

G_BEGIN_DECLS

#define META_TYPE_SURFACE_ACTOR_X11 (meta_surface_actor_x11_get_type ())
G_DECLARE_FINAL_TYPE (MetaSurfaceActorX11,
                      meta_surface_actor_x11,
                      META, SURFACE_ACTOR_X11,
                      MetaSurfaceActor)

MetaSurfaceActor * meta_surface_actor_x11_new (MetaWindow *window);

void meta_surface_actor_x11_set_size (MetaSurfaceActorX11 *self,
                                      int width, int height);
gboolean meta_surface_actor_x11_should_unredirect (MetaSurfaceActorX11 *self);

void meta_surface_actor_x11_set_unredirected (MetaSurfaceActorX11 *self,
                                              gboolean             unredirected);

gboolean meta_surface_actor_x11_is_unredirected (MetaSurfaceActorX11 *self);

gboolean meta_surface_actor_x11_is_visible (MetaSurfaceActorX11 *self);

G_END_DECLS

#endif /* __META_SURFACE_ACTOR_X11_H__ */
