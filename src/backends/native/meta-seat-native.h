/*
 * Copyright (C) 2010  Intel Corp.
 * Copyright (C) 2014  Jonas Ådahl
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
 * Author: Damien Lespiau <damien.lespiau@intel.com>
 * Author: Jonas Ådahl <jadahl@gmail.com>
 */

#ifndef META_SEAT_NATIVE_H
#define META_SEAT_NATIVE_H

#include <gudev/gudev.h>
#include <libinput.h>
#include <linux/input-event-codes.h>

#include "backends/native/meta-keymap-native.h"
#include "backends/native/meta-xkb-utils.h"
#include "clutter/clutter.h"

typedef struct _MetaTouchState MetaTouchState;
typedef struct _MetaSeatNative MetaSeatNative;
typedef struct _MetaEventSource  MetaEventSource;

/**
 * MetaPointerConstrainCallback:
 * @device: the core pointer device
 * @time: the event time in milliseconds
 * @x: (inout): the new X coordinate
 * @y: (inout): the new Y coordinate
 * @user_data: user data passed to this function
 *
 * This callback will be called for all pointer motion events, and should
 * update (@x, @y) to constrain the pointer position appropriately.
 * The subsequent motion event will use the updated values as the new coordinates.
 * Note that the coordinates are not clamped to the stage size, and the callback
 * must make sure that this happens before it returns.
 * Also note that the event will be emitted even if the pointer is constrained
 * to be in the same position.
 */
typedef void (* MetaPointerConstrainCallback) (ClutterInputDevice *device,
                                               uint32_t            time,
                                               float               prev_x,
                                               float               prev_y,
                                               float              *x,
                                               float              *y,
                                               gpointer            user_data);
typedef void (* MetaRelativeMotionFilter) (ClutterInputDevice *device,
                                           float               x,
                                           float               y,
                                           float              *dx,
                                           float              *dy,
                                           gpointer            user_data);

struct _MetaTouchState
{
  MetaSeatNative *seat;

  int device_slot;
  int seat_slot;
  graphene_point_t coords;
};

struct _MetaSeatNative
{
  ClutterSeat parent_instance;

  char *seat_id;
  MetaEventSource *event_source;
  struct libinput *libinput;
  struct libinput_seat *libinput_seat;

  GSList *devices;

  ClutterInputDevice *core_pointer;
  ClutterInputDevice *core_keyboard;

  MetaTouchState **touch_states;
  int n_alloc_touch_states;

  struct xkb_state *xkb;
  xkb_led_index_t caps_lock_led;
  xkb_led_index_t num_lock_led;
  xkb_led_index_t scroll_lock_led;
  xkb_layout_index_t layout_idx;
  uint32_t button_state;
  int button_count[KEY_CNT];

  ClutterStage *stage;
  ClutterStageManager *stage_manager;
  gulong stage_added_handler;
  gulong stage_removed_handler;

  int device_id_next;
  GList *free_device_ids;

  MetaPointerConstrainCallback constrain_callback;
  gpointer constrain_data;
  GDestroyNotify constrain_data_notify;

  MetaRelativeMotionFilter relative_motion_filter;
  gpointer relative_motion_filter_user_data;

  GSList *event_filters;

  MetaKeymapNative *keymap;

  GUdevClient *udev_client;
  guint tablet_mode_switch_state : 1;
  guint has_touchscreen          : 1;
  guint has_tablet_switch        : 1;
  guint touch_mode               : 1;

  /* keyboard repeat */
  gboolean repeat;
  uint32_t repeat_delay;
  uint32_t repeat_interval;
  uint32_t repeat_key;
  uint32_t repeat_count;
  uint32_t repeat_timer;
  ClutterInputDevice *repeat_device;

  float pointer_x;
  float pointer_y;

  /* Emulation of discrete scroll events out of smooth ones */
  float accum_scroll_dx;
  float accum_scroll_dy;

  gboolean released;
};

#define META_TYPE_SEAT_NATIVE meta_seat_native_get_type ()
G_DECLARE_FINAL_TYPE (MetaSeatNative, meta_seat_native,
                      META, SEAT_NATIVE, ClutterSeat)

static inline uint64_t
us (uint64_t us)
{
  return us;
}

static inline uint64_t
ms2us (uint64_t ms)
{
  return us (ms * 1000);
}

static inline uint32_t
us2ms (uint64_t us)
{
  return (uint32_t) (us / 1000);
}

void meta_seat_native_notify_key (MetaSeatNative     *seat,
                                  ClutterInputDevice *device,
                                  uint64_t            time_us,
                                  uint32_t            key,
                                  uint32_t            state,
                                  gboolean            update_keys);

void meta_seat_native_notify_relative_motion (MetaSeatNative     *seat_evdev,
                                              ClutterInputDevice *input_device,
                                              uint64_t            time_us,
                                              float               dx,
                                              float               dy,
                                              float               dx_unaccel,
                                              float               dy_unaccel);

void meta_seat_native_notify_absolute_motion (MetaSeatNative     *seat_evdev,
                                              ClutterInputDevice *input_device,
                                              uint64_t            time_us,
                                              float               x,
                                              float               y,
                                              double             *axes);

