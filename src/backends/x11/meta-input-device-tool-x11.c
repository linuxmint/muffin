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

#include "config.h"

#include "meta-input-device-tool-x11.h"

G_DEFINE_TYPE (MetaInputDeviceToolX11, meta_input_device_tool_x11,
               CLUTTER_TYPE_INPUT_DEVICE_TOOL)

static void
meta_input_device_tool_x11_class_init (MetaInputDeviceToolX11Class *klass)
{
}

static void
meta_input_device_tool_x11_init (MetaInputDeviceToolX11 *tool)
{
}

ClutterInputDeviceTool *
meta_input_device_tool_x11_new (guint                      serial,
                                ClutterInputDeviceToolType type)
{
  return g_object_new (META_TYPE_INPUT_DEVICE_TOOL_X11,
                       "type", type,
                       "serial", serial,
                       NULL);
}
