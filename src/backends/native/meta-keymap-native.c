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

#include "config.h"

#include "backends/native/meta-keymap-native.h"
#include "backends/native/meta-seat-native.h"

static const char *option_xkb_layout = "us";
static const char *option_xkb_variant = "";
static const char *option_xkb_options = "";

typedef struct _MetaKeymapNative MetaKeymapNative;

struct _MetaKeymapNative
{
  ClutterKeymap parent_instance;

  struct xkb_keymap *keymap;
};

G_DEFINE_TYPE (MetaKeymapNative, meta_keymap_native,
               CLUTTER_TYPE_KEYMAP)

static void
meta_keymap_native_finalize (GObject *object)
{
  MetaKeymapNative *keymap = META_KEYMAP_NATIVE (object);

  xkb_keymap_unref (keymap->keymap);

  G_OBJECT_CLASS (meta_keymap_native_parent_class)->finalize (object);
}

static gboolean
meta_keymap_native_get_num_lock_state (ClutterKeymap *keymap)
{
  struct xkb_state *xkb_state;
  ClutterSeat *seat;

  seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  xkb_state = meta_seat_native_get_xkb_state (META_SEAT_NATIVE (seat));

  return xkb_state_mod_name_is_active (xkb_state,
                                       XKB_MOD_NAME_NUM,
                                       XKB_STATE_MODS_LATCHED |
                                       XKB_STATE_MODS_LOCKED);
}

static gboolean
meta_keymap_native_get_caps_lock_state (ClutterKeymap *keymap)
{
  struct xkb_state *xkb_state;
  ClutterSeat *seat;

  seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  xkb_state = meta_seat_native_get_xkb_state (META_SEAT_NATIVE (seat));

  return xkb_state_mod_name_is_active (xkb_state,
                                       XKB_MOD_NAME_CAPS,
                                       XKB_STATE_MODS_LATCHED |
                                       XKB_STATE_MODS_LOCKED);
}

static PangoDirection
meta_keymap_native_get_direction (ClutterKeymap *keymap)
{
  return PANGO_DIRECTION_NEUTRAL;
}

static void
meta_keymap_native_class_init (MetaKeymapNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterKeymapClass *keymap_class = CLUTTER_KEYMAP_CLASS (klass);

  object_class->finalize = meta_keymap_native_finalize;

  keymap_class->get_num_lock_state = meta_keymap_native_get_num_lock_state;
  keymap_class->get_caps_lock_state = meta_keymap_native_get_caps_lock_state;
  keymap_class->get_direction = meta_keymap_native_get_direction;
}

static void
meta_keymap_native_init (MetaKeymapNative *keymap)
{
  struct xkb_context *ctx;
  struct xkb_rule_names names;

  names.rules = "evdev";
  names.model = "pc105";
  names.layout = option_xkb_layout;
  names.variant = option_xkb_variant;
  names.options = option_xkb_options;

  ctx = xkb_context_new (XKB_CONTEXT_NO_FLAGS);
  g_assert (ctx);
  keymap->keymap = xkb_keymap_new_from_names (ctx, &names, 0);
  xkb_context_unref (ctx);
}

void
meta_keymap_native_set_keyboard_map (MetaKeymapNative  *keymap,
                                     struct xkb_keymap *xkb_keymap)
{
  if (keymap->keymap)
    xkb_keymap_unref (keymap->keymap);
  keymap->keymap = xkb_keymap_ref (xkb_keymap);
}

struct xkb_keymap *
meta_keymap_native_get_keyboard_map (MetaKeymapNative *keymap)
{
  return keymap->keymap;
}
