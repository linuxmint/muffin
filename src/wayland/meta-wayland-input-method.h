/*
 * Wayland Support
 *
 * Copyright (C) 2026 the Cinnamon team
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
 */

#ifndef META_WAYLAND_INPUT_METHOD_H
#define META_WAYLAND_INPUT_METHOD_H

#include <clutter/clutter.h>

#include "wayland/meta-wayland-types.h"

MetaWaylandInputMethod * meta_wayland_input_method_new (MetaWaylandSeat *seat);

void meta_wayland_input_method_destroy (MetaWaylandInputMethod *im);

void meta_wayland_input_method_manager_init (MetaWaylandCompositor *compositor);

/* TRUE when an input-method client (fcitx) is bound to the seat. */
gboolean meta_wayland_input_method_has_client (MetaWaylandInputMethod *im);

/* Route a key fcitx re-injected (via virtual-keyboard) to the focused app
 * surface or Cinnamon's Clutter actor, depending on where input focus is. */
void meta_wayland_input_method_reinject_key (MetaWaylandInputMethod *im,
                                             uint32_t                time,
                                             uint32_t                key,
                                             uint32_t                state);

/* The MetaWaylandInputMethod is itself a ClutterInputMethod; this returns it
 * as such so the compositor can install it as the Clutter backend input
 * method in fcitx mode. */
ClutterInputMethod * meta_wayland_input_method_get_clutter_backend (MetaWaylandInputMethod *im);

/* Activation lifecycle + state, driven by the ClutterInputMethod vfuncs. */
void meta_wayland_input_method_activate   (MetaWaylandInputMethod *im);
void meta_wayland_input_method_deactivate (MetaWaylandInputMethod *im);
void meta_wayland_input_method_send_done  (MetaWaylandInputMethod *im);
void meta_wayland_input_method_send_surrounding_text (MetaWaylandInputMethod *im,
                                                      const char *text,
                                                      uint32_t    cursor,
                                                      uint32_t    anchor);
void meta_wayland_input_method_send_content_type (MetaWaylandInputMethod *im,
                                                  uint32_t                hint,
                                                  uint32_t                purpose);

#endif /* META_WAYLAND_INPUT_METHOD_H */
