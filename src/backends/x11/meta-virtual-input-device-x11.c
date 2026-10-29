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

#include <X11/extensions/XTest.h>

#include "clutter/clutter.h"
#include "clutter/x11/clutter-x11.h"
#include "meta-keymap-x11.h"
#include "meta-virtual-input-device-x11.h"

#define DISCRETE_SCROLL_STEP 10.0

struct _MetaVirtualInputDeviceX11
{
  ClutterVirtualInputDevice parent;

  double accum_scroll_dx;
  double accum_scroll_dy;
};

G_DEFINE_TYPE (MetaVirtualInputDeviceX11,
               meta_virtual_input_device_x11,
               CLUTTER_TYPE_VIRTUAL_INPUT_DEVICE)

static void
meta_virtual_input_device_x11_notify_relative_motion (ClutterVirtualInputDevice *virtual_device,
                                                      uint64_t                   time_us,
                                                      double                     dx,
                                                      double                     dy)
{
  XTestFakeRelativeMotionEvent (clutter_x11_get_default_display (),
                                (int) dx,
                                (int) dy,
                                0);
}

static void
meta_virtual_input_device_x11_notify_absolute_motion (ClutterVirtualInputDevice *virtual_device,
                                                      uint64_t                   time_us,
                                                      double                     x,
                                                      double                     y)
{
  XTestFakeMotionEvent (clutter_x11_get_default_display (),
                        clutter_x11_get_default_screen (),
                        (int) x,
                        (int) y,
                        0);
}

static void
meta_virtual_input_device_x11_notify_button (ClutterVirtualInputDevice *virtual_device,
                                             uint64_t                   time_us,
                                             uint32_t                   button,
                                             ClutterButtonState         button_state)
{
  XTestFakeButtonEvent (clutter_x11_get_default_display (),
                        button, button_state == CLUTTER_BUTTON_STATE_PRESSED, 0);
}

static void
meta_virtual_input_device_x11_notify_discrete_scroll (ClutterVirtualInputDevice *virtual_device,
                                                      uint64_t                   time_us,
                                                      ClutterScrollDirection     direction,
                                                      ClutterScrollSource        scroll_source)
{
  Display *xdisplay = clutter_x11_get_default_display ();
  int button;

  switch (direction)
    {
    case CLUTTER_SCROLL_UP:
      button = 4;
      break;
    case CLUTTER_SCROLL_DOWN:
      button = 5;
      break;
    case CLUTTER_SCROLL_LEFT:
      button = 6;
      break;
    case CLUTTER_SCROLL_RIGHT:
      button = 7;
      break;
    default:
      g_warn_if_reached ();
      return;
    }

  XTestFakeButtonEvent (xdisplay, button, True, 0);
  XTestFakeButtonEvent (xdisplay, button, False, 0);
}

static void
meta_virtual_input_device_x11_notify_scroll_continuous (ClutterVirtualInputDevice *virtual_device,
                                                        uint64_t                   time_us,
                                                        double                     dx,
                                                        double                     dy,
                                                        ClutterScrollSource        scroll_source,
                                                        ClutterScrollFinishFlags   finish_flags)
{
  MetaVirtualInputDeviceX11 *virtual_device_x11;
  ClutterScrollDirection direction;
  int i, n_xscrolls, n_yscrolls;

  virtual_device_x11 = META_VIRTUAL_INPUT_DEVICE_X11 (virtual_device);

  virtual_device_x11->accum_scroll_dx += dx;
  virtual_device_x11->accum_scroll_dy += dy;
  n_xscrolls = floor ((fabs (virtual_device_x11->accum_scroll_dx) + DBL_EPSILON) /
                      DISCRETE_SCROLL_STEP);
  n_yscrolls = floor ((fabs (virtual_device_x11->accum_scroll_dy) + DBL_EPSILON) /
                      DISCRETE_SCROLL_STEP);

  direction = virtual_device_x11->accum_scroll_dx > 0 ? CLUTTER_SCROLL_RIGHT
                                                      : CLUTTER_SCROLL_LEFT;
  for (i = 0; i < n_xscrolls; ++i)
    {
      meta_virtual_input_device_x11_notify_discrete_scroll (
        virtual_device, time_us, direction, CLUTTER_SCROLL_SOURCE_WHEEL);
    }

  direction = virtual_device_x11->accum_scroll_dy > 0 ? CLUTTER_SCROLL_DOWN
                                                      : CLUTTER_SCROLL_UP;
  for (i = 0; i < n_yscrolls; ++i)
    {
      meta_virtual_input_device_x11_notify_discrete_scroll (
        virtual_device, time_us, direction, CLUTTER_SCROLL_SOURCE_WHEEL);
    }

  virtual_device_x11->accum_scroll_dx =
    fmod (virtual_device_x11->accum_scroll_dx, DISCRETE_SCROLL_STEP);
  virtual_device_x11->accum_scroll_dy =
    fmod (virtual_device_x11->accum_scroll_dy, DISCRETE_SCROLL_STEP);
}

