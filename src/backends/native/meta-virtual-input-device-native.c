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

#include "config.h"

#include <glib-object.h>
#include <linux/input.h>

#include "backends/native/meta-input-device-native.h"
#include "backends/native/meta-keymap-native.h"
#include "backends/native/meta-seat-native.h"
#include "backends/native/meta-virtual-input-device-native.h"
#include "clutter/clutter-mutter.h"
#include "meta/util.h"

enum
{
  PROP_0,

  PROP_SEAT,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

struct _MetaVirtualInputDeviceNative
{
  ClutterVirtualInputDevice parent;

  ClutterInputDevice *device;
  MetaSeatNative *seat;
  int button_count[KEY_CNT];
};

G_DEFINE_TYPE (MetaVirtualInputDeviceNative,
               meta_virtual_input_device_native,
               CLUTTER_TYPE_VIRTUAL_INPUT_DEVICE)

typedef enum _EvdevButtonType
{
  EVDEV_BUTTON_TYPE_NONE,
  EVDEV_BUTTON_TYPE_KEY,
  EVDEV_BUTTON_TYPE_BUTTON,
} EvdevButtonType;

static int
update_button_count (MetaVirtualInputDeviceNative *virtual_evdev,
                     uint32_t                      button,
                     uint32_t                      state)
{
  if (state)
    return ++virtual_evdev->button_count[button];
  else
    return --virtual_evdev->button_count[button];
}

static EvdevButtonType
get_button_type (uint16_t code)
{
  switch (code)
    {
    case BTN_TOOL_PEN:
    case BTN_TOOL_RUBBER:
    case BTN_TOOL_BRUSH:
    case BTN_TOOL_PENCIL:
    case BTN_TOOL_AIRBRUSH:
    case BTN_TOOL_MOUSE:
    case BTN_TOOL_LENS:
    case BTN_TOOL_QUINTTAP:
    case BTN_TOOL_DOUBLETAP:
    case BTN_TOOL_TRIPLETAP:
    case BTN_TOOL_QUADTAP:
    case BTN_TOOL_FINGER:
    case BTN_TOUCH:
      return EVDEV_BUTTON_TYPE_NONE;
    }

  if (code >= KEY_ESC && code <= KEY_MICMUTE)
    return EVDEV_BUTTON_TYPE_KEY;
  if (code >= BTN_MISC && code <= BTN_GEAR_UP)
    return EVDEV_BUTTON_TYPE_BUTTON;
  if (code >= KEY_OK && code <= KEY_LIGHTS_TOGGLE)
    return EVDEV_BUTTON_TYPE_KEY;
  if (code >= BTN_DPAD_UP && code <= BTN_DPAD_RIGHT)
    return EVDEV_BUTTON_TYPE_BUTTON;
  if (code >= KEY_ALS_TOGGLE && code <= KEY_KBDINPUTASSIST_CANCEL)
    return EVDEV_BUTTON_TYPE_KEY;
  if (code >= BTN_TRIGGER_HAPPY && code <= BTN_TRIGGER_HAPPY40)
    return EVDEV_BUTTON_TYPE_BUTTON;
  return EVDEV_BUTTON_TYPE_NONE;
}

static void
release_pressed_buttons (ClutterVirtualInputDevice *virtual_device)
{
  MetaVirtualInputDeviceNative *virtual_evdev =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  int code;
  uint64_t time_us;

  time_us = g_get_monotonic_time ();

  meta_topic (META_DEBUG_INPUT,
              "Releasing pressed buttons while destroying virtual input device "
              "(device %p)\n", virtual_device);

  for (code = 0; code < G_N_ELEMENTS (virtual_evdev->button_count); code++)
    {
      if (virtual_evdev->button_count[code] == 0)
        continue;

      switch (get_button_type (code))
        {
        case EVDEV_BUTTON_TYPE_KEY:
          clutter_virtual_input_device_notify_key (virtual_device,
                                                   time_us,
                                                   code,
                                                   CLUTTER_KEY_STATE_RELEASED);
          break;
        case EVDEV_BUTTON_TYPE_BUTTON:
          clutter_virtual_input_device_notify_button (virtual_device,
                                                      time_us,
                                                      code,
                                                      CLUTTER_BUTTON_STATE_RELEASED);
          break;
        case EVDEV_BUTTON_TYPE_NONE:
          g_assert_not_reached ();
        }
    }
}

static void
meta_virtual_input_device_native_notify_relative_motion (ClutterVirtualInputDevice *virtual_device,
                                                         uint64_t                   time_us,
                                                         double                     dx,
                                                         double                     dy)
{
  MetaVirtualInputDeviceNative *virtual_evdev =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);

