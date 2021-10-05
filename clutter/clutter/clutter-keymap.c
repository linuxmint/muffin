/*
 * Copyright (C) 2018 Red Hat
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "clutter-build-config.h"

#include "clutter-keymap.h"
#include "clutter-private.h"

G_DEFINE_ABSTRACT_TYPE (ClutterKeymap, clutter_keymap, G_TYPE_OBJECT)

enum
{
  STATE_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0, };

static void
clutter_keymap_class_init (ClutterKeymapClass *klass)
{
  signals[STATE_CHANGED] =
    g_signal_new (I_("state-changed"),
		  G_TYPE_FROM_CLASS (klass),
		  G_SIGNAL_RUN_FIRST,
		  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE, 0);
}

static void
clutter_keymap_init (ClutterKeymap *keymap)
{
}

gboolean
clutter_keymap_get_num_lock_state (ClutterKeymap *keymap)
{
  return CLUTTER_KEYMAP_GET_CLASS (keymap)->get_num_lock_state (keymap);
}

gboolean
clutter_keymap_get_caps_lock_state (ClutterKeymap *keymap)
{
  return CLUTTER_KEYMAP_GET_CLASS (keymap)->get_caps_lock_state (keymap);
}

PangoDirection
clutter_keymap_get_direction (ClutterKeymap *keymap)
{
  return CLUTTER_KEYMAP_GET_CLASS (keymap)->get_direction (keymap);
}
