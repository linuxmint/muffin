/*
 * Copyright (C) 2019 Red Hat
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Author: Olivier Fourdan <ofourdan@redhat.com>
 *
 * This reimplements in Clutter the same behavior as mousetweaks original
 * implementation by Gerd Kohlberger <gerdko gmail com>
 * mousetweaks Copyright (C) 2007-2010 Gerd Kohlberger <gerdko gmail com>
 */

#include "clutter-build-config.h"

#include "clutter-enum-types.h"
#include "clutter-input-device.h"
#include "clutter-input-device-private.h"
#include "clutter-input-pointer-a11y-private.h"
#include "clutter-main.h"
#include "clutter-virtual-input-device.h"

static gboolean
is_secondary_click_enabled (ClutterInputDevice *device)
{
  ClutterPointerA11ySettings settings;

  clutter_seat_get_pointer_a11y_settings (device->seat, &settings);

  return (settings.controls & CLUTTER_A11Y_SECONDARY_CLICK_ENABLED);
}

static gboolean
is_dwell_click_enabled (ClutterInputDevice *device)
{
  ClutterPointerA11ySettings settings;

  clutter_seat_get_pointer_a11y_settings (device->seat, &settings);

  return (settings.controls & CLUTTER_A11Y_DWELL_ENABLED);
}

static unsigned int
get_secondary_click_delay (ClutterInputDevice *device)
{
  ClutterPointerA11ySettings settings;

  clutter_seat_get_pointer_a11y_settings (device->seat, &settings);

  return settings.secondary_click_delay;
}

static unsigned int
get_dwell_delay (ClutterInputDevice *device)
{
  ClutterPointerA11ySettings settings;

  clutter_seat_get_pointer_a11y_settings (device->seat, &settings);

  return settings.dwell_delay;
}

static unsigned int
get_dwell_threshold (ClutterInputDevice *device)
{
  ClutterPointerA11ySettings settings;

  clutter_seat_get_pointer_a11y_settings (device->seat, &settings);

  return settings.dwell_threshold;
}

static ClutterPointerA11yDwellMode
get_dwell_mode (ClutterInputDevice *device)
{
  ClutterPointerA11ySettings settings;

  clutter_seat_get_pointer_a11y_settings (device->seat, &settings);

  return settings.dwell_mode;
}

static ClutterPointerA11yDwellClickType
get_dwell_click_type (ClutterInputDevice *device)
{
  ClutterPointerA11ySettings settings;

  clutter_seat_get_pointer_a11y_settings (device->seat, &settings);

  return settings.dwell_click_type;
}

static ClutterPointerA11yDwellClickType
get_dwell_click_type_for_direction (ClutterInputDevice               *device,
                                    ClutterPointerA11yDwellDirection  direction)
{
  ClutterPointerA11ySettings settings;

  clutter_seat_get_pointer_a11y_settings (device->seat, &settings);

  if (direction == settings.dwell_gesture_single)
    return CLUTTER_A11Y_DWELL_CLICK_TYPE_PRIMARY;
  else if (direction == settings.dwell_gesture_double)
    return CLUTTER_A11Y_DWELL_CLICK_TYPE_DOUBLE;
  else if (direction == settings.dwell_gesture_drag)
    return CLUTTER_A11Y_DWELL_CLICK_TYPE_DRAG;
  else if (direction == settings.dwell_gesture_secondary)
    return CLUTTER_A11Y_DWELL_CLICK_TYPE_SECONDARY;

  return CLUTTER_A11Y_DWELL_CLICK_TYPE_NONE;
}

static void
emit_button_press (ClutterInputDevice *device,
                   gint                button)
{
  clutter_virtual_input_device_notify_button (device->accessibility_virtual_device,
                                              g_get_monotonic_time (),
                                              button,
                                              CLUTTER_BUTTON_STATE_PRESSED);
}

static void
emit_button_release (ClutterInputDevice *device,
                     gint                button)
{
  clutter_virtual_input_device_notify_button (device->accessibility_virtual_device,
                                              g_get_monotonic_time (),
                                              button,
                                              CLUTTER_BUTTON_STATE_RELEASED);
}

static void
emit_button_click (ClutterInputDevice *device,
                   gint                button)
{
  emit_button_press (device, button);
  emit_button_release (device, button);
}