  g_return_if_fail (virtual_evdev->device != NULL);

  if (time_us == CLUTTER_CURRENT_TIME)
    time_us = g_get_monotonic_time ();

  meta_seat_native_notify_relative_motion (virtual_evdev->seat,
                                           virtual_evdev->device,
                                           time_us,
                                           dx, dy,
                                           dx, dy);
}

static void
meta_virtual_input_device_native_notify_absolute_motion (ClutterVirtualInputDevice *virtual_device,
                                                         uint64_t                   time_us,
                                                         double                     x,
                                                         double                     y)
{
  MetaVirtualInputDeviceNative *virtual_evdev =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);

  g_return_if_fail (virtual_evdev->device != NULL);

  if (time_us == CLUTTER_CURRENT_TIME)
    time_us = g_get_monotonic_time ();

  meta_seat_native_notify_absolute_motion (virtual_evdev->seat,
                                           virtual_evdev->device,
                                           time_us,
                                           x, y,
                                           NULL);
}

static int
translate_to_evdev_button (int clutter_button)
{
  switch (clutter_button)
    {
    case CLUTTER_BUTTON_PRIMARY:
      return BTN_LEFT;
    case CLUTTER_BUTTON_SECONDARY:
      return BTN_RIGHT;
    case CLUTTER_BUTTON_MIDDLE:
      return BTN_MIDDLE;
    default:
      /*
       * For compatibility reasons, all additional buttons go after the old
       * 4-7 scroll ones.
       */
      return clutter_button + (BTN_LEFT - 1) - 4;
    }
}

static void
meta_virtual_input_device_native_notify_button (ClutterVirtualInputDevice *virtual_device,
                                                uint64_t                   time_us,
                                                uint32_t                   button,
                                                ClutterButtonState         button_state)
{
  MetaVirtualInputDeviceNative *virtual_evdev =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  int button_count;
  int evdev_button;

  g_return_if_fail (virtual_evdev->device != NULL);

  if (time_us == CLUTTER_CURRENT_TIME)
    time_us = g_get_monotonic_time ();

  evdev_button = translate_to_evdev_button (button);

  if (get_button_type (evdev_button) != EVDEV_BUTTON_TYPE_BUTTON)
    {
      g_warning ("Unknown/invalid virtual device button 0x%x pressed",
                 evdev_button);
      return;
    }

  button_count = update_button_count (virtual_evdev, evdev_button, button_state);
  if (button_count < 0 || button_count > 1)
    {
      g_warning ("Received multiple virtual 0x%x button %s (ignoring)", evdev_button,
                 button_state == CLUTTER_BUTTON_STATE_PRESSED ? "presses" : "releases");
      update_button_count (virtual_evdev, evdev_button, 1 - button_state);
      return;
    }

  meta_topic (META_DEBUG_INPUT,
              "Emitting virtual button-%s of button 0x%x (device %p)\n",
              button_state == CLUTTER_BUTTON_STATE_PRESSED ? "press" : "release",
              evdev_button, virtual_device);

  meta_seat_native_notify_button (virtual_evdev->seat,
                                  virtual_evdev->device,
                                  time_us,
                                  evdev_button,
                                  button_state);
}

