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

/* zwp_input_method_manager_v2 server. fcitx's waylandim frontend binds this
 * to act as the seat's input method: the compositor relays the focused text
 * input's state to it and applies the preedit/commit it sends back. This
 * object is a ClutterInputMethod subclass so both external text-input-v3
 * clients and Cinnamon's own Clutter actors reach fcitx through this.
 */

#include "config.h"

#include "wayland/meta-wayland-input-method.h"

#include <string.h>
#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>

#include "clutter/clutter.h"
#include "meta/meta-backend.h"
#include "core/meta-anonymous-file.h"
#include "meta/util.h"
#include "wayland/meta-wayland-input-device.h"
#include "wayland/meta-wayland-input-popup-surface.h"
#include "wayland/meta-wayland-keyboard.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-seat.h"
#include "wayland/meta-wayland-surface.h"
#include "wayland/meta-wayland-versions.h"

#include "input-method-unstable-v2-server-protocol.h"
#include "text-input-unstable-v3-server-protocol.h"

/* Same cap text-input-v3 documents for its surrounding text */
#define META_INPUT_METHOD_MAX_SURROUNDING_LEN 4000

struct _MetaWaylandInputMethod
{
  ClutterInputMethod parent;

  MetaWaylandSeat *seat;

  struct wl_resource *resource;         /* active zwp_input_method_v2, or NULL */
  struct wl_resource *keyboard_grab;    /* zwp_input_method_keyboard_grab_v2, or NULL */
  MetaWaylandKeyboardGrab seat_grab;    /* diverts physical keys while grabbed */
  struct wl_resource *popup_surface;    /* zwp_input_popup_surface_v2, or NULL */
  MetaWaylandInputPopupSurface *popup_role;  /* the popup's actor-surface role */

  ClutterInputFocus *focus;             /* the focused text input, or NULL */
  uint32_t content_hint;                /* protocol content hint bits, last seen */
  uint32_t content_purpose;             /* protocol content purpose, last seen */
  graphene_rect_t cursor_rect;          /* caret rect for popup positioning */

  uint32_t serial;                      /* bumped on each done() sent to the client */
  guint done_idle_id;                   /* coalesces state events into one done() */
  gboolean activate_sent;               /* an activate() is on the wire without its done() */

  /* The surrounding text last sent to the client, and the byte offsets of the
   * cursor and anchor within it; the client's delete_surrounding_text byte
   * counts are relative to this. Kept across rebinds so a relaunched fcitx
   * can be replayed the focused field's state. */
  char    *sent_surrounding;
  uint32_t sent_surrounding_cursor;
  uint32_t sent_surrounding_anchor;

  char    *current_preedit;             /* preedit currently painted, or NULL */

  /* Pending client state, applied on commit(). */
  gboolean pending_commit;
  char    *pending_commit_string;
  gboolean pending_preedit;
  char    *pending_preedit_string;
  int32_t  pending_preedit_cursor_begin;
  int32_t  pending_preedit_cursor_end;
  gboolean pending_delete;
  uint32_t pending_delete_before;
  uint32_t pending_delete_after;
};

typedef struct _MetaWaylandInputMethodClass MetaWaylandInputMethodClass;
struct _MetaWaylandInputMethodClass
{
  ClutterInputMethodClass parent_class;
};

#define META_TYPE_WAYLAND_INPUT_METHOD (meta_wayland_input_method_get_type ())
G_DEFINE_TYPE (MetaWaylandInputMethod, meta_wayland_input_method, CLUTTER_TYPE_INPUT_METHOD)

static void
clear_pending (MetaWaylandInputMethod *im)
{
  im->pending_commit = FALSE;
  g_clear_pointer (&im->pending_commit_string, g_free);
  im->pending_preedit = FALSE;
  g_clear_pointer (&im->pending_preedit_string, g_free);
  im->pending_preedit_cursor_begin = 0;
  im->pending_preedit_cursor_end = 0;
  im->pending_delete = FALSE;
  im->pending_delete_before = 0;
  im->pending_delete_after = 0;
}

static uint32_t
hints_to_protocol (ClutterInputContentHintFlags hints)
{
  uint32_t protocol = 0;

  if (hints & CLUTTER_INPUT_CONTENT_HINT_COMPLETION)
    protocol |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_COMPLETION;
  if (hints & CLUTTER_INPUT_CONTENT_HINT_SPELLCHECK)
    protocol |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_SPELLCHECK;
  if (hints & CLUTTER_INPUT_CONTENT_HINT_AUTO_CAPITALIZATION)
    protocol |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_AUTO_CAPITALIZATION;
  if (hints & CLUTTER_INPUT_CONTENT_HINT_LOWERCASE)
    protocol |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_LOWERCASE;
  if (hints & CLUTTER_INPUT_CONTENT_HINT_UPPERCASE)
    protocol |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_UPPERCASE;
  if (hints & CLUTTER_INPUT_CONTENT_HINT_TITLECASE)
    protocol |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_TITLECASE;
  if (hints & CLUTTER_INPUT_CONTENT_HINT_HIDDEN_TEXT)
    protocol |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_HIDDEN_TEXT;
  if (hints & CLUTTER_INPUT_CONTENT_HINT_SENSITIVE_DATA)
    protocol |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA;
  if (hints & CLUTTER_INPUT_CONTENT_HINT_LATIN)
    protocol |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_LATIN;
  if (hints & CLUTTER_INPUT_CONTENT_HINT_MULTILINE)
    protocol |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_MULTILINE;

  return protocol;
}

