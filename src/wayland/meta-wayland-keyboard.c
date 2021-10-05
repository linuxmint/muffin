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
 * Copyright © 2010-2011 Intel Corporation
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

/* The file is based on src/input.c from Weston */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "backends/meta-backend-private.h"
#include "core/display-private.h"
#include "wayland/meta-wayland-private.h"

#ifdef HAVE_NATIVE_BACKEND
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-event-native.h"
#endif

#define GSD_KEYBOARD_SCHEMA "org.gnome.settings-daemon.peripherals.keyboard"

G_DEFINE_TYPE (MetaWaylandKeyboard, meta_wayland_keyboard,
               META_TYPE_WAYLAND_INPUT_DEVICE)

static void meta_wayland_keyboard_update_xkb_state (MetaWaylandKeyboard *keyboard);
static void notify_modifiers (MetaWaylandKeyboard *keyboard);
static guint evdev_code (const ClutterKeyEvent *event);

static void
unbind_resource (struct wl_resource *resource)
{
  wl_list_remove (wl_resource_get_link (resource));
}

static int
create_anonymous_file (off_t    size,
                       GError **error)
{
  static const char template[] = "mutter-shared-XXXXXX";
  char *path;
  int fd, flags;

  fd = g_file_open_tmp (template, &path, error);

  if (fd == -1)
    return -1;

  unlink (path);
  g_free (path);

  flags = fcntl (fd, F_GETFD);
  if (flags == -1)
    goto err;

  if (fcntl (fd, F_SETFD, flags | FD_CLOEXEC) == -1)
    goto err;

  if (ftruncate (fd, size) < 0)
    goto err;

  return fd;

 err:
  g_set_error_literal (error,
                       G_FILE_ERROR,
                       g_file_error_from_errno (errno),
                       strerror (errno));
  close (fd);

  return -1;
}

static void
send_keymap (MetaWaylandKeyboard *keyboard,
             struct wl_resource  *resource)
{
  MetaWaylandXkbInfo *xkb_info = &keyboard->xkb_info;
  GError *error = NULL;
  int fd;
  char *keymap_area;

  if (!xkb_info->keymap_string)
    return;

  fd = create_anonymous_file (xkb_info->keymap_size, &error);
  if (fd < 0)
    {
      g_warning ("Creating a keymap file for %lu bytes failed: %s",
                 (unsigned long) xkb_info->keymap_size,
                 error->message);
      g_clear_error (&error);
      return;
    }


  keymap_area = mmap (NULL, xkb_info->keymap_size,
                      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (keymap_area == MAP_FAILED)
    {
      g_warning ("Failed to mmap() %lu bytes\n",
                 (unsigned long) xkb_info->keymap_size);
      close (fd);
      return;
    }

  strcpy (keymap_area, xkb_info->keymap_string);

  munmap (keymap_area, xkb_info->keymap_size);

  wl_keyboard_send_keymap (resource,
                           WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                           fd,
                           keyboard->xkb_info.keymap_size);
  close (fd);
}

static void
inform_clients_of_new_keymap (MetaWaylandKeyboard *keyboard)
{
  struct wl_resource *keyboard_resource;

  wl_resource_for_each (keyboard_resource, &keyboard->resource_list)
    send_keymap (keyboard, keyboard_resource);
  wl_resource_for_each (keyboard_resource, &keyboard->focus_resource_list)
    send_keymap (keyboard, keyboard_resource);
}

static void
meta_wayland_keyboard_take_keymap (MetaWaylandKeyboard *keyboard,
				   struct xkb_keymap   *keymap)
{
  MetaWaylandXkbInfo *xkb_info = &keyboard->xkb_info;

  if (keymap == NULL)
    {
      g_warning ("Attempting to set null keymap (compilation probably failed)");
      return;
    }

  g_clear_pointer (&xkb_info->keymap_string, g_free);
  xkb_keymap_unref (xkb_info->keymap);
  xkb_info->keymap = xkb_keymap_ref (keymap);

  meta_wayland_keyboard_update_xkb_state (keyboard);

