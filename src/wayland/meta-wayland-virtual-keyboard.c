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

/* zwp_virtual_keyboard_manager_v1 lets a client (here, fcitx via its
 * waylandim frontend) inject key events into the compositor as if from a
 * physical keyboard. It is the channel an external input method uses to
 * re-inject the keys it chose not to consume; each is routed to the focused
 * surface or Cinnamon actor via meta_wayland_input_method_reinject_key.
 */

#include "config.h"

#include "wayland/meta-wayland-virtual-keyboard.h"

#include <unistd.h>
#include <wayland-server.h>

#include "meta/util.h"
#include "wayland/meta-wayland-input-device.h"
#include "wayland/meta-wayland-input-method.h"
#include "wayland/meta-wayland-keyboard.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-seat.h"
#include "wayland/meta-wayland-versions.h"

#include "virtual-keyboard-unstable-v1-server-protocol.h"

struct _MetaWaylandVirtualKeyboardManager
{
  MetaWaylandSeat *seat;
};

typedef struct
{
  MetaWaylandVirtualKeyboardManager *manager;
  gboolean has_keymap;
} MetaWaylandVirtualKeyboard;

static void
virtual_keyboard_keymap (struct wl_client   *client,
                         struct wl_resource *resource,
                         uint32_t            format,
                         int32_t             fd,
                         uint32_t            size)
{
  MetaWaylandVirtualKeyboard *keyboard = wl_resource_get_user_data (resource);

  /* We inject by evdev keycode into the compositor's active layout; the
   * uploaded keymap contents are not needed because the input method was
   * handed the compositor's own keymap (over the input-method keyboard
   * grab), so its keycodes already live in that layout's space. We only
   * track that a keymap was provided, as the protocol requires before keys.
   *
   * This is a hard limitation of the re-injection model: a keysym with no
   * keycode in the active layout cannot be expressed (KWin goes as far as
   * compiling a temporary keymap and swapping it onto the seat for that
   * case; wlroots honors the uploaded keymap as a distinct device). It
   * cannot arise from fcitx's re-injection - those keycodes came from our
   * own keymap - only from an engine synthesizing foreign keysyms, which
   * fcitx delivers as commit strings instead. */
  keyboard->has_keymap = (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1);
  close (fd);
}

static void
virtual_keyboard_key (struct wl_client   *client,
                      struct wl_resource *resource,
                      uint32_t            time,
                      uint32_t            key,
                      uint32_t            state)
{
  MetaWaylandVirtualKeyboard *keyboard = wl_resource_get_user_data (resource);
  MetaWaylandSeat *seat = keyboard->manager->seat;

  if (!keyboard->has_keymap)
    {
      wl_resource_post_error (resource,
                              ZWP_VIRTUAL_KEYBOARD_V1_ERROR_NO_KEYMAP,
                              "key event sent before a keymap was set");
      return;
    }

  if (!seat->input_method)
    return;

  /* Route the re-injected key to wherever input focus is (an app surface or a
   * Cinnamon actor). The input method bypasses the seat's key path, so this avoids
   * the seat repeat timer / pressed-key dedup that the physical key already
   * triggered before the grab ate it. virtual-keyboard-v1 uses evdev codes. */
  meta_wayland_input_method_reinject_key (seat->input_method, time, key, state);
}

static void
virtual_keyboard_modifiers (struct wl_client   *client,
                            struct wl_resource *resource,
                            uint32_t            mods_depressed,
                            uint32_t            mods_latched,
                            uint32_t            mods_locked,
                            uint32_t            group)
{
  MetaWaylandVirtualKeyboard *keyboard = wl_resource_get_user_data (resource);

  if (!keyboard->has_keymap)
    {
      wl_resource_post_error (resource,
                              ZWP_VIRTUAL_KEYBOARD_V1_ERROR_NO_KEYMAP,
                              "modifiers event sent before a keymap was set");
      return;
    }

  /* Deliberately not applied. fcitx only ever sends this to mirror back the
   * physical modifier state we just delivered over the keyboard grab
   * (modifiersCallback in its waylandimserverv2.cpp) - information the
   * re-injection path already derives from the seat's own xkb state, which is
   * where that mirror was computed from in the first place. */
}

static void
virtual_keyboard_destroy (struct wl_client   *client,
                          struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct zwp_virtual_keyboard_v1_interface virtual_keyboard_impl = {
  virtual_keyboard_keymap,
  virtual_keyboard_key,
  virtual_keyboard_modifiers,
  virtual_keyboard_destroy,
};

static void
virtual_keyboard_destructor (struct wl_resource *resource)
{
  MetaWaylandVirtualKeyboard *keyboard = wl_resource_get_user_data (resource);

  g_free (keyboard);
}

static void
virtual_keyboard_manager_create_virtual_keyboard (struct wl_client   *client,
                                                  struct wl_resource *manager_resource,
                                                  struct wl_resource *seat_resource,
                                                  uint32_t            id)
{
  MetaWaylandVirtualKeyboardManager *manager =
    wl_resource_get_user_data (manager_resource);
  MetaWaylandVirtualKeyboard *keyboard;
  struct wl_resource *resource;

  /* No unauthorized check, and seat_resource is ignored: authorization is
   * enforced at the registry instead (meta_wayland_global_filter only
   * advertises this global to the compositor-launched fcitx client). */
  keyboard = g_new0 (MetaWaylandVirtualKeyboard, 1);
  keyboard->manager = manager;

  resource = wl_resource_create (client,
                                 &zwp_virtual_keyboard_v1_interface,
                                 wl_resource_get_version (manager_resource),
                                 id);
  wl_resource_set_implementation (resource, &virtual_keyboard_impl,
                                  keyboard, virtual_keyboard_destructor);
}

static const struct zwp_virtual_keyboard_manager_v1_interface virtual_keyboard_manager_impl = {
  virtual_keyboard_manager_create_virtual_keyboard,
};

static void
bind_virtual_keyboard_manager (struct wl_client *client,
                               void             *data,
                               uint32_t          version,
                               uint32_t          id)
{
  MetaWaylandVirtualKeyboardManager *manager = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &zwp_virtual_keyboard_manager_v1_interface,
                                 version, id);
  wl_resource_set_implementation (resource, &virtual_keyboard_manager_impl,
                                  manager, NULL);
}

MetaWaylandVirtualKeyboardManager *
meta_wayland_virtual_keyboard_manager_new (MetaWaylandSeat *seat)
{
  MetaWaylandVirtualKeyboardManager *manager;

  manager = g_new0 (MetaWaylandVirtualKeyboardManager, 1);
  manager->seat = seat;

  return manager;
}

void
meta_wayland_virtual_keyboard_manager_destroy (MetaWaylandVirtualKeyboardManager *manager)
{
  if (!manager)
    return;

  g_free (manager);
}

void
meta_wayland_virtual_keyboard_manager_init (MetaWaylandCompositor *compositor)
{
  /* Only advertise the key-injection global in fcitx sessions. ibus and
   * IM-less sessions never expose it - and even in fcitx sessions,
   * meta_wayland_global_filter hides it from every client except the
   * compositor-launched fcitx itself. */
  if (!meta_im_mode_is_fcitx ())
    return;

  wl_global_create (compositor->wayland_display,
                    &zwp_virtual_keyboard_manager_v1_interface,
                    META_ZWP_VIRTUAL_KEYBOARD_MANAGER_V1_VERSION,
                    compositor->seat->virtual_keyboard_manager,
                    bind_virtual_keyboard_manager);
}
