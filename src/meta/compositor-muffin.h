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

#ifndef MUFFIN_H_
#define MUFFIN_H_

#include "clutter/clutter.h"
#include "meta/compositor.h"
#include "meta/meta-window-actor.h"
#include "meta/types.h"

/* Public compositor API */
META_EXPORT
ClutterActor *meta_get_stage_for_display            (MetaDisplay *display);

META_EXPORT
GList        *meta_get_window_actors                (MetaDisplay *display);

META_EXPORT
ClutterActor *meta_get_window_group_for_display     (MetaDisplay *display);

META_EXPORT
ClutterActor *meta_get_top_window_group_for_display (MetaDisplay *display);

META_EXPORT
ClutterActor *meta_get_bottom_window_group_for_display   (MetaDisplay *display);

META_EXPORT
ClutterActor *meta_get_feedback_group_for_display   (MetaDisplay *display);

META_EXPORT
void meta_disable_unredirect_for_display (MetaDisplay *display);

META_EXPORT
void meta_enable_unredirect_for_display  (MetaDisplay *display);

META_EXPORT
void meta_focus_stage_window       (MetaDisplay  *display,
                                    guint32       timestamp);

META_EXPORT
gboolean meta_stage_is_focused     (MetaDisplay  *display);

META_EXPORT
ClutterActor *meta_get_x11_background_actor_for_display (MetaDisplay *display);

META_EXPORT
ClutterActor *meta_get_desklet_container_for_display (MetaDisplay *display);

#endif