static void
meta_virtual_input_device_native_notify_key (ClutterVirtualInputDevice *virtual_device,
                                             uint64_t                   time_us,
                                             uint32_t                   key,
                                             ClutterKeyState            key_state)
{
  MetaVirtualInputDeviceNative *virtual_evdev =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  int key_count;

  g_return_if_fail (virtual_evdev->device != NULL);

  if (time_us == CLUTTER_CURRENT_TIME)
    time_us = g_get_monotonic_time ();

  if (get_button_type (key) != EVDEV_BUTTON_TYPE_KEY)
    {
      g_warning ("Unknown/invalid virtual device key 0x%x pressed\n", key);
      return;
    }

  key_count = update_button_count (virtual_evdev, key, key_state);
  if (key_count < 0 || key_count > 1)
    {
      g_warning ("Received multiple virtual 0x%x key %s (ignoring)", key,
                 key_state == CLUTTER_KEY_STATE_PRESSED ? "presses" : "releases");
      update_button_count (virtual_evdev, key, 1 - key_state);
      return;
    }

  meta_topic (META_DEBUG_INPUT,
              "Emitting virtual key-%s of key 0x%x (device %p)\n",
              key_state == CLUTTER_KEY_STATE_PRESSED ? "press" : "release",
              key, virtual_device);

  meta_seat_native_notify_key (virtual_evdev->seat,
                               virtual_evdev->device,
                               time_us,
                               key,
                               key_state,
                               TRUE);
}

static gboolean
pick_keycode_for_keyval_in_current_group (ClutterVirtualInputDevice *virtual_device,
                                          guint                      keyval,
                                          guint                     *keycode_out,
                                          guint                     *level_out)
{
  MetaVirtualInputDeviceNative *virtual_evdev =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  ClutterBackend *backend;
  ClutterKeymap *keymap;
  struct xkb_keymap *xkb_keymap;
  struct xkb_state  *state;
  guint keycode, layout;
  xkb_keycode_t min_keycode, max_keycode;

  backend = clutter_get_default_backend ();
  keymap = clutter_seat_get_keymap (clutter_backend_get_default_seat (backend));
  xkb_keymap = meta_keymap_native_get_keyboard_map (META_KEYMAP_NATIVE (keymap));
  state = virtual_evdev->seat->xkb;

  layout = xkb_state_serialize_layout (state, XKB_STATE_LAYOUT_EFFECTIVE);
  min_keycode = xkb_keymap_min_keycode (xkb_keymap);
  max_keycode = xkb_keymap_max_keycode (xkb_keymap);
  for (keycode = min_keycode; keycode < max_keycode; keycode++)
    {
      gint num_levels, level;
      num_levels = xkb_keymap_num_levels_for_key (xkb_keymap, keycode, layout);
      for (level = 0; level < num_levels; level++)
        {
          const xkb_keysym_t *syms;
          gint num_syms, sym;
          num_syms = xkb_keymap_key_get_syms_by_level (xkb_keymap, keycode, layout, level, &syms);
          for (sym = 0; sym < num_syms; sym++)
            {
              if (syms[sym] == keyval)
                {
                  *keycode_out = keycode;
                  if (level_out)
                    *level_out = level;
                  return TRUE;
                }
            }
        }
    }

  return FALSE;
}

static void
apply_level_modifiers (ClutterVirtualInputDevice *virtual_device,
                       uint64_t                   time_us,
                       uint32_t                   level,
                       uint32_t                   key_state)
{
  MetaVirtualInputDeviceNative *virtual_evdev =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  guint keysym, keycode, evcode;

  if (level == 0)
    return;

  if (level == 1)
    {
      keysym = XKB_KEY_Shift_L;
    }
  else if (level == 2)
    {
      keysym = XKB_KEY_ISO_Level3_Shift;
    }
  else
    {
      g_warning ("Unhandled level: %d\n", level);
      return;
    }

  if (!pick_keycode_for_keyval_in_current_group (virtual_device, keysym,
                                                 &keycode, NULL))
    return;

  clutter_input_device_keycode_to_evdev (virtual_evdev->device,
                                         keycode, &evcode);

  meta_topic (META_DEBUG_INPUT,
              "Emitting virtual key-%s of modifier key 0x%x (device %p)\n",
              key_state == CLUTTER_KEY_STATE_PRESSED ? "press" : "release",
              evcode, virtual_device);

  meta_seat_native_notify_key (virtual_evdev->seat,
                               virtual_evdev->device,
                               time_us,
                               evcode,
                               key_state,
                               TRUE);
}

