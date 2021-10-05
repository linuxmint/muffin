/* Copyright (C) 2006, 2007, 2008  OpenedHand Ltd
 * Copyright (C) 2009, 2010  Intel Corp.
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
 *
 *
 * Authored by:
 *      Matthew Allum <mallum@openedhand.com>
 *      Emmanuele Bassi <ebassi@linux.intel.com>
 */

#include "config.h"

#include <glib.h>
#include <string.h>

#include "backends/x11/meta-event-x11.h"
#include "clutter/clutter-mutter.h"
#include "clutter/x11/clutter-x11.h"

MetaEventX11 *
meta_event_x11_new (void)
{
  return g_slice_new0 (MetaEventX11);
}

MetaEventX11 *
meta_event_x11_copy (MetaEventX11 *event_x11)
{
  if (event_x11 != NULL)
    return g_slice_dup (MetaEventX11, event_x11);

  return NULL;
}

void
meta_event_x11_free (MetaEventX11 *event_x11)
{
  if (event_x11 != NULL)
    g_slice_free (MetaEventX11, event_x11);
}

/**
 * meta_x11_handle_event:
 * @xevent: pointer to XEvent structure
 *
 * This function processes a single X event; it can be used to hook
 * into external X11 event processing (for example, a GDK filter
 * function).
 *
 * Return value: #ClutterX11FilterReturn. %CLUTTER_X11_FILTER_REMOVE
 *  indicates that Clutter has internally handled the event and the
 *  caller should do no further processing. %CLUTTER_X11_FILTER_CONTINUE
 *  indicates that Clutter is either not interested in the event,
 *  or has used the event to update internal state without taking
 *  any exclusive action. %CLUTTER_X11_FILTER_TRANSLATE will not
 *  occur.
 *
 * Since: 0.8
 */
ClutterX11FilterReturn
meta_x11_handle_event (XEvent *xevent)
{
  ClutterX11FilterReturn result;
  ClutterBackend *backend;
  ClutterEvent *event;
  gint spin = 1;
  ClutterBackendX11 *backend_x11;
  Display *xdisplay;
  gboolean allocated_event;

  /* The return values here are someone approximate; we return
   * CLUTTER_X11_FILTER_REMOVE if a clutter event is
   * generated for the event. This mostly, but not entirely,
   * corresponds to whether other event processing should be
   * excluded. As long as the stage window is not shared with another
   * toolkit it should be safe, and never return
   * %CLUTTER_X11_FILTER_REMOVE when more processing is needed.
   */

  result = CLUTTER_X11_FILTER_CONTINUE;

  _clutter_threads_acquire_lock ();

  backend = clutter_get_default_backend ();

  event = clutter_event_new (CLUTTER_NOTHING);

  backend_x11 = CLUTTER_BACKEND_X11 (backend);
  xdisplay = backend_x11->xdpy;

  allocated_event = XGetEventData (xdisplay, &xevent->xcookie);

  if (_clutter_backend_translate_event (backend, xevent, event))
    {
      _clutter_event_push (event, FALSE);

      result = CLUTTER_X11_FILTER_REMOVE;
    }
  else
    {
      clutter_event_free (event);
      goto out;
    }

  /*
   * Motion events can generate synthetic enter and leave events, so if we
   * are processing a motion event, we need to spin the event loop at least
   * two extra times to pump the enter/leave events through (otherwise they
   * just get pushed down the queue and never processed).
   */
  if (event->type == CLUTTER_MOTION)
    spin += 2;

  while (spin > 0 && (event = clutter_event_get ()))
    {
      /* forward the event into clutter for emission etc. */
      _clutter_stage_queue_event (event->any.stage, event, FALSE);
      --spin;
    }

out:
  if (allocated_event)
    XFreeEventData (xdisplay, &xevent->xcookie);

  _clutter_threads_release_lock ();

  return result;
}

Time
meta_x11_get_current_event_time (void)
{
  ClutterBackend *backend = clutter_get_default_backend ();

  return CLUTTER_BACKEND_X11 (backend)->last_event_time;
}

gint
meta_x11_event_get_key_group (const ClutterEvent *event)
{
  MetaEventX11 *event_x11;

  g_return_val_if_fail (event != NULL, 0);
  g_return_val_if_fail (event->type == CLUTTER_KEY_PRESS ||
                        event->type == CLUTTER_KEY_RELEASE, 0);

  event_x11 = _clutter_event_get_platform_data (event);
  if (event_x11 == NULL)
    return 0;

  return event_x11->key_group;
}

guint
meta_x11_event_sequence_get_touch_detail (const ClutterEventSequence *sequence)
{
  g_return_val_if_fail (sequence != NULL, 0);

  return GPOINTER_TO_UINT (sequence);
}
