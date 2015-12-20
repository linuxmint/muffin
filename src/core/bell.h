/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * \file bell.h Ring the bell or flash the screen
 *
 * Sometimes, X programs "ring the bell", whatever that means. Muffin lets
 * the user configure the bell to be audible or visible (aka visual), and
 * if it's visual it can be configured to be frame-flash or fullscreen-flash.
 * We never get told about audible bells; X handles them just fine by itself.
 *
 * The visual bell was the result of a discussion in Bugzilla here:
 * <http://bugzilla.gnome.org/show_bug.cgi?id=99886>.
 */

/* 
 * Copyright (C) 2002 Sun Microsystems Inc.
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

#include <X11/Xlib.h>
#ifdef HAVE_XKB
#include <X11/XKBlib.h>
#endif
#include "display-private.h"
#include "window-private.h"

#ifdef HAVE_XKB
/**
 * Gives the user some kind of visual bell; in fact, this is our response
 * to any kind of bell request, but we set it up so that we only get
 * notified about visual bells, and X deals with audible ones.
 *
 * If the configure script found we had no XKB, this does not exist.
 *
 * \param display  The display the bell event came in on
 * \param xkb_ev   The bell event we just received
 */
void meta_bell_notify (MetaDisplay *display, XkbAnyEvent *xkb_ev);
#endif

/**
 * Initialises the bell subsystem. This involves intialising
 * XKB (which, despite being a keyboard extension, is the
 * place to look for bell notifications), then asking it
 * to send us bell notifications, and then also switching
 * off the audible bell if we're using a visual one ourselves.
 *
 * Unlike most X extensions we use, we only initialise XKB here
 * (rather than in main()). It's possible that XKB is not
 * installed at all, but if that was known at build time
 * we will have HAVE_XKB undefined, which will cause this
 * function to be a no-op.
 *
 * \param display  The display which is opening
 *
 * \bug There is a line of code that's never run that tells
 * XKB to reset the bell status after we quit. Bill H said
 * (<http://bugzilla.gnome.org/show_bug.cgi?id=99886#c12>)
 * that XFree86's implementation is broken so we shouldn't
 * call it, but that was in 2002. Is it working now?
 */
gboolean meta_bell_init (MetaDisplay *display);

/**
 * Shuts down the bell subsystem.
 *
 * \param display  The display which is closing
 *
 * \bug This is never called! If we had XkbSetAutoResetControls
 * enabled in meta_bell_init(), this wouldn't be a problem, but
 * we don't.
 */
void meta_bell_shutdown (MetaDisplay *display);
