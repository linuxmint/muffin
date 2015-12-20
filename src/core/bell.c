/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Muffin visual bell */

/* 
 * Copyright (C) 2002 Sun Microsystems Inc.
 * Copyright (C) 2005, 2006 Elijah Newren
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
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 */

/**
 * SECTION:Bell
 * @short_description: Ring the bell or flash the screen
 *
 * Sometimes, X programs "ring the bell", whatever that means. Muffin lets
 * the user configure the bell to be audible or visible (aka visual), and
 * if it's visual it can be configured to be frame-flash or fullscreen-flash.
 * We never get told about audible bells; X handles them just fine by itself.
 *
 * Visual bells come in at meta_bell_notify(), which checks we are actually
 * in visual mode and calls through to bell_visual_notify(). That
 * function then checks what kind of visual flash you like, and calls either
 * bell_flash_fullscreen()-- which calls bell_flash_screen() to do
 * its work-- or bell_flash_frame(), which flashes the focussed window
 * using bell_flash_window_frame(), unless there is no such window, in
 * which case it flashes the screen instead. bell_flash_window_frame()
 * flashes the frame and calls bell_unflash_frame() as a timeout to
 * remove the flash.
 *
 * The visual bell was the result of a discussion in Bugzilla here:
 * <http://bugzilla.gnome.org/show_bug.cgi?id=99886>.
 *
 * Several of the functions in this file are ifdeffed out entirely if we are
 * found not to have the XKB extension, which is required to do these clever
 * things with bells; some others are entirely no-ops in that case.
 */

#include <config.h>
#include "bell.h"

LOCAL_SYMBOL gboolean
meta_bell_init (MetaDisplay *display)
{
#ifdef HAVE_XKB
  int xkb_base_error_type, xkb_opcode;

  if (!XkbQueryExtension (display->xdisplay, &xkb_opcode, 
			  &display->xkb_base_event_type, 
			  &xkb_base_error_type, 
			  NULL, NULL))
    {
      display->xkb_base_event_type = -1;
      g_message ("could not find XKB extension.");
      return FALSE;
    }
  else 
    {
      XkbSelectEvents (display->xdisplay,
		       XkbUseCoreKbd,
		       XkbBellNotifyMask,
		       XkbBellNotifyMask);
      return TRUE;
    }
#endif
  return FALSE;
}

LOCAL_SYMBOL void
meta_bell_shutdown (MetaDisplay *display)
{
#ifdef HAVE_XKB
  /* TODO: persist initial bell state in display, reset here */
  XkbChangeEnabledControls (display->xdisplay,
			    XkbUseCoreKbd,
			    XkbAudibleBellMask,
			    XkbAudibleBellMask);
#endif
}

LOCAL_SYMBOL void
meta_bell_notify (MetaDisplay *display,
                  XkbAnyEvent *xkb_ev)
{
    XkbBellNotifyEvent *xkb_bell_event = (XkbBellNotifyEvent *) xkb_ev;
    MetaWindow *bell_window = NULL;
  
    bell_window = meta_display_lookup_x_window (display, xkb_bell_event->window);

    g_signal_emit_by_name (display, "bell", bell_window);
}