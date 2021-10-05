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
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#ifndef __META_SURFACE_ACTOR_WAYLAND_H__
#define __META_SURFACE_ACTOR_WAYLAND_H__

#include <glib-object.h>

#include "backends/meta-monitor-manager-private.h"
#include "compositor/meta-surface-actor.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland.h"

G_BEGIN_DECLS

#define META_TYPE_SURFACE_ACTOR_WAYLAND (meta_surface_actor_wayland_get_type ())
G_DECLARE_FINAL_TYPE (MetaSurfaceActorWayland,
                      meta_surface_actor_wayland,
                      META, SURFACE_ACTOR_WAYLAND,
                      MetaSurfaceActor)

MetaSurfaceActor * meta_surface_actor_wayland_new (MetaWaylandSurface *surface);
MetaWaylandSurface * meta_surface_actor_wayland_get_surface (MetaSurfaceActorWayland *self);
void meta_surface_actor_wayland_surface_destroyed (MetaSurfaceActorWayland *self);

double meta_surface_actor_wayland_get_scale (MetaSurfaceActorWayland *actor);

void meta_surface_actor_wayland_get_subsurface_rect (MetaSurfaceActorWayland *self,
                                                     MetaRectangle           *rect);

void meta_surface_actor_wayland_add_frame_callbacks (MetaSurfaceActorWayland *self,
                                                     struct wl_list *frame_callbacks);

G_END_DECLS

#endif /* __META_SURFACE_ACTOR_WAYLAND_H__ */
