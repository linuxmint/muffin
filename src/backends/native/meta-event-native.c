/*
 * Copyright (C) 2015 Red Hat
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
 * Authored by:
 *      Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "backends/native/meta-event-native.h"
#include "backends/native/meta-input-device-native.h"
#include "clutter/clutter-mutter.h"

typedef struct _MetaEventNative MetaEventNative;

struct _MetaEventNative
{
  uint32_t evcode;

  uint64_t time_usec;

  gboolean has_relative_motion;
  double dx;
  double dy;
  double dx_unaccel;
  double dy_unaccel;
};

static MetaEventNative *
meta_event_native_new (void)
{
  return g_slice_new0 (MetaEventNative);
}

MetaEventNative *
meta_event_native_copy (MetaEventNative *event_evdev)
{
  if (event_evdev != NULL)
    return g_slice_dup (MetaEventNative, event_evdev);

  return NULL;
}

void
meta_event_native_free (MetaEventNative *event_evdev)
{
  if (event_evdev != NULL)
    g_slice_free (MetaEventNative, event_evdev);
}

static MetaEventNative *
meta_event_native_ensure_platform_data (ClutterEvent *event)
{
  MetaEventNative *event_evdev = _clutter_event_get_platform_data (event);

  if (!event_evdev)
    {
      event_evdev = meta_event_native_new ();
      _clutter_event_set_platform_data (event, event_evdev);
    }

  return event_evdev;
}

void
meta_event_native_set_event_code (ClutterEvent *event,
                                  uint32_t      evcode)
{
  MetaEventNative *event_evdev;

  event_evdev = meta_event_native_ensure_platform_data (event);
  event_evdev->evcode = evcode;
}

void
meta_event_native_set_time_usec (ClutterEvent *event,
                                 uint64_t      time_usec)
{
  MetaEventNative *event_evdev;

  event_evdev = meta_event_native_ensure_platform_data (event);
  event_evdev->time_usec = time_usec;
}

void
meta_event_native_set_relative_motion (ClutterEvent *event,
                                       double        dx,
                                       double        dy,
                                       double        dx_unaccel,
                                       double        dy_unaccel)
{
  MetaEventNative *event_evdev;

  event_evdev = meta_event_native_ensure_platform_data (event);
  event_evdev->dx = dx;
  event_evdev->dy = dy;
  event_evdev->dx_unaccel = dx_unaccel;
  event_evdev->dy_unaccel = dy_unaccel;
  event_evdev->has_relative_motion = TRUE;
}

/**
 * meta_event_native_get_event_code:
 * @event: a #ClutterEvent
 *
 * Returns the event code of the original event. See linux/input.h for more
 * information.
 *
 * Returns: The event code.
 **/
uint32_t
meta_event_native_get_event_code (const ClutterEvent *event)
{
  MetaEventNative *event_evdev = _clutter_event_get_platform_data (event);

  if (event_evdev)
    return event_evdev->evcode;

  return 0;
}

/**
 * meta_event_native_get_time_usec:
 * @event: a #ClutterEvent
 *
 * Returns the time in microsecond granularity, or 0 if unavailable.
 *
 * Returns: The time in microsecond granularity, or 0 if unavailable.
 */
uint64_t
meta_event_native_get_time_usec (const ClutterEvent *event)
{
  MetaEventNative *event_evdev = _clutter_event_get_platform_data (event);

  if (event_evdev)
    return event_evdev->time_usec;

  return 0;
}

/**
 * meta_event_get_pointer_motion
 * @event: a #ClutterEvent
 *
 * If available, the normal and unaccelerated motion deltas are written
 * to the dx, dy, dx_unaccel and dy_unaccel and TRUE is returned.
 *
 * If unavailable, FALSE is returned.
 *
 * Returns: TRUE on success, otherwise FALSE.
 **/
gboolean
meta_event_native_get_relative_motion (const ClutterEvent *event,
                                       double             *dx,
                                       double             *dy,
                                       double             *dx_unaccel,
                                       double             *dy_unaccel)
{
  MetaEventNative *event_evdev = _clutter_event_get_platform_data (event);

  if (event_evdev && event_evdev->has_relative_motion)
    {
      if (dx)
        *dx = event_evdev->dx;
      if (dy)
        *dy = event_evdev->dy;
      if (dx_unaccel)
        *dx_unaccel = event_evdev->dx_unaccel;
      if (dy_unaccel)
        *dy_unaccel = event_evdev->dy_unaccel;
      return TRUE;
    }
  else
    return FALSE;
}

/**
 * meta_event_native_sequence_get_slot:
 * @sequence: a #ClutterEventSequence
 *
 * Retrieves the touch slot triggered by this @sequence
 *
 * Returns: the libinput touch slot.
 *
 * Since: 1.20
 * Stability: unstable
 **/
int32_t
meta_event_native_sequence_get_slot (const ClutterEventSequence *sequence)
{
  if (!sequence)
    return -1;

  return GPOINTER_TO_INT (sequence) - 1;
}
