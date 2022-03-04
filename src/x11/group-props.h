/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* MetaGroup property handling */

/*
 * Copyright (C) 2002 Red Hat, Inc.
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

#ifndef META_GROUP_PROPS_H
#define META_GROUP_PROPS_H

#include "core/window-private.h"
#include "meta/group.h"

void meta_group_reload_property         (MetaGroup   *group,
                                         Atom         property);
void meta_group_reload_properties       (MetaGroup   *group,
                                         const Atom  *properties,
                                         int          n_properties);

void meta_x11_display_init_group_prop_hooks (MetaX11Display *x11_display);
void meta_x11_display_free_group_prop_hooks (MetaX11Display *x11_display);

#endif /* META_GROUP_PROPS_H */
