/*
 * Copyright © 2020  Red Hat Ltd.
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */
#ifndef META_INPUT_DEVICE_H
#define META_INPUT_DEVICE_H

#include <glib-object.h>

#ifdef HAVE_LIBWACOM
#include <libwacom/libwacom.h>
#endif

#include "clutter/clutter-mutter.h"

typedef struct _MetaInputDeviceClass MetaInputDeviceClass;
typedef struct _MetaInputDevice MetaInputDevice;

struct _MetaInputDeviceClass
{
  ClutterInputDeviceClass parent_class;
};

#define META_TYPE_INPUT_DEVICE (meta_input_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaInputDevice,
			  meta_input_device,
			  META, INPUT_DEVICE,
			  ClutterInputDevice)

#ifdef HAVE_LIBWACOM
WacomDevice * meta_input_device_get_wacom_device (MetaInputDevice *input_device);
#endif

#endif /* META_INPUT_DEVICE_H */