static uint32_t
purpose_to_protocol (ClutterInputContentPurpose purpose)
{
  switch (purpose)
    {
    case CLUTTER_INPUT_CONTENT_PURPOSE_NORMAL:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL;
    case CLUTTER_INPUT_CONTENT_PURPOSE_ALPHA:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_ALPHA;
    case CLUTTER_INPUT_CONTENT_PURPOSE_DIGITS:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_DIGITS;
    case CLUTTER_INPUT_CONTENT_PURPOSE_NUMBER:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NUMBER;
    case CLUTTER_INPUT_CONTENT_PURPOSE_PHONE:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PHONE;
    case CLUTTER_INPUT_CONTENT_PURPOSE_URL:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_URL;
    case CLUTTER_INPUT_CONTENT_PURPOSE_EMAIL:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_EMAIL;
    case CLUTTER_INPUT_CONTENT_PURPOSE_NAME:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NAME;
    case CLUTTER_INPUT_CONTENT_PURPOSE_PASSWORD:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PASSWORD;
    case CLUTTER_INPUT_CONTENT_PURPOSE_DATE:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_DATE;
    case CLUTTER_INPUT_CONTENT_PURPOSE_TIME:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_TIME;
    case CLUTTER_INPUT_CONTENT_PURPOSE_DATETIME:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_DATETIME;
    case CLUTTER_INPUT_CONTENT_PURPOSE_TERMINAL:
      return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_TERMINAL;
    }

  return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL;
}

/* Applies the pending commit/preedit/delete the input method sent, feeding it
 * into the focused text input through the ClutterInputMethod funnel. Order
 * follows the input-method-v2 spec: delete surrounding, then commit, then set
 * the new preedit. */
static void
apply_pending (MetaWaylandInputMethod *im)
{
  ClutterInputMethod *method = CLUTTER_INPUT_METHOD (im);

  if (im->pending_delete)
    {
      glong chars_before = im->pending_delete_before;
      glong chars_after = im->pending_delete_after;

      /* The protocol counts bytes, relative to the surrounding text we last
       * sent; the Clutter funnel counts characters. Convert against the
       * cached copy. Without one (the focus never provided surrounding text)
       * there is nothing the counts can refer to, so they pass through as a
       * best effort. */
      if (im->sent_surrounding)
        {
          const char *sent = im->sent_surrounding;
          gsize sent_len = strlen (sent);
          gsize sent_cursor = MIN (im->sent_surrounding_cursor, sent_len);
          gsize before = MIN (im->pending_delete_before, sent_cursor);
          gsize after = MIN (im->pending_delete_after, sent_len - sent_cursor);

          chars_before = g_utf8_strlen (sent + sent_cursor - before, before);
          chars_after = g_utf8_strlen (sent + sent_cursor, after);
        }

      clutter_input_method_delete_surrounding (method,
                                               (int) -chars_before,
                                               chars_before + chars_after);
    }

  if (im->pending_commit)
    {
      clutter_input_method_commit (method,
                                   im->pending_commit_string ? im->pending_commit_string : "");
    }

  if (im->pending_preedit)
    {
      const char *text = im->pending_preedit_string ? im->pending_preedit_string : "";
      guint cursor;

      if (im->pending_preedit_cursor_begin >= 0)
        {
          /* The protocol cursor is a byte offset into the preedit; the Clutter
           * funnel wants characters (its consumers walk the string with
           * g_utf8_offset_to_pointer). Clamp before converting - the offset
           * comes from the client. */
          size_t begin = MIN ((size_t) im->pending_preedit_cursor_begin,
                              strlen (text));

          cursor = g_utf8_pointer_to_offset (text, text + begin);
        }
      else
        {
          /* -1 means "hide the cursor", which the Clutter API cannot express;
           * the least-wrong visible position is the end of the preedit. */
          cursor = g_utf8_strlen (text, -1);
        }

      clutter_input_method_set_preedit_text (method, text, cursor);

      g_free (im->current_preedit);
      im->current_preedit = *text != '\0' ? g_strdup (text) : NULL;
    }
  else
    {
      /* No preedit this cycle: clear any preedit currently shown. */
      clutter_input_method_set_preedit_text (method, "", 0);
      g_clear_pointer (&im->current_preedit, g_free);
    }

  clear_pending (im);
}

/* zwp_input_popup_surface_v2 */