static void
meta_virtual_input_device_native_notify_keyval (ClutterVirtualInputDevice *virtual_device,
                                                uint64_t                   time_us,
                                                uint32_t                   keyval,
                                                ClutterKeyState            key_state)
{
  MetaVirtualInputDeviceNative *virtual_evdev =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  int key_count;
  guint keycode = 0, level = 0, evcode = 0;

  g_return_if_fail (virtual_evdev->device != NULL);

  if (time_us == CLUTTER_CURRENT_TIME)
    time_us = g_get_monotonic_time ();

  if (!pick_keycode_for_keyval_in_current_group (virtual_device,
                                                 keyval, &keycode, &level))
    {
      g_warning ("No keycode found for keyval %x in current group", keyval);
      return;
    }

  clutter_input_device_keycode_to_evdev (virtual_evdev->device,
                                         keycode, &evcode);

  if (get_button_type (evcode) != EVDEV_BUTTON_TYPE_KEY)
    {
      g_warning ("Unknown/invalid virtual device key 0x%x pressed\n", evcode);
      return;
    }

  key_count = update_button_count (virtual_evdev, evcode, key_state);
  if (key_count < 0 || key_count > 1)
    {
      g_warning ("Received multiple virtual 0x%x key %s (ignoring)", evcode,
                 key_state == CLUTTER_KEY_STATE_PRESSED ? "presses" : "releases");
      update_button_count (virtual_evdev, evcode, 1 - key_state);
      return;
    }

  meta_topic (META_DEBUG_INPUT,
              "Emitting virtual key-%s of key 0x%x with modifier level %d, "
              "press count %d (device %p)\n",
              key_state == CLUTTER_KEY_STATE_PRESSED ? "press" : "release",
              evcode, level, key_count, virtual_device);

  if (key_state)
    apply_level_modifiers (virtual_device, time_us, level, key_state);

  meta_seat_native_notify_key (virtual_evdev->seat,
                               virtual_evdev->device,
                               time_us,
                               evcode,
                               key_state,
                               TRUE);

  if (!key_state)
    apply_level_modifiers (virtual_device, time_us, level, key_state);
}

static void
direction_to_discrete (ClutterScrollDirection direction,
                       double                *discrete_dx,
                       double                *discrete_dy)
{
  switch (direction)
    {
    case CLUTTER_SCROLL_UP:
      *discrete_dx = 0.0;
      *discrete_dy = -1.0;
      break;
    case CLUTTER_SCROLL_DOWN:
      *discrete_dx = 0.0;
      *discrete_dy = 1.0;
      break;
    case CLUTTER_SCROLL_LEFT:
      *discrete_dx = -1.0;
      *discrete_dy = 0.0;
      break;
    case CLUTTER_SCROLL_RIGHT:
      *discrete_dx = 1.0;
      *discrete_dy = 0.0;
      break;
    case CLUTTER_SCROLL_SMOOTH:
      g_assert_not_reached ();
      break;
    }
}

static void
meta_virtual_input_device_native_notify_discrete_scroll (ClutterVirtualInputDevice *virtual_device,
                                                         uint64_t                   time_us,
                                                         ClutterScrollDirection     direction,
                                                         ClutterScrollSource        scroll_source)
{
  MetaVirtualInputDeviceNative *virtual_evdev =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  double discrete_dx = 0.0, discrete_dy = 0.0;

  g_return_if_fail (virtual_evdev->device != NULL);

  if (time_us == CLUTTER_CURRENT_TIME)
    time_us = g_get_monotonic_time ();

  direction_to_discrete (direction, &discrete_dx, &discrete_dy);

  meta_seat_native_notify_discrete_scroll (virtual_evdev->seat,
                                           virtual_evdev->device,
                                           time_us,
                                           discrete_dx, discrete_dy,
                                           scroll_source);
}

