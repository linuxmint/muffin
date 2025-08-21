/*
 * Copyright (C) 2019 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef COGL_SCANOUT_H
#define COGL_SCANOUT_H

#include "cogl/cogl-types.h"

#include <glib-object.h>

#define COGL_TYPE_SCANOUT (cogl_scanout_get_type ())
COGL_EXPORT
G_DECLARE_INTERFACE (CoglScanout, cogl_scanout,
                     COGL, SCANOUT, GObject)

struct _CoglScanoutInterface
{
    GTypeInterface parent_iface;
};

#endif /* COGL_SCANOUT_H */