static void
input_popup_surface_destroy (struct wl_client   *client,
                             struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct zwp_input_popup_surface_v2_interface input_popup_surface_impl = {
  input_popup_surface_destroy,
};

static void
input_popup_surface_destructor (struct wl_resource *resource)
{
  MetaWaylandInputMethod *im = wl_resource_get_user_data (resource);

  if (im && im->popup_surface == resource)
    {
      im->popup_surface = NULL;
      if (im->popup_role)
        {
          g_object_remove_weak_pointer (G_OBJECT (im->popup_role),
                                        (gpointer *) &im->popup_role);
          im->popup_role = NULL;
        }
    }
}

static void
im_send_text_input_rectangle (MetaWaylandInputMethod *im)
{
  if (!im->popup_surface)
    return;

  /* The popup is anchored at the caret, so report the caret at the surface
   * origin with its size, letting fcitx fine-position its window. */
  zwp_input_popup_surface_v2_send_text_input_rectangle (im->popup_surface, 0, 0,
                                                        (int) im->cursor_rect.size.width,
                                                        (int) im->cursor_rect.size.height);
}

/* zwp_input_method_keyboard_grab_v2 — while the input method holds the grab,
 * physical key events are diverted to it through a MetaWaylandKeyboardGrab.
 * Keys the input method does not consume come back via virtual-keyboard and
 * are delivered to the focused surface (which keeps its keyboard focus). */

static void
im_keyboard_grab_end (MetaWaylandInputMethod *im)
{
  MetaWaylandKeyboard *keyboard = im->seat->keyboard;

  if (keyboard && keyboard->grab == &im->seat_grab)
    meta_wayland_keyboard_end_grab (keyboard);
}

static void
send_key_to_grab (MetaWaylandInputMethod *im,
                  MetaWaylandKeyboard    *keyboard,
                  const ClutterEvent     *event)
{
  uint32_t code, state, serial;

  code = meta_wayland_keyboard_get_key_evdev_code (event);
  state = (event->type == CLUTTER_KEY_PRESS) ?
    WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED;
  serial = meta_wayland_input_device_next_serial (META_WAYLAND_INPUT_DEVICE (keyboard));

  zwp_input_method_keyboard_grab_v2_send_key (im->keyboard_grab, serial,
                                              event->key.time, code, state);
}

static gboolean
im_keyboard_grab_key (MetaWaylandKeyboardGrab *grab,
                      const ClutterEvent      *event)
{
  MetaWaylandInputMethod *im = wl_container_of (grab, im, seat_grab);
  MetaWaylandKeyboard *keyboard = grab->keyboard;

  /* Never send our own re-emitted keys back to the input method; treat them
   * like ungrabbed keys. Event routing already keeps these off the wayland
   * path (they are only dispatched to Clutter actors), so this is a local
   * guard against that cross-subsystem invariant ever changing - without it,
   * a re-routed INPUT_METHOD key would loop key -> fcitx -> reinject -> key. */
  if (!im->keyboard_grab ||
      event->key.flags & CLUTTER_EVENT_FLAG_INPUT_METHOD)
    return keyboard->default_grab.interface->key (&keyboard->default_grab, event);

  /* Autorepeat is client-side; the input method uses repeat_info. Swallow
   * the synthetic repeat entirely so it doesn't fall through to Clutter. */
  if (event->key.flags & CLUTTER_EVENT_FLAG_REPEATED)
    return TRUE;

  send_key_to_grab (im, keyboard, event);

  return TRUE;
}

static void
im_keyboard_grab_modifiers (MetaWaylandKeyboardGrab *grab,
                            ClutterModifierType      modifiers)
{
  MetaWaylandInputMethod *im = wl_container_of (grab, im, seat_grab);
  MetaWaylandKeyboard *keyboard = grab->keyboard;
  uint32_t depressed, latched, locked, group, serial;

  if (!im->keyboard_grab)
    return;

  meta_wayland_keyboard_get_modifiers (keyboard, &depressed, &latched, &locked, &group);
  serial = meta_wayland_input_device_next_serial (META_WAYLAND_INPUT_DEVICE (keyboard));
  zwp_input_method_keyboard_grab_v2_send_modifiers (im->keyboard_grab, serial,
                                                    depressed, latched, locked, group);
}

static const MetaWaylandKeyboardGrabInterface im_keyboard_grab_interface = {
  im_keyboard_grab_key,
  im_keyboard_grab_modifiers,
};

static void
im_keyboard_grab_send_keymap (MetaWaylandInputMethod *im,
                              MetaWaylandKeyboard    *keyboard)
{
  int fd;
  size_t size;

  fd = meta_anonymous_file_open_fd (keyboard->xkb_info.keymap_rofile,
                                    META_ANONYMOUS_FILE_MAPMODE_PRIVATE);
  if (fd == -1)
    return;

  size = meta_anonymous_file_size (keyboard->xkb_info.keymap_rofile);
  zwp_input_method_keyboard_grab_v2_send_keymap (im->keyboard_grab,
                                                 WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                                                 fd, size);
  meta_anonymous_file_close_fd (fd);
}

/* fcitx re-injects the keys it does not consume through virtual-keyboard.
 * Route each to wherever input focus is: an external app's surface (the
 * wayland keyboard path), or Cinnamon's own Clutter actor (re-emitted as a
 * Clutter key event carrying CLUTTER_EVENT_FLAG_INPUT_METHOD, so it reaches
 * the actor without being filtered back to fcitx). */
static void
reinject_to_actor (MetaWaylandInputMethod *im,
                   uint32_t                time,
                   uint32_t                key,
                   uint32_t                state)
{
  MetaWaylandKeyboard *keyboard = im->seat->keyboard;
  struct xkb_state *xkb_state;
  uint32_t keycode, keyval, modifiers;

  if (!keyboard || !keyboard->xkb_info.state)
    return;

  xkb_state = keyboard->xkb_info.state;
  keycode = key + 8;
  keyval = xkb_state_key_get_one_sym (xkb_state, keycode);
  /* Serialized xkb mods share the X11 bit positions with ClutterModifierType. */
  modifiers = xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_EFFECTIVE);

  clutter_input_method_forward_key (CLUTTER_INPUT_METHOD (im),
                                    keyval, keycode, modifiers, time,
                                    state == WL_KEYBOARD_KEY_STATE_PRESSED);
}

/* A Cinnamon chrome actor (not the bare stage) holds Clutter key focus exactly
 * when the physical key was routed to Cinnamon rather than forwarded to a
 * Wayland surface: events.c bypasses Wayland for key events while a chrome
 * actor is focused (see stage_has_key_focus there). Mirror that split here so
 * fcitx's re-injected keys land where the original key would have. */
