/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * meta-dnd-actor-private.h: Actor for painting the DnD surface
 *
 * Copyright 2014 Red Hat, Inc.
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
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#ifndef META_DND_ACTOR_PRIVATE_H
#define META_DND_ACTOR_PRIVATE_H

#include "compositor/meta-feedback-actor-private.h"

/**
 * MetaDnDActor:
 *
 * This class handles the rendering of the DnD surface
 */

#define META_TYPE_DND_ACTOR (meta_dnd_actor_get_type ())
G_DECLARE_FINAL_TYPE (MetaDnDActor,
                      meta_dnd_actor,
                      META, DND_ACTOR,
                      MetaFeedbackActor)


ClutterActor *meta_dnd_actor_new (ClutterActor *drag_origin,
                                  int           start_x,
                                  int           start_y);

void          meta_dnd_actor_drag_finish (MetaDnDActor *self,
                                          gboolean      success);

#endif /* META_DND_ACTOR_PRIVATE_H */
