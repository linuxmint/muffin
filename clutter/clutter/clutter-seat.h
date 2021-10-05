/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2019 Red Hat Inc.
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
#ifndef CLUTTER_SEAT_H
#define CLUTTER_SEAT_H

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-types.h"
#include "clutter/clutter-keymap.h"
#include "clutter/clutter-virtual-input-device.h"

#define CLUTTER_TYPE_SEAT (clutter_seat_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterSeat, clutter_seat,
			  CLUTTER, SEAT, GObject)

/**
 * ClutterKbdA11ySettings:
 *
 * The #ClutterKbdA11ySettings structure contains keyboard accessibility
 * settings
 *
 */
typedef struct _ClutterKbdA11ySettings
{
  ClutterKeyboardA11yFlags controls;
  gint slowkeys_delay;
  gint debounce_delay;
  gint timeout_delay;
  gint mousekeys_init_delay;
  gint mousekeys_max_speed;
  gint mousekeys_accel_time;
} ClutterKbdA11ySettings;

/**
 * ClutterPointerA11ySettings:
 *
 * The #ClutterPointerA11ySettings structure contains pointer accessibility
 * settings
 *
 */
typedef struct _ClutterPointerA11ySettings
{
  ClutterPointerA11yFlags controls;
  ClutterPointerA11yDwellClickType dwell_click_type;
  ClutterPointerA11yDwellMode dwell_mode;
  ClutterPointerA11yDwellDirection dwell_gesture_single;
  ClutterPointerA11yDwellDirection dwell_gesture_double;
  ClutterPointerA11yDwellDirection dwell_gesture_drag;
  ClutterPointerA11yDwellDirection dwell_gesture_secondary;
  gint secondary_click_delay;
  gint dwell_delay;
  gint dwell_threshold;
} ClutterPointerA11ySettings;

/**
 * ClutterVirtualDeviceType:
 */
typedef enum
{
  CLUTTER_VIRTUAL_DEVICE_TYPE_NONE = 0,
  CLUTTER_VIRTUAL_DEVICE_TYPE_KEYBOARD = 1 << 0,
  CLUTTER_VIRTUAL_DEVICE_TYPE_POINTER = 1 << 1,
  CLUTTER_VIRTUAL_DEVICE_TYPE_TOUCHSCREEN = 1 << 2,
} ClutterVirtualDeviceType;

typedef struct _ClutterSeatClass ClutterSeatClass;

struct _ClutterSeatClass
{
  GObjectClass parent_class;

  ClutterInputDevice * (* get_pointer)  (ClutterSeat *seat);
  ClutterInputDevice * (* get_keyboard) (ClutterSeat *seat);

  GList * (* list_devices) (ClutterSeat *seat);

  void (* bell_notify) (ClutterSeat *seat);

  ClutterKeymap * (* get_keymap) (ClutterSeat *seat);

  void (* compress_motion) (ClutterSeat        *seat,
                            ClutterEvent       *event,
                            const ClutterEvent *to_discard);

  gboolean (* handle_device_event) (ClutterSeat  *seat,
                                    ClutterEvent *event);

  void (* warp_pointer) (ClutterSeat *seat,
                         int          x,
                         int          y);

  /* Event platform data */
  void (* copy_event_data) (ClutterSeat        *seat,
                            const ClutterEvent *src,
                            ClutterEvent       *dest);
  void (* free_event_data) (ClutterSeat        *seat,
                            ClutterEvent       *event);

  /* Keyboard accessibility */
  void (* apply_kbd_a11y_settings) (ClutterSeat            *seat,
                                    ClutterKbdA11ySettings *settings);

  /* Virtual devices */
  ClutterVirtualInputDevice * (* create_virtual_device) (ClutterSeat            *seat,
                                                         ClutterInputDeviceType  device_type);
  ClutterVirtualDeviceType (* get_supported_virtual_device_types) (ClutterSeat *seat);
};

CLUTTER_EXPORT
ClutterInputDevice * clutter_seat_get_pointer  (ClutterSeat *seat);
CLUTTER_EXPORT
ClutterInputDevice * clutter_seat_get_keyboard (ClutterSeat *seat);
CLUTTER_EXPORT
GList * clutter_seat_list_devices (ClutterSeat *seat);
CLUTTER_EXPORT
void clutter_seat_bell_notify (ClutterSeat *seat);

CLUTTER_EXPORT
ClutterKeymap * clutter_seat_get_keymap (ClutterSeat *seat);

CLUTTER_EXPORT
void clutter_seat_set_kbd_a11y_settings (ClutterSeat            *seat,
                                         ClutterKbdA11ySettings *settings);
CLUTTER_EXPORT
void clutter_seat_get_kbd_a11y_settings (ClutterSeat            *seat,
                                         ClutterKbdA11ySettings *settings);
CLUTTER_EXPORT
void clutter_seat_ensure_a11y_state     (ClutterSeat            *seat);

CLUTTER_EXPORT
void clutter_seat_set_pointer_a11y_settings (ClutterSeat                *seat,
                                             ClutterPointerA11ySettings *settings);

CLUTTER_EXPORT
void clutter_seat_get_pointer_a11y_settings (ClutterSeat                *seat,
                                             ClutterPointerA11ySettings *settings);

CLUTTER_EXPORT
void clutter_seat_set_pointer_a11y_dwell_click_type (ClutterSeat                      *seat,
                                                     ClutterPointerA11yDwellClickType  click_type);

CLUTTER_EXPORT
void clutter_seat_inhibit_unfocus (ClutterSeat *seat);

CLUTTER_EXPORT
void clutter_seat_uninhibit_unfocus (ClutterSeat *seat);

CLUTTER_EXPORT
gboolean clutter_seat_is_unfocus_inhibited (ClutterSeat *seat);

CLUTTER_EXPORT
ClutterVirtualInputDevice *clutter_seat_create_virtual_device (ClutterSeat            *seat,
                                                               ClutterInputDeviceType  device_type);

CLUTTER_EXPORT
ClutterVirtualDeviceType clutter_seat_get_supported_virtual_device_types (ClutterSeat *seat);

void clutter_seat_compress_motion (ClutterSeat        *seat,
                                   ClutterEvent       *event,
                                   const ClutterEvent *to_discard);

gboolean clutter_seat_handle_device_event (ClutterSeat  *seat,
                                           ClutterEvent *event);

CLUTTER_EXPORT
void clutter_seat_warp_pointer (ClutterSeat *seat,
                                int          x,
                                int          y);
CLUTTER_EXPORT
gboolean clutter_seat_get_touch_mode (ClutterSeat *seat);

#endif /* CLUTTER_SEAT_H */