static void
meta_virtual_input_device_native_notify_scroll_continuous (ClutterVirtualInputDevice *virtual_device,
                                                           uint64_t                   time_us,
                                                           double                     dx,
                                                           double                     dy,
                                                           ClutterScrollSource        scroll_source,
                                                           ClutterScrollFinishFlags   finish_flags)
{
  MetaVirtualInputDeviceNative *virtual_evdev =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);

  g_return_if_fail (virtual_evdev->device != NULL);

  if (time_us == CLUTTER_CURRENT_TIME)
    time_us = g_get_monotonic_time ();

  meta_seat_native_notify_scroll_continuous (virtual_evdev->seat,
                                             virtual_evdev->device,
                                             time_us,
                                             dx, dy,
                                             scroll_source,
                                             CLUTTER_SCROLL_FINISHED_NONE);
}

static void
meta_virtual_input_device_native_notify_touch_down (ClutterVirtualInputDevice *virtual_device,
                                                    uint64_t                   time_us,
                                                    int                        device_slot,
                                                    double                     x,
                                                    double                     y)
{
  MetaVirtualInputDeviceNative *virtual_evdev =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  MetaInputDeviceNative *device_evdev =
    META_INPUT_DEVICE_NATIVE (virtual_evdev->device);
  MetaTouchState *touch_state;

  g_return_if_fail (virtual_evdev->device != NULL);

  if (time_us == CLUTTER_CURRENT_TIME)
    time_us = g_get_monotonic_time ();

  touch_state = meta_input_device_native_acquire_touch_state (device_evdev,
                                                              device_slot);
  if (!touch_state)
    return;

  touch_state->coords.x = x;
  touch_state->coords.y = y;

  meta_seat_native_notify_touch_event (virtual_evdev->seat,
                                       virtual_evdev->device,
                                       CLUTTER_TOUCH_BEGIN,
                                       time_us,
                                       touch_state->seat_slot,
                                       touch_state->coords.x,
                                       touch_state->coords.y);
}

static void
meta_virtual_input_device_native_notify_touch_motion (ClutterVirtualInputDevice *virtual_device,
                                                      uint64_t                   time_us,
                                                      int                        device_slot,
                                                      double                     x,
                                                      double                     y)
{
  MetaVirtualInputDeviceNative *virtual_evdev =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  MetaInputDeviceNative *device_evdev =
    META_INPUT_DEVICE_NATIVE (virtual_evdev->device);
  MetaTouchState *touch_state;

  g_return_if_fail (virtual_evdev->device != NULL);

  if (time_us == CLUTTER_CURRENT_TIME)
    time_us = g_get_monotonic_time ();

  touch_state = meta_input_device_native_lookup_touch_state (device_evdev,
                                                             device_slot);
  if (!touch_state)
    return;

  touch_state->coords.x = x;
  touch_state->coords.y = y;

  meta_seat_native_notify_touch_event (virtual_evdev->seat,
                                       virtual_evdev->device,
                                       CLUTTER_TOUCH_BEGIN,
                                       time_us,
                                       touch_state->seat_slot,
                                       touch_state->coords.x,
                                       touch_state->coords.y);
}

static void
meta_virtual_input_device_native_notify_touch_up (ClutterVirtualInputDevice *virtual_device,
                                                  uint64_t                   time_us,
                                                  int                        device_slot)
{
  MetaVirtualInputDeviceNative *virtual_evdev =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (virtual_device);
  MetaInputDeviceNative *device_evdev =
    META_INPUT_DEVICE_NATIVE (virtual_evdev->device);
  MetaTouchState *touch_state;

  g_return_if_fail (virtual_evdev->device != NULL);

  if (time_us == CLUTTER_CURRENT_TIME)
    time_us = g_get_monotonic_time ();

  touch_state = meta_input_device_native_lookup_touch_state (device_evdev,
                                                             device_slot);
  if (!touch_state)
    return;

  meta_seat_native_notify_touch_event (virtual_evdev->seat,
                                       virtual_evdev->device,
                                       CLUTTER_TOUCH_BEGIN,
                                       time_us,
                                       touch_state->seat_slot,
                                       touch_state->coords.x,
                                       touch_state->coords.y);

  meta_input_device_native_release_touch_state (device_evdev, touch_state);
}

