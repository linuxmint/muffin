/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
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

#include "clutter-build-config.h"

#include <glib-object.h>

#include "clutter-virtual-input-device.h"

#include "clutter-enum-types.h"
#include "clutter-private.h"
#include "clutter-seat.h"

enum
{
  PROP_0,

  PROP_SEAT,
  PROP_DEVICE_TYPE,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

typedef struct _ClutterVirtualInputDevicePrivate
{
  ClutterSeat *seat;
  ClutterInputDeviceType device_type;
} ClutterVirtualInputDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (ClutterVirtualInputDevice,
                            clutter_virtual_input_device,
                            G_TYPE_OBJECT)

void
clutter_virtual_input_device_notify_relative_motion (ClutterVirtualInputDevice *virtual_device,
                                                     uint64_t                   time_us,
                                                     double                     dx,
                                                     double                     dy)
{
  ClutterVirtualInputDeviceClass *klass =
    CLUTTER_VIRTUAL_INPUT_DEVICE_GET_CLASS (virtual_device);

  klass->notify_relative_motion (virtual_device, time_us, dx, dy);
}

void
clutter_virtual_input_device_notify_absolute_motion (ClutterVirtualInputDevice *virtual_device,
                                                     uint64_t                   time_us,
                                                     double                     x,
                                                     double                     y)
{
  ClutterVirtualInputDeviceClass *klass =
    CLUTTER_VIRTUAL_INPUT_DEVICE_GET_CLASS (virtual_device);

  klass->notify_absolute_motion (virtual_device, time_us, x, y);
}

void
clutter_virtual_input_device_notify_button (ClutterVirtualInputDevice *virtual_device,
                                            uint64_t                   time_us,
                                            uint32_t                   button,
                                            ClutterButtonState         button_state)
{
  ClutterVirtualInputDeviceClass *klass =
    CLUTTER_VIRTUAL_INPUT_DEVICE_GET_CLASS (virtual_device);

  klass->notify_button (virtual_device, time_us, button, button_state);
}

void
clutter_virtual_input_device_notify_key (ClutterVirtualInputDevice *virtual_device,
                                         uint64_t                   time_us,
                                         uint32_t                   key,
                                         ClutterKeyState            key_state)
{
  ClutterVirtualInputDeviceClass *klass =
    CLUTTER_VIRTUAL_INPUT_DEVICE_GET_CLASS (virtual_device);

  klass->notify_key (virtual_device, time_us, key, key_state);
}

void
clutter_virtual_input_device_notify_keyval (ClutterVirtualInputDevice *virtual_device,
                                            uint64_t                   time_us,
                                            uint32_t                   keyval,
                                            ClutterKeyState            key_state)
{
  ClutterVirtualInputDeviceClass *klass =
    CLUTTER_VIRTUAL_INPUT_DEVICE_GET_CLASS (virtual_device);

  klass->notify_keyval (virtual_device, time_us, keyval, key_state);
}

void
clutter_virtual_input_device_notify_discrete_scroll (ClutterVirtualInputDevice *virtual_device,
                                                     uint64_t                   time_us,
                                                     ClutterScrollDirection     direction,
                                                     ClutterScrollSource        scroll_source)
{
  ClutterVirtualInputDeviceClass *klass =
    CLUTTER_VIRTUAL_INPUT_DEVICE_GET_CLASS (virtual_device);

  klass->notify_discrete_scroll (virtual_device, time_us,
                                 direction, scroll_source);
}

void
clutter_virtual_input_device_notify_scroll_continuous (ClutterVirtualInputDevice *virtual_device,
                                                       uint64_t                   time_us,
                                                       double                     dx,
                                                       double                     dy,
                                                       ClutterScrollSource        scroll_source,
                                                       ClutterScrollFinishFlags   finish_flags)
{
  ClutterVirtualInputDeviceClass *klass =
    CLUTTER_VIRTUAL_INPUT_DEVICE_GET_CLASS (virtual_device);

  klass->notify_scroll_continuous (virtual_device, time_us,
                                   dx, dy, scroll_source, finish_flags);
}

void
clutter_virtual_input_device_notify_touch_down (ClutterVirtualInputDevice *virtual_device,
                                                uint64_t                   time_us,
                                                int                        slot,
                                                double                     x,
                                                double                     y)
{
  ClutterVirtualInputDeviceClass *klass =
    CLUTTER_VIRTUAL_INPUT_DEVICE_GET_CLASS (virtual_device);

  klass->notify_touch_down (virtual_device, time_us,
                            slot, x, y);
}

void
clutter_virtual_input_device_notify_touch_motion (ClutterVirtualInputDevice *virtual_device,
                                                  uint64_t                   time_us,
                                                  int                        slot,
                                                  double                     x,
                                                  double                     y)
{
  ClutterVirtualInputDeviceClass *klass =
    CLUTTER_VIRTUAL_INPUT_DEVICE_GET_CLASS (virtual_device);

  klass->notify_touch_motion (virtual_device, time_us,
                              slot, x, y);
}

void
clutter_virtual_input_device_notify_touch_up (ClutterVirtualInputDevice *virtual_device,
                                              uint64_t                   time_us,
                                              int                        slot)
{
  ClutterVirtualInputDeviceClass *klass =
    CLUTTER_VIRTUAL_INPUT_DEVICE_GET_CLASS (virtual_device);

  klass->notify_touch_up (virtual_device, time_us,
                          slot);
}

int
clutter_virtual_input_device_get_device_type (ClutterVirtualInputDevice *virtual_device)
{
  ClutterVirtualInputDevicePrivate *priv =
    clutter_virtual_input_device_get_instance_private (virtual_device);

  return priv->device_type;
}

static void
clutter_virtual_input_device_get_property (GObject    *object,
                                           guint       prop_id,
                                           GValue     *value,
                                           GParamSpec *pspec)
{
  ClutterVirtualInputDevice *virtual_device =
    CLUTTER_VIRTUAL_INPUT_DEVICE (object);
  ClutterVirtualInputDevicePrivate *priv =
    clutter_virtual_input_device_get_instance_private (virtual_device);

  switch (prop_id)
    {
    case PROP_SEAT:
      g_value_set_object (value, priv->seat);
      break;
    case PROP_DEVICE_TYPE:
      g_value_set_enum (value, priv->device_type);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_virtual_input_device_set_property (GObject      *object,
                                           guint         prop_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
  ClutterVirtualInputDevice *virtual_device =
    CLUTTER_VIRTUAL_INPUT_DEVICE (object);
  ClutterVirtualInputDevicePrivate *priv =
    clutter_virtual_input_device_get_instance_private (virtual_device);

  switch (prop_id)
    {
    case PROP_SEAT:
      priv->seat = g_value_get_object (value);
      break;
    case PROP_DEVICE_TYPE:
      priv->device_type = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_virtual_input_device_init (ClutterVirtualInputDevice *virtual_device)
{
}

static void
clutter_virtual_input_device_class_init (ClutterVirtualInputDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = clutter_virtual_input_device_get_property;
  object_class->set_property = clutter_virtual_input_device_set_property;

  obj_props[PROP_SEAT] =
    g_param_spec_object ("seat",
                         P_("Seat"),
                         P_("Seat"),
                         CLUTTER_TYPE_SEAT,
                         CLUTTER_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  obj_props[PROP_DEVICE_TYPE] =
    g_param_spec_enum ("device-type",
                       P_("Device type"),
                       P_("Device type"),
                       CLUTTER_TYPE_INPUT_DEVICE_TYPE,
                       CLUTTER_POINTER_DEVICE,
                       CLUTTER_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, PROP_LAST, obj_props);
}