  xkb_info->keymap_string =
    xkb_keymap_get_as_string (xkb_info->keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
  if (!xkb_info->keymap_string)
    {
      g_warning ("Failed to get string version of keymap");
      return;
    }
  xkb_info->keymap_size = strlen (xkb_info->keymap_string) + 1;

  inform_clients_of_new_keymap (keyboard);

  notify_modifiers (keyboard);
}

static xkb_mod_mask_t
kbd_a11y_apply_mask (MetaWaylandKeyboard *keyboard)
{
  xkb_mod_mask_t latched, locked, depressed, group;
  xkb_mod_mask_t update_mask = 0;

  depressed = xkb_state_serialize_mods(keyboard->xkb_info.state, XKB_STATE_DEPRESSED);
  latched = xkb_state_serialize_mods (keyboard->xkb_info.state, XKB_STATE_MODS_LATCHED);
  locked = xkb_state_serialize_mods (keyboard->xkb_info.state, XKB_STATE_MODS_LOCKED);
  group = xkb_state_serialize_layout (keyboard->xkb_info.state, XKB_STATE_LAYOUT_EFFECTIVE);

  if ((latched & keyboard->kbd_a11y_latched_mods) != keyboard->kbd_a11y_latched_mods)
    update_mask |= XKB_STATE_MODS_LATCHED;

  if ((locked & keyboard->kbd_a11y_locked_mods) != keyboard->kbd_a11y_locked_mods)
    update_mask |= XKB_STATE_MODS_LOCKED;

  if (update_mask)
    {
      latched |= keyboard->kbd_a11y_latched_mods;
      locked |= keyboard->kbd_a11y_locked_mods;
      xkb_state_update_mask (keyboard->xkb_info.state, depressed, latched, locked, 0, 0, group);
    }

  return update_mask;
}

static void
on_keymap_changed (MetaBackend *backend,
                   gpointer     data)
{
  MetaWaylandKeyboard *keyboard = data;

  meta_wayland_keyboard_take_keymap (keyboard, meta_backend_get_keymap (backend));
}

static void
on_keymap_layout_group_changed (MetaBackend *backend,
                                guint        idx,
                                gpointer     data)
{
  MetaWaylandKeyboard *keyboard = data;
  xkb_mod_mask_t depressed_mods;
  xkb_mod_mask_t latched_mods;
  xkb_mod_mask_t locked_mods;
  struct xkb_state *state;

  state = keyboard->xkb_info.state;

  depressed_mods = xkb_state_serialize_mods (state, XKB_STATE_MODS_DEPRESSED);
  latched_mods = xkb_state_serialize_mods (state, XKB_STATE_MODS_LATCHED);
  locked_mods = xkb_state_serialize_mods (state, XKB_STATE_MODS_LOCKED);

  xkb_state_update_mask (state, depressed_mods, latched_mods, locked_mods, 0, 0, idx);
  kbd_a11y_apply_mask (keyboard);

  notify_modifiers (keyboard);
}

static void
keyboard_handle_focus_surface_destroy (struct wl_listener *listener, void *data)
{
  MetaWaylandKeyboard *keyboard = wl_container_of (listener, keyboard,
                                                   focus_surface_listener);

  meta_wayland_keyboard_set_focus (keyboard, NULL);
}

static gboolean
meta_wayland_keyboard_broadcast_key (MetaWaylandKeyboard *keyboard,
                                     uint32_t             time,
                                     uint32_t             key,
                                     uint32_t             state)
{
  struct wl_resource *resource;

  if (!wl_list_empty (&keyboard->focus_resource_list))
    {
      MetaWaylandInputDevice *input_device =
        META_WAYLAND_INPUT_DEVICE (keyboard);
      uint32_t serial;

      serial = meta_wayland_input_device_next_serial (input_device);

      if (state)
        {
          keyboard->key_down_serial = serial;
          keyboard->key_down_keycode = key;
        }
      else
        {
          keyboard->key_up_serial = serial;
          keyboard->key_up_keycode = key;
        }

      wl_resource_for_each (resource, &keyboard->focus_resource_list)
        wl_keyboard_send_key (resource, serial, time, key, state);
    }

  /* Eat the key events if we have a focused surface. */
  return (keyboard->focus_surface != NULL);
}

static gboolean
notify_key (MetaWaylandKeyboard *keyboard,
            const ClutterEvent  *event)
{
  return keyboard->grab->interface->key (keyboard->grab, event);
}

static xkb_mod_mask_t
add_vmod (xkb_mod_mask_t mask,
          xkb_mod_mask_t mod,
          xkb_mod_mask_t vmod,
          xkb_mod_mask_t *added)
{
  if ((mask & mod) && !(mod & *added))
    {
      mask |= vmod;
      *added |= mod;
    }
  return mask;
}

static xkb_mod_mask_t
add_virtual_mods (xkb_mod_mask_t mask)
{
  MetaKeyBindingManager *keys = &(meta_get_display ()->key_binding_manager);
  xkb_mod_mask_t added;
  guint i;
  /* Order is important here: if multiple vmods share the same real
     modifier we only want to add the first. */
  struct {
    xkb_mod_mask_t mod;
    xkb_mod_mask_t vmod;
  } mods[] = {
    { keys->super_mask, keys->virtual_super_mask },
    { keys->hyper_mask, keys->virtual_hyper_mask },
    { keys->meta_mask,  keys->virtual_meta_mask },
  };

  added = 0;
  for (i = 0; i < G_N_ELEMENTS (mods); ++i)
    mask = add_vmod (mask, mods[i].mod, mods[i].vmod, &added);

  return mask;
}

static void
keyboard_send_modifiers (MetaWaylandKeyboard *keyboard,
                         struct wl_resource  *resource,
                         uint32_t             serial)
{
  struct xkb_state *state = keyboard->xkb_info.state;
  xkb_mod_mask_t depressed, latched, locked;

  depressed = add_virtual_mods (xkb_state_serialize_mods (state, XKB_STATE_MODS_DEPRESSED));
  latched = add_virtual_mods (xkb_state_serialize_mods (state, XKB_STATE_MODS_LATCHED));
  locked = add_virtual_mods (xkb_state_serialize_mods (state, XKB_STATE_MODS_LOCKED));

  wl_keyboard_send_modifiers (resource, serial, depressed, latched, locked,
                              xkb_state_serialize_layout (state, XKB_STATE_LAYOUT_EFFECTIVE));
}

static void
meta_wayland_keyboard_broadcast_modifiers (MetaWaylandKeyboard *keyboard)
{
  struct wl_resource *resource;

  if (!wl_list_empty (&keyboard->focus_resource_list))
    {
      MetaWaylandInputDevice *input_device =
        META_WAYLAND_INPUT_DEVICE (keyboard);
      uint32_t serial;

      serial = meta_wayland_input_device_next_serial (input_device);

      wl_resource_for_each (resource, &keyboard->focus_resource_list)
        keyboard_send_modifiers (keyboard, resource, serial);
    }
}

static void
notify_modifiers (MetaWaylandKeyboard *keyboard)
{
  struct xkb_state *state;

  state = keyboard->xkb_info.state;
  keyboard->grab->interface->modifiers (keyboard->grab,
                                        xkb_state_serialize_mods (state, XKB_STATE_MODS_EFFECTIVE));
}

static void
meta_wayland_keyboard_update_xkb_state (MetaWaylandKeyboard *keyboard)
{
  MetaWaylandXkbInfo *xkb_info = &keyboard->xkb_info;
  xkb_mod_mask_t latched, locked, numlock;
  MetaBackend *backend = meta_get_backend ();
  xkb_layout_index_t layout_idx;
  ClutterKeymap *keymap;
  ClutterSeat *seat;

  /* Preserve latched/locked modifiers state */
  if (xkb_info->state)
    {
      latched = xkb_state_serialize_mods (xkb_info->state, XKB_STATE_MODS_LATCHED);
      locked = xkb_state_serialize_mods (xkb_info->state, XKB_STATE_MODS_LOCKED);
      xkb_state_unref (xkb_info->state);
    }
  else
    {
      latched = locked = 0;
    }

  seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  keymap = clutter_seat_get_keymap (seat);
  numlock = (1 <<  xkb_keymap_mod_get_index (xkb_info->keymap, "Mod2"));

  if (clutter_keymap_get_num_lock_state (keymap))
    locked |= numlock;
  else
    locked &= ~numlock;

  xkb_info->state = xkb_state_new (xkb_info->keymap);

  layout_idx = meta_backend_get_keymap_layout_group (backend);
  xkb_state_update_mask (xkb_info->state, 0, latched, locked, 0, 0, layout_idx);

  kbd_a11y_apply_mask (keyboard);
}

static void
on_kbd_a11y_mask_changed (ClutterSeat         *seat,
                          xkb_mod_mask_t       new_latched_mods,
                          xkb_mod_mask_t       new_locked_mods,
                          MetaWaylandKeyboard *keyboard)
{
  xkb_mod_mask_t latched, locked, depressed, group;

  if (keyboard->xkb_info.state == NULL)
    return;

  depressed = xkb_state_serialize_mods(keyboard->xkb_info.state, XKB_STATE_DEPRESSED);
  latched = xkb_state_serialize_mods (keyboard->xkb_info.state, XKB_STATE_MODS_LATCHED);
  locked = xkb_state_serialize_mods (keyboard->xkb_info.state, XKB_STATE_MODS_LOCKED);
  group = xkb_state_serialize_layout (keyboard->xkb_info.state, XKB_STATE_LAYOUT_EFFECTIVE);

  /* Clear previous masks */
  latched &= ~keyboard->kbd_a11y_latched_mods;
  locked &= ~keyboard->kbd_a11y_locked_mods;
  xkb_state_update_mask (keyboard->xkb_info.state, depressed, latched, locked, 0, 0, group);

  /* Apply new masks */
  keyboard->kbd_a11y_latched_mods = new_latched_mods;
  keyboard->kbd_a11y_locked_mods = new_locked_mods;
  kbd_a11y_apply_mask (keyboard);

  notify_modifiers (keyboard);
}

static void
notify_key_repeat_for_resource (MetaWaylandKeyboard *keyboard,
                                struct wl_resource  *keyboard_resource)
{
  if (wl_resource_get_version (keyboard_resource) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION)
    {
      gboolean repeat;
      unsigned int delay, rate;

      repeat = g_settings_get_boolean (keyboard->settings, "repeat");

      if (repeat)
        {
          unsigned int interval;
          interval = g_settings_get_uint (keyboard->settings, "repeat-interval");
          /* Our setting is in the milliseconds between keys. "rate" is the number
           * of keys per second. */
          if (interval > 0)
            rate = (1000 / interval);
          else
            rate = 0;

          delay = g_settings_get_uint (keyboard->settings, "delay");
        }
      else
        {
          rate = 0;
          delay = 0;
        }

      wl_keyboard_send_repeat_info (keyboard_resource, rate, delay);
    }
}

static void
notify_key_repeat (MetaWaylandKeyboard *keyboard)
{
  struct wl_resource *keyboard_resource;

  wl_resource_for_each (keyboard_resource, &keyboard->resource_list)
    {
      notify_key_repeat_for_resource (keyboard, keyboard_resource);
    }

  wl_resource_for_each (keyboard_resource, &keyboard->focus_resource_list)
    {
      notify_key_repeat_for_resource (keyboard, keyboard_resource);
    }
}

static void
settings_changed (GSettings           *settings,
                  const char          *key,
                  gpointer             data)
{
  MetaWaylandKeyboard *keyboard = data;

  notify_key_repeat (keyboard);
}

static gboolean
default_grab_key (MetaWaylandKeyboardGrab *grab,
                  const ClutterEvent      *event)
{
  MetaWaylandKeyboard *keyboard = grab->keyboard;
  gboolean is_press = event->type == CLUTTER_KEY_PRESS;
  guint32 code = 0;
#ifdef HAVE_NATIVE_BACKEND
  MetaBackend *backend = meta_get_backend ();
#endif

  /* Ignore autorepeat events, as autorepeat in Wayland is done on the client
   * side. */
  if (event->key.flags & CLUTTER_EVENT_FLAG_REPEATED)
    return FALSE;

#ifdef HAVE_NATIVE_BACKEND
  if (META_IS_BACKEND_NATIVE (backend))
    code = meta_event_native_get_event_code (event);
  if (code == 0)
#endif
    code = evdev_code (&event->key);

  return meta_wayland_keyboard_broadcast_key (keyboard, event->key.time,
                                              code, is_press);
}

static void
default_grab_modifiers (MetaWaylandKeyboardGrab *grab,
                        ClutterModifierType      modifiers)
{
  meta_wayland_keyboard_broadcast_modifiers (grab->keyboard);
}

static const MetaWaylandKeyboardGrabInterface default_keyboard_grab_interface = {
  default_grab_key,
  default_grab_modifiers
};

void
meta_wayland_keyboard_enable (MetaWaylandKeyboard *keyboard)
{
  MetaBackend *backend = meta_get_backend ();
  ClutterBackend *clutter_backend = clutter_get_default_backend ();

  keyboard->settings = g_settings_new ("org.gnome.desktop.peripherals.keyboard");
  g_signal_connect (keyboard->settings, "changed",
                    G_CALLBACK (settings_changed), keyboard);

  g_signal_connect (backend, "keymap-changed",
                    G_CALLBACK (on_keymap_changed), keyboard);
  g_signal_connect (backend, "keymap-layout-group-changed",
                    G_CALLBACK (on_keymap_layout_group_changed), keyboard);

  g_signal_connect (clutter_backend_get_default_seat (clutter_backend),
		    "kbd-a11y-mods-state-changed",
                    G_CALLBACK (on_kbd_a11y_mask_changed), keyboard);

  meta_wayland_keyboard_take_keymap (keyboard, meta_backend_get_keymap (backend));
}

static void
meta_wayland_xkb_info_destroy (MetaWaylandXkbInfo *xkb_info)
{
  g_clear_pointer (&xkb_info->keymap, xkb_keymap_unref);
  g_clear_pointer (&xkb_info->state, xkb_state_unref);
  g_clear_pointer (&xkb_info->keymap_string, g_free);
}

void
meta_wayland_keyboard_disable (MetaWaylandKeyboard *keyboard)
{
  MetaBackend *backend = meta_get_backend ();

  g_signal_handlers_disconnect_by_func (backend, on_keymap_changed, keyboard);
  g_signal_handlers_disconnect_by_func (backend, on_keymap_layout_group_changed, keyboard);

  meta_wayland_keyboard_end_grab (keyboard);
  meta_wayland_keyboard_set_focus (keyboard, NULL);

  wl_list_remove (&keyboard->resource_list);
  wl_list_init (&keyboard->resource_list);
  wl_list_remove (&keyboard->focus_resource_list);
  wl_list_init (&keyboard->focus_resource_list);

  g_clear_object (&keyboard->settings);
}

static guint
evdev_code (const ClutterKeyEvent *event)
{
  /* clutter-xkb-utils.c adds a fixed offset of 8 to go into XKB's
   * range, so we do the reverse here. */
  return event->hardware_keycode - 8;
}

void
meta_wayland_keyboard_update (MetaWaylandKeyboard *keyboard,
                              const ClutterKeyEvent *event)
{
  gboolean is_press = event->type == CLUTTER_KEY_PRESS;

  /* Only handle real, non-synthetic, events here. The IM is free to reemit
   * key events (incl. modifiers), handling those additionally will result
   * in doubly-pressed keys.
   */
  if ((event->flags &
       (CLUTTER_EVENT_FLAG_SYNTHETIC | CLUTTER_EVENT_FLAG_INPUT_METHOD)) != 0)
    return;

  /* If we get a key event but still have pending modifier state
   * changes from a previous event that didn't get cleared, we need to
   * send that state right away so that the new key event can be
   * interpreted by clients correctly modified. */
  if (keyboard->mods_changed)
    notify_modifiers (keyboard);

  keyboard->mods_changed = xkb_state_update_key (keyboard->xkb_info.state,
                                                 event->hardware_keycode,
                                                 is_press ? XKB_KEY_DOWN : XKB_KEY_UP);
  keyboard->mods_changed |= kbd_a11y_apply_mask (keyboard);
}

gboolean
meta_wayland_keyboard_handle_event (MetaWaylandKeyboard *keyboard,
                                    const ClutterKeyEvent *event)
{
#ifdef WITH_VERBOSE_MODE
  gboolean is_press = event->type == CLUTTER_KEY_PRESS;
#endif
  gboolean handled;

  /* Synthetic key events are for autorepeat. Ignore those, as
   * autorepeat in Wayland is done on the client side. */
  if ((event->flags & CLUTTER_EVENT_FLAG_SYNTHETIC) &&
      !(event->flags & CLUTTER_EVENT_FLAG_INPUT_METHOD))
    return FALSE;

  meta_verbose ("Handling key %s event code %d\n",
		is_press ? "press" : "release",
		event->hardware_keycode);

  handled = notify_key (keyboard, (const ClutterEvent *) event);

  if (handled)
    meta_verbose ("Sent event to wayland client\n");
  else
    meta_verbose ("No wayland surface is focused, continuing normal operation\n");

  if (keyboard->mods_changed != 0)
    {
      notify_modifiers (keyboard);
      keyboard->mods_changed = 0;
    }

  return handled;
}

void
meta_wayland_keyboard_update_key_state (MetaWaylandKeyboard *keyboard,
                                        char                *key_vector,
                                        int                  key_vector_len,
                                        int                  offset)
{
  gboolean mods_changed = FALSE;

  int i;
  for (i = offset; i < key_vector_len * 8; i++)
    {
      gboolean set = (key_vector[i/8] & (1 << (i % 8))) != 0;

      /* The 'offset' parameter allows the caller to have the indices
       * into key_vector to either be X-style (base 8) or evdev (base 0), or
       * something else (unlikely). We subtract 'offset' to convert to evdev
       * style, then add 8 to convert the "evdev" style keycode back to
       * the X-style that xkbcommon expects.
       */
      mods_changed |= xkb_state_update_key (keyboard->xkb_info.state,
                                            i - offset + 8,
                                            set ? XKB_KEY_DOWN : XKB_KEY_UP);
    }

  mods_changed |= kbd_a11y_apply_mask (keyboard);
  if (mods_changed)
    notify_modifiers (keyboard);
}

static void
move_resources (struct wl_list *destination, struct wl_list *source)
{
  wl_list_insert_list (destination, source);
  wl_list_init (source);
}

static void
move_resources_for_client (struct wl_list *destination,
			   struct wl_list *source,
			   struct wl_client *client)
{
  struct wl_resource *resource, *tmp;
  wl_resource_for_each_safe (resource, tmp, source)
    {
      if (wl_resource_get_client (resource) == client)
        {
          wl_list_remove (wl_resource_get_link (resource));
          wl_list_insert (destination, wl_resource_get_link (resource));
        }
    }
}

static void
broadcast_focus (MetaWaylandKeyboard *keyboard,
                 struct wl_resource  *resource)
{
  struct wl_array fake_keys;