static void
restore_dwell_position (ClutterInputDevice *device)
{
  clutter_virtual_input_device_notify_absolute_motion (device->accessibility_virtual_device,
                                                       g_get_monotonic_time (),
                                                       device->ptr_a11y_data->dwell_x,
                                                       device->ptr_a11y_data->dwell_y);
}

static gboolean
trigger_secondary_click (gpointer data)
{
  ClutterInputDevice *device = data;

  device->ptr_a11y_data->secondary_click_triggered = TRUE;
  device->ptr_a11y_data->secondary_click_timer = 0;

  g_signal_emit_by_name (device->seat,
                         "ptr-a11y-timeout-stopped",
                         device,
                         CLUTTER_A11Y_TIMEOUT_TYPE_SECONDARY_CLICK,
                         TRUE);

  return G_SOURCE_REMOVE;
}

static void
start_secondary_click_timeout (ClutterInputDevice *device)
{
  unsigned int delay = get_secondary_click_delay (device);

  device->ptr_a11y_data->secondary_click_timer =
    clutter_threads_add_timeout (delay, trigger_secondary_click, device);

  g_signal_emit_by_name (device->seat,
                         "ptr-a11y-timeout-started",
                         device,
                         CLUTTER_A11Y_TIMEOUT_TYPE_SECONDARY_CLICK,
                         delay);
}

static void
stop_secondary_click_timeout (ClutterInputDevice *device)
{
  if (device->ptr_a11y_data->secondary_click_timer)
    {
      g_clear_handle_id (&device->ptr_a11y_data->secondary_click_timer,
                         g_source_remove);

      g_signal_emit_by_name (device->seat,
                             "ptr-a11y-timeout-stopped",
                             device,
                             CLUTTER_A11Y_TIMEOUT_TYPE_SECONDARY_CLICK,
                             FALSE);
    }
  device->ptr_a11y_data->secondary_click_triggered = FALSE;
}

static gboolean
pointer_has_moved (ClutterInputDevice *device)
{
  float dx, dy;
  gint threshold;

  dx = device->ptr_a11y_data->dwell_x - device->ptr_a11y_data->current_x;
  dy = device->ptr_a11y_data->dwell_y - device->ptr_a11y_data->current_y;
  threshold = get_dwell_threshold (device);

  /* Pythagorean theorem */
  return ((dx * dx) + (dy * dy)) > (threshold * threshold);
}

static gboolean
is_secondary_click_pending (ClutterInputDevice *device)
{
  return device->ptr_a11y_data->secondary_click_timer != 0;
}

static gboolean
is_secondary_click_triggered (ClutterInputDevice *device)
{
  return device->ptr_a11y_data->secondary_click_triggered;
}

static gboolean
is_dwell_click_pending (ClutterInputDevice *device)
{
  return device->ptr_a11y_data->dwell_timer != 0;
}

static gboolean
is_dwell_dragging (ClutterInputDevice *device)
{
  return device->ptr_a11y_data->dwell_drag_started;
}

static gboolean
is_dwell_gesturing (ClutterInputDevice *device)
{
  return device->ptr_a11y_data->dwell_gesture_started;
}

static gboolean
has_button_pressed (ClutterInputDevice *device)
{
  return device->ptr_a11y_data->n_btn_pressed > 0;
}

static gboolean
should_start_secondary_click_timeout (ClutterInputDevice *device)
{
  return !is_dwell_dragging (device);
}

static gboolean
should_start_dwell (ClutterInputDevice *device)
{
  /* We should trigger a dwell if we've not already started one, and if
   * no button is currently pressed or we are in the middle of a dwell
   * drag action.
   */
  return !is_dwell_click_pending (device) &&
         (is_dwell_dragging (device) ||
          !has_button_pressed (device));
}

static gboolean
should_stop_dwell (ClutterInputDevice *device)
{
  /* We should stop a dwell if the motion exceeds the threshold, unless
   * we've started a gesture, because we want to keep the original dwell
   * location to both detect a gesture and restore the original pointer
   * location once the gesture is finished.
   */
  return pointer_has_moved (device) &&
         !is_dwell_gesturing (device);
}


static gboolean
should_update_dwell_position (ClutterInputDevice *device)
{
  return !is_dwell_gesturing (device) &&
         !is_dwell_click_pending (device) &&
         !is_secondary_click_pending (device);
}

