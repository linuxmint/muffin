/*
 * Wayland Support
 *
 * Copyright (C) 2013 Intel Corporation
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

/*
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2012 Collabora, Ltd.
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef META_WAYLAND_KEYBOARD_H
#define META_WAYLAND_KEYBOARD_H

#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>

#include "clutter/clutter.h"
#include "core/meta-anonymous-file.h"
#include "wayland/meta-wayland-types.h"

#define META_TYPE_WAYLAND_KEYBOARD (meta_wayland_keyboard_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandKeyboard, meta_wayland_keyboard,
                      META, WAYLAND_KEYBOARD,
                      MetaWaylandInputDevice)

struct _MetaWaylandKeyboardGrabInterface
{
  gboolean (*key)       (MetaWaylandKeyboardGrab *grab,
                         const ClutterEvent      *event);
  void     (*modifiers) (MetaWaylandKeyboardGrab *grab,
                         ClutterModifierType      modifiers);
};

struct _MetaWaylandKeyboardGrab
{
  const MetaWaylandKeyboardGrabInterface *interface;
  MetaWaylandKeyboard *keyboard;
};

typedef struct
{
  struct xkb_keymap *keymap;
  struct xkb_state *state;
  MetaAnonymousFile *keymap_rofile;
} MetaWaylandXkbInfo;

struct _MetaWaylandKeyboard
{
  MetaWaylandInputDevice parent;

  struct wl_list resource_list;
  struct wl_list focus_resource_list;

  MetaWaylandSurface *focus_surface;
  struct wl_listener focus_surface_listener;
  uint32_t focus_serial;

  uint32_t key_down_keycode;
  uint32_t key_down_serial;

  uint32_t key_up_keycode;
  uint32_t key_up_serial;

  struct wl_array pressed_keys;

  MetaWaylandXkbInfo xkb_info;
  enum xkb_state_component mods_changed;
  xkb_mod_mask_t kbd_a11y_latched_mods;
  xkb_mod_mask_t kbd_a11y_locked_mods;

  /* Set while a patched keysym-typing keymap is active: the layout keymap
   * to return to, the deferred restore, and the symbol/keycode currently
   * patched in (see meta_wayland_keyboard_maybe_type_keysym). */
  struct xkb_keymap *saved_keymap;
  guint temp_keymap_timeout_id;
  gboolean typing_temp_keymap;
  uint32_t temp_keysym;
  uint32_t temp_evdev_code;

  MetaWaylandKeyboardGrab *grab;
  MetaWaylandKeyboardGrab default_grab;

  GSettings *settings;
};

void meta_wayland_keyboard_enable (MetaWaylandKeyboard *keyboard);

void meta_wayland_keyboard_disable (MetaWaylandKeyboard *keyboard);

void meta_wayland_keyboard_update (MetaWaylandKeyboard *keyboard,
                                   const ClutterKeyEvent *event);

gboolean meta_wayland_keyboard_handle_event (MetaWaylandKeyboard *keyboard,
                                             const ClutterKeyEvent *event);
void meta_wayland_keyboard_update_key_state (MetaWaylandKeyboard *compositor,
                                             char                *key_vector,
                                             int                  key_vector_len,
                                             int                  offset);

void meta_wayland_keyboard_set_focus (MetaWaylandKeyboard *keyboard,
                                      MetaWaylandSurface *surface);

struct wl_client * meta_wayland_keyboard_get_focus_client (MetaWaylandKeyboard *keyboard);

void meta_wayland_keyboard_create_new_resource (MetaWaylandKeyboard *keyboard,
                                                struct wl_client    *client,
                                                struct wl_resource  *seat_resource,
                                                uint32_t id);

gboolean meta_wayland_keyboard_can_grab_surface (MetaWaylandKeyboard *keyboard,
                                                 MetaWaylandSurface  *surface,
                                                 uint32_t             serial);

gboolean meta_wayland_keyboard_can_popup (MetaWaylandKeyboard *keyboard,
                                          uint32_t             serial);

void meta_wayland_keyboard_start_grab (MetaWaylandKeyboard     *keyboard,
                                       MetaWaylandKeyboardGrab *grab);
void meta_wayland_keyboard_end_grab   (MetaWaylandKeyboard     *keyboard);

/* Like start_grab, but does not clear the focused surface's keyboard focus.
 * The input-method grab redirects physical keys to the input method, yet the
 * focused surface must keep focus to receive the keys the input method
 * re-injects through virtual-keyboard. */
void meta_wayland_keyboard_start_grab_no_focus_change (MetaWaylandKeyboard     *keyboard,
                                                       MetaWaylandKeyboardGrab *grab);

/* Deliver a re-injected key (from the input method's virtual-keyboard) directly
 * to the focused surface, bypassing the seat. */
void meta_wayland_keyboard_inject_key (MetaWaylandKeyboard *keyboard,
                                       uint32_t             time,
                                       uint32_t             key,
                                       uint32_t             state);

/* Type a keysym absent from the current layout by briefly swapping in a
 * temporary keymap that contains it (KWin's approach). Returns FALSE when the
 * keysym is reachable through the layout - the caller should use a virtual
 * input device for those, which keeps normal key semantics. */
gboolean meta_wayland_keyboard_maybe_type_keysym (MetaWaylandKeyboard *keyboard,
                                                  uint32_t             keysym);

/* Helpers shared with the input-method keyboard grab. */
guint meta_wayland_keyboard_get_key_evdev_code (const ClutterEvent *event);
void  meta_wayland_keyboard_get_modifiers (MetaWaylandKeyboard *keyboard,
                                           uint32_t            *mods_depressed,
                                           uint32_t            *mods_latched,
                                           uint32_t            *mods_locked,
                                           uint32_t            *group);
void  meta_wayland_keyboard_get_repeat_info (MetaWaylandKeyboard *keyboard,
                                             int32_t             *rate,
                                             int32_t             *delay);

#endif /* META_WAYLAND_KEYBOARD_H */
