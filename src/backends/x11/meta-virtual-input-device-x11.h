/*
 * Copyright (C) 2016  Red Hat Inc.
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
 *
 * Author: Jonas Ådahl <jadahl@gmail.com>
 */

#ifndef META_VIRTUAL_INPUT_DEVICE_X11_H
#define META_VIRTUAL_INPUT_DEVICE_X11_H

#include "clutter/clutter.h"

#define META_TYPE_VIRTUAL_INPUT_DEVICE_X11 (meta_virtual_input_device_x11_get_type ())
G_DECLARE_FINAL_TYPE (MetaVirtualInputDeviceX11,
                      meta_virtual_input_device_x11,
                      META, VIRTUAL_INPUT_DEVICE_X11,
                      ClutterVirtualInputDevice)

#endif /* META_VIRTUAL_INPUT_DEVICE_X11_H */
