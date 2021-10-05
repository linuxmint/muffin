/*
 * Copyright © 2009, 2010, 2011  Intel Corp.
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

#ifndef META_INPUT_DEVICE_NATIVE_TOOL_H
#define META_INPUT_DEVICE_NATIVE_TOOL_H

#include <libinput.h>

#include "clutter/clutter.h"

G_BEGIN_DECLS

#define META_TYPE_INPUT_DEVICE_TOOL_NATIVE (meta_input_device_tool_native_get_type ())

#define META_INPUT_DEVICE_TOOL_NATIVE(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), \
  META_TYPE_INPUT_DEVICE_TOOL_NATIVE, MetaInputDeviceToolNative))

#define META_IS_INPUT_DEVICE_TOOL_NATIVE(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), \
  META_TYPE_INPUT_DEVICE_TOOL_NATIVE))

#define META_INPUT_DEVICE_TOOL_NATIVE_CLASS(c) \
  (G_TYPE_CHECK_CLASS_CAST ((c), \
  META_TYPE_INPUT_DEVICE_TOOL_EVDEV, MetaInputDeviceToolNativeClass))

#define META_IS_INPUT_DEVICE_TOOL_NATIVE_CLASS(c) \
  (G_TYPE_CHECK_CLASS_TYPE ((c), \
  META_TYPE_INPUT_DEVICE_TOOL_NATIVE))

#define META_INPUT_DEVICE_TOOL_NATIVE_GET_CLASS(o) \
  (G_TYPE_INSTANCE_GET_CLASS ((o), \
  META_TYPE_INPUT_DEVICE_TOOL_NATIVE, MetaInputDeviceToolNativeClass))

typedef struct _MetaInputDeviceToolNative MetaInputDeviceToolNative;
typedef struct _MetaInputDeviceToolNativeClass MetaInputDeviceToolNativeClass;

struct _MetaInputDeviceToolNative
{
  ClutterInputDeviceTool parent_instance;
  struct libinput_tablet_tool *tool;
  GHashTable *button_map;
  double pressure_curve[4];
};

struct _MetaInputDeviceToolNativeClass
{
  ClutterInputDeviceToolClass parent_class;
};

GType                    meta_input_device_tool_native_get_type (void) G_GNUC_CONST;

ClutterInputDeviceTool * meta_input_device_tool_native_new      (struct libinput_tablet_tool *tool,
                                                                 uint64_t                     serial,
                                                                 ClutterInputDeviceToolType   type);

gdouble                  meta_input_device_tool_native_translate_pressure (ClutterInputDeviceTool *tool,
                                                                           double                  pressure);
uint32_t                 meta_input_device_tool_native_get_button_code    (ClutterInputDeviceTool *tool,
                                                                           uint32_t                button);

void                     meta_input_device_tool_native_set_pressure_curve (ClutterInputDeviceTool *tool,
                                                                           double                  curve[4]);
void                     meta_input_device_tool_native_set_button_code    (ClutterInputDeviceTool *tool,
                                                                           uint32_t                button,
                                                                           uint32_t                evcode);

G_END_DECLS

#endif /* META_INPUT_DEVICE_NATIVE_TOOL_H */