void meta_seat_native_notify_button (MetaSeatNative     *seat,
                                     ClutterInputDevice *input_device,
                                     uint64_t            time_us,
                                     uint32_t            button,
                                     uint32_t            state);

void meta_seat_native_notify_scroll_continuous (MetaSeatNative           *seat,
                                                ClutterInputDevice       *input_device,
                                                uint64_t                  time_us,
                                                double                    dx,
                                                double                    dy,
                                                ClutterScrollSource       source,
                                                ClutterScrollFinishFlags  flags);

void meta_seat_native_notify_discrete_scroll (MetaSeatNative      *seat,
                                              ClutterInputDevice  *input_device,
                                              uint64_t             time_us,
                                              double               discrete_dx,
                                              double               discrete_dy,
                                              ClutterScrollSource  source);

void meta_seat_native_notify_touch_event (MetaSeatNative     *seat,
                                          ClutterInputDevice *input_device,
                                          ClutterEventType    evtype,
                                          uint64_t            time_us,
                                          int                 slot,
                                          double              x,
                                          double              y);

void meta_seat_native_set_libinput_seat (MetaSeatNative       *seat,
                                         struct libinput_seat *libinput_seat);

void meta_seat_native_sync_leds (MetaSeatNative *seat);

ClutterInputDevice * meta_seat_native_get_device (MetaSeatNative *seat,
                                                  int             id);

MetaTouchState * meta_seat_native_acquire_touch_state (MetaSeatNative *seat,
                                                       int             device_slot);

void meta_seat_native_release_touch_state (MetaSeatNative *seat,
                                           MetaTouchState *touch_state);

void meta_seat_native_set_stage (MetaSeatNative *seat,
                                 ClutterStage   *stage);
ClutterStage * meta_seat_native_get_stage (MetaSeatNative *seat);

void meta_seat_native_clear_repeat_timer (MetaSeatNative *seat);

gint meta_seat_native_acquire_device_id (MetaSeatNative     *seat);
void meta_seat_native_release_device_id (MetaSeatNative     *seat,
                                         ClutterInputDevice *device);

void meta_seat_native_update_xkb_state (MetaSeatNative *seat);

void meta_seat_native_constrain_pointer (MetaSeatNative     *seat,
                                         ClutterInputDevice *core_pointer,
                                         uint64_t            time_us,
                                         float               x,
                                         float               y,
                                         float              *new_x,
                                         float              *new_y);

void meta_seat_native_filter_relative_motion (MetaSeatNative     *seat,
                                              ClutterInputDevice *device,
                                              float               x,
                                              float               y,
                                              float              *dx,
                                              float              *dy);

void meta_seat_native_dispatch (MetaSeatNative *seat);

/**
 * MetaOpenDeviceCallback:
 * @path: the device path
 * @flags: flags to be passed to open
 *
 * This callback will be called when Clutter needs to access an input
 * device. It should return an open file descriptor for the file at @path,
 * or -1 if opening failed.
 */
typedef int (* MetaOpenDeviceCallback) (const char  *path,
                                        int          flags,
                                        gpointer     user_data,
                                        GError     **error);
typedef void (* MetaCloseDeviceCallback) (int          fd,
                                          gpointer     user_data);

void  meta_seat_native_set_device_callbacks (MetaOpenDeviceCallback  open_callback,
                                             MetaCloseDeviceCallback close_callback,
                                             gpointer                user_data);

void  meta_seat_native_release_devices (MetaSeatNative *seat);
void  meta_seat_native_reclaim_devices (MetaSeatNative *seat);

void  meta_seat_native_set_pointer_constrain_callback (MetaSeatNative               *seat,
                                                       MetaPointerConstrainCallback  callback,
                                                       gpointer                      user_data,
                                                       GDestroyNotify                user_data_notify);

void meta_seat_native_set_relative_motion_filter (MetaSeatNative           *seat,
                                                  MetaRelativeMotionFilter  filter,
                                                  gpointer                  user_data);

typedef gboolean (* MetaEvdevFilterFunc) (struct libinput_event *event,
                                          gpointer               data);

void meta_seat_native_add_filter    (MetaSeatNative        *seat,
                                     MetaEvdevFilterFunc    func,
                                     gpointer               data,
                                     GDestroyNotify         destroy_notify);
void meta_seat_native_remove_filter (MetaSeatNative        *seat,
                                     MetaEvdevFilterFunc    func,
                                     gpointer               data);

struct xkb_state * meta_seat_native_get_xkb_state (MetaSeatNative *seat);

void               meta_seat_native_set_keyboard_map   (MetaSeatNative    *seat,
                                                        struct xkb_keymap *keymap);

struct xkb_keymap * meta_seat_native_get_keyboard_map (MetaSeatNative *seat);

void meta_seat_native_set_keyboard_layout_index (MetaSeatNative     *seat,
                                                 xkb_layout_index_t  idx);

xkb_layout_index_t meta_seat_native_get_keyboard_layout_index (MetaSeatNative *seat);

void meta_seat_native_set_keyboard_numlock (MetaSeatNative *seat,
                                            gboolean        numlock_state);

void meta_seat_native_set_keyboard_repeat (MetaSeatNative *seat,
                                           gboolean        repeat,
                                           uint32_t        delay,
                                           uint32_t        interval);

#endif /* META_SEAT_NATIVE_H */
