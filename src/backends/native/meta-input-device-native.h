/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010 Intel Corp.
 * Copyright (C) 2014 Jonas Ådahl
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
 * Author: Damien Lespiau <damien.lespiau@intel.com>
 * Author: Jonas Ådahl <jadahl@gmail.com>
 */

#ifndef META_INPUT_DEVICE_NATIVE_H
#define META_INPUT_DEVICE_NATIVE_H

#include <glib-object.h>

#include "backends/meta-input-device-private.h"
#include "backends/native/meta-seat-native.h"
#include "clutter/clutter-mutter.h"

#define META_TYPE_INPUT_DEVICE_NATIVE meta_input_device_native_get_type()

#define META_INPUT_DEVICE_NATIVE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  META_TYPE_INPUT_DEVICE_NATIVE, MetaInputDeviceNative))

#define META_INPUT_DEVICE_NATIVE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  META_TYPE_INPUT_DEVICE_NATIVE, MetaInputDeviceNativeClass))

#define META_IS_INPUT_DEVICE_NATIVE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  META_TYPE_INPUT_DEVICE_NATIVE))

#define META_IS_INPUT_DEVICE_NATIVE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  META_TYPE_INPUT_DEVICE_NATIVE))

#define META_INPUT_DEVICE_NATIVE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  META_TYPE_INPUT_DEVICE_NATIVE, MetaInputDeviceNativeClass))

typedef struct _MetaInputDeviceNative MetaInputDeviceNative;
typedef struct _MetaInputDeviceNativeClass MetaInputDeviceNativeClass;

struct _MetaInputDeviceNative
{
  ClutterInputDevice parent;

  struct libinput_device *libinput_device;
  MetaSeatNative *seat;
  ClutterInputDeviceTool *last_tool;

  cairo_matrix_t device_matrix;
  double device_aspect_ratio; /* w:h */
  double output_ratio;        /* w:h */

  GHashTable *touches;

  /* Keyboard a11y */
  ClutterKeyboardA11yFlags a11y_flags;
  GList *slow_keys_list;
  guint debounce_timer;
  uint16_t debounce_key;
  xkb_mod_mask_t stickykeys_depressed_mask;
  xkb_mod_mask_t stickykeys_latched_mask;
  xkb_mod_mask_t stickykeys_locked_mask;
  guint toggle_slowkeys_timer;
  uint16_t shift_count;
  uint32_t last_shift_time;
  int mousekeys_btn;
  gboolean mousekeys_btn_states[3];
  uint32_t mousekeys_first_motion_time; /* ms */
  uint32_t mousekeys_last_motion_time; /* ms */
  guint mousekeys_init_delay;
  guint mousekeys_accel_time;
  guint mousekeys_max_speed;
  double mousekeys_curve_factor;
  guint move_mousekeys_timer;
  uint16_t last_mousekeys_key;
};

struct _MetaInputDeviceNativeClass
{
  ClutterInputDeviceClass parent_class;
};


GType                     meta_input_device_native_get_type        (void) G_GNUC_CONST;

ClutterInputDevice *      meta_input_device_native_new             (MetaSeatNative          *seat,
                                                                    struct libinput_device  *libinput_device);

ClutterInputDevice *      meta_input_device_native_new_virtual     (MetaSeatNative          *seat,
                                                                    ClutterInputDeviceType   type,
                                                                    ClutterInputMode         mode);

MetaSeatNative *          meta_input_device_native_get_seat        (MetaInputDeviceNative   *device);

void                      meta_input_device_native_update_leds     (MetaInputDeviceNative   *device,
                                                                    enum libinput_led        leds);

ClutterInputDeviceType    meta_input_device_native_determine_type  (struct libinput_device  *libinput_device);


void                      meta_input_device_native_translate_coordinates (ClutterInputDevice *device,
                                                                          ClutterStage       *stage,
                                                                          float              *x,
                                                                          float              *y);

void                      meta_input_device_native_apply_kbd_a11y_settings (MetaInputDeviceNative  *device,
                                                                            ClutterKbdA11ySettings *settings);

MetaTouchState *          meta_input_device_native_acquire_touch_state (MetaInputDeviceNative *device,
                                                                        int                    device_slot);

MetaTouchState *          meta_input_device_native_lookup_touch_state (MetaInputDeviceNative *device,
                                                                       int                    device_slot);

void                      meta_input_device_native_release_touch_state (MetaInputDeviceNative *device,
                                                                        MetaTouchState        *touch_state);

void                      meta_input_device_native_release_touch_slots (MetaInputDeviceNative *device_evdev,
                                                                        uint64_t               time_us);

void                      meta_input_device_native_a11y_maybe_notify_toggle_keys  (MetaInputDeviceNative *device_evdev);

struct libinput_device * meta_input_device_native_get_libinput_device (ClutterInputDevice *device);

#endif /* META_INPUT_DEVICE_NATIVE_H */