static void
update_dwell_click_type (ClutterInputDevice *device)
{
  ClutterPointerA11ySettings settings;
  ClutterPointerA11yDwellClickType dwell_click_type;

  clutter_seat_get_pointer_a11y_settings (device->seat, &settings);

  dwell_click_type = settings.dwell_click_type;
  switch (dwell_click_type)
    {
    case CLUTTER_A11Y_DWELL_CLICK_TYPE_DOUBLE:
    case CLUTTER_A11Y_DWELL_CLICK_TYPE_SECONDARY:
    case CLUTTER_A11Y_DWELL_CLICK_TYPE_MIDDLE:
      dwell_click_type = CLUTTER_A11Y_DWELL_CLICK_TYPE_PRIMARY;
      break;

    case CLUTTER_A11Y_DWELL_CLICK_TYPE_DRAG:
      if (!is_dwell_dragging (device))
        dwell_click_type = CLUTTER_A11Y_DWELL_CLICK_TYPE_PRIMARY;
      break;

    case CLUTTER_A11Y_DWELL_CLICK_TYPE_PRIMARY:
    case CLUTTER_A11Y_DWELL_CLICK_TYPE_NONE:
    default:
      break;
    }

  if (dwell_click_type != settings.dwell_click_type)
    {
      settings.dwell_click_type = dwell_click_type;
      clutter_seat_set_pointer_a11y_settings (device->seat, &settings);

      g_signal_emit_by_name (device->seat,
                             "ptr-a11y-dwell-click-type-changed",
                             dwell_click_type);
    }
}

static void
emit_dwell_click (ClutterInputDevice               *device,
                  ClutterPointerA11yDwellClickType  dwell_click_type)
{
  switch (dwell_click_type)
    {
    case CLUTTER_A11Y_DWELL_CLICK_TYPE_PRIMARY:
      emit_button_click (device, CLUTTER_BUTTON_PRIMARY);
      break;

    case CLUTTER_A11Y_DWELL_CLICK_TYPE_DOUBLE:
      emit_button_click (device, CLUTTER_BUTTON_PRIMARY);
      emit_button_click (device, CLUTTER_BUTTON_PRIMARY);
      break;

    case CLUTTER_A11Y_DWELL_CLICK_TYPE_DRAG:
      if (is_dwell_dragging (device))
        {
          emit_button_release (device, CLUTTER_BUTTON_PRIMARY);
          device->ptr_a11y_data->dwell_drag_started = FALSE;
        }
      else
        {
          emit_button_press (device, CLUTTER_BUTTON_PRIMARY);
          device->ptr_a11y_data->dwell_drag_started = TRUE;
        }
      break;

    case CLUTTER_A11Y_DWELL_CLICK_TYPE_SECONDARY:
      emit_button_click (device, CLUTTER_BUTTON_SECONDARY);
      break;

    case CLUTTER_A11Y_DWELL_CLICK_TYPE_MIDDLE:
      emit_button_click (device, CLUTTER_BUTTON_MIDDLE);
      break;

    case CLUTTER_A11Y_DWELL_CLICK_TYPE_NONE:
    default:
      break;
    }
}

static ClutterPointerA11yDwellDirection
get_dwell_direction (ClutterInputDevice *device)
{
  float dx, dy;

  dx = ABS (device->ptr_a11y_data->dwell_x - device->ptr_a11y_data->current_x);
  dy = ABS (device->ptr_a11y_data->dwell_y - device->ptr_a11y_data->current_y);

  /* The pointer hasn't moved */
  if (!pointer_has_moved (device))
    return CLUTTER_A11Y_DWELL_DIRECTION_NONE;

  if (device->ptr_a11y_data->dwell_x < device->ptr_a11y_data->current_x)
    {
      if (dx > dy)
        return CLUTTER_A11Y_DWELL_DIRECTION_LEFT;
    }
  else
    {
      if (dx > dy)
        return CLUTTER_A11Y_DWELL_DIRECTION_RIGHT;
    }

  if (device->ptr_a11y_data->dwell_y < device->ptr_a11y_data->current_y)
    return CLUTTER_A11Y_DWELL_DIRECTION_UP;

  return CLUTTER_A11Y_DWELL_DIRECTION_DOWN;
}

