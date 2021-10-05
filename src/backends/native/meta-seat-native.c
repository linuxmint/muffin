/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
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

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <libinput.h>
#include <linux/input.h>
#include <math.h>

#include "backends/meta-cursor-tracker-private.h"
#include "backends/native/meta-seat-native.h"
#include "backends/native/meta-event-native.h"
#include "backends/native/meta-input-device-native.h"
#include "backends/native/meta-input-device-tool-native.h"
#include "backends/native/meta-keymap-native.h"
#include "backends/native/meta-virtual-input-device-native.h"
#include "clutter/clutter-mutter.h"
#include "core/bell.h"

/*
 * Clutter makes the assumption that two core devices have ID's 2 and 3 (core
 * pointer and core keyboard).
 *
 * Since the two first devices that will ever be created will be the virtual
 * pointer and virtual keyboard of the first seat, we fulfill the made
 * assumptions by having the first device having ID 2 and following 3.
 */
#define INITIAL_DEVICE_ID 2

/* Try to keep the pointer inside the stage. Hopefully no one is using
 * this backend with stages smaller than this. */
#define INITIAL_POINTER_X 16
#define INITIAL_POINTER_Y 16

#define AUTOREPEAT_VALUE 2

#define DISCRETE_SCROLL_STEP 10.0

#ifndef BTN_STYLUS3
#define BTN_STYLUS3 0x149 /* Linux 4.15 */
#endif

typedef struct _MetaEventFilter MetaEventFilter;

struct _MetaEventFilter
{
  MetaEvdevFilterFunc func;
  gpointer data;
  GDestroyNotify destroy_notify;
};

struct _MetaEventSource
{
  GSource source;

  MetaSeatNative *seat;
  GPollFD event_poll_fd;
};

static MetaOpenDeviceCallback  device_open_callback;
static MetaCloseDeviceCallback device_close_callback;
static gpointer                device_callback_data;

#ifdef CLUTTER_ENABLE_DEBUG
static const char *device_type_str[] = {
  "pointer",            /* CLUTTER_POINTER_DEVICE */
  "keyboard",           /* CLUTTER_KEYBOARD_DEVICE */
  "extension",          /* CLUTTER_EXTENSION_DEVICE */
  "joystick",           /* CLUTTER_JOYSTICK_DEVICE */
  "tablet",             /* CLUTTER_TABLET_DEVICE */
  "touchpad",           /* CLUTTER_TOUCHPAD_DEVICE */
  "touchscreen",        /* CLUTTER_TOUCHSCREEN_DEVICE */
  "pen",                /* CLUTTER_PEN_DEVICE */
  "eraser",             /* CLUTTER_ERASER_DEVICE */
  "cursor",             /* CLUTTER_CURSOR_DEVICE */
  "pad",                /* CLUTTER_PAD_DEVICE */
};
#endif /* CLUTTER_ENABLE_DEBUG */

enum
{
  PROP_0,
  PROP_SEAT_ID,
  N_PROPS,

  /* This property is overridden */
  PROP_TOUCH_MODE,
};

GParamSpec *props[N_PROPS] = { NULL };

G_DEFINE_TYPE (MetaSeatNative, meta_seat_native, CLUTTER_TYPE_SEAT)

static void process_events (MetaSeatNative *seat);

void
meta_seat_native_set_libinput_seat (MetaSeatNative       *seat,
                                    struct libinput_seat *libinput_seat)
{
  g_assert (seat->libinput_seat == NULL);

  libinput_seat_ref (libinput_seat);
  libinput_seat_set_user_data (libinput_seat, seat);
  seat->libinput_seat = libinput_seat;
}

void
meta_seat_native_sync_leds (MetaSeatNative *seat)
{
  GSList *iter;
  MetaInputDeviceNative *device_evdev;
  int caps_lock, num_lock, scroll_lock;
  enum libinput_led leds = 0;

  caps_lock = xkb_state_led_index_is_active (seat->xkb, seat->caps_lock_led);
  num_lock = xkb_state_led_index_is_active (seat->xkb, seat->num_lock_led);
  scroll_lock = xkb_state_led_index_is_active (seat->xkb, seat->scroll_lock_led);

  if (caps_lock)
    leds |= LIBINPUT_LED_CAPS_LOCK;
  if (num_lock)
    leds |= LIBINPUT_LED_NUM_LOCK;
  if (scroll_lock)
    leds |= LIBINPUT_LED_SCROLL_LOCK;

  for (iter = seat->devices; iter; iter = iter->next)
    {
      device_evdev = iter->data;
      meta_input_device_native_update_leds (device_evdev, leds);
    }
}

static void
clutter_touch_state_free (MetaTouchState *touch_state)
{
  g_slice_free (MetaTouchState, touch_state);
}

static void
ensure_seat_slot_allocated (MetaSeatNative *seat,
                            int             seat_slot)
{
  if (seat_slot >= seat->n_alloc_touch_states)
    {
      const int size_increase = 5;
      int i;

      seat->n_alloc_touch_states += size_increase;
      seat->touch_states = g_realloc_n (seat->touch_states,
                                        seat->n_alloc_touch_states,
                                        sizeof (MetaTouchState *));
      for (i = 0; i < size_increase; i++)
        seat->touch_states[seat->n_alloc_touch_states - (i + 1)] = NULL;
    }
}

MetaTouchState *
meta_seat_native_acquire_touch_state (MetaSeatNative *seat,
                                      int             device_slot)
{
  MetaTouchState *touch_state;
  int seat_slot;

  for (seat_slot = 0; seat_slot < seat->n_alloc_touch_states; seat_slot++)
    {
      if (!seat->touch_states[seat_slot])
        break;
    }

  ensure_seat_slot_allocated (seat, seat_slot);

  touch_state = g_slice_new0 (MetaTouchState);
  *touch_state = (MetaTouchState) {
    .seat = seat,
    .seat_slot = seat_slot,
    .device_slot = device_slot,
  };

  seat->touch_states[seat_slot] = touch_state;

  return touch_state;
}

void
meta_seat_native_release_touch_state (MetaSeatNative *seat,
                                      MetaTouchState *touch_state)
{
  g_clear_pointer (&seat->touch_states[touch_state->seat_slot],
                   clutter_touch_state_free);
}

void
meta_seat_native_clear_repeat_timer (MetaSeatNative *seat)
{
  if (seat->repeat_timer)
    {
      g_clear_handle_id (&seat->repeat_timer, g_source_remove);
      g_clear_object (&seat->repeat_device);
    }
}

static void
dispatch_libinput (MetaSeatNative *seat)
{
  libinput_dispatch (seat->libinput);
  process_events (seat);
}

static gboolean
keyboard_repeat (gpointer data)
{
  MetaSeatNative *seat = data;
  GSource *source;

  /* There might be events queued in libinput that could cancel the
     repeat timer. */
  dispatch_libinput (seat);
  if (!seat->repeat_timer)
    return G_SOURCE_REMOVE;

  g_return_val_if_fail (seat->repeat_device != NULL, G_SOURCE_REMOVE);
  source = g_main_context_find_source_by_id (NULL, seat->repeat_timer);

  meta_seat_native_notify_key (seat,
                               seat->repeat_device,
                               g_source_get_time (source),
                               seat->repeat_key,
                               AUTOREPEAT_VALUE,
                               FALSE);

  return G_SOURCE_CONTINUE;
}

static void
queue_event (ClutterEvent *event)
{
  _clutter_event_push (event, FALSE);
}

static int
update_button_count (MetaSeatNative *seat,
                     uint32_t        button,
                     uint32_t        state)
{
  if (state)
    {
      return ++seat->button_count[button];
    }
  else
    {
      /* Handle cases where we newer saw the initial pressed event. */
      if (seat->button_count[button] == 0)
        {
          meta_topic (META_DEBUG_INPUT,
                      "Counting release of key 0x%x and count is already 0\n",
                      button);
          return 0;
        }

      return --seat->button_count[button];
    }
}

void
meta_seat_native_notify_key (MetaSeatNative     *seat,
                             ClutterInputDevice *device,
                             uint64_t            time_us,
                             uint32_t            key,
                             uint32_t            state,
                             gboolean            update_keys)
{
  ClutterStage *stage;
  ClutterEvent *event = NULL;
  enum xkb_state_component changed_state;

  if (state != AUTOREPEAT_VALUE)
    {
      /* Drop any repeated button press (for example from virtual devices. */
      int count = update_button_count (seat, key, state);
      if ((state && count > 1) ||
          (!state && count != 0))
        {
          meta_topic (META_DEBUG_INPUT,
                      "Dropping repeated %s of key 0x%x, count %d, state %d\n",
                      state ? "press" : "release", key, count, state);
          return;
        }
    }

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (device);
  if (stage == NULL)
    {
      meta_seat_native_clear_repeat_timer (seat);
      return;
    }

  event = meta_key_event_new_from_evdev (device,
                                         seat->core_keyboard,
                                         stage,
                                         seat->xkb,
                                         seat->button_state,
                                         us2ms (time_us), key, state);
  meta_event_native_set_event_code (event, key);

  /* We must be careful and not pass multiple releases to xkb, otherwise it gets
     confused and locks the modifiers */
  if (state != AUTOREPEAT_VALUE)
    {
      changed_state = xkb_state_update_key (seat->xkb,
                                            event->key.hardware_keycode,
                                            state ? XKB_KEY_DOWN : XKB_KEY_UP);
    }
  else
    {
      changed_state = 0;
      clutter_event_set_flags (event, CLUTTER_EVENT_FLAG_REPEATED);
    }

  queue_event (event);

  if (update_keys && (changed_state & XKB_STATE_LEDS))
    {
      g_signal_emit_by_name (seat->keymap, "state-changed");
      meta_seat_native_sync_leds (seat);
      meta_input_device_native_a11y_maybe_notify_toggle_keys (META_INPUT_DEVICE_NATIVE (seat->core_keyboard));
    }

  if (state == 0 ||             /* key release */
      !seat->repeat ||
      !xkb_keymap_key_repeats (xkb_state_get_keymap (seat->xkb),
                               event->key.hardware_keycode))
    {
      meta_seat_native_clear_repeat_timer (seat);
      return;
    }

  if (state == 1)               /* key press */
    seat->repeat_count = 0;

  seat->repeat_count += 1;
  seat->repeat_key = key;

  switch (seat->repeat_count)
    {
    case 1:
    case 2:
      {
        uint32_t interval;

        meta_seat_native_clear_repeat_timer (seat);
        seat->repeat_device = g_object_ref (device);

        if (seat->repeat_count == 1)
          interval = seat->repeat_delay;
        else
          interval = seat->repeat_interval;

        seat->repeat_timer =
          clutter_threads_add_timeout_full (CLUTTER_PRIORITY_EVENTS,
                                            interval,
                                            keyboard_repeat,
                                            seat,
                                            NULL);
        return;
      }
    default:
      return;
    }
}

static ClutterEvent *
new_absolute_motion_event (MetaSeatNative     *seat,
                           ClutterInputDevice *input_device,
                           uint64_t            time_us,
                           float               x,
                           float               y,
                           double             *axes)
{
  ClutterStage *stage = _clutter_input_device_get_stage (input_device);
  ClutterEvent *event;

  event = clutter_event_new (CLUTTER_MOTION);

  if (clutter_input_device_get_device_type (input_device) != CLUTTER_TABLET_DEVICE)
    {
      meta_seat_native_constrain_pointer (seat,
                                          seat->core_pointer,
                                          time_us,
                                          seat->pointer_x,
                                          seat->pointer_y,
                                          &x, &y);
    }

  meta_event_native_set_time_usec (event, time_us);
  event->motion.time = us2ms (time_us);
  event->motion.stage = stage;
  meta_xkb_translate_state (event, seat->xkb, seat->button_state);
  event->motion.x = x;
  event->motion.y = y;
  meta_input_device_native_translate_coordinates (input_device, stage,
                                                  &event->motion.x,
                                                  &event->motion.y);
  event->motion.axes = axes;
  clutter_event_set_device (event, seat->core_pointer);
  clutter_event_set_source_device (event, input_device);

  if (clutter_input_device_get_device_type (input_device) == CLUTTER_TABLET_DEVICE)
    {
      MetaInputDeviceNative *device_evdev =
        META_INPUT_DEVICE_NATIVE (input_device);

      clutter_event_set_device_tool (event, device_evdev->last_tool);
      clutter_event_set_device (event, input_device);
    }
  else
    {
      clutter_event_set_device (event, seat->core_pointer);
    }

  _clutter_input_device_set_stage (seat->core_pointer, stage);

  if (clutter_input_device_get_device_type (input_device) != CLUTTER_TABLET_DEVICE)
    {
      seat->pointer_x = x;
      seat->pointer_y = y;
    }

  return event;
}

