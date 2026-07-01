/*
 * Copyright (C) 2024 Linux Mint
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
 */

#ifndef META_WAYLAND_BACKGROUND_ACTOR_H
#define META_WAYLAND_BACKGROUND_ACTOR_H

#include <clutter/clutter.h>
#include <meta/display.h>

#define META_TYPE_WAYLAND_BACKGROUND_ACTOR (meta_wayland_background_actor_get_type ())

META_EXPORT
G_DECLARE_FINAL_TYPE (MetaWaylandBackgroundActor,
                      meta_wayland_background_actor,
                      META, WAYLAND_BACKGROUND_ACTOR,
                      ClutterActor)

META_EXPORT
ClutterActor * meta_wayland_background_actor_new_for_monitor (MetaDisplay *display,
                                                               int          monitor);

#endif /* META_WAYLAND_BACKGROUND_ACTOR_H */