  /* We never want to send pressed keys to wayland clients on
   * enter. The protocol says that we should send them, presumably so
   * that clients can trigger their own key repeat routine in case
   * they are given focus and a key is physically pressed.
   *
   * Unfortunately this causes some clients, in particular Xwayland,
   * to register key events that they really shouldn't handle,
   * e.g. on an Alt+Tab keybinding, where Alt is released before Tab,
   * clients would see Tab being pressed on enter followed by a key
   * release event for Tab, meaning that Tab would be processed by
   * the client when it really shouldn't.
   *
   * Since the use case for the pressed keys array on enter seems weak
   * to us, we'll just fake that there are no pressed keys instead
   * which should be spec compliant even if it might not be true.
   */
  wl_array_init (&fake_keys);

  keyboard_send_modifiers (keyboard, resource, keyboard->focus_serial);
  wl_keyboard_send_enter (resource, keyboard->focus_serial,
                          keyboard->focus_surface->resource,
                          &fake_keys);
}

void
meta_wayland_keyboard_set_focus (MetaWaylandKeyboard *keyboard,
                                 MetaWaylandSurface *surface)
{
  MetaWaylandInputDevice *input_device = META_WAYLAND_INPUT_DEVICE (keyboard);

  if (keyboard->focus_surface == surface)
    return;

  if (keyboard->focus_surface != NULL)
    {
      if (!wl_list_empty (&keyboard->focus_resource_list))
        {
          struct wl_resource *resource;
          uint32_t serial;

          serial = meta_wayland_input_device_next_serial (input_device);

          wl_resource_for_each (resource, &keyboard->focus_resource_list)
            {
              wl_keyboard_send_leave (resource, serial,
                                      keyboard->focus_surface->resource);
            }

          move_resources (&keyboard->resource_list,
                          &keyboard->focus_resource_list);
        }

      wl_list_remove (&keyboard->focus_surface_listener.link);
      keyboard->focus_surface = NULL;
    }

  if (surface != NULL)
    {
      struct wl_resource *focus_surface_resource;

      keyboard->focus_surface = surface;
      focus_surface_resource = keyboard->focus_surface->resource;
      wl_resource_add_destroy_listener (focus_surface_resource,
                                        &keyboard->focus_surface_listener);

      move_resources_for_client (&keyboard->focus_resource_list,
                                 &keyboard->resource_list,
                                 wl_resource_get_client (focus_surface_resource));

      /* Make sure a11y masks are applied before braodcasting modifiers */
      kbd_a11y_apply_mask (keyboard);

      if (!wl_list_empty (&keyboard->focus_resource_list))
        {
          struct wl_resource *resource;

          keyboard->focus_serial =
            meta_wayland_input_device_next_serial (input_device);

          wl_resource_for_each (resource, &keyboard->focus_resource_list)
            {
              broadcast_focus (keyboard, resource);
            }
        }
    }
}

struct wl_client *
meta_wayland_keyboard_get_focus_client (MetaWaylandKeyboard *keyboard)
{
  if (keyboard->focus_surface)
    return wl_resource_get_client (keyboard->focus_surface->resource);
  else
    return NULL;
}

static void
keyboard_release (struct wl_client *client,
                  struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct wl_keyboard_interface keyboard_interface = {
  keyboard_release,
};

void
meta_wayland_keyboard_create_new_resource (MetaWaylandKeyboard *keyboard,
                                           struct wl_client    *client,
                                           struct wl_resource  *seat_resource,
                                           uint32_t id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client, &wl_keyboard_interface,
                                 wl_resource_get_version (seat_resource), id);
  wl_resource_set_implementation (resource, &keyboard_interface,
                                  keyboard, unbind_resource);

  send_keymap (keyboard, resource);

  notify_key_repeat_for_resource (keyboard, resource);

  if (keyboard->focus_surface &&
      wl_resource_get_client (keyboard->focus_surface->resource) == client)
    {
      wl_list_insert (&keyboard->focus_resource_list,
                      wl_resource_get_link (resource));
      broadcast_focus (keyboard, resource);
    }
  else
    {
      wl_list_insert (&keyboard->resource_list,
                      wl_resource_get_link (resource));
    }
}

gboolean
meta_wayland_keyboard_can_popup (MetaWaylandKeyboard *keyboard,
                                 uint32_t             serial)
{
  return (keyboard->key_down_serial == serial ||
          ((keyboard->key_down_keycode == keyboard->key_up_keycode) &&
           keyboard->key_up_serial == serial));
}

void
meta_wayland_keyboard_start_grab (MetaWaylandKeyboard     *keyboard,
                                  MetaWaylandKeyboardGrab *grab)
{
  meta_wayland_keyboard_set_focus (keyboard, NULL);
  keyboard->grab = grab;
  grab->keyboard = keyboard;
}

void
meta_wayland_keyboard_end_grab (MetaWaylandKeyboard *keyboard)
{
  keyboard->grab = &keyboard->default_grab;
}

static void
meta_wayland_keyboard_init (MetaWaylandKeyboard *keyboard)
{
  wl_list_init (&keyboard->resource_list);
  wl_list_init (&keyboard->focus_resource_list);

  keyboard->default_grab.interface = &default_keyboard_grab_interface;
  keyboard->default_grab.keyboard = keyboard;
  keyboard->grab = &keyboard->default_grab;

  keyboard->focus_surface_listener.notify =
    keyboard_handle_focus_surface_destroy;
}

static void
meta_wayland_keyboard_finalize (GObject *object)
{
  MetaWaylandKeyboard *keyboard = META_WAYLAND_KEYBOARD (object);

  meta_wayland_xkb_info_destroy (&keyboard->xkb_info);
}

static void
meta_wayland_keyboard_class_init (MetaWaylandKeyboardClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_wayland_keyboard_finalize;
}