void
meta_seat_native_notify_relative_motion (MetaSeatNative     *seat,
                                         ClutterInputDevice *input_device,
                                         uint64_t            time_us,
                                         float               dx,
                                         float               dy,
                                         float               dx_unaccel,
                                         float               dy_unaccel)
{
  float new_x, new_y;
  ClutterEvent *event;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  if (!_clutter_input_device_get_stage (input_device))
    return;

  meta_seat_native_filter_relative_motion (seat,
                                           input_device,
                                           seat->pointer_x,
                                           seat->pointer_y,
                                           &dx,
                                           &dy);

  new_x = seat->pointer_x + dx;
  new_y = seat->pointer_y + dy;
  event = new_absolute_motion_event (seat, input_device,
                                     time_us, new_x, new_y, NULL);

  meta_event_native_set_relative_motion (event,
                                         dx, dy,
                                         dx_unaccel, dy_unaccel);

  queue_event (event);
}

void
meta_seat_native_notify_absolute_motion (MetaSeatNative     *seat,
                                         ClutterInputDevice *input_device,
                                         uint64_t            time_us,
                                         float               x,
                                         float               y,
                                         double             *axes)
{
  ClutterEvent *event;

  event = new_absolute_motion_event (seat, input_device, time_us, x, y, axes);

  queue_event (event);
}

void
meta_seat_native_notify_button (MetaSeatNative     *seat,
                                ClutterInputDevice *input_device,
                                uint64_t            time_us,
                                uint32_t            button,
                                uint32_t            state)
{
  MetaInputDeviceNative *device_evdev = (MetaInputDeviceNative *) input_device;
  ClutterStage *stage;
  ClutterEvent *event = NULL;
  int button_nr;
  static int maskmap[8] =
    {
      CLUTTER_BUTTON1_MASK, CLUTTER_BUTTON3_MASK, CLUTTER_BUTTON2_MASK,
      CLUTTER_BUTTON4_MASK, CLUTTER_BUTTON5_MASK, 0, 0, 0
    };
  int button_count;

  /* Drop any repeated button press (for example from virtual devices. */
  button_count = update_button_count (seat, button, state);
  if ((state && button_count > 1) ||
      (!state && button_count != 0))
    {
      meta_topic (META_DEBUG_INPUT,
                  "Dropping repeated %s of button 0x%x, count %d\n",
                  state ? "press" : "release", button, button_count);
      return;
    }

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  /* The evdev button numbers don't map sequentially to clutter button
   * numbers (the right and middle mouse buttons are in the opposite
   * order) so we'll map them directly with a switch statement */
  switch (button)
    {
    case BTN_LEFT:
    case BTN_TOUCH:
      button_nr = CLUTTER_BUTTON_PRIMARY;
      break;

    case BTN_RIGHT:
    case BTN_STYLUS:
      button_nr = CLUTTER_BUTTON_SECONDARY;
      break;

    case BTN_MIDDLE:
    case BTN_STYLUS2:
      button_nr = CLUTTER_BUTTON_MIDDLE;
      break;

    case 0x149: /* BTN_STYLUS3 */
      button_nr = 8;
      break;

    default:
      /* For compatibility reasons, all additional buttons go after the old 4-7 scroll ones */
      if (clutter_input_device_get_device_type (input_device) == CLUTTER_TABLET_DEVICE)
        button_nr = button - BTN_TOOL_PEN + 4;
      else
        button_nr = button - (BTN_LEFT - 1) + 4;
      break;
    }

  if (button_nr < 1 || button_nr > 12)
    {
      g_warning ("Unhandled button event 0x%x", button);
      return;
    }

  if (state)
    event = clutter_event_new (CLUTTER_BUTTON_PRESS);
  else
    event = clutter_event_new (CLUTTER_BUTTON_RELEASE);

  if (button_nr < G_N_ELEMENTS (maskmap))
    {
      /* Update the modifiers */
      if (state)
        seat->button_state |= maskmap[button_nr - 1];
      else
        seat->button_state &= ~maskmap[button_nr - 1];
    }

  meta_event_native_set_time_usec (event, time_us);
  event->button.time = us2ms (time_us);
  event->button.stage = CLUTTER_STAGE (stage);
  meta_xkb_translate_state (event, seat->xkb, seat->button_state);
  event->button.button = button_nr;

  if (clutter_input_device_get_device_type (input_device) == CLUTTER_TABLET_DEVICE)
    {
      graphene_point_t point;

      clutter_input_device_get_coords (input_device, NULL, &point);
      event->button.x = point.x;
      event->button.y = point.y;
    }
  else
    {
      event->button.x = seat->pointer_x;
      event->button.y = seat->pointer_y;
    }

  clutter_event_set_device (event, seat->core_pointer);
  clutter_event_set_source_device (event, input_device);

  if (device_evdev->last_tool)
    {
      /* Apply the button event code as per the tool mapping */
      uint32_t mapped_button;

      mapped_button = meta_input_device_tool_native_get_button_code (device_evdev->last_tool,
                                                                     button_nr);
      if (mapped_button != 0)
        button = mapped_button;
    }

  meta_event_native_set_event_code (event, button);

  if (clutter_input_device_get_device_type (input_device) == CLUTTER_TABLET_DEVICE)
    {
      clutter_event_set_device_tool (event, device_evdev->last_tool);
      clutter_event_set_device (event, input_device);
    }
  else
    {
      clutter_event_set_device (event, seat->core_pointer);
    }

  _clutter_input_device_set_stage (seat->core_pointer, stage);

  queue_event (event);
}

static void
notify_scroll (ClutterInputDevice       *input_device,
               uint64_t                  time_us,
               double                    dx,
               double                    dy,
               ClutterScrollSource       scroll_source,
               ClutterScrollFinishFlags  flags,
               gboolean                  emulated)
{
  MetaInputDeviceNative *device_evdev;
  MetaSeatNative *seat;
  ClutterStage *stage;
  ClutterEvent *event = NULL;
  double scroll_factor;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  device_evdev = META_INPUT_DEVICE_NATIVE (input_device);
  seat = meta_input_device_native_get_seat (device_evdev);

  event = clutter_event_new (CLUTTER_SCROLL);

  meta_event_native_set_time_usec (event, time_us);
  event->scroll.time = us2ms (time_us);
  event->scroll.stage = CLUTTER_STAGE (stage);
  meta_xkb_translate_state (event, seat->xkb, seat->button_state);

  /* libinput pointer axis events are in pointer motion coordinate space.
   * To convert to Xi2 discrete step coordinate space, multiply the factor
   * 1/10. */
  event->scroll.direction = CLUTTER_SCROLL_SMOOTH;
  scroll_factor = 1.0 / DISCRETE_SCROLL_STEP;
  clutter_event_set_scroll_delta (event,
                                  scroll_factor * dx,
                                  scroll_factor * dy);

  event->scroll.x = seat->pointer_x;
  event->scroll.y = seat->pointer_y;
  clutter_event_set_device (event, seat->core_pointer);
  clutter_event_set_source_device (event, input_device);
  event->scroll.scroll_source = scroll_source;
  event->scroll.finish_flags = flags;

  _clutter_event_set_pointer_emulated (event, emulated);

  queue_event (event);
}

static void
notify_discrete_scroll (ClutterInputDevice     *input_device,
                        uint64_t                time_us,
                        ClutterScrollDirection  direction,
                        ClutterScrollSource     scroll_source,
                        gboolean                emulated)
{
  MetaInputDeviceNative *device_evdev;
  MetaSeatNative *seat;
  ClutterStage *stage;
  ClutterEvent *event = NULL;

  if (direction == CLUTTER_SCROLL_SMOOTH)
    return;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  device_evdev = META_INPUT_DEVICE_NATIVE (input_device);
  seat = meta_input_device_native_get_seat (device_evdev);

  event = clutter_event_new (CLUTTER_SCROLL);

  meta_event_native_set_time_usec (event, time_us);
  event->scroll.time = us2ms (time_us);
  event->scroll.stage = CLUTTER_STAGE (stage);
  meta_xkb_translate_state (event, seat->xkb, seat->button_state);

  event->scroll.direction = direction;

  event->scroll.x = seat->pointer_x;
  event->scroll.y = seat->pointer_y;
  clutter_event_set_device (event, seat->core_pointer);
  clutter_event_set_source_device (event, input_device);
  event->scroll.scroll_source = scroll_source;

  _clutter_event_set_pointer_emulated (event, emulated);

  queue_event (event);
}

static void
check_notify_discrete_scroll (MetaSeatNative     *seat,
                              ClutterInputDevice *device,
                              uint64_t            time_us,
                              ClutterScrollSource scroll_source)
{
  int i, n_xscrolls, n_yscrolls;

  n_xscrolls = floor (fabs (seat->accum_scroll_dx) / DISCRETE_SCROLL_STEP);
  n_yscrolls = floor (fabs (seat->accum_scroll_dy) / DISCRETE_SCROLL_STEP);

  for (i = 0; i < n_xscrolls; i++)
    {
      notify_discrete_scroll (device, time_us,
                              seat->accum_scroll_dx > 0 ?
                              CLUTTER_SCROLL_RIGHT : CLUTTER_SCROLL_LEFT,
                              scroll_source, TRUE);
    }

  for (i = 0; i < n_yscrolls; i++)
    {
      notify_discrete_scroll (device, time_us,
                              seat->accum_scroll_dy > 0 ?
                              CLUTTER_SCROLL_DOWN : CLUTTER_SCROLL_UP,
                              scroll_source, TRUE);
    }

  seat->accum_scroll_dx = fmodf (seat->accum_scroll_dx, DISCRETE_SCROLL_STEP);
  seat->accum_scroll_dy = fmodf (seat->accum_scroll_dy, DISCRETE_SCROLL_STEP);
}

void
meta_seat_native_notify_scroll_continuous (MetaSeatNative           *seat,
                                           ClutterInputDevice       *input_device,
                                           uint64_t                  time_us,
                                           double                    dx,
                                           double                    dy,
                                           ClutterScrollSource       scroll_source,
                                           ClutterScrollFinishFlags  finish_flags)
{
  if (finish_flags & CLUTTER_SCROLL_FINISHED_HORIZONTAL)
    seat->accum_scroll_dx = 0;
  else
    seat->accum_scroll_dx += dx;

  if (finish_flags & CLUTTER_SCROLL_FINISHED_VERTICAL)
    seat->accum_scroll_dy = 0;
  else
    seat->accum_scroll_dy += dy;

  notify_scroll (input_device, time_us, dx, dy, scroll_source,
                 finish_flags, FALSE);
  check_notify_discrete_scroll (seat, input_device, time_us, scroll_source);
}

static ClutterScrollDirection
discrete_to_direction (double discrete_dx,
                       double discrete_dy)
{
  if (discrete_dx > 0)
    return CLUTTER_SCROLL_RIGHT;
  else if (discrete_dx < 0)
    return CLUTTER_SCROLL_LEFT;
  else if (discrete_dy > 0)
    return CLUTTER_SCROLL_DOWN;
  else if (discrete_dy < 0)
    return CLUTTER_SCROLL_UP;
  else
    g_assert_not_reached ();
  return 0;
}

void
meta_seat_native_notify_discrete_scroll (MetaSeatNative      *seat,
                                         ClutterInputDevice  *input_device,
                                         uint64_t             time_us,
                                         double               discrete_dx,
                                         double               discrete_dy,
                                         ClutterScrollSource  scroll_source)
{
  notify_scroll (input_device, time_us,
                 discrete_dx * DISCRETE_SCROLL_STEP,
                 discrete_dy * DISCRETE_SCROLL_STEP,
                 scroll_source, CLUTTER_SCROLL_FINISHED_NONE,
                 TRUE);
  notify_discrete_scroll (input_device, time_us,
                          discrete_to_direction (discrete_dx, discrete_dy),
                          scroll_source, FALSE);

}

void
meta_seat_native_notify_touch_event (MetaSeatNative     *seat,
                                     ClutterInputDevice *input_device,
                                     ClutterEventType    evtype,
                                     uint64_t            time_us,
                                     int                 slot,
                                     double              x,
                                     double              y)
{
  ClutterStage *stage;
  ClutterEvent *event = NULL;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  event = clutter_event_new (evtype);

  meta_event_native_set_time_usec (event, time_us);
  event->touch.time = us2ms (time_us);
  event->touch.stage = CLUTTER_STAGE (stage);
  event->touch.x = x;
  event->touch.y = y;
  meta_input_device_native_translate_coordinates (input_device, stage,
                                                  &event->touch.x,
                                                  &event->touch.y);

  /* "NULL" sequences are special cased in clutter */
  event->touch.sequence = GINT_TO_POINTER (MAX (1, slot + 1));
  meta_xkb_translate_state (event, seat->xkb, seat->button_state);

  if (evtype == CLUTTER_TOUCH_BEGIN ||
      evtype == CLUTTER_TOUCH_UPDATE)
    event->touch.modifier_state |= CLUTTER_BUTTON1_MASK;

  clutter_event_set_device (event, seat->core_pointer);
  clutter_event_set_source_device (event, input_device);

  queue_event (event);
}


