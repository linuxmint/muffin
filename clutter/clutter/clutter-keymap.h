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

#ifndef CLUTTER_KEYMAP_H
#define CLUTTER_KEYMAP_H

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <clutter/clutter-macros.h>

#include <glib-object.h>
#include <pango/pango.h>

typedef struct _ClutterKeymap ClutterKeymap;
typedef struct _ClutterKeymapClass ClutterKeymapClass;

struct _ClutterKeymapClass
{
  GObjectClass parent_class;

  gboolean (* get_num_lock_state)  (ClutterKeymap *keymap);
  gboolean (* get_caps_lock_state) (ClutterKeymap *keymap);
  PangoDirection (* get_direction) (ClutterKeymap *keymap);
};

#define CLUTTER_TYPE_KEYMAP (clutter_keymap_get_type ())
CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterKeymap, clutter_keymap,
			  CLUTTER, KEYMAP,
			  GObject)

CLUTTER_EXPORT
gboolean clutter_keymap_get_num_lock_state  (ClutterKeymap *keymap);

CLUTTER_EXPORT
gboolean clutter_keymap_get_caps_lock_state (ClutterKeymap *keymap);

PangoDirection clutter_keymap_get_direction (ClutterKeymap *keymap);

#endif /* CLUTTER_KEYMAP_H */
