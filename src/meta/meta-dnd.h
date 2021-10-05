/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Hyungwon Hwang
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

#ifndef META_DND_H
#define META_DND_H

#include <glib-object.h>
#include <string.h>

#include <meta/common.h>
#include <meta/types.h>

#define META_TYPE_DND (meta_dnd_get_type ())

META_EXPORT
G_DECLARE_FINAL_TYPE (MetaDnd, meta_dnd, META, DND, GObject)

#endif /* META_DND_H */