/*
 * MetaEventSource for reading input devices
 */
static gboolean
meta_event_prepare (GSource *source,
                    gint    *timeout)
{
  gboolean retval;

  _clutter_threads_acquire_lock ();

  *timeout = -1;
  retval = clutter_events_pending ();

  _clutter_threads_release_lock ();

  return retval;
}

static gboolean
meta_event_check (GSource *source)
{
  MetaEventSource *event_source = (MetaEventSource *) source;
  gboolean retval;

  _clutter_threads_acquire_lock ();

  retval = ((event_source->event_poll_fd.revents & G_IO_IN) ||
            clutter_events_pending ());

  _clutter_threads_release_lock ();

  return retval;
}

void
meta_seat_native_constrain_pointer (MetaSeatNative     *seat,
                                    ClutterInputDevice *core_pointer,
                                    uint64_t            time_us,
                                    float               x,
                                    float               y,
                                    float              *new_x,
                                    float              *new_y)
{
  if (seat->constrain_callback)
    {
      seat->constrain_callback (core_pointer,
                                us2ms (time_us),
                                x, y,
                                new_x, new_y,
                                seat->constrain_data);
    }
  else
    {
      ClutterActor *stage = CLUTTER_ACTOR (meta_seat_native_get_stage (seat));
      float stage_width = clutter_actor_get_width (stage);
      float stage_height = clutter_actor_get_height (stage);

      *new_x = CLAMP (*new_x, 0.f, stage_width - 1);
      *new_y = CLAMP (*new_y, 0.f, stage_height - 1);
    }
}

void
meta_seat_native_filter_relative_motion (MetaSeatNative     *seat,
                                         ClutterInputDevice *device,
                                         float               x,
                                         float               y,
                                         float              *dx,
                                         float              *dy)
{
  if (!seat->relative_motion_filter)
    return;

  seat->relative_motion_filter (device, x, y, dx, dy,
                                seat->relative_motion_filter_user_data);
}

static void
notify_absolute_motion (ClutterInputDevice *input_device,
                        uint64_t            time_us,
                        float               x,
                        float               y,
                        double             *axes)
{
  MetaSeatNative *seat;
  ClutterEvent *event;

  seat = meta_input_device_native_get_seat (META_INPUT_DEVICE_NATIVE (input_device));
  event = new_absolute_motion_event (seat, input_device, time_us, x, y, axes);

  queue_event (event);
}

static void
notify_relative_tool_motion (ClutterInputDevice *input_device,
                             uint64_t            time_us,
                             float               dx,
                             float               dy,
                             double             *axes)
{
  MetaInputDeviceNative *device_evdev;
  ClutterEvent *event;
  MetaSeatNative *seat;
  gfloat x, y;

  device_evdev = META_INPUT_DEVICE_NATIVE (input_device);
  seat = meta_input_device_native_get_seat (device_evdev);
  x = input_device->current_x + dx;
  y = input_device->current_y + dy;

  meta_seat_native_filter_relative_motion (seat,
                                           input_device,
                                           seat->pointer_x,
                                           seat->pointer_y,
                                           &dx,
                                           &dy);

  event = new_absolute_motion_event (seat, input_device, time_us, x, y, axes);
  meta_event_native_set_relative_motion (event, dx, dy, 0, 0);

  queue_event (event);
}

static void
notify_pinch_gesture_event (ClutterInputDevice          *input_device,
                            ClutterTouchpadGesturePhase  phase,
                            uint64_t                     time_us,
                            double                       dx,
                            double                       dy,
                            double                       angle_delta,
                            double                       scale,
                            uint32_t                     n_fingers)
{
  MetaInputDeviceNative *device_evdev;
  MetaSeatNative *seat;
  ClutterStage *stage;
  ClutterEvent *event = NULL;
  graphene_point_t pos;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  device_evdev = META_INPUT_DEVICE_NATIVE (input_device);
  seat = meta_input_device_native_get_seat (device_evdev);

  event = clutter_event_new (CLUTTER_TOUCHPAD_PINCH);

  clutter_input_device_get_coords (seat->core_pointer, NULL, &pos);

  meta_event_native_set_time_usec (event, time_us);
  event->touchpad_pinch.phase = phase;
  event->touchpad_pinch.time = us2ms (time_us);
  event->touchpad_pinch.stage = CLUTTER_STAGE (stage);
  event->touchpad_pinch.x = pos.x;
  event->touchpad_pinch.y = pos.y;
  event->touchpad_pinch.dx = dx;
  event->touchpad_pinch.dy = dy;
  event->touchpad_pinch.angle_delta = angle_delta;
  event->touchpad_pinch.scale = scale;
  event->touchpad_pinch.n_fingers = n_fingers;

  meta_xkb_translate_state (event, seat->xkb, seat->button_state);

  clutter_event_set_device (event, seat->core_pointer);
  clutter_event_set_source_device (event, input_device);

  queue_event (event);
}

static void
notify_swipe_gesture_event (ClutterInputDevice          *input_device,
                            ClutterTouchpadGesturePhase  phase,
                            uint64_t                     time_us,
                            uint32_t                     n_fingers,
                            double                       dx,
                            double                       dy)
{
  MetaInputDeviceNative *device_evdev;
  MetaSeatNative *seat;
  ClutterStage *stage;
  ClutterEvent *event = NULL;
  graphene_point_t pos;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  device_evdev = META_INPUT_DEVICE_NATIVE (input_device);
  seat = meta_input_device_native_get_seat (device_evdev);

  event = clutter_event_new (CLUTTER_TOUCHPAD_SWIPE);

  meta_event_native_set_time_usec (event, time_us);
  event->touchpad_swipe.phase = phase;
  event->touchpad_swipe.time = us2ms (time_us);
  event->touchpad_swipe.stage = CLUTTER_STAGE (stage);

  clutter_input_device_get_coords (seat->core_pointer, NULL, &pos);
  event->touchpad_swipe.x = pos.x;
  event->touchpad_swipe.y = pos.y;
  event->touchpad_swipe.dx = dx;
  event->touchpad_swipe.dy = dy;
  event->touchpad_swipe.n_fingers = n_fingers;

  meta_xkb_translate_state (event, seat->xkb, seat->button_state);

  clutter_event_set_device (event, seat->core_pointer);
  clutter_event_set_source_device (event, input_device);

  queue_event (event);
}

static void
notify_proximity (ClutterInputDevice *input_device,
                  uint64_t            time_us,
                  gboolean            in)
{
  MetaInputDeviceNative *device_evdev;
  MetaSeatNative *seat;
  ClutterStage *stage;
  ClutterEvent *event = NULL;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  device_evdev = META_INPUT_DEVICE_NATIVE (input_device);
  seat = meta_input_device_native_get_seat (device_evdev);

  if (in)
    event = clutter_event_new (CLUTTER_PROXIMITY_IN);
  else
    event = clutter_event_new (CLUTTER_PROXIMITY_OUT);

  meta_event_native_set_time_usec (event, time_us);

  event->proximity.time = us2ms (time_us);
  event->proximity.stage = CLUTTER_STAGE (stage);
  clutter_event_set_device_tool (event, device_evdev->last_tool);
  clutter_event_set_device (event, seat->core_pointer);
  clutter_event_set_source_device (event, input_device);

  _clutter_input_device_set_stage (seat->core_pointer, stage);

  queue_event (event);
}

static void
notify_pad_button (ClutterInputDevice *input_device,
                   uint64_t            time_us,
                   uint32_t            button,
                   uint32_t            mode_group,
                   uint32_t            mode,
                   uint32_t            pressed)
{
  MetaInputDeviceNative *device_evdev;
  MetaSeatNative *seat;
  ClutterStage *stage;
  ClutterEvent *event;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  if (pressed)
    event = clutter_event_new (CLUTTER_PAD_BUTTON_PRESS);
  else
    event = clutter_event_new (CLUTTER_PAD_BUTTON_RELEASE);

  device_evdev = META_INPUT_DEVICE_NATIVE (input_device);
  seat = meta_input_device_native_get_seat (device_evdev);

  meta_event_native_set_time_usec (event, time_us);
  event->pad_button.stage = stage;
  event->pad_button.button = button;
  event->pad_button.group = mode_group;
  event->pad_button.mode = mode;
  clutter_event_set_device (event, input_device);
  clutter_event_set_source_device (event, input_device);
  clutter_event_set_time (event, us2ms (time_us));

  _clutter_input_device_set_stage (seat->core_pointer, stage);

  queue_event (event);
}

static void
notify_pad_strip (ClutterInputDevice *input_device,
                  uint64_t            time_us,
                  uint32_t            strip_number,
                  uint32_t            strip_source,
                  uint32_t            mode_group,
                  uint32_t            mode,
                  double              value)
{
  MetaInputDeviceNative *device_evdev;
  ClutterInputDevicePadSource source;
  MetaSeatNative *seat;
  ClutterStage *stage;
  ClutterEvent *event;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  if (strip_source == LIBINPUT_TABLET_PAD_STRIP_SOURCE_FINGER)
    source = CLUTTER_INPUT_DEVICE_PAD_SOURCE_FINGER;
  else
    source = CLUTTER_INPUT_DEVICE_PAD_SOURCE_UNKNOWN;

  device_evdev = META_INPUT_DEVICE_NATIVE (input_device);
  seat = meta_input_device_native_get_seat (device_evdev);

  event = clutter_event_new (CLUTTER_PAD_STRIP);
  meta_event_native_set_time_usec (event, time_us);
  event->pad_strip.strip_source = source;
  event->pad_strip.stage = stage;
  event->pad_strip.strip_number = strip_number;
  event->pad_strip.value = value;
  event->pad_strip.group = mode_group;
  event->pad_strip.mode = mode;
  clutter_event_set_device (event, input_device);
  clutter_event_set_source_device (event, input_device);
  clutter_event_set_time (event, us2ms (time_us));

  _clutter_input_device_set_stage (seat->core_pointer, stage);

  queue_event (event);
}

static void
notify_pad_ring (ClutterInputDevice *input_device,
                 uint64_t            time_us,
                 uint32_t            ring_number,
                 uint32_t            ring_source,
                 uint32_t            mode_group,
                 uint32_t            mode,
                 double              angle)
{
  MetaInputDeviceNative *device_evdev;
  ClutterInputDevicePadSource source;
  MetaSeatNative *seat;
  ClutterStage *stage;
  ClutterEvent *event;

  /* We can drop the event on the floor if no stage has been
   * associated with the device yet. */
  stage = _clutter_input_device_get_stage (input_device);
  if (stage == NULL)
    return;

  if (ring_source == LIBINPUT_TABLET_PAD_RING_SOURCE_FINGER)
    source = CLUTTER_INPUT_DEVICE_PAD_SOURCE_FINGER;
  else
    source = CLUTTER_INPUT_DEVICE_PAD_SOURCE_UNKNOWN;

  device_evdev = META_INPUT_DEVICE_NATIVE (input_device);
  seat = meta_input_device_native_get_seat (device_evdev);

  event = clutter_event_new (CLUTTER_PAD_RING);
  meta_event_native_set_time_usec (event, time_us);
  event->pad_ring.ring_source = source;
  event->pad_ring.stage = stage;
  event->pad_ring.ring_number = ring_number;
  event->pad_ring.angle = angle;
  event->pad_ring.group = mode_group;
  event->pad_ring.mode = mode;
  clutter_event_set_device (event, input_device);
  clutter_event_set_source_device (event, input_device);
  clutter_event_set_time (event, us2ms (time_us));

  _clutter_input_device_set_stage (seat->core_pointer, stage);

  queue_event (event);
}

static gboolean
meta_event_dispatch (GSource     *g_source,
                     GSourceFunc  callback,
                     gpointer     user_data)
{
  MetaEventSource *source = (MetaEventSource *) g_source;
  MetaSeatNative *seat;
  ClutterEvent *event;

  _clutter_threads_acquire_lock ();

  seat = source->seat;

  /* Don't queue more events if we haven't finished handling the previous batch
   */
  if (clutter_events_pending ())
    goto queue_event;

  dispatch_libinput (seat);

 queue_event:
  event = clutter_event_get ();

  if (event)
    {
      ClutterModifierType event_state;
      ClutterInputDevice *input_device =
        clutter_event_get_source_device (event);
      MetaInputDeviceNative *device_evdev =
        META_INPUT_DEVICE_NATIVE (input_device);
      MetaSeatNative *seat =
        meta_input_device_native_get_seat (device_evdev);

      /* Drop events if we don't have any stage to forward them to */
      if (!_clutter_input_device_get_stage (input_device))
        goto out;

      /* update the device states *before* the event */
      event_state = seat->button_state |
        xkb_state_serialize_mods (seat->xkb, XKB_STATE_MODS_EFFECTIVE);
      _clutter_input_device_set_state (seat->core_pointer, event_state);
      _clutter_input_device_set_state (seat->core_keyboard, event_state);

      /* forward the event into clutter for emission etc. */
      _clutter_stage_queue_event (event->any.stage, event, FALSE);
    }

out:
  _clutter_threads_release_lock ();

  return TRUE;
}
static GSourceFuncs event_funcs = {
  meta_event_prepare,
  meta_event_check,
  meta_event_dispatch,
  NULL
};

