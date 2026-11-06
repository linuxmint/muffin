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
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "backends/meta-backend-private.h"
#include "core/display-private.h"
#include "core/meta-anonymous-file.h"
#include "wayland/meta-wayland-private.h"

#ifdef HAVE_NATIVE_BACKEND
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-event-native.h"
#endif

#define GSD_KEYBOARD_SCHEMA "org.cinnamon.settings-daemon.peripherals.keyboard"

G_DEFINE_TYPE (MetaWaylandKeyboard, meta_wayland_keyboard,
               META_TYPE_WAYLAND_INPUT_DEVICE)

static void meta_wayland_keyboard_update_xkb_state (MetaWaylandKeyboard *keyboard);
static void notify_modifiers (MetaWaylandKeyboard *keyboard);
static void meta_wayland_keyboard_broadcast_modifiers (MetaWaylandKeyboard *keyboard);
static guint evdev_code (const ClutterKeyEvent *event);

static void
unbind_resource (struct wl_resource *resource)
{
  wl_list_remove (wl_resource_get_link (resource));
}

static void
send_keymap (MetaWaylandKeyboard *keyboard,
             struct wl_resource  *resource)
{
  MetaWaylandXkbInfo *xkb_info = &keyboard->xkb_info;
  int fd;
  size_t size;
  MetaAnonymousFileMapmode mapmode;

  if (wl_resource_get_version (resource) < 7)
    mapmode = META_ANONYMOUS_FILE_MAPMODE_SHARED;
  else
    mapmode = META_ANONYMOUS_FILE_MAPMODE_PRIVATE;

  fd = meta_anonymous_file_open_fd (xkb_info->keymap_rofile, mapmode);
  size = meta_anonymous_file_size (xkb_info->keymap_rofile);


  if (fd == -1)
    {
      g_warning ("Creating a keymap file failed: %s", strerror (errno));
      return;
    }

  wl_keyboard_send_keymap (resource,
                           WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                           fd, size);

  meta_anonymous_file_close_fd (fd);
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
  char *keymap_string;
  size_t keymap_size;

  if (keymap == NULL)
    {
      g_warning ("Attempting to set null keymap (compilation probably failed)");
      return;
    }

  /* An externally driven keymap change (e.g. a layout switch) supersedes any
   * pending temporary typing keymap: forget the saved layout so the deferred
   * restore doesn't revert to it. */
  if (!keyboard->typing_temp_keymap)
    {
      g_clear_pointer (&keyboard->saved_keymap, xkb_keymap_unref);
      g_clear_handle_id (&keyboard->temp_keymap_timeout_id, g_source_remove);
    }

  xkb_keymap_unref (xkb_info->keymap);
  xkb_info->keymap = xkb_keymap_ref (keymap);

  meta_wayland_keyboard_update_xkb_state (keyboard);

