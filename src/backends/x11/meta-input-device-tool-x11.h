/*
 * Copyright © 2016 Red Hat
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

#ifndef META_INPUT_DEVICE_TOOL_X11_H
#define META_INPUT_DEVICE_TOOL_X11_H

#include "clutter/clutter.h"

#define META_TYPE_INPUT_DEVICE_TOOL_X11 (meta_input_device_tool_x11_get_type ())

#define META_INPUT_DEVICE_TOOL_X11(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), META_TYPE_INPUT_DEVICE_TOOL_X11, MetaInputDeviceToolX11))
#define META_IS_INPUT_DEVICE_TOOL_X11(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), META_TYPE_INPUT_DEVICE_TOOL_X11))
#define META_INPUT_DEVICE_TOOL_X11_CLASS(c) (G_TYPE_CHECK_CLASS_CAST ((c), META_TYPE_INPUT_DEVICE_TOOL_X11, MetaInputDeviceToolX11Class))
#define META_IS_INPUT_DEVICE_TOOL_X11_CLASS(c) (G_TYPE_CHECK_CLASS_TYPE ((c), META_TYPE_INPUT_DEVICE_TOOL_X1))
#define META_INPUT_DEVICE_TOOL_X11_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), META_TYPE_INPUT_DEVICE_TOOL_X11, MetaInputDeviceToolX11Class))

typedef struct _MetaInputDeviceToolX11 MetaInputDeviceToolX11;
typedef struct _MetaInputDeviceToolX11Class MetaInputDeviceToolX11Class;

struct _MetaInputDeviceToolX11
{
  ClutterInputDeviceTool parent_instance;
};

struct _MetaInputDeviceToolX11Class
{
  ClutterInputDeviceToolClass parent_class;
};

GType meta_input_device_tool_x11_get_type (void) G_GNUC_CONST;

ClutterInputDeviceTool * meta_input_device_tool_x11_new (guint                        serial,
                                                         ClutterInputDeviceToolType   type);

#endif /* META_INPUT_DEVICE_TOOL_X11_H */