static MetaEventSource *
meta_event_source_new (MetaSeatNative *seat)
{
  GSource *source;
  MetaEventSource *event_source;
  gint fd;

  source = g_source_new (&event_funcs, sizeof (MetaEventSource));
  event_source = (MetaEventSource *) source;

  /* setup the source */
  event_source->seat = seat;

  fd = libinput_get_fd (seat->libinput);
  event_source->event_poll_fd.fd = fd;
  event_source->event_poll_fd.events = G_IO_IN;

  /* and finally configure and attach the GSource */
  g_source_set_priority (source, CLUTTER_PRIORITY_EVENTS);
  g_source_add_poll (source, &event_source->event_poll_fd);
  g_source_set_can_recurse (source, TRUE);
  g_source_attach (source, NULL);

  return event_source;
}

static void
meta_event_source_free (MetaEventSource *source)
{
  GSource *g_source = (GSource *) source;

  /* ignore the return value of close, it's not like we can do something
   * about it */
  close (source->event_poll_fd.fd);

  g_source_destroy (g_source);
  g_source_unref (g_source);
}

static gboolean
has_touchscreen (MetaSeatNative *seat)
{
  GSList *l;

  for (l = seat->devices; l; l = l->next)
    {
      ClutterInputDeviceType device_type;

      device_type = clutter_input_device_get_device_type (l->data);

      if (device_type == CLUTTER_TOUCHSCREEN_DEVICE)
        return TRUE;
    }

  return FALSE;
}

static void
update_touch_mode (MetaSeatNative *seat)
{
  gboolean touch_mode;

  /* No touch mode if we don't have a touchscreen, easy */
  if (!seat->has_touchscreen)
    touch_mode = FALSE;
  /* If we have a tablet mode switch, honor it being unset */
  else if (seat->has_tablet_switch && !seat->tablet_mode_switch_state)
    touch_mode = FALSE;
  /* If tablet mode is enabled, or if there is no tablet mode switch
   * (eg. kiosk machines), assume touch-mode.
   */
  else
    touch_mode = TRUE;

  if (seat->touch_mode != touch_mode)
    {
      seat->touch_mode = touch_mode;
      g_object_notify (G_OBJECT (seat), "touch-mode");
    }
}

static ClutterInputDevice *
evdev_add_device (MetaSeatNative         *seat,
                  struct libinput_device *libinput_device)
{
  ClutterInputDeviceType type;
  ClutterInputDevice *device, *master = NULL;
  ClutterActor *stage;

  device = meta_input_device_native_new (seat, libinput_device);
  stage = CLUTTER_ACTOR (meta_seat_native_get_stage (seat));
  _clutter_input_device_set_stage (device, CLUTTER_STAGE (stage));

  seat->devices = g_slist_prepend (seat->devices, device);

  /* Clutter assumes that device types are exclusive in the
   * ClutterInputDevice API */
  type = meta_input_device_native_determine_type (libinput_device);

  if (type == CLUTTER_KEYBOARD_DEVICE)
    master = seat->core_keyboard;
  else if (type == CLUTTER_POINTER_DEVICE)
    master = seat->core_pointer;

  if (master)
    {
      _clutter_input_device_set_associated_device (device, master);
      _clutter_input_device_add_slave (master, device);
    }

  return device;
}

static void
evdev_remove_device (MetaSeatNative        *seat,
                     MetaInputDeviceNative *device_evdev)
{
  ClutterInputDevice *device;

  device = CLUTTER_INPUT_DEVICE (device_evdev);
  seat->devices = g_slist_remove (seat->devices, device);

  g_object_unref (device);
}

static gboolean
meta_seat_native_handle_device_event (ClutterSeat  *seat,
                                      ClutterEvent *event)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (seat);
  ClutterInputDevice *device = event->device.device;
  MetaInputDeviceNative *device_native = META_INPUT_DEVICE_NATIVE (device);
  gboolean check_touch_mode;

  check_touch_mode =
    clutter_input_device_get_device_type (device) == CLUTTER_TOUCHSCREEN_DEVICE;

  switch (event->type)
    {
      case CLUTTER_DEVICE_ADDED:
        seat_native->has_touchscreen = check_touch_mode;

        if (libinput_device_has_capability (device_native->libinput_device,
                                            LIBINPUT_DEVICE_CAP_SWITCH) &&
            libinput_device_switch_has_switch (device_native->libinput_device,
                                               LIBINPUT_SWITCH_TABLET_MODE))
          {
            seat_native->has_tablet_switch = TRUE;
            check_touch_mode = TRUE;
          }
        break;

      case CLUTTER_DEVICE_REMOVED:
        if (check_touch_mode)
          seat_native->has_touchscreen = has_touchscreen (seat_native);

        if (seat_native->repeat_timer && seat_native->repeat_device == device)
          meta_seat_native_clear_repeat_timer (seat_native);
        break;

      default:
        break;
    }

  if (check_touch_mode)
    update_touch_mode (seat_native);

  return TRUE;
}

static gboolean
process_base_event (MetaSeatNative        *seat,
                    struct libinput_event *event)
{
  ClutterInputDevice *device = NULL;
  ClutterEvent *device_event;
  struct libinput_device *libinput_device;

  switch (libinput_event_get_type (event))
    {
    case LIBINPUT_EVENT_DEVICE_ADDED:
      libinput_device = libinput_event_get_device (event);

      device = evdev_add_device (seat, libinput_device);
      device_event = clutter_event_new (CLUTTER_DEVICE_ADDED);
      clutter_event_set_device (device_event, device);
      break;

    case LIBINPUT_EVENT_DEVICE_REMOVED:
      libinput_device = libinput_event_get_device (event);

      device = libinput_device_get_user_data (libinput_device);
      device_event = clutter_event_new (CLUTTER_DEVICE_REMOVED);
      clutter_event_set_device (device_event, device);
      evdev_remove_device (seat,
                           META_INPUT_DEVICE_NATIVE (device));
      break;

    default:
      device_event = NULL;
    }

  if (device_event)
    {
      device_event->device.stage = _clutter_input_device_get_stage (device);
      queue_event (device_event);
      return TRUE;
    }

  return FALSE;
}

static ClutterScrollSource
translate_scroll_source (enum libinput_pointer_axis_source source)
{
  switch (source)
    {
    case LIBINPUT_POINTER_AXIS_SOURCE_WHEEL:
      return CLUTTER_SCROLL_SOURCE_WHEEL;
    case LIBINPUT_POINTER_AXIS_SOURCE_FINGER:
      return CLUTTER_SCROLL_SOURCE_FINGER;
    case LIBINPUT_POINTER_AXIS_SOURCE_CONTINUOUS:
      return CLUTTER_SCROLL_SOURCE_CONTINUOUS;
    default:
      return CLUTTER_SCROLL_SOURCE_UNKNOWN;
    }
}

static ClutterInputDeviceToolType
translate_tool_type (struct libinput_tablet_tool *libinput_tool)
{
  enum libinput_tablet_tool_type tool;

  tool = libinput_tablet_tool_get_type (libinput_tool);

  switch (tool)
    {
    case LIBINPUT_TABLET_TOOL_TYPE_PEN:
      return CLUTTER_INPUT_DEVICE_TOOL_PEN;
    case LIBINPUT_TABLET_TOOL_TYPE_ERASER:
      return CLUTTER_INPUT_DEVICE_TOOL_ERASER;
    case LIBINPUT_TABLET_TOOL_TYPE_BRUSH:
      return CLUTTER_INPUT_DEVICE_TOOL_BRUSH;
    case LIBINPUT_TABLET_TOOL_TYPE_PENCIL:
      return CLUTTER_INPUT_DEVICE_TOOL_PENCIL;
    case LIBINPUT_TABLET_TOOL_TYPE_AIRBRUSH:
      return CLUTTER_INPUT_DEVICE_TOOL_AIRBRUSH;
    case LIBINPUT_TABLET_TOOL_TYPE_MOUSE:
      return CLUTTER_INPUT_DEVICE_TOOL_MOUSE;
    case LIBINPUT_TABLET_TOOL_TYPE_LENS:
      return CLUTTER_INPUT_DEVICE_TOOL_LENS;
    default:
      return CLUTTER_INPUT_DEVICE_TOOL_NONE;
    }
}

static void
input_device_update_tool (ClutterInputDevice          *input_device,
                          struct libinput_tablet_tool *libinput_tool)
{
  MetaInputDeviceNative *evdev_device = META_INPUT_DEVICE_NATIVE (input_device);
  MetaSeatNative *seat = meta_input_device_native_get_seat (evdev_device);
  ClutterInputDeviceTool *tool = NULL;
  ClutterInputDeviceToolType tool_type;
  uint64_t tool_serial;

  if (libinput_tool)
    {
      tool_serial = libinput_tablet_tool_get_serial (libinput_tool);
      tool_type = translate_tool_type (libinput_tool);
      tool = clutter_input_device_lookup_tool (input_device,
                                               tool_serial, tool_type);

      if (!tool)
        {
          tool = meta_input_device_tool_native_new (libinput_tool,
                                                    tool_serial, tool_type);
          clutter_input_device_add_tool (input_device, tool);
        }
    }

  if (evdev_device->last_tool != tool)
    {
      evdev_device->last_tool = tool;
      g_signal_emit_by_name (seat, "tool-changed", input_device, tool);
    }
}

static gdouble *
translate_tablet_axes (struct libinput_event_tablet_tool *tablet_event,
                       ClutterInputDeviceTool            *tool)
{
  GArray *axes = g_array_new (FALSE, FALSE, sizeof (gdouble));
  struct libinput_tablet_tool *libinput_tool;
  gdouble value;

  libinput_tool = libinput_event_tablet_tool_get_tool (tablet_event);

  value = libinput_event_tablet_tool_get_x (tablet_event);
  g_array_append_val (axes, value);
  value = libinput_event_tablet_tool_get_y (tablet_event);
  g_array_append_val (axes, value);

  if (libinput_tablet_tool_has_distance (libinput_tool))
    {
      value = libinput_event_tablet_tool_get_distance (tablet_event);
      g_array_append_val (axes, value);
    }

  if (libinput_tablet_tool_has_pressure (libinput_tool))
    {
      value = libinput_event_tablet_tool_get_pressure (tablet_event);
      value = meta_input_device_tool_native_translate_pressure (tool, value);
      g_array_append_val (axes, value);
    }

  if (libinput_tablet_tool_has_tilt (libinput_tool))
    {
      value = libinput_event_tablet_tool_get_tilt_x (tablet_event);
      g_array_append_val (axes, value);
      value = libinput_event_tablet_tool_get_tilt_y (tablet_event);
      g_array_append_val (axes, value);
    }

  if (libinput_tablet_tool_has_rotation (libinput_tool))
    {
      value = libinput_event_tablet_tool_get_rotation (tablet_event);
      g_array_append_val (axes, value);
    }

  if (libinput_tablet_tool_has_slider (libinput_tool))
    {
      value = libinput_event_tablet_tool_get_slider_position (tablet_event);
      g_array_append_val (axes, value);
    }

  if (libinput_tablet_tool_has_wheel (libinput_tool))
    {
      value = libinput_event_tablet_tool_get_wheel_delta (tablet_event);
      g_array_append_val (axes, value);
    }

  if (axes->len == 0)
    {
      g_array_free (axes, TRUE);
      return NULL;
    }
  else
    return (gdouble *) g_array_free (axes, FALSE);
}

static MetaSeatNative *
seat_from_device (ClutterInputDevice *device)
{
  MetaInputDeviceNative *device_evdev = META_INPUT_DEVICE_NATIVE (device);

  return meta_input_device_native_get_seat (device_evdev);
}

static void
notify_continuous_axis (MetaSeatNative                *seat,
                        ClutterInputDevice            *device,
                        uint64_t                       time_us,
                        ClutterScrollSource            scroll_source,
                        struct libinput_event_pointer *axis_event)
{
  gdouble dx = 0.0, dy = 0.0;
  ClutterScrollFinishFlags finish_flags = CLUTTER_SCROLL_FINISHED_NONE;

  if (libinput_event_pointer_has_axis (axis_event,
                                       LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL))
    {
      dx = libinput_event_pointer_get_axis_value (
          axis_event, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);

      if (fabs (dx) < DBL_EPSILON)
        finish_flags |= CLUTTER_SCROLL_FINISHED_HORIZONTAL;
    }
  if (libinput_event_pointer_has_axis (axis_event,
                                       LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL))
    {
      dy = libinput_event_pointer_get_axis_value (
          axis_event, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);

      if (fabs (dy) < DBL_EPSILON)
        finish_flags |= CLUTTER_SCROLL_FINISHED_VERTICAL;
    }