static gboolean
cinnamon_has_key_focus (void)
{
  MetaBackend *backend = meta_get_backend ();
  ClutterActor *stage = meta_backend_get_stage (backend);
  ClutterActor *focus = clutter_stage_get_key_focus (CLUTTER_STAGE (stage));

  return focus && focus != stage;
}

void
meta_wayland_input_method_reinject_key (MetaWaylandInputMethod *im,
                                        uint32_t                time,
                                        uint32_t                key,
                                        uint32_t                state)
{
  MetaWaylandKeyboard *keyboard = im->seat->keyboard;

  /* focus_surface tracks the Wayland surface with focus, but a grab-less Cinnamon
   * dialog (a popup dialog over a still-focused client) leaves it pointing at
   * the client while a chrome entry actually holds key focus. Route to the
   * actor whenever Cinnamon owns key focus, matching the inbound routing. */
  if (keyboard && keyboard->focus_surface && !cinnamon_has_key_focus ())
    meta_wayland_keyboard_inject_key (keyboard, time, key, state);
  else
    reinject_to_actor (im, time, key, state);
}

static void
input_method_keyboard_grab_release (struct wl_client   *client,
                                    struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct zwp_input_method_keyboard_grab_v2_interface input_method_keyboard_grab_impl = {
  input_method_keyboard_grab_release,
};

static void
input_method_keyboard_grab_destructor (struct wl_resource *resource)
{
  MetaWaylandInputMethod *im = wl_resource_get_user_data (resource);

  if (im && im->keyboard_grab == resource)
    {
      im->keyboard_grab = NULL;
      im_keyboard_grab_end (im);
    }
}

/* zwp_input_method_v2 */

static void
input_method_commit_string (struct wl_client   *client,
                            struct wl_resource *resource,
                            const char         *text)
{
  MetaWaylandInputMethod *im = wl_resource_get_user_data (resource);

  if (im->resource != resource)
    return;

  im->pending_commit = TRUE;
  g_clear_pointer (&im->pending_commit_string, g_free);
  im->pending_commit_string = g_strdup (text);
}

static void
input_method_set_preedit_string (struct wl_client   *client,
                                 struct wl_resource *resource,
                                 const char         *text,
                                 int32_t             cursor_begin,
                                 int32_t             cursor_end)
{
  MetaWaylandInputMethod *im = wl_resource_get_user_data (resource);

  if (im->resource != resource)
    return;

  im->pending_preedit = TRUE;
  g_clear_pointer (&im->pending_preedit_string, g_free);
  im->pending_preedit_string = g_strdup (text);
  im->pending_preedit_cursor_begin = cursor_begin;
  im->pending_preedit_cursor_end = cursor_end;
}

static void
input_method_delete_surrounding_text (struct wl_client   *client,
                                      struct wl_resource *resource,
                                      uint32_t            before_length,
                                      uint32_t            after_length)
{
  MetaWaylandInputMethod *im = wl_resource_get_user_data (resource);

  if (im->resource != resource)
    return;

  im->pending_delete = TRUE;
  im->pending_delete_before = before_length;
  im->pending_delete_after = after_length;
}

static void
input_method_commit (struct wl_client   *client,
                     struct wl_resource *resource,
                     uint32_t            serial)
{
  MetaWaylandInputMethod *im = wl_resource_get_user_data (resource);

  if (im->resource != resource)
    return;

  /* The serial echoes the number of done() events the client had seen when it
   * built this state. If it doesn't match the number we have sent, the client
   * acted on state we have since replaced (e.g. a delete_surrounding computed
   * against a field that lost focus) - discard rather than corrupt the current
   * text. The client rebuilds from the done() it hasn't processed yet. */
  if (serial != im->serial)
    {
      clear_pending (im);
      return;
    }

  apply_pending (im);
}

static void
input_method_get_input_popup_surface (struct wl_client   *client,
                                      struct wl_resource *resource,
                                      uint32_t            id,
                                      struct wl_resource *surface_resource)
{
  MetaWaylandInputMethod *im = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  struct wl_resource *popup;

  if (im->resource != resource)
    return;

  if (!meta_wayland_surface_assign_role (surface,
                                         META_TYPE_WAYLAND_INPUT_POPUP_SURFACE,
                                         NULL))
    {
      wl_resource_post_error (resource, ZWP_INPUT_METHOD_V2_ERROR_ROLE,
                              "wl_surface@%d already has a role",
                              wl_resource_get_id (surface_resource));
      return;
    }

  /* Only one popup is tracked; a newer one replaces it. Detach the old one
   * first - in particular its weak pointer, which otherwise still targets
   * &im->popup_role and would clear the new role when the old one finalizes. */
  if (im->popup_role)
    {
      g_object_remove_weak_pointer (G_OBJECT (im->popup_role),
                                    (gpointer *) &im->popup_role);
      im->popup_role = NULL;
    }
  if (im->popup_surface)
    {
      wl_resource_set_user_data (im->popup_surface, NULL);
      im->popup_surface = NULL;
    }

  popup = wl_resource_create (client, &zwp_input_popup_surface_v2_interface,
                              wl_resource_get_version (resource), id);
  wl_resource_set_implementation (popup, &input_popup_surface_impl, im,
                                  input_popup_surface_destructor);
  im->popup_surface = popup;

  im->popup_role = META_WAYLAND_INPUT_POPUP_SURFACE (surface->role);
  g_object_add_weak_pointer (G_OBJECT (im->popup_role),
                             (gpointer *) &im->popup_role);

  /* Popups must only be visible while the input method is active. */
  meta_wayland_input_popup_surface_set_visible (im->popup_role,
                                                im->focus != NULL);
  meta_wayland_input_popup_surface_set_text_input_rect (im->popup_role,
                                                        &im->cursor_rect);
  im_send_text_input_rectangle (im);
}

static void
input_method_grab_keyboard (struct wl_client   *client,
                            struct wl_resource *resource,
                            uint32_t            keyboard)
{
  MetaWaylandInputMethod *im = wl_resource_get_user_data (resource);
  MetaWaylandKeyboard *kbd;
  struct wl_resource *grab;
  uint32_t depressed, latched, locked, group, serial;
  int32_t rate, delay;

  if (im->resource != resource)
    return;

  /* Only one grab can be live. fcitx always releases before re-grabbing, so
   * this is defensive: detach a leftover grab rather than stranding it with
   * dangling expectations (wlroots instead ignores the request, leaving the
   * new object unbound - a fatal error on first use). */
  if (im->keyboard_grab)
    {
      wl_resource_set_user_data (im->keyboard_grab, NULL);
      im->keyboard_grab = NULL;
    }

  grab = wl_resource_create (client, &zwp_input_method_keyboard_grab_v2_interface,
                             wl_resource_get_version (resource), keyboard);
  wl_resource_set_implementation (grab, &input_method_keyboard_grab_impl, im,
                                  input_method_keyboard_grab_destructor);
  im->keyboard_grab = grab;

  kbd = im->seat->keyboard;
  if (!kbd)
    return;

  /* Divert physical keys to the input method, keeping the surface's focus. */
  im->seat_grab.interface = &im_keyboard_grab_interface;
  meta_wayland_keyboard_start_grab_no_focus_change (kbd, &im->seat_grab);

  /* Prime fcitx with the keymap, current modifier state and repeat settings. */
  im_keyboard_grab_send_keymap (im, kbd);

  meta_wayland_keyboard_get_modifiers (kbd, &depressed, &latched, &locked, &group);
  serial = meta_wayland_input_device_next_serial (META_WAYLAND_INPUT_DEVICE (kbd));
  zwp_input_method_keyboard_grab_v2_send_modifiers (grab, serial, depressed,
                                                    latched, locked, group);

  meta_wayland_keyboard_get_repeat_info (kbd, &rate, &delay);
  zwp_input_method_keyboard_grab_v2_send_repeat_info (grab, rate, delay);
}

static void
input_method_destroy (struct wl_client   *client,
                      struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct zwp_input_method_v2_interface input_method_impl = {
  input_method_commit_string,
  input_method_set_preedit_string,
  input_method_delete_surrounding_text,
  input_method_commit,
  input_method_get_input_popup_surface,
  input_method_grab_keyboard,
  input_method_destroy,
};

static void
input_method_resource_destructor (struct wl_resource *resource)
{
  MetaWaylandInputMethod *im = wl_resource_get_user_data (resource);

  if (im->resource != resource)
    return;

  im->resource = NULL;
  /* commit(serial) echoes the number of done() events the client has seen,
   * and a newly bound client starts counting from zero — carrying the old
   * count across a rebind would make every commit fail the serial check. */
  im->serial = 0;
  im->activate_sent = FALSE;
  g_clear_handle_id (&im->done_idle_id, g_source_remove);
  /* sent_surrounding is deliberately NOT cleared: it mirrors the focused
   * field, not the dead client, and get_input_method replays it to the
   * next binding so the byte offsets in any delete_surrounding_text it
   * sends stay meaningful. focus_out clears it when the field goes away. */
  clear_pending (im);

  /* A crashed input method leaves its last preedit painted in the focused
   * client or actor - display-only text nothing else will remove. The engine
   * state died with the client, but the text the user saw is recoverable:
   * commit it rather than discard those keystrokes (fcitx itself commits the
   * preedit on unfocus for the same reason). The commit path also clears the
   * preedit rendering and refreshes the surrounding text, so the replay to
   * the next binding already carries the rescued word. */
  if (im->focus)
    {
      if (im->current_preedit)
        clutter_input_method_commit (CLUTTER_INPUT_METHOD (im),
                                     im->current_preedit);

      /* Normally the client clears the preedit itself as part of committing;
       * the dead client can't, so push the clear through the funnel too -
       * otherwise the buffered preedit is resent with the next done() and the
       * rescued text appears twice. */
      clutter_input_method_set_preedit_text (CLUTTER_INPUT_METHOD (im),
                                             NULL, 0);
    }
  g_clear_pointer (&im->current_preedit, g_free);

  /* Destroying the input method also invalidates its child objects (spec):
   * detach the grab and popup server-side, leaving their wire objects inert
   * for the client to destroy (as wlroots does). The MetaWaylandInputMethod
   * is a per-seat singleton reused across binds, so without this a re-binding
   * fcitx could be acted on through stale children left by a crashed
   * predecessor. */
  if (im->keyboard_grab)
    {
      wl_resource_set_user_data (im->keyboard_grab, NULL);
      im->keyboard_grab = NULL;
    }
  im_keyboard_grab_end (im);

  if (im->popup_surface)
    {
      wl_resource_set_user_data (im->popup_surface, NULL);
      im->popup_surface = NULL;
    }
  if (im->popup_role)
    {
      g_object_remove_weak_pointer (G_OBJECT (im->popup_role),
                                    (gpointer *) &im->popup_role);
      im->popup_role = NULL;
    }
}

/* Event senders (compositor -> input method) */

void
meta_wayland_input_method_activate (MetaWaylandInputMethod *im)
{
  if (!im->resource)
    return;

  zwp_input_method_v2_send_activate (im->resource);
  im->activate_sent = TRUE;
}

void
meta_wayland_input_method_deactivate (MetaWaylandInputMethod *im)
{
  if (!im->resource)
    return;

  zwp_input_method_v2_send_deactivate (im->resource);
  im->activate_sent = FALSE;
}

void
meta_wayland_input_method_send_done (MetaWaylandInputMethod *im)
{
  if (!im->resource)
    return;

  im->serial++;
  zwp_input_method_v2_send_done (im->resource);
  im->activate_sent = FALSE;
}

static gboolean
send_done_idle_cb (gpointer user_data)
{
  MetaWaylandInputMethod *im = user_data;

  im->done_idle_id = 0;
  meta_wayland_input_method_send_done (im);

  return G_SOURCE_REMOVE;
}

/* The ClutterInputFocus vfuncs below fire one-per-property, but done() marks
 * the end of one atomic state update: a focus change arrives as e.g.
 * activate + surrounding_text + content_type and must carry a single done(),
 * not one per event (per the spec, state sent before the first done after
 * activate is what the input method treats as supported). Coalesce with a
 * high-priority idle so the batch flushes before any new input events are
 * dispatched to the client. */
static void
meta_wayland_input_method_schedule_done (MetaWaylandInputMethod *im)
{
  if (!im->resource || im->done_idle_id)
    return;

  im->done_idle_id = g_idle_add_full (G_PRIORITY_HIGH, send_done_idle_cb,
                                      im, NULL);
}

void
meta_wayland_input_method_send_surrounding_text (MetaWaylandInputMethod *im,
                                                 const char             *text,
                                                 uint32_t                cursor,
                                                 uint32_t                anchor)
{
  if (!im->resource)
    return;

  zwp_input_method_v2_send_surrounding_text (im->resource,
                                             text ? text : "",
                                             cursor, anchor);
}

void
meta_wayland_input_method_send_content_type (MetaWaylandInputMethod *im,
                                             uint32_t                hint,
                                             uint32_t                purpose)
{
  if (!im->resource)
    return;

  zwp_input_method_v2_send_content_type (im->resource, hint, purpose);
}

/* Manager */

static void
input_method_manager_get_input_method (struct wl_client   *client,
                                       struct wl_resource *resource,
                                       struct wl_resource *seat_resource,
                                       uint32_t            input_method)
{
  MetaWaylandSeat *seat = wl_resource_get_user_data (seat_resource);
  MetaWaylandInputMethod *im = seat->input_method;
  struct wl_resource *im_resource;

  im_resource = wl_resource_create (client, &zwp_input_method_v2_interface,
                                    wl_resource_get_version (resource),
                                    input_method);
  wl_resource_set_implementation (im_resource, &input_method_impl, im,
                                  input_method_resource_destructor);

  /* Only one input method may be active on a seat. The global filter already
   * guarantees only the compositor-launched fcitx can reach this. */
  if (im->resource)
    {
      zwp_input_method_v2_send_unavailable (im_resource);
      return;
    }

  im->resource = im_resource;

  /* This object is a per-seat singleton reused across binds. If fcitx
   * crashed and was relaunched while a text field is focused, nothing
   * re-fires focus_in for the new instance — and the client only grabs
   * the keyboard after an activate/done cycle, so without a replay it
   * would sit dead until the next real focus change. */
  if (im->focus)
    {
      meta_wayland_input_method_activate (im);
      if (im->sent_surrounding)
        meta_wayland_input_method_send_surrounding_text (im,
                                                         im->sent_surrounding,
                                                         im->sent_surrounding_cursor,
                                                         im->sent_surrounding_anchor);
      meta_wayland_input_method_send_content_type (im, im->content_hint,
                                                   im->content_purpose);
      meta_wayland_input_method_schedule_done (im);
    }
}

static void
input_method_manager_destroy (struct wl_client   *client,
                              struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct zwp_input_method_manager_v2_interface input_method_manager_impl = {
  input_method_manager_get_input_method,
  input_method_manager_destroy,
};

static void
bind_input_method_manager (struct wl_client *client,
                           void             *data,
                           uint32_t          version,
                           uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &zwp_input_method_manager_v2_interface,
                                 version, id);
  wl_resource_set_implementation (resource, &input_method_manager_impl,
                                  data, NULL);
}

/* ClutterInputMethod vfuncs (compositor funnel -> input method).
 *
 * Both external text-input-v3 clients and Cinnamon's own Clutter actors feed
 * into these through the single ClutterInputMethod, so fcitx sees one input
 * stream regardless of source. Each state change is followed by done(), which
 * tells the input method its state is complete; the input method replies with
 * commit_string/set_preedit_string/commit, handled above. */

static void
meta_wayland_input_method_focus_in (ClutterInputMethod *method,
                                    ClutterInputFocus  *focus)
{
  MetaWaylandInputMethod *im = (MetaWaylandInputMethod *) method;

  g_set_object (&im->focus, focus);
  meta_wayland_input_method_activate (im);

  /* Per the spec, activate resets all input-method state, but fcitx only
   * drops its surrounding text when a new surrounding event arrives - which
   * never comes when the new focus can't provide any (e.g. a terminal), so
   * its engines would act on the previous field's text. Send an explicit
   * empty state; a focus that does have surrounding pushes the real value
   * right after, within the same done(). */
  g_free (im->sent_surrounding);
  im->sent_surrounding = g_strdup ("");
  im->sent_surrounding_cursor = 0;
  im->sent_surrounding_anchor = 0;
  meta_wayland_input_method_send_surrounding_text (im, "", 0, 0);

  meta_wayland_input_method_schedule_done (im);

  /* No surrounding-text request here: this vfunc runs before the focus is
   * marked focused (clutter_input_focus_focus_in comes after), so the focus
   * would refuse to answer. Clutter entries deliver it right after focus-in
   * completes - clutter_text_im_focus() ends with update_cursor_location(),
   * which requests surrounding - and it coalesces into this same done(). */

  if (im->popup_role)
    meta_wayland_input_popup_surface_set_visible (im->popup_role, TRUE);
}

static void
meta_wayland_input_method_focus_out (ClutterInputMethod *method)
{
  MetaWaylandInputMethod *im = (MetaWaylandInputMethod *) method;

  /* If an activate from this same mainloop iteration is still awaiting its
   * coalesced done(), flush that first: activate + deactivate sharing one
   * done batch leaves fcitx ACTIVE — its done handler applies a pending
   * deactivate before a pending activate regardless of arrival order — so
   * it would keep the keyboard grab and eat keys with nothing focused.
   * (The opposite order, deactivate then activate on a focus switch,
   * batches safely for the same reason.) */
  if (im->activate_sent && im->done_idle_id)
    {
      g_clear_handle_id (&im->done_idle_id, g_source_remove);
      meta_wayland_input_method_send_done (im);
    }

  g_clear_object (&im->focus);
  g_clear_pointer (&im->sent_surrounding, g_free);
  g_clear_pointer (&im->current_preedit, g_free);
  meta_wayland_input_method_deactivate (im);
  meta_wayland_input_method_schedule_done (im);

  /* Spec: popups must not be shown while the input method is inactive. */
  if (im->popup_role)
    meta_wayland_input_popup_surface_set_visible (im->popup_role, FALSE);
}

static void
meta_wayland_input_method_reset (ClutterInputMethod *method)
{
  MetaWaylandInputMethod *im = (MetaWaylandInputMethod *) method;

  /* No dedicated reset event exists; a fresh done() lets the input method
   * resynchronise. fcitx also resets on (de)activation. Refresh the
   * surrounding text as part of that resync. */
  clutter_input_method_request_surrounding (method);
  meta_wayland_input_method_schedule_done (im);
}

static void
meta_wayland_input_method_set_cursor_location (ClutterInputMethod    *method,
                                               const graphene_rect_t *rect)
{
  MetaWaylandInputMethod *im = (MetaWaylandInputMethod *) method;

  im->cursor_rect = *rect;

  if (im->popup_role)
    meta_wayland_input_popup_surface_set_text_input_rect (im->popup_role, rect);
  im_send_text_input_rectangle (im);
}

static void
meta_wayland_input_method_set_surrounding (ClutterInputMethod *method,
                                           const gchar        *text,
                                           guint               cursor,
                                           guint               anchor)
{
  MetaWaylandInputMethod *im = (MetaWaylandInputMethod *) method;
  g_autofree char *window = NULL;
  gsize len, cursor_bytes, anchor_bytes;
  glong n_chars;

  text = text ? text : "";
  len = strlen (text);

  /* The Clutter funnel deals in character offsets; the protocol (like
   * text-input-v3) wants byte offsets into the sent string. */
  n_chars = g_utf8_strlen (text, -1);
  cursor = MIN (cursor, (guint) n_chars);
  anchor = MIN (anchor, (guint) n_chars);
  cursor_bytes = g_utf8_offset_to_pointer (text, cursor) - text;
  anchor_bytes = g_utf8_offset_to_pointer (text, anchor) - text;

  /* Protocol messages are hard-capped by libwayland (text-input-v3 caps the
   * equivalent field at 4000 bytes); an oversized Cinnamon entry would kill
   * the fcitx connection. Send a window around the cursor/anchor instead,
   * shrunk to UTF-8 character boundaries, with the offsets rebased. */
  if (len > META_INPUT_METHOD_MAX_SURROUNDING_LEN)
    {
      gsize lo = MIN (cursor_bytes, anchor_bytes);
      gsize hi = MAX (cursor_bytes, anchor_bytes);
      gsize start, end, space;

      /* If the selection alone exceeds the cap, center on the cursor. */
      if (hi - lo > META_INPUT_METHOD_MAX_SURROUNDING_LEN)
        lo = hi = cursor_bytes;

      space = (META_INPUT_METHOD_MAX_SURROUNDING_LEN - (hi - lo)) / 2;
      start = lo > space ? lo - space : 0;
      end = MIN (start + META_INPUT_METHOD_MAX_SURROUNDING_LEN, len);
      if (end == len && end - start < META_INPUT_METHOD_MAX_SURROUNDING_LEN)
        start = end - META_INPUT_METHOD_MAX_SURROUNDING_LEN;

      while (start < end && (text[start] & 0xC0) == 0x80)
        start++;
      while (end > start && end < len && (text[end] & 0xC0) == 0x80)
        end--;

      cursor_bytes = CLAMP (cursor_bytes, start, end) - start;
      anchor_bytes = CLAMP (anchor_bytes, start, end) - start;

      window = g_strndup (text + start, end - start);
      text = window;
    }

  g_free (im->sent_surrounding);
  im->sent_surrounding = g_strdup (text);
  im->sent_surrounding_cursor = cursor_bytes;
  im->sent_surrounding_anchor = anchor_bytes;

  meta_wayland_input_method_send_surrounding_text (im, text,
                                                   cursor_bytes, anchor_bytes);
  meta_wayland_input_method_schedule_done (im);
}

static void
meta_wayland_input_method_update_content_hints (ClutterInputMethod           *method,
                                                ClutterInputContentHintFlags  hints)
{
  MetaWaylandInputMethod *im = (MetaWaylandInputMethod *) method;

  im->content_hint = hints_to_protocol (hints);
  meta_wayland_input_method_send_content_type (im, im->content_hint,
                                               im->content_purpose);
  meta_wayland_input_method_schedule_done (im);
}

static void
meta_wayland_input_method_update_content_purpose (ClutterInputMethod         *method,
                                                  ClutterInputContentPurpose  purpose)
{
  MetaWaylandInputMethod *im = (MetaWaylandInputMethod *) method;

  im->content_purpose = purpose_to_protocol (purpose);
  meta_wayland_input_method_send_content_type (im, im->content_hint,
                                               im->content_purpose);
  meta_wayland_input_method_schedule_done (im);
}

static gboolean
meta_wayland_input_method_filter_key_event (ClutterInputMethod *method,
                                            const ClutterEvent *key)
{
  MetaWaylandInputMethod *im = (MetaWaylandInputMethod *) method;
  MetaWaylandKeyboard *keyboard = im->seat->keyboard;

  /* filter_key_event only fires for Cinnamon's own Clutter entries; external
   * apps reach fcitx through the wayland keyboard grab. Our own re-emitted
   * keys (INPUT_METHOD) must pass through to the actor, not loop to fcitx. */
  if (key->key.flags & CLUTTER_EVENT_FLAG_INPUT_METHOD)
    return FALSE;

  if (!im->keyboard_grab || !keyboard)
    return FALSE;

  send_key_to_grab (im, keyboard, key);

  /* Consume-pending: fcitx will commit, or re-inject via virtual-keyboard. */
  return TRUE;
}

/* GObject */

static void
meta_wayland_input_method_finalize (GObject *object)
{
  MetaWaylandInputMethod *im = (MetaWaylandInputMethod *) object;

  g_clear_handle_id (&im->done_idle_id, g_source_remove);
  g_clear_pointer (&im->sent_surrounding, g_free);
  g_clear_pointer (&im->current_preedit, g_free);
  clear_pending (im);
  g_clear_object (&im->focus);

  G_OBJECT_CLASS (meta_wayland_input_method_parent_class)->finalize (object);
}

static void
meta_wayland_input_method_class_init (MetaWaylandInputMethodClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterInputMethodClass *im_class = CLUTTER_INPUT_METHOD_CLASS (klass);

  object_class->finalize = meta_wayland_input_method_finalize;

  im_class->focus_in = meta_wayland_input_method_focus_in;
  im_class->focus_out = meta_wayland_input_method_focus_out;
  im_class->reset = meta_wayland_input_method_reset;
  im_class->set_cursor_location = meta_wayland_input_method_set_cursor_location;
  im_class->set_surrounding = meta_wayland_input_method_set_surrounding;
  im_class->update_content_hints = meta_wayland_input_method_update_content_hints;
  im_class->update_content_purpose = meta_wayland_input_method_update_content_purpose;
  im_class->filter_key_event = meta_wayland_input_method_filter_key_event;
}

static void
meta_wayland_input_method_init (MetaWaylandInputMethod *im)
{
}

MetaWaylandInputMethod *
meta_wayland_input_method_new (MetaWaylandSeat *seat)
{
  MetaWaylandInputMethod *im;

  im = g_object_new (META_TYPE_WAYLAND_INPUT_METHOD, NULL);
  im->seat = seat;

  return im;
}

void
meta_wayland_input_method_destroy (MetaWaylandInputMethod *im)
{
  if (im)
    g_object_unref (im);
}

void
meta_wayland_input_method_manager_init (MetaWaylandCompositor *compositor)
{
  MetaWaylandInputMethod *im = compositor->seat->input_method;

  /* Only advertise the input-method global in fcitx sessions. */
  if (!meta_im_mode_is_fcitx ())
    return;

  wl_global_create (compositor->wayland_display,
                    &zwp_input_method_manager_v2_interface,
                    META_ZWP_INPUT_METHOD_MANAGER_V2_VERSION,
                    im, bind_input_method_manager);

  /* Install our native backend as the Clutter input method so that both
   * Cinnamon's own actors and external text-input-v3 clients funnel through it to
   * fcitx. Cinnamon adopts this same object as Main.inputMethod on Wayland
   * rather than installing its own. */
  clutter_backend_set_input_method (clutter_get_default_backend (),
                                    CLUTTER_INPUT_METHOD (im));
}

gboolean
meta_wayland_input_method_has_client (MetaWaylandInputMethod *im)
{
  return im && im->resource != NULL;
}

ClutterInputMethod *
meta_wayland_input_method_get_clutter_backend (MetaWaylandInputMethod *im)
{
  return CLUTTER_INPUT_METHOD (im);
}