static gboolean
trigger_clear_dwell_gesture (gpointer data)
{
  ClutterInputDevice *device = data;

  device->ptr_a11y_data->dwell_timer = 0;
  device->ptr_a11y_data->dwell_gesture_started = FALSE;

  return G_SOURCE_REMOVE;
}

static gboolean
trigger_dwell_gesture (gpointer data)
{
  ClutterInputDevice *device = data;
  ClutterPointerA11yDwellDirection direction;
  unsigned int delay = get_dwell_delay (device);

  restore_dwell_position (device);
  direction = get_dwell_direction (device);
  emit_dwell_click (device,
                    get_dwell_click_type_for_direction (device,
                                                        direction));

  /* Do not clear the gesture right away, otherwise we'll start another one */
  device->ptr_a11y_data->dwell_timer =
    clutter_threads_add_timeout (delay, trigger_clear_dwell_gesture, device);

  g_signal_emit_by_name (device->seat,
                         "ptr-a11y-timeout-stopped",
                         device,
                         CLUTTER_A11Y_TIMEOUT_TYPE_GESTURE,
                         TRUE);

  return G_SOURCE_REMOVE;
}

static void
start_dwell_gesture_timeout (ClutterInputDevice *device)
{
  unsigned int delay = get_dwell_delay (device);

  device->ptr_a11y_data->dwell_timer =
    clutter_threads_add_timeout (delay, trigger_dwell_gesture, device);
  device->ptr_a11y_data->dwell_gesture_started = TRUE;

  g_signal_emit_by_name (device->seat,
                         "ptr-a11y-timeout-started",
                         device,
                         CLUTTER_A11Y_TIMEOUT_TYPE_GESTURE,
                         delay);
}

static gboolean
trigger_dwell_click (gpointer data)
{
  ClutterInputDevice *device = data;

  device->ptr_a11y_data->dwell_timer = 0;

  g_signal_emit_by_name (device->seat,
                         "ptr-a11y-timeout-stopped",
                         device,
                         CLUTTER_A11Y_TIMEOUT_TYPE_DWELL,
                         TRUE);

  if (get_dwell_mode (device) == CLUTTER_A11Y_DWELL_MODE_GESTURE)
    {
      if (is_dwell_dragging (device))
        emit_dwell_click (device, CLUTTER_A11Y_DWELL_CLICK_TYPE_DRAG);
      else
        start_dwell_gesture_timeout (device);
    }
  else
    {
      emit_dwell_click (device, get_dwell_click_type (device));
      update_dwell_click_type (device);
    }

  return G_SOURCE_REMOVE;
}

static void
start_dwell_timeout (ClutterInputDevice *device)
{
  unsigned int delay = get_dwell_delay (device);

  device->ptr_a11y_data->dwell_timer =
    clutter_threads_add_timeout (delay, trigger_dwell_click, device);

  g_signal_emit_by_name (device->seat,
                         "ptr-a11y-timeout-started",
                         device,
                         CLUTTER_A11Y_TIMEOUT_TYPE_DWELL,
                         delay);
}

static void
stop_dwell_timeout (ClutterInputDevice *device)
{
  if (device->ptr_a11y_data->dwell_timer)
    {
      g_clear_handle_id (&device->ptr_a11y_data->dwell_timer, g_source_remove);
      device->ptr_a11y_data->dwell_gesture_started = FALSE;

      g_signal_emit_by_name (device->seat,
                             "ptr-a11y-timeout-stopped",
                             device,
                             CLUTTER_A11Y_TIMEOUT_TYPE_DWELL,
                             FALSE);
    }
}

static gboolean
trigger_dwell_position_timeout (gpointer data)
{
  ClutterInputDevice *device = data;

  device->ptr_a11y_data->dwell_position_timer = 0;

  if (is_dwell_click_enabled (device))
    {
      if (!pointer_has_moved (device))
        start_dwell_timeout (device);
    }

  return G_SOURCE_REMOVE;
}

static void
start_dwell_position_timeout (ClutterInputDevice *device)
{
  device->ptr_a11y_data->dwell_position_timer =
    clutter_threads_add_timeout (100, trigger_dwell_position_timeout, device);
}