  meta_seat_native_notify_scroll_continuous (seat, device, time_us,
                                             dx, dy,
                                             scroll_source, finish_flags);
}

static void
notify_discrete_axis (MetaSeatNative                *seat,
                      ClutterInputDevice            *device,
                      uint64_t                       time_us,
                      ClutterScrollSource            scroll_source,
                      struct libinput_event_pointer *axis_event)
{
  gdouble discrete_dx = 0.0, discrete_dy = 0.0;

  if (libinput_event_pointer_has_axis (axis_event,
                                       LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL))
    {
      discrete_dx = libinput_event_pointer_get_axis_value_discrete (
          axis_event, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
    }
  if (libinput_event_pointer_has_axis (axis_event,
                                       LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL))
    {
      discrete_dy = libinput_event_pointer_get_axis_value_discrete (
          axis_event, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
    }

  meta_seat_native_notify_discrete_scroll (seat, device,
                                           time_us,
                                           discrete_dx, discrete_dy,
                                           scroll_source);
}

static void
process_tablet_axis (MetaSeatNative        *seat,
                     struct libinput_event *event)
{
  struct libinput_device *libinput_device = libinput_event_get_device (event);
  uint64_t time;
  double x, y, dx, dy, *axes;
  float stage_width, stage_height;
  ClutterStage *stage;
  ClutterInputDevice *device;
  struct libinput_event_tablet_tool *tablet_event =
    libinput_event_get_tablet_tool_event (event);
  MetaInputDeviceNative *evdev_device;

  device = libinput_device_get_user_data (libinput_device);
  evdev_device = META_INPUT_DEVICE_NATIVE (device);

  stage = _clutter_input_device_get_stage (device);
  if (!stage)
    return;

  axes = translate_tablet_axes (tablet_event,
                                evdev_device->last_tool);
  if (!axes)
    return;

  stage_width = clutter_actor_get_width (CLUTTER_ACTOR (stage));
  stage_height = clutter_actor_get_height (CLUTTER_ACTOR (stage));

  time = libinput_event_tablet_tool_get_time_usec (tablet_event);

  if (clutter_input_device_get_mapping_mode (device) == CLUTTER_INPUT_DEVICE_MAPPING_RELATIVE ||
      clutter_input_device_tool_get_tool_type (evdev_device->last_tool) == CLUTTER_INPUT_DEVICE_TOOL_MOUSE ||
      clutter_input_device_tool_get_tool_type (evdev_device->last_tool) == CLUTTER_INPUT_DEVICE_TOOL_LENS)
    {
      dx = libinput_event_tablet_tool_get_dx (tablet_event);
      dy = libinput_event_tablet_tool_get_dy (tablet_event);
      notify_relative_tool_motion (device, time, dx, dy, axes);
    }
  else
    {
      x = libinput_event_tablet_tool_get_x_transformed (tablet_event, stage_width);
      y = libinput_event_tablet_tool_get_y_transformed (tablet_event, stage_height);
      notify_absolute_motion (device, time, x, y, axes);
    }
}

static gboolean
process_device_event (MetaSeatNative        *seat,
                      struct libinput_event *event)
{
  gboolean handled = TRUE;
  struct libinput_device *libinput_device = libinput_event_get_device(event);
  ClutterInputDevice *device;
  MetaInputDeviceNative *device_evdev;

  switch (libinput_event_get_type (event))
    {
    case LIBINPUT_EVENT_KEYBOARD_KEY:
      {
        uint32_t key, key_state, seat_key_count;
        uint64_t time_us;
        struct libinput_event_keyboard *key_event =
          libinput_event_get_keyboard_event (event);

        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_keyboard_get_time_usec (key_event);
        key = libinput_event_keyboard_get_key (key_event);
        key_state = libinput_event_keyboard_get_key_state (key_event) ==
                    LIBINPUT_KEY_STATE_PRESSED;
        seat_key_count =
          libinput_event_keyboard_get_seat_key_count (key_event);

        /* Ignore key events that are not seat wide state changes. */
        if ((key_state == LIBINPUT_KEY_STATE_PRESSED &&
             seat_key_count != 1) ||
            (key_state == LIBINPUT_KEY_STATE_RELEASED &&
             seat_key_count != 0))
          {
            meta_topic (META_DEBUG_INPUT,
                        "Dropping key-%s of key 0x%x because seat-wide "
                        "key count is %d\n",
                        key_state == LIBINPUT_KEY_STATE_PRESSED ? "press" : "release",
                        key, seat_key_count);
            break;
          }

        meta_seat_native_notify_key (seat_from_device (device),
                                     device,
                                     time_us, key, key_state, TRUE);

        break;
      }

    case LIBINPUT_EVENT_POINTER_MOTION:
      {
        struct libinput_event_pointer *pointer_event =
          libinput_event_get_pointer_event (event);
        uint64_t time_us;
        double dx;
        double dy;
        double dx_unaccel;
        double dy_unaccel;

        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_pointer_get_time_usec (pointer_event);
        dx = libinput_event_pointer_get_dx (pointer_event);
        dy = libinput_event_pointer_get_dy (pointer_event);
        dx_unaccel = libinput_event_pointer_get_dx_unaccelerated (pointer_event);
        dy_unaccel = libinput_event_pointer_get_dy_unaccelerated (pointer_event);

        meta_seat_native_notify_relative_motion (seat_from_device (device),
                                                 device,
                                                 time_us,
                                                 dx, dy,
                                                 dx_unaccel, dy_unaccel);

        break;
      }

    case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
      {
        uint64_t time_us;
        double x, y;
        float stage_width, stage_height;
        ClutterStage *stage;
        struct libinput_event_pointer *motion_event =
          libinput_event_get_pointer_event (event);
        device = libinput_device_get_user_data (libinput_device);

        stage = _clutter_input_device_get_stage (device);
        if (stage == NULL)
          break;

        stage_width = clutter_actor_get_width (CLUTTER_ACTOR (stage));
        stage_height = clutter_actor_get_height (CLUTTER_ACTOR (stage));

        time_us = libinput_event_pointer_get_time_usec (motion_event);
        x = libinput_event_pointer_get_absolute_x_transformed (motion_event,
                                                               stage_width);
        y = libinput_event_pointer_get_absolute_y_transformed (motion_event,
                                                               stage_height);

        meta_seat_native_notify_absolute_motion (seat_from_device (device),
                                                 device,
                                                 time_us,
                                                 x, y,
                                                 NULL);

        break;
      }

    case LIBINPUT_EVENT_POINTER_BUTTON:
      {
        uint32_t button, button_state, seat_button_count;
        uint64_t time_us;
        struct libinput_event_pointer *button_event =
          libinput_event_get_pointer_event (event);
        device = libinput_device_get_user_data (libinput_device);

        time_us = libinput_event_pointer_get_time_usec (button_event);
        button = libinput_event_pointer_get_button (button_event);
        button_state = libinput_event_pointer_get_button_state (button_event) ==
                       LIBINPUT_BUTTON_STATE_PRESSED;
        seat_button_count =
          libinput_event_pointer_get_seat_button_count (button_event);

        /* Ignore button events that are not seat wide state changes. */
        if ((button_state == LIBINPUT_BUTTON_STATE_PRESSED &&
             seat_button_count != 1) ||
            (button_state == LIBINPUT_BUTTON_STATE_RELEASED &&
             seat_button_count != 0))
          {
            meta_topic (META_DEBUG_INPUT,
                        "Dropping button-%s of button 0x%x because seat-wide "
                        "button count is %d\n",
                        button_state == LIBINPUT_BUTTON_STATE_PRESSED ? "press" : "release",
                        button, seat_button_count);
            break;
          }

        meta_seat_native_notify_button (seat_from_device (device), device,
                                        time_us, button, button_state);
        break;
      }

    case LIBINPUT_EVENT_POINTER_AXIS:
      {
        uint64_t time_us;
        enum libinput_pointer_axis_source source;
        struct libinput_event_pointer *axis_event =
          libinput_event_get_pointer_event (event);
        MetaSeatNative *seat;
        ClutterScrollSource scroll_source;

        device = libinput_device_get_user_data (libinput_device);
        seat = meta_input_device_native_get_seat (META_INPUT_DEVICE_NATIVE (device));

        time_us = libinput_event_pointer_get_time_usec (axis_event);
        source = libinput_event_pointer_get_axis_source (axis_event);
        scroll_source = translate_scroll_source (source);

        /* libinput < 0.8 sent wheel click events with value 10. Since 0.8
           the value is the angle of the click in degrees. To keep
           backwards-compat with existing clients, we just send multiples of
           the click count. */

        switch (scroll_source)
          {
          case CLUTTER_SCROLL_SOURCE_WHEEL:
            notify_discrete_axis (seat, device, time_us, scroll_source,
                                  axis_event);
            break;
          case CLUTTER_SCROLL_SOURCE_FINGER:
          case CLUTTER_SCROLL_SOURCE_CONTINUOUS:
          case CLUTTER_SCROLL_SOURCE_UNKNOWN:
            notify_continuous_axis (seat, device, time_us, scroll_source,
                                    axis_event);
            break;
          }
        break;
      }

    case LIBINPUT_EVENT_TOUCH_DOWN:
      {
        int device_slot;
        uint64_t time_us;
        double x, y;
        float stage_width, stage_height;
        MetaSeatNative *seat;
        ClutterStage *stage;
        MetaTouchState *touch_state;
        struct libinput_event_touch *touch_event =
          libinput_event_get_touch_event (event);

        device = libinput_device_get_user_data (libinput_device);
        device_evdev = META_INPUT_DEVICE_NATIVE (device);
        seat = meta_input_device_native_get_seat (device_evdev);

        stage = _clutter_input_device_get_stage (device);
        if (stage == NULL)
          break;

        stage_width = clutter_actor_get_width (CLUTTER_ACTOR (stage));
        stage_height = clutter_actor_get_height (CLUTTER_ACTOR (stage));

        device_slot = libinput_event_touch_get_slot (touch_event);
        time_us = libinput_event_touch_get_time_usec (touch_event);
        x = libinput_event_touch_get_x_transformed (touch_event,
                                                    stage_width);
        y = libinput_event_touch_get_y_transformed (touch_event,
                                                    stage_height);

        touch_state =
          meta_input_device_native_acquire_touch_state (device_evdev,
                                                        device_slot);
        touch_state->coords.x = x;
        touch_state->coords.y = y;

        meta_seat_native_notify_touch_event (seat, device,
                                             CLUTTER_TOUCH_BEGIN,
                                             time_us,
                                             touch_state->seat_slot,
                                             touch_state->coords.x,
                                             touch_state->coords.y);
        break;
      }

    case LIBINPUT_EVENT_TOUCH_UP:
      {
        int device_slot;
        uint64_t time_us;
        MetaSeatNative *seat;
        MetaTouchState *touch_state;
        struct libinput_event_touch *touch_event =
          libinput_event_get_touch_event (event);

        device = libinput_device_get_user_data (libinput_device);
        device_evdev = META_INPUT_DEVICE_NATIVE (device);
        seat = meta_input_device_native_get_seat (device_evdev);

        device_slot = libinput_event_touch_get_slot (touch_event);
        time_us = libinput_event_touch_get_time_usec (touch_event);
        touch_state =
          meta_input_device_native_lookup_touch_state (device_evdev,
                                                       device_slot);
        if (!touch_state)
          break;

        meta_seat_native_notify_touch_event (seat, device,
                                             CLUTTER_TOUCH_END, time_us,
                                             touch_state->seat_slot,
                                             touch_state->coords.x,
                                             touch_state->coords.y);
        meta_input_device_native_release_touch_state (device_evdev,
                                                      touch_state);
        break;
      }

    case LIBINPUT_EVENT_TOUCH_MOTION:
      {
        int device_slot;
        uint64_t time_us;
        double x, y;
        float stage_width, stage_height;
        MetaSeatNative *seat;
        ClutterStage *stage;
        MetaTouchState *touch_state;
        struct libinput_event_touch *touch_event =
          libinput_event_get_touch_event (event);

        device = libinput_device_get_user_data (libinput_device);
        device_evdev = META_INPUT_DEVICE_NATIVE (device);
        seat = meta_input_device_native_get_seat (device_evdev);

        stage = _clutter_input_device_get_stage (device);
        if (stage == NULL)
          break;

        stage_width = clutter_actor_get_width (CLUTTER_ACTOR (stage));
        stage_height = clutter_actor_get_height (CLUTTER_ACTOR (stage));

        device_slot = libinput_event_touch_get_slot (touch_event);
        time_us = libinput_event_touch_get_time_usec (touch_event);
        x = libinput_event_touch_get_x_transformed (touch_event,
                                                    stage_width);
        y = libinput_event_touch_get_y_transformed (touch_event,
                                                    stage_height);

        touch_state =
          meta_input_device_native_lookup_touch_state (device_evdev,
                                                       device_slot);
        if (!touch_state)
          break;

        touch_state->coords.x = x;
        touch_state->coords.y = y;

        meta_seat_native_notify_touch_event (seat, device,
                                             CLUTTER_TOUCH_UPDATE,
                                             time_us,
                                             touch_state->seat_slot,
                                             touch_state->coords.x,
                                             touch_state->coords.y);
        break;
      }
    case LIBINPUT_EVENT_TOUCH_CANCEL:
      {
        uint64_t time_us;
        struct libinput_event_touch *touch_event =
          libinput_event_get_touch_event (event);

        device = libinput_device_get_user_data (libinput_device);
        device_evdev = META_INPUT_DEVICE_NATIVE (device);
        time_us = libinput_event_touch_get_time_usec (touch_event);

        meta_input_device_native_release_touch_slots (device_evdev, time_us);

        break;
      }
    case LIBINPUT_EVENT_GESTURE_PINCH_BEGIN:
    case LIBINPUT_EVENT_GESTURE_PINCH_END:
      {
        struct libinput_event_gesture *gesture_event =
          libinput_event_get_gesture_event (event);
        ClutterTouchpadGesturePhase phase;
        uint32_t n_fingers;
        uint64_t time_us;

        if (libinput_event_get_type (event) == LIBINPUT_EVENT_GESTURE_PINCH_BEGIN)
          phase = CLUTTER_TOUCHPAD_GESTURE_PHASE_BEGIN;
        else
          phase = libinput_event_gesture_get_cancelled (gesture_event) ?
            CLUTTER_TOUCHPAD_GESTURE_PHASE_CANCEL : CLUTTER_TOUCHPAD_GESTURE_PHASE_END;

        n_fingers = libinput_event_gesture_get_finger_count (gesture_event);
        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_gesture_get_time_usec (gesture_event);
        notify_pinch_gesture_event (device, phase, time_us, 0, 0, 0, 0, n_fingers);
        break;
      }
    case LIBINPUT_EVENT_GESTURE_PINCH_UPDATE:
      {
        struct libinput_event_gesture *gesture_event =
          libinput_event_get_gesture_event (event);
        gdouble angle_delta, scale, dx, dy;
        uint32_t n_fingers;
        uint64_t time_us;

        n_fingers = libinput_event_gesture_get_finger_count (gesture_event);
        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_gesture_get_time_usec (gesture_event);
        angle_delta = libinput_event_gesture_get_angle_delta (gesture_event);
        scale = libinput_event_gesture_get_scale (gesture_event);
        dx = libinput_event_gesture_get_dx (gesture_event);
        dy = libinput_event_gesture_get_dy (gesture_event);

        notify_pinch_gesture_event (device,
                                    CLUTTER_TOUCHPAD_GESTURE_PHASE_UPDATE,
                                    time_us, dx, dy, angle_delta, scale, n_fingers);
        break;
      }
    case LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN:
    case LIBINPUT_EVENT_GESTURE_SWIPE_END:
      {
        struct libinput_event_gesture *gesture_event =
          libinput_event_get_gesture_event (event);
        ClutterTouchpadGesturePhase phase;
        uint32_t n_fingers;
        uint64_t time_us;

        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_gesture_get_time_usec (gesture_event);
        n_fingers = libinput_event_gesture_get_finger_count (gesture_event);

        if (libinput_event_get_type (event) == LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN)
          phase = CLUTTER_TOUCHPAD_GESTURE_PHASE_BEGIN;
        else
          phase = libinput_event_gesture_get_cancelled (gesture_event) ?
            CLUTTER_TOUCHPAD_GESTURE_PHASE_CANCEL : CLUTTER_TOUCHPAD_GESTURE_PHASE_END;

        notify_swipe_gesture_event (device, phase, time_us, n_fingers, 0, 0);
        break;
      }
    case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE:
      {
        struct libinput_event_gesture *gesture_event =
          libinput_event_get_gesture_event (event);
        uint32_t n_fingers;
        uint64_t time_us;
        double dx, dy;

        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_gesture_get_time_usec (gesture_event);
        n_fingers = libinput_event_gesture_get_finger_count (gesture_event);
        dx = libinput_event_gesture_get_dx (gesture_event);
        dy = libinput_event_gesture_get_dy (gesture_event);

        notify_swipe_gesture_event (device,
                                    CLUTTER_TOUCHPAD_GESTURE_PHASE_UPDATE,
                                    time_us, n_fingers, dx, dy);
        break;
      }
    case LIBINPUT_EVENT_TABLET_TOOL_AXIS:
      {
        process_tablet_axis (seat, event);
        break;
      }
    case LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY:
      {
        uint64_t time;
        struct libinput_event_tablet_tool *tablet_event =
          libinput_event_get_tablet_tool_event (event);
        struct libinput_tablet_tool *libinput_tool = NULL;
        enum libinput_tablet_tool_proximity_state state;

        state = libinput_event_tablet_tool_get_proximity_state (tablet_event);
        time = libinput_event_tablet_tool_get_time_usec (tablet_event);
        device = libinput_device_get_user_data (libinput_device);

        libinput_tool = libinput_event_tablet_tool_get_tool (tablet_event);

        if (state == LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN)
          input_device_update_tool (device, libinput_tool);
        notify_proximity (device, time, state == LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN);
        if (state == LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT)
          input_device_update_tool (device, NULL);
        break;
      }
    case LIBINPUT_EVENT_TABLET_TOOL_BUTTON:
      {
        uint64_t time_us;
        uint32_t button_state;
        struct libinput_event_tablet_tool *tablet_event =
          libinput_event_get_tablet_tool_event (event);
        uint32_t tablet_button;

        process_tablet_axis (seat, event);

        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_tablet_tool_get_time_usec (tablet_event);
        tablet_button = libinput_event_tablet_tool_get_button (tablet_event);

        button_state = libinput_event_tablet_tool_get_button_state (tablet_event) ==
                       LIBINPUT_BUTTON_STATE_PRESSED;

        meta_seat_native_notify_button (seat_from_device (device), device,
                                        time_us, tablet_button, button_state);
        break;
      }
    case LIBINPUT_EVENT_TABLET_TOOL_TIP:
      {
        uint64_t time_us;
        uint32_t button_state;
        struct libinput_event_tablet_tool *tablet_event =
          libinput_event_get_tablet_tool_event (event);

        device = libinput_device_get_user_data (libinput_device);
        time_us = libinput_event_tablet_tool_get_time_usec (tablet_event);

        button_state = libinput_event_tablet_tool_get_tip_state (tablet_event) ==
                       LIBINPUT_TABLET_TOOL_TIP_DOWN;

        /* To avoid jumps on tip, notify axes before the tip down event
           but after the tip up event */
        if (button_state)
          process_tablet_axis (seat, event);

        meta_seat_native_notify_button (seat_from_device (device), device,
                                        time_us, BTN_TOUCH, button_state);
        if (!button_state)
          process_tablet_axis (seat, event);
        break;
      }
    case LIBINPUT_EVENT_TABLET_PAD_BUTTON:
      {
        uint64_t time;
        uint32_t button_state, button, group, mode;
        struct libinput_tablet_pad_mode_group *mode_group;
        struct libinput_event_tablet_pad *pad_event =
          libinput_event_get_tablet_pad_event (event);

        device = libinput_device_get_user_data (libinput_device);
        time = libinput_event_tablet_pad_get_time_usec (pad_event);

        mode_group = libinput_event_tablet_pad_get_mode_group (pad_event);
        group = libinput_tablet_pad_mode_group_get_index (mode_group);
        mode = libinput_event_tablet_pad_get_mode (pad_event);

        button = libinput_event_tablet_pad_get_button_number (pad_event);
        button_state = libinput_event_tablet_pad_get_button_state (pad_event) ==
                       LIBINPUT_BUTTON_STATE_PRESSED;
        notify_pad_button (device, time, button, group, mode, button_state);
        break;
      }
    case LIBINPUT_EVENT_TABLET_PAD_STRIP:
      {
        uint64_t time;
        uint32_t number, source, group, mode;
        struct libinput_tablet_pad_mode_group *mode_group;
        struct libinput_event_tablet_pad *pad_event =
          libinput_event_get_tablet_pad_event (event);
        double value;

        device = libinput_device_get_user_data (libinput_device);
        time = libinput_event_tablet_pad_get_time_usec (pad_event);
        number = libinput_event_tablet_pad_get_strip_number (pad_event);
        value = libinput_event_tablet_pad_get_strip_position (pad_event);
        source = libinput_event_tablet_pad_get_strip_source (pad_event);

        mode_group = libinput_event_tablet_pad_get_mode_group (pad_event);
        group = libinput_tablet_pad_mode_group_get_index (mode_group);
        mode = libinput_event_tablet_pad_get_mode (pad_event);

        notify_pad_strip (device, time, number, source, group, mode, value);
        break;
      }
    case LIBINPUT_EVENT_TABLET_PAD_RING:
      {
        uint64_t time;
        uint32_t number, source, group, mode;
        struct libinput_tablet_pad_mode_group *mode_group;
        struct libinput_event_tablet_pad *pad_event =
          libinput_event_get_tablet_pad_event (event);
        double angle;

        device = libinput_device_get_user_data (libinput_device);
        time = libinput_event_tablet_pad_get_time_usec (pad_event);
        number = libinput_event_tablet_pad_get_ring_number (pad_event);
        angle = libinput_event_tablet_pad_get_ring_position (pad_event);
        source = libinput_event_tablet_pad_get_ring_source (pad_event);

        mode_group = libinput_event_tablet_pad_get_mode_group (pad_event);
        group = libinput_tablet_pad_mode_group_get_index (mode_group);
        mode = libinput_event_tablet_pad_get_mode (pad_event);

        notify_pad_ring (device, time, number, source, group, mode, angle);
        break;
      }
    case LIBINPUT_EVENT_SWITCH_TOGGLE:
      {
        struct libinput_event_switch *switch_event =
          libinput_event_get_switch_event (event);
        enum libinput_switch sw =
          libinput_event_switch_get_switch (switch_event);
        enum libinput_switch_state state =
          libinput_event_switch_get_switch_state (switch_event);

        if (sw == LIBINPUT_SWITCH_TABLET_MODE)
          {
            seat->tablet_mode_switch_state = (state == LIBINPUT_SWITCH_STATE_ON);
            update_touch_mode (seat);
          }
        break;
      }
    default:
      handled = FALSE;
    }

  return handled;
}

static gboolean
filter_event (MetaSeatNative        *seat,
              struct libinput_event *event)
{
  gboolean retval = CLUTTER_EVENT_PROPAGATE;
  MetaEventFilter *filter;
  GSList *tmp_list;

  tmp_list = seat->event_filters;

  while (tmp_list)
    {
      filter = tmp_list->data;
      retval = filter->func (event, filter->data);
      tmp_list = tmp_list->next;

      if (retval != CLUTTER_EVENT_PROPAGATE)
        break;
    }

  return retval;
}

static void
process_event (MetaSeatNative        *seat,
               struct libinput_event *event)
{
  gboolean retval;

  retval = filter_event (seat, event);

  if (retval != CLUTTER_EVENT_PROPAGATE)
    return;

  if (process_base_event (seat, event))
    return;
  if (process_device_event (seat, event))
    return;
}

static void
process_events (MetaSeatNative *seat)
{
  struct libinput_event *event;

  while ((event = libinput_get_event (seat->libinput)))
    {
      process_event(seat, event);
      libinput_event_destroy(event);
    }
}

static int
open_restricted (const char *path,
                 int         flags,
                 void       *user_data)
{
  gint fd;

  if (device_open_callback)
    {
      GError *error = NULL;

      fd = device_open_callback (path, flags, device_callback_data, &error);

      if (fd < 0)
        {
          g_warning ("Could not open device %s: %s", path, error->message);
          g_error_free (error);
        }
    }
  else
    {
      fd = open (path, O_RDWR | O_NONBLOCK);
      if (fd < 0)
        {
          g_warning ("Could not open device %s: %s", path, strerror (errno));
        }
    }

  return fd;
}

static void
close_restricted (int   fd,
                  void *user_data)
{
  if (device_close_callback)
    device_close_callback (fd, device_callback_data);
  else
    close (fd);
}

static const struct libinput_interface libinput_interface = {
  open_restricted,
  close_restricted
};

static void
meta_seat_native_constructed (GObject *object)
{
  MetaSeatNative *seat = META_SEAT_NATIVE (object);
  ClutterInputDevice *device;
  ClutterStage *stage;
  MetaEventSource *source;
  struct udev *udev;
  struct xkb_keymap *xkb_keymap;

  device = meta_input_device_native_new_virtual (
      seat, CLUTTER_POINTER_DEVICE,
      CLUTTER_INPUT_MODE_MASTER);
  stage = meta_seat_native_get_stage (seat);
  _clutter_input_device_set_stage (device, stage);
  seat->pointer_x = INITIAL_POINTER_X;
  seat->pointer_y = INITIAL_POINTER_Y;
  _clutter_input_device_set_coords (device, NULL,
                                    seat->pointer_x, seat->pointer_y,
                                    NULL);
  seat->core_pointer = device;

  device = meta_input_device_native_new_virtual (
      seat, CLUTTER_KEYBOARD_DEVICE,
      CLUTTER_INPUT_MODE_MASTER);
  _clutter_input_device_set_stage (device, stage);
  seat->core_keyboard = device;

  udev = udev_new ();
  if (G_UNLIKELY (udev == NULL))
    {
      g_warning ("Failed to create udev object");
      return;
    }

  seat->libinput = libinput_udev_create_context (&libinput_interface,
                                                 seat, udev);
  if (seat->libinput == NULL)
    {
      g_critical ("Failed to create the libinput object.");
      return;
    }

  if (libinput_udev_assign_seat (seat->libinput, seat->seat_id) == -1)
    {
      g_critical ("Failed to assign a seat to the libinput object.");
      libinput_unref (seat->libinput);
      seat->libinput = NULL;
      return;
    }

  udev_unref (udev);

  seat->udev_client = g_udev_client_new ((const gchar *[]) { "input", NULL });

  source = meta_event_source_new (seat);
  seat->event_source = source;

  seat->keymap = g_object_new (META_TYPE_KEYMAP_NATIVE, NULL);
  xkb_keymap = meta_keymap_native_get_keyboard_map (seat->keymap);

  if (xkb_keymap)
    {
      seat->xkb = xkb_state_new (xkb_keymap);

      seat->caps_lock_led =
        xkb_keymap_led_get_index (xkb_keymap, XKB_LED_NAME_CAPS);
      seat->num_lock_led =
        xkb_keymap_led_get_index (xkb_keymap, XKB_LED_NAME_NUM);
      seat->scroll_lock_led =
        xkb_keymap_led_get_index (xkb_keymap, XKB_LED_NAME_SCROLL);
    }

  seat->has_touchscreen = has_touchscreen (seat);
  update_touch_mode (seat);

  if (G_OBJECT_CLASS (meta_seat_native_parent_class)->constructed)
    G_OBJECT_CLASS (meta_seat_native_parent_class)->constructed (object);
}

static void
meta_seat_native_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (object);

  switch (prop_id)
    {
    case PROP_SEAT_ID:
      seat_native->seat_id = g_value_dup_string (value);
      break;
    case PROP_TOUCH_MODE:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_seat_native_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (object);

  switch (prop_id)
    {
    case PROP_SEAT_ID:
      g_value_set_string (value, seat_native->seat_id);
      break;
    case PROP_TOUCH_MODE:
      g_value_set_boolean (value, seat_native->touch_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_seat_native_dispose (GObject *object)
{
  MetaSeatNative *seat = META_SEAT_NATIVE (object);

  g_clear_signal_handler (&seat->stage_added_handler, seat->stage_manager);
  g_clear_signal_handler (&seat->stage_removed_handler, seat->stage_manager);

  if (seat->stage_manager)
    {
      g_object_unref (seat->stage_manager);
      seat->stage_manager = NULL;
    }

  if (seat->libinput)
    {
      libinput_unref (seat->libinput);
      seat->libinput = NULL;
    }

  G_OBJECT_CLASS (meta_seat_native_parent_class)->dispose (object);
}

static void
meta_seat_native_finalize (GObject *object)
{
  MetaSeatNative *seat = META_SEAT_NATIVE (object);
  GSList *iter;

  for (iter = seat->devices; iter; iter = g_slist_next (iter))
    {
      ClutterInputDevice *device = iter->data;

      g_object_unref (device);
    }
  g_slist_free (seat->devices);
  g_free (seat->touch_states);

  g_object_unref (seat->udev_client);

  meta_event_source_free (seat->event_source);

  xkb_state_unref (seat->xkb);

  meta_seat_native_clear_repeat_timer (seat);

  if (seat->libinput_seat)
    libinput_seat_unref (seat->libinput_seat);

  g_list_free (seat->free_device_ids);

  if (seat->constrain_data_notify != NULL)
    seat->constrain_data_notify (seat->constrain_data);

  g_free (seat->seat_id);

  G_OBJECT_CLASS (meta_seat_native_parent_class)->finalize (object);
}

static ClutterInputDevice *
meta_seat_native_get_pointer (ClutterSeat *seat)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (seat);

  return seat_native->core_pointer;
}

static ClutterInputDevice *
meta_seat_native_get_keyboard (ClutterSeat *seat)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (seat);

  return seat_native->core_keyboard;
}

static GList *
meta_seat_native_list_devices (ClutterSeat *seat)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (seat);
  GList *devices = NULL;
  GSList *l;

  for (l = seat_native->devices; l; l = l->next)
    devices = g_list_prepend (devices, l->data);

  return devices;
}

static void
meta_seat_native_bell_notify (ClutterSeat *seat)
{
  MetaDisplay *display = meta_get_display ();

  meta_bell_notify (display, NULL);
}

static ClutterKeymap *
meta_seat_native_get_keymap (ClutterSeat *seat)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (seat);

  return CLUTTER_KEYMAP (seat_native->keymap);
}

static void
meta_seat_native_copy_event_data (ClutterSeat        *seat,
                                  const ClutterEvent *src,
                                  ClutterEvent       *dest)
{
  MetaEventNative *event_evdev;

  event_evdev = _clutter_event_get_platform_data (src);
  if (event_evdev != NULL)
    _clutter_event_set_platform_data (dest, meta_event_native_copy (event_evdev));
}

static void
meta_seat_native_free_event_data (ClutterSeat  *seat,
                                  ClutterEvent *event)
{
  MetaEventNative *event_evdev;

  event_evdev = _clutter_event_get_platform_data (event);
  if (event_evdev != NULL)
    meta_event_native_free (event_evdev);
}

static void
meta_seat_native_apply_kbd_a11y_settings (ClutterSeat            *seat,
                                          ClutterKbdA11ySettings *settings)
{
  ClutterInputDevice *device;

  device = clutter_seat_get_keyboard (seat);
  if (device)
    meta_input_device_native_apply_kbd_a11y_settings (META_INPUT_DEVICE_NATIVE (device),
                                                      settings);
}

static ClutterVirtualInputDevice *
meta_seat_native_create_virtual_device (ClutterSeat            *seat,
                                        ClutterInputDeviceType  device_type)
{
  return g_object_new (META_TYPE_VIRTUAL_INPUT_DEVICE_NATIVE,
                       "seat", seat,
                       "device-type", device_type,
                       NULL);
}

static ClutterVirtualDeviceType
meta_seat_native_get_supported_virtual_device_types (ClutterSeat *seat)
{
  return (CLUTTER_VIRTUAL_DEVICE_TYPE_KEYBOARD |
          CLUTTER_VIRTUAL_DEVICE_TYPE_POINTER |
          CLUTTER_VIRTUAL_DEVICE_TYPE_TOUCHSCREEN);
}

static void
meta_seat_native_compress_motion (ClutterSeat        *seat,
                                  ClutterEvent       *event,
                                  const ClutterEvent *to_discard)
{
  double dx, dy;
  double dx_unaccel, dy_unaccel;
  double dst_dx = 0.0, dst_dy = 0.0;
  double dst_dx_unaccel = 0.0, dst_dy_unaccel = 0.0;

  if (!meta_event_native_get_relative_motion (to_discard,
                                              &dx, &dy,
                                              &dx_unaccel, &dy_unaccel))
    return;

  meta_event_native_get_relative_motion (event,
                                         &dst_dx, &dst_dy,
                                         &dst_dx_unaccel, &dst_dy_unaccel);
  meta_event_native_set_relative_motion (event,
                                         dx + dst_dx,
                                         dy + dst_dy,
                                         dx_unaccel + dst_dx_unaccel,
                                         dy_unaccel + dst_dy_unaccel);
}

static void
meta_seat_native_warp_pointer (ClutterSeat *seat,
                               int          x,
                               int          y)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (seat);
  MetaBackend *backend = meta_get_backend ();
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);

  notify_absolute_motion (seat_native->core_pointer, 0, x, y, NULL);

  meta_cursor_tracker_update_position (cursor_tracker, x, y);
}

