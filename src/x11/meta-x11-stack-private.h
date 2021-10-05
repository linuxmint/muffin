/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2019 Red Hat, Inc
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

#ifndef META_X11_STACK_H
#define META_X11_STACK_H

#include <glib-object.h>

#include "meta/types.h"

#define META_TYPE_X11_STACK (meta_x11_stack_get_type ())
G_DECLARE_FINAL_TYPE (MetaX11Stack, meta_x11_stack, META, X11_STACK, GObject)

typedef struct _MetaX11Stack MetaX11Stack;

MetaX11Stack * meta_x11_stack_new (MetaX11Display *x11_display);

#endif /* META_X11_STACK_H */
