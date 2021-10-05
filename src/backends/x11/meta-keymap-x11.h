/*
 * Copyright (C) 2009  Intel Corp.
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
 * Author: Emmanuele Bassi <ebassi@linux.intel.com>
 */

#ifndef META_KEYMAP_X11_H
#define META_KEYMAP_X11_H

#include <glib-object.h>
#include <pango/pango.h>

#include "clutter/clutter.h"

G_BEGIN_DECLS

#define META_TYPE_KEYMAP_X11 (meta_keymap_x11_get_type ())
G_DECLARE_FINAL_TYPE (MetaKeymapX11, meta_keymap_x11,
                      META, KEYMAP_X11, ClutterKeymap)

int      meta_keymap_x11_get_key_group       (MetaKeymapX11       *keymap,
                                              ClutterModifierType  state);
int      meta_keymap_x11_translate_key_state (MetaKeymapX11       *keymap,
                                              guint                hardware_keycode,
                                              ClutterModifierType *modifier_state_p,
                                              ClutterModifierType *mods_p);
gboolean meta_keymap_x11_get_is_modifier     (MetaKeymapX11       *keymap,
                                              int                  keycode);

gboolean meta_keymap_x11_keycode_for_keyval       (MetaKeymapX11    *keymap_x11,
                                                   guint             keyval,
                                                   guint            *keycode_out,
                                                   guint            *level_out);
void     meta_keymap_x11_latch_modifiers          (MetaKeymapX11 *keymap_x11,
                                                   uint32_t          level,
                                                   gboolean          enable);
gboolean meta_keymap_x11_reserve_keycode           (MetaKeymapX11 *keymap_x11,
                                                    guint             keyval,
                                                    guint            *keycode_out);
void     meta_keymap_x11_release_keycode_if_needed (MetaKeymapX11 *keymap_x11,
                                                    guint             keycode);

gboolean meta_keymap_x11_handle_event        (MetaKeymapX11 *keymap_x11,
                                              XEvent        *xevent);

G_END_DECLS

#endif /* META_KEYMAP_X11_H */