static void
meta_seat_native_class_init (MetaSeatNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterSeatClass *seat_class = CLUTTER_SEAT_CLASS (klass);

  object_class->constructed = meta_seat_native_constructed;
  object_class->set_property = meta_seat_native_set_property;
  object_class->get_property = meta_seat_native_get_property;
  object_class->dispose = meta_seat_native_dispose;
  object_class->finalize = meta_seat_native_finalize;

  seat_class->get_pointer = meta_seat_native_get_pointer;
  seat_class->get_keyboard = meta_seat_native_get_keyboard;
  seat_class->list_devices = meta_seat_native_list_devices;
  seat_class->bell_notify = meta_seat_native_bell_notify;
  seat_class->get_keymap = meta_seat_native_get_keymap;
  seat_class->copy_event_data = meta_seat_native_copy_event_data;
  seat_class->free_event_data = meta_seat_native_free_event_data;
  seat_class->apply_kbd_a11y_settings = meta_seat_native_apply_kbd_a11y_settings;
  seat_class->create_virtual_device = meta_seat_native_create_virtual_device;
  seat_class->get_supported_virtual_device_types = meta_seat_native_get_supported_virtual_device_types;
  seat_class->compress_motion = meta_seat_native_compress_motion;
  seat_class->warp_pointer = meta_seat_native_warp_pointer;
  seat_class->handle_device_event = meta_seat_native_handle_device_event;

  props[PROP_SEAT_ID] =
    g_param_spec_string ("seat-id",
                         "Seat ID",
                         "Seat ID",
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, N_PROPS, props);

  g_object_class_override_property (object_class, PROP_TOUCH_MODE,
                                    "touch-mode");
}

