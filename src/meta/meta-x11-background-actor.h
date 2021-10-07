/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * meta-background-actor.h: Actor for painting the root window background
 *
 * Copyright 2010 Red Hat, Inc.
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
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 */

#ifndef META_X11_BACKGROUND_ACTOR_H
#define META_X11_BACKGROUND_ACTOR_H

#include <clutter/clutter.h>

#include <meta/display.h>

/**
 * MetaX11BackgroundActor:
 *
 * This class handles tracking and painting the root window background.
 * By integrating with #MetaWindowGroup we can avoid painting parts of
 * the background that are obscured by other windows.
 */

#define META_TYPE_X11_BACKGROUND_ACTOR (meta_x11_background_actor_get_type ())

META_EXPORT
G_DECLARE_FINAL_TYPE (MetaX11BackgroundActor,
                      meta_x11_background_actor,
                      META, X11_BACKGROUND_ACTOR,
                      ClutterActor)

META_EXPORT
ClutterActor *meta_x11_background_actor_new_for_display (MetaDisplay *display);

#endif /* META_X11_BACKGROUND_ACTOR_H */
