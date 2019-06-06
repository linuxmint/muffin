/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Muffin X error handling */

/* 
 * Copyright (C) 2001 Havoc Pennington, error trapping inspired by GDK
 * code copyrighted by the GTK team.
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
 * SECTION:errors
 * @title: Errors
 * @short_description: Muffin X error handling
 */

#include <config.h>
#include <meta/errors.h>
#include "display-private.h"
#include <errno.h>
#include <stdlib.h>
#include <gdk/gdkx.h>

/* In GTK+-3.0, the error trapping code was significantly rewritten. The new code
 * has some neat features (like knowing automatically if a sync is needed or not
 * and handling errors asynchronously when the error code isn't needed immediately),
 * but it's basically incompatible with the hacks we played with GTK+-2.0 to
 * use a custom error handler along with gdk_error_trap_push().
 *
 * Since the main point of our custom error trap was to get the error logged
 * to the right place, with GTK+-3.0 we simply omit our own error handler and
 * use the GTK+ handling straight-up.
 * (See https://bugzilla.gnome.org/show_bug.cgi?id=630216 for restoring logging.)
 */

void
meta_error_trap_push (MetaDisplay *display)
{
  gdk_x11_display_error_trap_push (display->gdk_display);
}

void
meta_error_trap_pop (MetaDisplay *display)
{
  gdk_x11_display_error_trap_pop_ignored (display->gdk_display);
}

void
meta_error_trap_push_with_return (MetaDisplay *display)
{
  gdk_x11_display_error_trap_push (display->gdk_display);
}

int
meta_error_trap_pop_with_return  (MetaDisplay *display)
{
  return gdk_x11_display_error_trap_pop (display->gdk_display);
}