static void
meta_seat_native_stage_added_cb (ClutterStageManager *manager,
                                 ClutterStage        *stage,
                                 MetaSeatNative      *seat)
{
  /* NB: Currently we can only associate a single stage with all evdev
   * devices.
   *
   * We save a pointer to the stage so if we release/reclaim input
   * devices due to switching virtual terminals then we know what
   * stage to re associate the devices with.
   */
  meta_seat_native_set_stage (seat, stage);

  /* We only want to do this once so we can catch the default
     stage. If the application has multiple stages then it will need
     to manage the stage of the input devices itself */
  g_clear_signal_handler (&seat->stage_added_handler, seat->stage_manager);
}

static void
meta_seat_native_stage_removed_cb (ClutterStageManager *manager,
                                   ClutterStage        *stage,
                                   MetaSeatNative      *seat)
{
  meta_seat_native_set_stage (seat, NULL);
}

static void
meta_seat_native_init (MetaSeatNative *seat)
{
  seat->stage_manager = clutter_stage_manager_get_default ();
  g_object_ref (seat->stage_manager);

  /* evdev doesn't have any way to link an event to a particular stage
     so we'll have to leave it up to applications to set the
     corresponding stage for an input device. However to make it
     easier for applications that are only using one fullscreen stage
     (which is probably the most frequent use-case for the evdev
     backend) we'll associate any input devices that don't have a
     stage with the first stage created. */
  seat->stage_added_handler =
    g_signal_connect (seat->stage_manager,
                      "stage-added",
                      G_CALLBACK (meta_seat_native_stage_added_cb),
                      seat);
  seat->stage_removed_handler =
    g_signal_connect (seat->stage_manager,
                      "stage-removed",
                      G_CALLBACK (meta_seat_native_stage_removed_cb),
                      seat);

  seat->device_id_next = INITIAL_DEVICE_ID;

  seat->repeat = TRUE;
  seat->repeat_delay = 250;     /* ms */
  seat->repeat_interval = 33;   /* ms */
}

ClutterInputDevice *
meta_seat_native_get_device (MetaSeatNative *seat,
                             int             id)
{
  ClutterInputDevice *device;
  GSList *l;

  for (l = seat->devices; l; l = l->next)
    {
      device = l->data;

      if (clutter_input_device_get_device_id (device) == id)
        return device;
    }

  return NULL;
}

void
meta_seat_native_set_stage (MetaSeatNative *seat,
                            ClutterStage   *stage)
{
  GSList *l;

  if (seat->stage == stage)
    return;

  seat->stage = stage;
  _clutter_input_device_set_stage (seat->core_pointer, stage);
  _clutter_input_device_set_stage (seat->core_keyboard, stage);

  for (l = seat->devices; l; l = l->next)
    {
      ClutterInputDevice *device = l->data;

      _clutter_input_device_set_stage (device, stage);
    }
}

ClutterStage *
meta_seat_native_get_stage (MetaSeatNative *seat)
{
  return seat->stage;
}

/**
 * meta_seat_native_set_device_callbacks: (skip)
 * @open_callback: the user replacement for open()
 * @close_callback: the user replacement for close()
 * @user_data: user data for @callback
 *
 * Through this function, the application can set a custom callback
 * to be invoked when Clutter is about to open an evdev device. It can do
 * so if special handling is needed, for example to circumvent permission
 * problems.
 *
 * Setting @callback to %NULL will reset the default behavior.
 *
 * For reliable effects, this function must be called before clutter_init().
 */
