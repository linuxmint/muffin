/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter size/position constraints */

/*
 * Copyright (C) 2002 Red Hat, Inc.
 * Copyright (C) 2005 Elijah Newren
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

#ifndef META_CONSTRAINTS_H
#define META_CONSTRAINTS_H

#include "core/frame.h"
#include "core/window-private.h"
#include "meta/util.h"

void meta_window_constrain (MetaWindow          *window,
                            MetaMoveResizeFlags  flags,
                            MetaGravity          resize_gravity,
                            const MetaRectangle *orig,
                            MetaRectangle       *new,
                            MetaRectangle       *intermediate,
                            int                 *rel_x,
                            int                 *rel_y);

#endif /* META_CONSTRAINTS_H */
