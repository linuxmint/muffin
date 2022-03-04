/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat, Inc.
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

#ifndef META_MAIN_PRIVATE_H
#define META_MAIN_PRIVATE_H

#include "core/util-private.h"

typedef enum _MetaCompositorType
{
#ifdef HAVE_WAYLAND
  META_COMPOSITOR_TYPE_WAYLAND,
#endif
  META_COMPOSITOR_TYPE_X11,
} MetaCompositorType;

typedef enum _MetaDisplayPolicy
{
  META_DISPLAY_POLICY_MANDATORY,
  META_DISPLAY_POLICY_ON_DEMAND,
  META_DISPLAY_POLICY_DISABLED,
} MetaDisplayPolicy;

#define META_POINT_IN_RECT(xcoord, ycoord, rect) \
 ((xcoord) >= (rect).x &&                   \
  (xcoord) <  ((rect).x + (rect).width) &&  \
  (ycoord) >= (rect).y &&                   \
  (ycoord) <  ((rect).y + (rect).height))

#define META_GRAB_OP_GET_BASE_TYPE(op) (op & 0x00FF)

META_EXPORT_TEST
void meta_override_compositor_configuration (MetaCompositorType compositor_type,
                                             GType              backend_gtype);

MetaDisplayPolicy meta_get_x11_display_policy (void);

#endif /* META_MAIN_PRIVATE_H */