static void
meta_virtual_input_device_x11_notify_key (ClutterVirtualInputDevice *virtual_device,
                                          uint64_t                   time_us,
                                          uint32_t                   key,
                                          ClutterKeyState            key_state)
{
  XTestFakeKeyEvent (clutter_x11_get_default_display (),
                     key + 8, key_state == CLUTTER_KEY_STATE_PRESSED, 0);
}

static void
meta_virtual_input_device_x11_notify_keyval (ClutterVirtualInputDevice *virtual_device,
                                             uint64_t                   time_us,
                                             uint32_t                   keyval,
                                             ClutterKeyState            key_state)
{
  ClutterBackend *backend = clutter_get_default_backend ();
  ClutterSeat *seat = clutter_backend_get_default_seat (backend);
  MetaKeymapX11 *keymap = META_KEYMAP_X11 (clutter_seat_get_keymap (seat));
  uint32_t keycode, level;

  if (!meta_keymap_x11_keycode_for_keyval (keymap, keyval, &keycode, &level))
    {
      level = 0;

      if (!meta_keymap_x11_reserve_keycode (keymap, keyval, &keycode))
        {
          g_warning ("No keycode found for keyval %x in current group", keyval);
          return;
        }
    }

  if (!meta_keymap_x11_get_is_modifier (keymap, keycode) &&
      key_state == CLUTTER_KEY_STATE_PRESSED)
    meta_keymap_x11_latch_modifiers (keymap, level, TRUE);

  XTestFakeKeyEvent (clutter_x11_get_default_display (),
                     (KeyCode) keycode,
                     key_state == CLUTTER_KEY_STATE_PRESSED, 0);


  if (key_state == CLUTTER_KEY_STATE_RELEASED)
    {
      if (!meta_keymap_x11_get_is_modifier (keymap, keycode))
        meta_keymap_x11_latch_modifiers (keymap, level, FALSE);
      meta_keymap_x11_release_keycode_if_needed (keymap, keycode);
    }
}

static void
meta_virtual_input_device_x11_notify_touch_down (ClutterVirtualInputDevice *virtual_device,
                                                 uint64_t                   time_us,
                                                 int                        device_slot,
                                                 double                     x,
                                                 double                     y)
{
  g_warning ("Virtual touch motion not implemented under X11");
}

static void
meta_virtual_input_device_x11_notify_touch_motion (ClutterVirtualInputDevice *virtual_device,
                                                   uint64_t                   time_us,
                                                   int                        device_slot,
                                                   double                     x,
                                                   double                     y)
{
  g_warning ("Virtual touch motion not implemented under X11");
}

static void
meta_virtual_input_device_x11_notify_touch_up (ClutterVirtualInputDevice *virtual_device,
                                               uint64_t                   time_us,
                                               int                        device_slot)
{
  g_warning ("Virtual touch motion not implemented under X11");
}

static void
meta_virtual_input_device_x11_init (MetaVirtualInputDeviceX11 *virtual_device_x11)
{
}

static void
meta_virtual_input_device_x11_class_init (MetaVirtualInputDeviceX11Class *klass)
{
  ClutterVirtualInputDeviceClass *virtual_input_device_class =
    CLUTTER_VIRTUAL_INPUT_DEVICE_CLASS (klass);

  virtual_input_device_class->notify_relative_motion = meta_virtual_input_device_x11_notify_relative_motion;
  virtual_input_device_class->notify_absolute_motion = meta_virtual_input_device_x11_notify_absolute_motion;
  virtual_input_device_class->notify_button = meta_virtual_input_device_x11_notify_button;
  virtual_input_device_class->notify_discrete_scroll = meta_virtual_input_device_x11_notify_discrete_scroll;
  virtual_input_device_class->notify_scroll_continuous = meta_virtual_input_device_x11_notify_scroll_continuous;
  virtual_input_device_class->notify_key = meta_virtual_input_device_x11_notify_key;
  virtual_input_device_class->notify_keyval = meta_virtual_input_device_x11_notify_keyval;
  virtual_input_device_class->notify_touch_down = meta_virtual_input_device_x11_notify_touch_down;
  virtual_input_device_class->notify_touch_motion = meta_virtual_input_device_x11_notify_touch_motion;
  virtual_input_device_class->notify_touch_up = meta_virtual_input_device_x11_notify_touch_up;
}