void
meta_seat_native_set_device_callbacks (MetaOpenDeviceCallback  open_callback,
                                       MetaCloseDeviceCallback close_callback,
                                       gpointer                user_data)
{
  device_open_callback = open_callback;
  device_close_callback = close_callback;
  device_callback_data = user_data;
}

/**
 * meta_seat_native_set_pointer_constrain_callback:
 * @seat: the #ClutterSeat created by the evdev backend
 * @callback: the callback
 * @user_data: data to pass to the callback
 * @user_data_notify: function to be called when removing the callback
 *
 * Sets a callback to be invoked for every pointer motion. The callback
 * can then modify the new pointer coordinates to constrain movement within
 * a specific region.
 */
void
meta_seat_native_set_pointer_constrain_callback (MetaSeatNative               *seat,
                                                 MetaPointerConstrainCallback  callback,
                                                 gpointer                      user_data,
                                                 GDestroyNotify                user_data_notify)
{
  g_return_if_fail (META_IS_SEAT_NATIVE (seat));

  if (seat->constrain_data_notify)
    seat->constrain_data_notify (seat->constrain_data);

  seat->constrain_callback = callback;
  seat->constrain_data = user_data;
  seat->constrain_data_notify = user_data_notify;
}

void
meta_seat_native_set_relative_motion_filter (MetaSeatNative           *seat,
                                             MetaRelativeMotionFilter  filter,
                                             gpointer                  user_data)
{
  g_return_if_fail (META_IS_SEAT_NATIVE (seat));

  seat->relative_motion_filter = filter;
  seat->relative_motion_filter_user_data = user_data;
}

/**
 * meta_seat_native_add_filter: (skip)
 * @func: (closure data): a filter function
 * @data: (allow-none): user data to be passed to the filter function, or %NULL
 * @destroy_notify: (allow-none): function to call on @data when the filter is removed, or %NULL
 *
 * Adds an event filter function.
 */
void
meta_seat_native_add_filter (MetaSeatNative      *seat,
                             MetaEvdevFilterFunc  func,
                             gpointer             data,
                             GDestroyNotify       destroy_notify)
{
  MetaEventFilter *filter;

  g_return_if_fail (func != NULL);

  filter = g_new0 (MetaEventFilter, 1);
  filter->func = func;
  filter->data = data;
  filter->destroy_notify = destroy_notify;

  seat->event_filters = g_slist_append (seat->event_filters, filter);
}

/**
 * meta_seat_native_remove_filter: (skip)
 * @func: a filter function
 * @data: (allow-none): user data to be passed to the filter function, or %NULL
 *
 * Removes the given filter function.
 */
void
meta_seat_native_remove_filter (MetaSeatNative      *seat,
                                MetaEvdevFilterFunc  func,
                                gpointer             data)
{
  MetaEventFilter *filter;
  GSList *tmp_list;

  g_return_if_fail (func != NULL);

  tmp_list = seat->event_filters;

  while (tmp_list)
    {
      filter = tmp_list->data;

      if (filter->func == func && filter->data == data)
        {
          if (filter->destroy_notify)
            filter->destroy_notify (filter->data);
          g_free (filter);
          seat->event_filters =
            g_slist_delete_link (seat->event_filters, tmp_list);
          return;
        }

      tmp_list = tmp_list->next;
    }
}

void
meta_seat_native_update_xkb_state (MetaSeatNative *seat)
{
  xkb_mod_mask_t latched_mods;
  xkb_mod_mask_t locked_mods;
  struct xkb_keymap *xkb_keymap;

  xkb_keymap = meta_keymap_native_get_keyboard_map (seat->keymap);

  latched_mods = xkb_state_serialize_mods (seat->xkb,
                                           XKB_STATE_MODS_LATCHED);
  locked_mods = xkb_state_serialize_mods (seat->xkb,
                                          XKB_STATE_MODS_LOCKED);
  xkb_state_unref (seat->xkb);
  seat->xkb = xkb_state_new (xkb_keymap);

  xkb_state_update_mask (seat->xkb,
                         0, /* depressed */
                         latched_mods,
                         locked_mods,
                         0, 0, seat->layout_idx);

  seat->caps_lock_led = xkb_keymap_led_get_index (xkb_keymap, XKB_LED_NAME_CAPS);
  seat->num_lock_led = xkb_keymap_led_get_index (xkb_keymap, XKB_LED_NAME_NUM);
  seat->scroll_lock_led = xkb_keymap_led_get_index (xkb_keymap, XKB_LED_NAME_SCROLL);

  meta_seat_native_sync_leds (seat);
}

gint
meta_seat_native_acquire_device_id (MetaSeatNative *seat)
{
  GList *first;
  gint next_id;

  if (seat->free_device_ids == NULL)
    {
      gint i;

      /* We ran out of free ID's, so append 10 new ones. */
      for (i = 0; i < 10; i++)
        seat->free_device_ids =
          g_list_append (seat->free_device_ids,
                         GINT_TO_POINTER (seat->device_id_next++));
    }

  first = g_list_first (seat->free_device_ids);
  next_id = GPOINTER_TO_INT (first->data);
  seat->free_device_ids = g_list_delete_link (seat->free_device_ids, first);

  return next_id;
}

static int
compare_ids (gconstpointer a,
             gconstpointer b)
{
  return GPOINTER_TO_INT (a) - GPOINTER_TO_INT (b);
}

void
meta_seat_native_release_device_id (MetaSeatNative     *seat,
                                    ClutterInputDevice *device)
{
  gint device_id;

  device_id = clutter_input_device_get_device_id (device);
  seat->free_device_ids = g_list_insert_sorted (seat->free_device_ids,
                                                GINT_TO_POINTER (device_id),
                                                compare_ids);
}

/**
 * meta_seat_native_release_devices:
 *
 * Releases all the evdev devices that Clutter is currently managing. This api
 * is typically used when switching away from the Clutter application when
 * switching tty. The devices can be reclaimed later with a call to
 * meta_seat_native_reclaim_devices().
 *
 * This function should only be called after clutter has been initialized.
 */
void
meta_seat_native_release_devices (MetaSeatNative *seat)
{
  g_return_if_fail (META_IS_SEAT_NATIVE (seat));

  if (seat->released)
    {
      g_warning ("meta_seat_native_release_devices() shouldn't be called "
                 "multiple times without a corresponding call to "
                 "meta_seat_native_reclaim_devices() first");
      return;
    }

  libinput_suspend (seat->libinput);
  process_events (seat);

  seat->released = TRUE;
}

/**
 * meta_seat_native_reclaim_devices:
 *
 * This causes Clutter to re-probe for evdev devices. This is must only be
 * called after a corresponding call to meta_seat_native_release_devices()
 * was previously used to release all evdev devices. This API is typically
 * used when a clutter application using evdev has regained focus due to
 * switching ttys.
 *
 * This function should only be called after clutter has been initialized.
 */
void
meta_seat_native_reclaim_devices (MetaSeatNative *seat)
{
  if (!seat->released)
    {
      g_warning ("Spurious call to meta_seat_native_reclaim_devices() without "
                 "previous call to meta_seat_native_release_devices");
      return;
    }

  libinput_resume (seat->libinput);
  meta_seat_native_update_xkb_state (seat);
  process_events (seat);

  seat->released = FALSE;
}

/**
 * meta_seat_native_set_keyboard_map: (skip)
 * @seat: the #ClutterSeat created by the evdev backend
 * @keymap: the new keymap
 *
 * Instructs @evdev to use the speficied keyboard map. This will cause
 * the backend to drop the state and create a new one with the new
 * map. To avoid state being lost, callers should ensure that no key
 * is pressed when calling this function.
 */
void
meta_seat_native_set_keyboard_map (MetaSeatNative    *seat,
                                   struct xkb_keymap *xkb_keymap)
{
  ClutterKeymap *keymap;

  g_return_if_fail (META_IS_SEAT_NATIVE (seat));

  keymap = clutter_seat_get_keymap (CLUTTER_SEAT (seat));
  meta_keymap_native_set_keyboard_map (META_KEYMAP_NATIVE (keymap),
                                       xkb_keymap);

  meta_seat_native_update_xkb_state (seat);
}

/**
 * meta_seat_native_get_keyboard_map: (skip)
 * @seat: the #ClutterSeat created by the evdev backend
 *
 * Retrieves the #xkb_keymap in use by the evdev backend.
 *
 * Return value: the #xkb_keymap.
 */
struct xkb_keymap *
meta_seat_native_get_keyboard_map (MetaSeatNative *seat)
{
  g_return_val_if_fail (META_IS_SEAT_NATIVE (seat), NULL);

  return xkb_state_get_keymap (seat->xkb);
}

/**
 * meta_seat_native_set_keyboard_layout_index: (skip)
 * @seat: the #ClutterSeat created by the evdev backend
 * @idx: the xkb layout index to set
 *
 * Sets the xkb layout index on the backend's #xkb_state .
 */
void
meta_seat_native_set_keyboard_layout_index (MetaSeatNative     *seat,
                                            xkb_layout_index_t  idx)
{
  xkb_mod_mask_t depressed_mods;
  xkb_mod_mask_t latched_mods;
  xkb_mod_mask_t locked_mods;
  struct xkb_state *state;

  g_return_if_fail (META_IS_SEAT_NATIVE (seat));

  state = seat->xkb;

  depressed_mods = xkb_state_serialize_mods (state, XKB_STATE_MODS_DEPRESSED);
  latched_mods = xkb_state_serialize_mods (state, XKB_STATE_MODS_LATCHED);
  locked_mods = xkb_state_serialize_mods (state, XKB_STATE_MODS_LOCKED);

  xkb_state_update_mask (state, depressed_mods, latched_mods, locked_mods, 0, 0, idx);

  seat->layout_idx = idx;
}

/**
 * meta_seat_native_get_keyboard_layout_index: (skip)
 */
xkb_layout_index_t
meta_seat_native_get_keyboard_layout_index (MetaSeatNative *seat)
{
  return seat->layout_idx;
}

/**
 * meta_seat_native_set_keyboard_numlock: (skip)
 * @seat: the #ClutterSeat created by the evdev backend
 * @numlock_set: TRUE to set NumLock ON, FALSE otherwise.
 *
 * Sets the NumLock state on the backend's #xkb_state .
 */
void
meta_seat_native_set_keyboard_numlock (MetaSeatNative *seat,
                                       gboolean        numlock_state)
{
  xkb_mod_mask_t depressed_mods;
  xkb_mod_mask_t latched_mods;
  xkb_mod_mask_t locked_mods;
  xkb_mod_mask_t group_mods;
  xkb_mod_mask_t numlock;
  struct xkb_keymap *xkb_keymap;
  ClutterKeymap *keymap;

  g_return_if_fail (META_IS_SEAT_NATIVE (seat));

  keymap = clutter_seat_get_keymap (CLUTTER_SEAT (seat));
  xkb_keymap = meta_keymap_native_get_keyboard_map (META_KEYMAP_NATIVE (keymap));

  numlock = (1 << xkb_keymap_mod_get_index (xkb_keymap, "Mod2"));

  depressed_mods = xkb_state_serialize_mods (seat->xkb, XKB_STATE_MODS_DEPRESSED);
  latched_mods = xkb_state_serialize_mods (seat->xkb, XKB_STATE_MODS_LATCHED);
  locked_mods = xkb_state_serialize_mods (seat->xkb, XKB_STATE_MODS_LOCKED);
  group_mods = xkb_state_serialize_layout (seat->xkb, XKB_STATE_LAYOUT_EFFECTIVE);

  if (numlock_state)
    locked_mods |= numlock;
  else
    locked_mods &= ~numlock;

  xkb_state_update_mask (seat->xkb,
                         depressed_mods,
                         latched_mods,
                         locked_mods,
                         0, 0,
                         group_mods);

  meta_seat_native_sync_leds (seat);
}

/**
 * meta_seat_native_set_keyboard_repeat:
 * @seat: the #ClutterSeat created by the evdev backend
 * @repeat: whether to enable or disable keyboard repeat events
 * @delay: the delay in ms between the hardware key press event and
 * the first synthetic event
 * @interval: the period in ms between consecutive synthetic key
 * press events
 *
 * Enables or disables sythetic key press events, allowing for initial
 * delay and interval period to be specified.
 */
void
meta_seat_native_set_keyboard_repeat (MetaSeatNative *seat,
                                      gboolean        repeat,
                                      uint32_t        delay,
                                      uint32_t        interval)
{
  g_return_if_fail (META_IS_SEAT_NATIVE (seat));

  seat->repeat = repeat;
  seat->repeat_delay = delay;
  seat->repeat_interval = interval;
}

struct xkb_state *
meta_seat_native_get_xkb_state (MetaSeatNative *seat)
{
  return seat->xkb;
}
