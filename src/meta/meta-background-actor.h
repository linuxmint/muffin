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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef META_BACKGROUND_ACTOR_H
#define META_BACKGROUND_ACTOR_H

#include <gsettings-desktop-schemas/gdesktop-enums.h>

#include "clutter/clutter.h"
#include "meta/meta-background.h"

/**
 * MetaBackgroundActor:
 *
 * This class handles tracking and painting the root window background.
 * By integrating with #MetaWindowGroup we can avoid painting parts of
 * the background that are obscured by other windows.
 */

#define META_TYPE_BACKGROUND_ACTOR (meta_background_actor_get_type ())

META_EXPORT
G_DECLARE_FINAL_TYPE (MetaBackgroundActor,
                      meta_background_actor,
                      META, BACKGROUND_ACTOR,
                      ClutterActor)


META_EXPORT
ClutterActor *meta_background_actor_new    (MetaDisplay *display,
                                            int          monitor);

META_EXPORT
void meta_background_actor_set_background  (MetaBackgroundActor *self,
                                            MetaBackground      *background);

META_EXPORT
void meta_background_actor_set_gradient (MetaBackgroundActor *self,
                                         gboolean             enabled,
                                         int                  height,
                                         double               tone_start);

META_EXPORT
void meta_background_actor_set_monitor  (MetaBackgroundActor *self,
                                         int                  monitor);

META_EXPORT
void meta_background_actor_set_vignette (MetaBackgroundActor *self,
                                         gboolean             enabled,
                                         double               brightness,
                                         double               sharpness);

#endif /* META_BACKGROUND_ACTOR_H */
