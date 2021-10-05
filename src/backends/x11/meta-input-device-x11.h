/*
 * Copyright © 2011  Intel Corp.
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
 * Author: Emmanuele Bassi <ebassi@linux.intel.com>
 */

#ifndef META_INPUT_DEVICE_X11_H
#define META_INPUT_DEVICE_X11_H

#include <X11/extensions/XInput2.h>

#ifdef HAVE_LIBWACOM
#include <libwacom/libwacom.h>
#endif

#include "backends/meta-input-device-private.h"
#include "clutter/clutter.h"

G_BEGIN_DECLS

#define META_TYPE_INPUT_DEVICE_X11 (meta_input_device_x11_get_type ())
#define META_INPUT_DEVICE_X11(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), META_TYPE_INPUT_DEVICE_X11, MetaInputDeviceX11))
#define META_IS_INPUT_DEVICE_X11(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), META_TYPE_INPUT_DEVICE_X11))
#define META_INPUT_DEVICE_X11_CLASS(c) (G_TYPE_CHECK_CLASS_CAST ((c), META_TYPE_INPUT_DEVICE_X11, MetaInputDeviceX11Class))
#define META_IS_INPUT_DEVICE_X11_CLASS(c) (G_TYPE_CHECK_CLASS_TYPE ((c), META_TYPE_INPUT_DEVICE_X11))
#define META_INPUT_DEVICE_X11_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), META_TYPE_INPUT_DEVICE_X11, MetaInputDeviceX11Class))

typedef struct _MetaInputDeviceX11 MetaInputDeviceX11;
typedef struct _MetaInputDeviceX11Class MetaInputDeviceX11Class;

GType meta_input_device_x11_get_type (void) G_GNUC_CONST;

void  meta_input_device_x11_translate_state (ClutterEvent    *event,
                                             XIModifierState *modifiers_state,
                                             XIButtonState   *buttons_state,
                                             XIGroupState    *group_state);
void  meta_input_device_x11_update_tool     (ClutterInputDevice     *device,
                                             ClutterInputDeviceTool *tool);
ClutterInputDeviceTool * meta_input_device_x11_get_current_tool (ClutterInputDevice *device);

#ifdef HAVE_LIBWACOM
void meta_input_device_x11_ensure_wacom_info (ClutterInputDevice  *device,
                                              WacomDeviceDatabase *wacom_db);

uint32_t meta_input_device_x11_get_pad_group_mode (ClutterInputDevice *device,
                                                   uint32_t            group);

void meta_input_device_x11_update_pad_state (ClutterInputDevice *device,
                                             uint32_t            button,
                                             uint32_t            state,
                                             uint32_t           *group,
                                             uint32_t           *mode);

#endif

gboolean meta_input_device_x11_get_pointer_location (ClutterInputDevice *device,
                                                     float              *x,
                                                     float              *y);

G_END_DECLS

#endif /* META_INPUT_DEVICE_X11_H */
