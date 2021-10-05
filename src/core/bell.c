/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter visual bell */

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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * SECTION:bell
 * @short_description: Ring the bell or flash the screen
 *
 * Sometimes, X programs "ring the bell", whatever that means. Mutter lets
 * the user configure the bell to be audible or visible (aka visual), and
 * if it's visual it can be configured to be frame-flash or fullscreen-flash.
 * We never get told about audible bells; X handles them just fine by itself.
 *
 * Visual bells come in at meta_bell_notify(), which checks we are actually
 * in visual mode and calls through to bell_visual_notify(). That
 * function then checks what kind of visual flash you like, and calls either
 * bell_flash_fullscreen()-- which calls bell_flash_screen() to do
 * its work-- or bell_flash_frame(), which flashes the focussed window
 * using bell_flash_window(), unless there is no such window, in
 * which case it flashes the screen instead.
 *
 * The visual bell was the result of a discussion in Bugzilla here:
 * <http://bugzilla.gnome.org/show_bug.cgi?id=99886>.
 *
 * Several of the functions in this file are ifdeffed out entirely if we are
 * found not to have the XKB extension, which is required to do these clever
 * things with bells; some others are entirely no-ops in that case.
 */

#include "config.h"

#include "core/bell.h"

#include "compositor/compositor-private.h"
#include "core/util-private.h"
#include "core/window-private.h"
#include "meta/compositor.h"

G_DEFINE_TYPE (MetaBell, meta_bell, G_TYPE_OBJECT)

enum
{
  IS_AUDIBLE_CHANGED,
  LAST_SIGNAL
};

static guint bell_signals [LAST_SIGNAL] = { 0 };

static void
prefs_changed_callback (MetaPreference pref,
                        gpointer       data)
{
  MetaBell *bell = data;

  if (pref == META_PREF_AUDIBLE_BELL)
    {
      g_signal_emit (bell, bell_signals[IS_AUDIBLE_CHANGED], 0,
                     meta_prefs_bell_is_audible ());
    }
}

static void
meta_bell_finalize (GObject *object)
{
  MetaBell *bell = META_BELL (object);

  meta_prefs_remove_listener (prefs_changed_callback, bell);

  G_OBJECT_CLASS (meta_bell_parent_class)->finalize (object);
}

static void
meta_bell_class_init (MetaBellClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_bell_finalize;

  bell_signals[IS_AUDIBLE_CHANGED] =
    g_signal_new ("is-audible-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  G_TYPE_BOOLEAN);
}

static void
meta_bell_init (MetaBell *bell)
{
  meta_prefs_add_listener (prefs_changed_callback, bell);
}

MetaBell *
meta_bell_new (MetaDisplay *display)
{
  return g_object_new (META_TYPE_BELL, NULL);
}

/**
 * bell_flash_fullscreen:
 * @display: The display the event came in on
 * @xkb_ev: The bell event
 *
 * Flashes one screen, or all screens, in response to a bell event.
 * If the event is on a particular window, flash the screen that
 * window is on. Otherwise, flash every screen on this display.
 *
 * If the configure script found we had no XKB, this does not exist.
 */
static void
bell_flash_fullscreen (MetaDisplay *display)
{
  meta_compositor_flash_display (display->compositor, display);
}

static void
bell_flash_window (MetaWindow *window)
{
  meta_compositor_flash_window (window->display->compositor, window);
}

/**
 * bell_flash_frame:
 * @display:  The display the bell event came in on
 * @xkb_ev:   The bell event we just received
 *
 * Flashes the frame of the focused window. If there is no focused window,
 * flashes the screen.
 */
static void
bell_flash_frame (MetaDisplay *display,
                  MetaWindow  *window)
{
  if (window)
    bell_flash_window (window);
  else
    bell_flash_fullscreen (display);
}

/**
 * bell_visual_notify:
 * @display: The display the bell event came in on
 * @xkb_ev: The bell event we just received
 *
 * Gives the user some kind of visual bell substitute, in response to a
 * bell event. What this is depends on the "visual bell type" pref.
 */
static void
bell_visual_notify (MetaDisplay *display,
                    MetaWindow  *window)
{
  switch (meta_prefs_get_visual_bell_type ())
    {
    case G_DESKTOP_VISUAL_BELL_FULLSCREEN_FLASH:
      bell_flash_fullscreen (display);
      break;
    case G_DESKTOP_VISUAL_BELL_FRAME_FLASH:
      bell_flash_frame (display, window);
      break;
    }
}

static gboolean
bell_audible_notify (MetaDisplay *display,
                     MetaWindow  *window)
{
  MetaSoundPlayer *player;

  player = meta_display_get_sound_player (display);
  meta_sound_player_play_from_theme (player,
                                     "bell-window-system",
                                     _("Bell event"),
                                     NULL);
  return TRUE;
}

gboolean
meta_bell_notify (MetaDisplay *display,
                  MetaWindow  *window)
{
  /* flash something */
  if (meta_prefs_get_visual_bell ())
    bell_visual_notify (display, window);

  if (meta_prefs_bell_is_audible ())
    return bell_audible_notify (display, window);

  return TRUE;
}