static void
stop_dwell_position_timeout (ClutterInputDevice *device)
{
  g_clear_handle_id (&device->ptr_a11y_data->dwell_position_timer,
                     g_source_remove);
}

static void
update_dwell_position (ClutterInputDevice *device)
{
  device->ptr_a11y_data->dwell_x = device->ptr_a11y_data->current_x;
  device->ptr_a11y_data->dwell_y = device->ptr_a11y_data->current_y;
}

static void
update_current_position (ClutterInputDevice *device,
                         float               x,
                         float               y)
{
  device->ptr_a11y_data->current_x = x;
  device->ptr_a11y_data->current_y = y;
}

static gboolean
is_device_core_pointer (ClutterInputDevice *device)
{
  ClutterInputDevice *core_pointer;

  core_pointer = clutter_seat_get_pointer (device->seat);
  if (core_pointer == NULL)
    return FALSE;

  return (core_pointer == device);
}

void
_clutter_input_pointer_a11y_add_device (ClutterInputDevice *device)
{
  if (!is_device_core_pointer (device))
    return;

  device->accessibility_virtual_device =
    clutter_seat_create_virtual_device (device->seat,
                                        CLUTTER_POINTER_DEVICE);

  device->ptr_a11y_data = g_new0 (ClutterPtrA11yData, 1);
}

void
_clutter_input_pointer_a11y_remove_device (ClutterInputDevice *device)
{
  if (!is_device_core_pointer (device))
    return;

  /* Terminate a drag if started */
  if (is_dwell_dragging (device))
    emit_dwell_click (device, CLUTTER_A11Y_DWELL_CLICK_TYPE_DRAG);

  stop_dwell_position_timeout (device);
  stop_dwell_timeout (device);
  stop_secondary_click_timeout (device);

  g_clear_pointer (&device->ptr_a11y_data, g_free);
}

void
_clutter_input_pointer_a11y_on_motion_event (ClutterInputDevice *device,
                                             float               x,
                                             float               y)
{
  if (!is_device_core_pointer (device))
    return;

  if (!_clutter_is_input_pointer_a11y_enabled (device))
    return;

  update_current_position (device, x, y);

  if (is_secondary_click_enabled (device))
    {
      if (pointer_has_moved (device))
        stop_secondary_click_timeout (device);
    }

  if (is_dwell_click_enabled (device))
    {
      stop_dwell_position_timeout (device);

      if (should_stop_dwell (device))
        stop_dwell_timeout (device);

      if (should_start_dwell (device))
        start_dwell_position_timeout (device);
    }

  if (should_update_dwell_position (device))
    update_dwell_position (device);
}

void
_clutter_input_pointer_a11y_on_button_event (ClutterInputDevice *device,
                                             int                 button,
                                             gboolean            pressed)
{
  if (!is_device_core_pointer (device))
    return;

  if (!_clutter_is_input_pointer_a11y_enabled (device))
    return;

  if (pressed)
    {
      device->ptr_a11y_data->n_btn_pressed++;

      stop_dwell_position_timeout (device);

      if (is_dwell_click_enabled (device))
        stop_dwell_timeout (device);

      if (is_dwell_dragging (device))
        stop_dwell_timeout (device);

      if (is_secondary_click_enabled (device))
        {
          if (button == CLUTTER_BUTTON_PRIMARY)
            {
              if (should_start_secondary_click_timeout (device))
                start_secondary_click_timeout (device);
            }
          else if (is_secondary_click_pending (device))
            {
              stop_secondary_click_timeout (device);
            }
        }
    }
  else
    {
      if (has_button_pressed (device))
        device->ptr_a11y_data->n_btn_pressed--;

      if (is_secondary_click_triggered (device))
        {
          emit_button_click (device, CLUTTER_BUTTON_SECONDARY);
          stop_secondary_click_timeout (device);
        }

      if (is_secondary_click_pending (device))
        stop_secondary_click_timeout (device);

      if (is_dwell_dragging (device))
        emit_dwell_click (device, CLUTTER_A11Y_DWELL_CLICK_TYPE_DRAG);
    }
}

gboolean
_clutter_is_input_pointer_a11y_enabled (ClutterInputDevice *device)
{
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), FALSE);

  return (is_secondary_click_enabled (device) || is_dwell_click_enabled (device));
}