  keymap_string =
    xkb_keymap_get_as_string (xkb_info->keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
  if (!keymap_string)
    {
      g_warning ("Failed to get string version of keymap");
      return;
    }
  keymap_size = strlen (keymap_string) + 1;

  g_clear_pointer (&xkb_info->keymap_rofile, meta_anonymous_file_free);
  xkb_info->keymap_rofile =
    meta_anonymous_file_new (keymap_size, (const uint8_t *) keymap_string);

  free (keymap_string);

  if (!xkb_info->keymap_rofile)
    {
      g_warning ("Failed to create anonymous file for keymap");
      return;
    }

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

void
meta_wayland_keyboard_inject_key (MetaWaylandKeyboard *keyboard,
                                  uint32_t             time,
                                  uint32_t             key,
                                  uint32_t             state)
{
  /* Deliver a key the input method re-injected (via virtual-keyboard) straight
   * to the focused surface. The physical key was already processed by the seat
   * (repeat timer, pressed-key count, xkb) before the input-method grab ate it,
   * so re-injecting through the seat would collide with that state; delivering
   * at the wayland layer avoids the loop, the dedup, and the seat repeat timer.
   * The client does its own key repeat from the single press/release pair.
   *
   * While the grab is active, physical modifier changes were forwarded to the
   * input method rather than the surface, so the surface's modifier view is
   * stale. keyboard->xkb_info.state does track them, so sync it first — without
   * this, re-injected keys arrive unmodified (no Shift, etc.). */
  meta_wayland_keyboard_broadcast_modifiers (keyboard);
  meta_wayland_keyboard_broadcast_key (keyboard, time, key, state);
}

/* An X11 client can only address keycodes 8..255. */
#define TYPE_KEYSYM_MAX_XKB_KEYCODE 255

static gboolean
keymap_has_keysym_in_current_layout (struct xkb_keymap *keymap,
                                     struct xkb_state  *state,
                                     xkb_keysym_t       keysym)
{
  xkb_layout_index_t layout;
  xkb_keycode_t keycode, min_keycode, max_keycode;

  layout = xkb_state_serialize_layout (state, XKB_STATE_LAYOUT_EFFECTIVE);
  min_keycode = xkb_keymap_min_keycode (keymap);
  max_keycode = xkb_keymap_max_keycode (keymap);
  for (keycode = min_keycode; keycode < max_keycode; keycode++)
    {
      gint num_levels, level;

      num_levels = xkb_keymap_num_levels_for_key (keymap, keycode, layout);
      for (level = 0; level < num_levels; level++)
        {
          const xkb_keysym_t *syms;
          int num_syms, sym;

          num_syms = xkb_keymap_key_get_syms_by_level (keymap, keycode,
                                                       layout, level, &syms);
          for (sym = 0; sym < num_syms; sym++)
            {
              if (syms[sym] == keysym)
                return TRUE;
            }
        }
    }

  return FALSE;
}

static gboolean
keycode_has_any_syms (struct xkb_keymap *keymap,
                      xkb_keycode_t      keycode)
{
  gint num_layouts, layout;

  num_layouts = xkb_keymap_num_layouts_for_key (keymap, keycode);
  for (layout = 0; layout < num_layouts; layout++)
    {
      gint num_levels, level;

      num_levels = xkb_keymap_num_levels_for_key (keymap, keycode, layout);
      for (level = 0; level < num_levels; level++)
        {
          const xkb_keysym_t *syms;

          if (xkb_keymap_key_get_syms_by_level (keymap, keycode,
                                                layout, level, &syms) > 0)
            return TRUE;
        }
    }

  return FALSE;
}

/* Patch a copy of @base so that some X11-addressable keycode produces
 * @keysym, returning it and the keycode used. A KWin-style minimal one-key
 * keymap does NOT work here: its keycode range collapses to [key,key],
 * which XWayland cannot represent as an X keymap (the X keycode range is a
 * fixed structural property), so X11 clients never saw the symbol. Editing
 * the real keymap keeps the range, the layout, and every other key intact
 * while the patch is active.
 *
 * The insertion targets an evdev keycode that has a name in the keymap but
 * no symbols in any layout (standard evdev keymaps name every code up to
 * 255 while leaving many unmapped), so a symbols-section entry for it is
 * the first definition - no conflicts to resolve. */
static struct xkb_keymap *
create_patched_keymap (struct xkb_keymap *base,
                       xkb_keysym_t       keysym,
                       xkb_keycode_t     *keycode_out)
{
  struct xkb_context *context;
  struct xkb_keymap *keymap = NULL;
  char sym_name[64];
  char *base_string;
  const char *keycodes_section, *symbols_section, *symbols_end;
  char key_name[64] = { 0 };
  xkb_keycode_t keycode = 0, candidate;
  gsize insert_offset;
  g_autoptr (GString) patched = NULL;
  g_autofree char *insert = NULL;

  if (xkb_keysym_get_name (keysym, sym_name, sizeof (sym_name)) <= 0)
    return NULL;

  base_string = xkb_keymap_get_as_string (base, XKB_KEYMAP_FORMAT_TEXT_V1);
  if (!base_string)
    return NULL;

  keycodes_section = strstr (base_string, "xkb_keycodes");
  symbols_section = strstr (base_string, "xkb_symbols");
  symbols_end = symbols_section ? strstr (symbols_section, "\n};") : NULL;
  if (!keycodes_section || !symbols_end)
    {
      free (base_string);
      return NULL;
    }

  for (candidate = TYPE_KEYSYM_MAX_XKB_KEYCODE;
       candidate >= xkb_keymap_min_keycode (base);
       candidate--)
    {
      char pattern[32];
      const char *hit, *lt, *gt;

      if (keycode_has_any_syms (base, candidate))
        continue;

      /* Addressable in the symbols section only if named. */
      g_snprintf (pattern, sizeof (pattern), " = %d;", candidate);
      hit = strstr (keycodes_section, pattern);
      if (!hit)
        continue;

      for (lt = hit; lt > keycodes_section && *lt != '<'; lt--);
      gt = strchr (lt, '>');
      if (*lt != '<' || !gt || gt > hit ||
          (gsize) (gt - lt + 2) > sizeof (key_name))
        continue;

      g_strlcpy (key_name, lt, gt - lt + 2);
      keycode = candidate;
      break;
    }

  if (keycode == 0)
    {
      g_warning ("No spare keycode found to type keysym %s", sym_name);
      free (base_string);
      return NULL;
    }

  insert_offset = symbols_end - base_string;
  patched = g_string_new (base_string);
  free (base_string);

  insert = g_strdup_printf ("\n\tkey %s {\t[ %s ] };", key_name, sym_name);
  g_string_insert (patched, insert_offset, insert);

  context = xkb_context_new (XKB_CONTEXT_NO_FLAGS);
  keymap = xkb_keymap_new_from_string (context, patched->str,
                                       XKB_KEYMAP_FORMAT_TEXT_V1,
                                       XKB_KEYMAP_COMPILE_NO_FLAGS);
  xkb_context_unref (context);

  if (keymap)
    *keycode_out = keycode;

  return keymap;
}

/* How long a temporary typing keymap may outlive its key event. Wayland
 * clients resolve the keycode strictly in event order, but X11 clients
 * re-fetch the keymap from the server with a round trip when translating
 * the key - restoring immediately (as KWin does) makes them look up the
 * keycode in the already-restored layout and drop the character. */
#define TEMP_KEYMAP_RESTORE_TIMEOUT_MS 500

static void
restore_saved_keymap (MetaWaylandKeyboard *keyboard)
{
  struct xkb_keymap *saved;

  if (!keyboard->saved_keymap)
    return;

  saved = g_steal_pointer (&keyboard->saved_keymap);
  g_clear_handle_id (&keyboard->temp_keymap_timeout_id, g_source_remove);
  keyboard->temp_keysym = 0;
  keyboard->temp_evdev_code = 0;

  meta_wayland_keyboard_take_keymap (keyboard, saved);
  xkb_keymap_unref (saved);
}

static gboolean
temp_keymap_timeout_cb (gpointer user_data)
{
  MetaWaylandKeyboard *keyboard = user_data;

  keyboard->temp_keymap_timeout_id = 0;
  restore_saved_keymap (keyboard);

  return G_SOURCE_REMOVE;
}

gboolean
meta_wayland_keyboard_maybe_type_keysym (MetaWaylandKeyboard *keyboard,
                                         uint32_t             keysym)
{
  MetaWaylandXkbInfo *xkb_info = &keyboard->xkb_info;
  struct xkb_keymap *pristine;
  uint32_t time_ms;

  if (!xkb_info->keymap || !xkb_info->state)
    return FALSE;

  /* While a patched keymap is active, the layout to reason about (and to
   * patch again for a different symbol) is the saved one. */
  pristine = keyboard->saved_keymap ? keyboard->saved_keymap
                                    : xkb_info->keymap;

  /* Typeable through the layout: let the caller use the virtual device,
   * which keeps normal press/release semantics and modifier handling. */
  if (keymap_has_keysym_in_current_layout (pristine, xkb_info->state, keysym))
    return FALSE;

  if (keysym != keyboard->temp_keysym || !keyboard->saved_keymap)
    {
      struct xkb_keymap *patched;
      struct xkb_keymap *original = NULL;
      xkb_keycode_t keycode;

      patched = create_patched_keymap (pristine, keysym, &keycode);
      if (!patched)
        return FALSE;

      /* The patched map is the real layout plus one key, so leaving it in
       * place is harmless; the swap back is still deferred (see
       * restore_saved_keymap) rather than immediate, because X11 clients
       * re-fetch the keymap from the server with a round trip when
       * translating the key and must find the mapping still present. */
      if (!keyboard->saved_keymap)
        original = xkb_keymap_ref (xkb_info->keymap);

      keyboard->typing_temp_keymap = TRUE;
      meta_wayland_keyboard_take_keymap (keyboard, patched);
      keyboard->typing_temp_keymap = FALSE;

      if (original)
        keyboard->saved_keymap = g_steal_pointer (&original);

      keyboard->temp_keysym = keysym;
      keyboard->temp_evdev_code = keycode - 8;
      xkb_keymap_unref (patched);
    }

  time_ms = (uint32_t) (g_get_monotonic_time () / 1000);
  meta_wayland_keyboard_inject_key (keyboard, time_ms,
                                    keyboard->temp_evdev_code,
                                    WL_KEYBOARD_KEY_STATE_PRESSED);
  meta_wayland_keyboard_inject_key (keyboard, time_ms,
                                    keyboard->temp_evdev_code,
                                    WL_KEYBOARD_KEY_STATE_RELEASED);

  g_clear_handle_id (&keyboard->temp_keymap_timeout_id, g_source_remove);
  keyboard->temp_keymap_timeout_id =
    g_timeout_add (TEMP_KEYMAP_RESTORE_TIMEOUT_MS,
                   temp_keymap_timeout_cb, keyboard);

  return TRUE;
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
  uint32_t depressed, latched, locked, group;

  meta_wayland_keyboard_get_modifiers (keyboard, &depressed, &latched, &locked, &group);
  wl_keyboard_send_modifiers (resource, serial, depressed, latched, locked, group);
}

void
meta_wayland_keyboard_get_modifiers (MetaWaylandKeyboard *keyboard,
                                     uint32_t            *mods_depressed,
                                     uint32_t            *mods_latched,
                                     uint32_t            *mods_locked,
                                     uint32_t            *group)
{
  struct xkb_state *state = keyboard->xkb_info.state;

  *mods_depressed = add_virtual_mods (xkb_state_serialize_mods (state, XKB_STATE_MODS_DEPRESSED));
  *mods_latched = add_virtual_mods (xkb_state_serialize_mods (state, XKB_STATE_MODS_LATCHED));
  *mods_locked = add_virtual_mods (xkb_state_serialize_mods (state, XKB_STATE_MODS_LOCKED));
  *group = xkb_state_serialize_layout (state, XKB_STATE_LAYOUT_EFFECTIVE);
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

void
meta_wayland_keyboard_get_repeat_info (MetaWaylandKeyboard *keyboard,
                                       int32_t             *rate,
                                       int32_t             *delay)
{
  if (g_settings_get_boolean (keyboard->settings, "repeat"))
    {
      unsigned int interval;

      interval = g_settings_get_uint (keyboard->settings, "repeat-interval");
      /* Our setting is in the milliseconds between keys. "rate" is the number
       * of keys per second. */
      *rate = interval > 0 ? (1000 / interval) : 0;
      *delay = g_settings_get_uint (keyboard->settings, "delay");
    }
  else
    {
      *rate = 0;
      *delay = 0;
    }
}

static void
notify_key_repeat_for_resource (MetaWaylandKeyboard *keyboard,
                                struct wl_resource  *keyboard_resource)
{
  if (wl_resource_get_version (keyboard_resource) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION)
    {
      int32_t rate, delay;

      meta_wayland_keyboard_get_repeat_info (keyboard, &rate, &delay);
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

guint
meta_wayland_keyboard_get_key_evdev_code (const ClutterEvent *event)
{
  guint32 code = 0;
#ifdef HAVE_NATIVE_BACKEND
  MetaBackend *backend = meta_get_backend ();

  if (META_IS_BACKEND_NATIVE (backend))
    code = meta_event_native_get_event_code (event);
  if (code == 0)
#endif
    code = evdev_code (&event->key);

  return code;
}

static gboolean
default_grab_key (MetaWaylandKeyboardGrab *grab,
                  const ClutterEvent      *event)
{
  MetaWaylandKeyboard *keyboard = grab->keyboard;
  gboolean is_press = event->type == CLUTTER_KEY_PRESS;
  guint32 code;

  /* Ignore autorepeat events, as autorepeat in Wayland is done on the client
   * side. */
  if (event->key.flags & CLUTTER_EVENT_FLAG_REPEATED)
    return FALSE;

  code = meta_wayland_keyboard_get_key_evdev_code (event);

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

  wl_array_init (&keyboard->pressed_keys);

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
  meta_anonymous_file_free (xkb_info->keymap_rofile);
}

void
meta_wayland_keyboard_disable (MetaWaylandKeyboard *keyboard)
{
  MetaBackend *backend = meta_get_backend ();

  g_signal_handlers_disconnect_by_func (backend, on_keymap_changed, keyboard);
  g_signal_handlers_disconnect_by_func (backend, on_keymap_layout_group_changed, keyboard);

  g_clear_pointer (&keyboard->saved_keymap, xkb_keymap_unref);
  g_clear_handle_id (&keyboard->temp_keymap_timeout_id, g_source_remove);

  meta_wayland_keyboard_end_grab (keyboard);
  meta_wayland_keyboard_set_focus (keyboard, NULL);

  wl_list_remove (&keyboard->resource_list);
  wl_list_init (&keyboard->resource_list);
  wl_list_remove (&keyboard->focus_resource_list);
  wl_list_init (&keyboard->focus_resource_list);

  wl_array_release (&keyboard->pressed_keys);

  g_clear_object (&keyboard->settings);
}

static guint
evdev_code (const ClutterKeyEvent *event)
{
  /* clutter-xkb-utils.c adds a fixed offset of 8 to go into XKB's
   * range, so we do the reverse here. */
  return event->hardware_keycode - 8;
}

static void
update_pressed_keys (MetaWaylandKeyboard *keyboard,
                     uint32_t             evdev_code,
                     gboolean             is_press)
{
  if (is_press)
    {
      uint32_t *end = (uint32_t *) ((char *) keyboard->pressed_keys.data +
                                    keyboard->pressed_keys.size);
      uint32_t *k;

      for (k = keyboard->pressed_keys.data; k < end; k++)
        {
          if (*k == evdev_code)
            return;
        }

      k = wl_array_add (&keyboard->pressed_keys, sizeof (*k));
      if (k)
        *k = evdev_code;
    }
  else
    {
      uint32_t *end = (uint32_t *) ((char *) keyboard->pressed_keys.data +
                                    keyboard->pressed_keys.size);
      uint32_t *k;

      for (k = keyboard->pressed_keys.data; k < end; k++)
        {
          if (*k == evdev_code)
            {
              *k = *(end - 1);
              keyboard->pressed_keys.size -= sizeof (*k);
              return;
            }
        }
    }
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

  /* Key events must not be translated against a lingering temporary
   * typing keymap. */
  restore_saved_keymap (keyboard);

  update_pressed_keys (keyboard, evdev_code (event), is_press);

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
  wl_keyboard_send_enter (resource, keyboard->focus_serial,
                          keyboard->focus_surface->resource,
                          &keyboard->pressed_keys);
  keyboard_send_modifiers (keyboard, resource, keyboard->focus_serial);
}

void
meta_wayland_keyboard_set_focus (MetaWaylandKeyboard *keyboard,
                                 MetaWaylandSurface *surface)
{
  MetaWaylandInputDevice *input_device = META_WAYLAND_INPUT_DEVICE (keyboard);

  if (keyboard->focus_surface == surface)
    return;

  /* Don't hand a newly focused client a lingering temporary typing keymap. */
  restore_saved_keymap (keyboard);

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
meta_wayland_keyboard_can_grab_surface (MetaWaylandKeyboard *keyboard,
                                        MetaWaylandSurface  *surface,
                                        uint32_t             serial)
{
  if (keyboard->focus_surface != surface)
    return FALSE;

  return (keyboard->focus_serial == serial ||
          meta_wayland_keyboard_can_popup (keyboard, serial));
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
meta_wayland_keyboard_start_grab_no_focus_change (MetaWaylandKeyboard     *keyboard,
                                                  MetaWaylandKeyboardGrab *grab)
{
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
