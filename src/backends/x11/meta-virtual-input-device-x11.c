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

#define KEY_PRESSED (1 << 0)
#define KEYVAL_PRESSED (1 << 1)
#define KEYVAL_LEVEL_SHIFT 2

struct _MetaVirtualInputDeviceX11
{
  ClutterVirtualInputDevice parent;

  uint8_t key_state[256];
  uint8_t button_state[256];
};

G_DEFINE_TYPE (MetaVirtualInputDeviceX11,
               meta_virtual_input_device_x11,
               CLUTTER_TYPE_VIRTUAL_INPUT_DEVICE)

static void
fake_key_event (ClutterVirtualInputDevice *virtual_device,
                unsigned int              keycode,
                gboolean                  pressed)
{
  MetaVirtualInputDeviceX11 *virtual_x11 =
    META_VIRTUAL_INPUT_DEVICE_X11 (virtual_device);

  if (keycode < 8 || keycode >= G_N_ELEMENTS (virtual_x11->key_state))
    return;

  XTestFakeKeyEvent (clutter_x11_get_default_display (),
                     keycode, pressed, 0);

  if (pressed)
    virtual_x11->key_state[keycode] |= KEY_PRESSED;
  else
    virtual_x11->key_state[keycode] &= ~KEY_PRESSED;
}

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
  MetaVirtualInputDeviceX11 *virtual_x11 =
    META_VIRTUAL_INPUT_DEVICE_X11 (virtual_device);

  if (button == 0 || button >= G_N_ELEMENTS (virtual_x11->button_state))
    return;

  XTestFakeButtonEvent (clutter_x11_get_default_display (),
                        button, button_state == CLUTTER_BUTTON_STATE_PRESSED, 0);

  virtual_x11->button_state[button] =
    button_state == CLUTTER_BUTTON_STATE_PRESSED;
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
}

static void
meta_virtual_input_device_x11_notify_key (ClutterVirtualInputDevice *virtual_device,
                                          uint64_t                   time_us,
                                          uint32_t                   key,
                                          ClutterKeyState            key_state)
{
  fake_key_event (virtual_device,
                  key, key_state == CLUTTER_KEY_STATE_PRESSED);
}

static void
meta_virtual_input_device_x11_notify_keyval (ClutterVirtualInputDevice *virtual_device,
                                             uint64_t                   time_us,
                                             uint32_t                   keyval,
                                             ClutterKeyState            key_state)
{
  MetaVirtualInputDeviceX11 *virtual_x11 =
    META_VIRTUAL_INPUT_DEVICE_X11 (virtual_device);
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

  fake_key_event (virtual_device, (KeyCode) keycode,
                  key_state == CLUTTER_KEY_STATE_PRESSED);

  if (keycode < G_N_ELEMENTS (virtual_x11->key_state) &&
      key_state == CLUTTER_KEY_STATE_PRESSED)
    virtual_x11->key_state[keycode] =
      KEY_PRESSED | KEYVAL_PRESSED | (MIN (level, 3) << KEYVAL_LEVEL_SHIFT);


  if (key_state == CLUTTER_KEY_STATE_RELEASED)
    {
      if (!meta_keymap_x11_get_is_modifier (keymap, keycode))
        meta_keymap_x11_latch_modifiers (keymap, level, FALSE);
      meta_keymap_x11_release_keycode_if_needed (keymap, keycode);
      if (keycode < G_N_ELEMENTS (virtual_x11->key_state))
        virtual_x11->key_state[keycode] = 0;
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
meta_virtual_input_device_x11_dispose (GObject *object)
{
  MetaVirtualInputDeviceX11 *virtual_x11 =
    META_VIRTUAL_INPUT_DEVICE_X11 (object);
  MetaKeymapX11 *keymap = NULL;
  Display *display = clutter_x11_get_default_display ();
  unsigned int code;

  for (code = 1; code < G_N_ELEMENTS (virtual_x11->button_state); code++)
    {
      if (!virtual_x11->button_state[code])
        continue;

      XTestFakeButtonEvent (display, code, False, 0);
      virtual_x11->button_state[code] = 0;
    }

  for (code = 8; code < G_N_ELEMENTS (virtual_x11->key_state); code++)
    {
      uint8_t state = virtual_x11->key_state[code];

      if (!state)
        continue;

      if (state & KEY_PRESSED)
        XTestFakeKeyEvent (display, code, False, 0);

      if (state & KEYVAL_PRESSED)
        {
          if (!keymap)
            {
              ClutterBackend *backend = clutter_get_default_backend ();
              ClutterSeat *seat = clutter_backend_get_default_seat (backend);

              keymap = META_KEYMAP_X11 (clutter_seat_get_keymap (seat));
            }

          if (!meta_keymap_x11_get_is_modifier (keymap, code))
            meta_keymap_x11_latch_modifiers (keymap,
                                             state >> KEYVAL_LEVEL_SHIFT,
                                             FALSE);
          meta_keymap_x11_release_keycode_if_needed (keymap, code);
        }

      virtual_x11->key_state[code] = 0;
    }

  G_OBJECT_CLASS (meta_virtual_input_device_x11_parent_class)->dispose (object);
}

static void
meta_virtual_input_device_x11_init (MetaVirtualInputDeviceX11 *virtual_device_x11)
{
}

static void
meta_virtual_input_device_x11_class_init (MetaVirtualInputDeviceX11Class *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterVirtualInputDeviceClass *virtual_input_device_class =
    CLUTTER_VIRTUAL_INPUT_DEVICE_CLASS (klass);

  object_class->dispose = meta_virtual_input_device_x11_dispose;

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