static void
meta_virtual_input_device_native_get_property (GObject    *object,
                                               guint       prop_id,
                                               GValue     *value,
                                               GParamSpec *pspec)
{
  MetaVirtualInputDeviceNative *virtual_evdev =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (object);

  switch (prop_id)
    {
    case PROP_SEAT:
      g_value_set_pointer (value, virtual_evdev->seat);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_virtual_input_device_native_set_property (GObject      *object,
                                               guint         prop_id,
                                               const GValue *value,
                                               GParamSpec   *pspec)
{
  MetaVirtualInputDeviceNative *virtual_evdev =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (object);

  switch (prop_id)
    {
    case PROP_SEAT:
      virtual_evdev->seat = g_value_get_pointer (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_virtual_input_device_native_constructed (GObject *object)
{
  ClutterVirtualInputDevice *virtual_device =
    CLUTTER_VIRTUAL_INPUT_DEVICE (object);
  MetaVirtualInputDeviceNative *virtual_evdev =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (object);
  ClutterInputDeviceType device_type;
  ClutterStage *stage;

  device_type = clutter_virtual_input_device_get_device_type (virtual_device);

  meta_topic (META_DEBUG_INPUT,
              "Creating new virtual input device of type %d (%p)\n",
              device_type, virtual_device);

  virtual_evdev->device =
    meta_input_device_native_new_virtual (virtual_evdev->seat,
                                          device_type,
                                          CLUTTER_INPUT_MODE_SLAVE);

  stage = meta_seat_native_get_stage (virtual_evdev->seat);
  _clutter_input_device_set_stage (virtual_evdev->device, stage);

  g_signal_emit_by_name (virtual_evdev->seat,
                         "device-added",
                         virtual_evdev->device);
}

static void
meta_virtual_input_device_native_dispose (GObject *object)
{
  ClutterVirtualInputDevice *virtual_device =
    CLUTTER_VIRTUAL_INPUT_DEVICE (object);
  MetaVirtualInputDeviceNative *virtual_evdev =
    META_VIRTUAL_INPUT_DEVICE_NATIVE (object);
  GObjectClass *object_class =
    G_OBJECT_CLASS (meta_virtual_input_device_native_parent_class);

  if (virtual_evdev->device)
    {
      release_pressed_buttons (virtual_device);
      g_signal_emit_by_name (virtual_evdev->seat,
                             "device-removed",
                             virtual_evdev->device);

      g_clear_object (&virtual_evdev->device);
    }

  object_class->dispose (object);
}

static void
meta_virtual_input_device_native_init (MetaVirtualInputDeviceNative *virtual_device_evdev)
{
}

static void
meta_virtual_input_device_native_class_init (MetaVirtualInputDeviceNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterVirtualInputDeviceClass *virtual_input_device_class =
    CLUTTER_VIRTUAL_INPUT_DEVICE_CLASS (klass);

  object_class->get_property = meta_virtual_input_device_native_get_property;
  object_class->set_property = meta_virtual_input_device_native_set_property;
  object_class->constructed = meta_virtual_input_device_native_constructed;
  object_class->dispose = meta_virtual_input_device_native_dispose;

  virtual_input_device_class->notify_relative_motion = meta_virtual_input_device_native_notify_relative_motion;
  virtual_input_device_class->notify_absolute_motion = meta_virtual_input_device_native_notify_absolute_motion;
  virtual_input_device_class->notify_button = meta_virtual_input_device_native_notify_button;
  virtual_input_device_class->notify_key = meta_virtual_input_device_native_notify_key;
  virtual_input_device_class->notify_keyval = meta_virtual_input_device_native_notify_keyval;
  virtual_input_device_class->notify_discrete_scroll = meta_virtual_input_device_native_notify_discrete_scroll;
  virtual_input_device_class->notify_scroll_continuous = meta_virtual_input_device_native_notify_scroll_continuous;
  virtual_input_device_class->notify_touch_down = meta_virtual_input_device_native_notify_touch_down;
  virtual_input_device_class->notify_touch_motion = meta_virtual_input_device_native_notify_touch_motion;
  virtual_input_device_class->notify_touch_up = meta_virtual_input_device_native_notify_touch_up;

  obj_props[PROP_SEAT] = g_param_spec_pointer ("seat",
                                               "Seat",
                                               "Seat",
                                               CLUTTER_PARAM_READWRITE |
                                               G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_properties (object_class, PROP_LAST, obj_props);
}
