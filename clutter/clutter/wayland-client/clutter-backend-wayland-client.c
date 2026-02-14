/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2024 Linux Mint
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
 * Authors:
 *   Michael Webster <miketwebster@gmail.com>
 */

#include "clutter-build-config.h"

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>

#include <wayland-client.h>
#include <wayland-egl.h>

#include <cogl/cogl.h>
#include <cogl/cogl-egl.h>
#include <cogl/cogl-wayland-client.h>

#include "clutter-backend-wayland-client.h"
#include "clutter-stage-wayland-client.h"
#include "clutter-seat-wayland-client.h"
#include "clutter-keymap-wayland-client.h"
#include "clutter-input-device-private.h"
#include "clutter-event-private.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include "clutter-backend.h"
#include "clutter-debug.h"
#include "clutter-event.h"
#include "clutter-input-method.h"
#include "clutter-main.h"
#include "clutter-private.h"
#include "clutter-settings-private.h"
#include "clutter-stage-private.h"

/* Minimal no-op input method so ClutterText's input focus system works
 * without critical warnings. All methods are no-ops; key events pass
 * through to ClutterText's normal key handling. */
#define CLUTTER_TYPE_INPUT_METHOD_WAYLAND_CLIENT (clutter_input_method_wayland_client_get_type ())
G_DECLARE_FINAL_TYPE (ClutterInputMethodWaylandClient,
                      clutter_input_method_wayland_client,
                      CLUTTER, INPUT_METHOD_WAYLAND_CLIENT,
                      ClutterInputMethod)

struct _ClutterInputMethodWaylandClient
{
    ClutterInputMethod parent_instance;
};

G_DEFINE_TYPE (ClutterInputMethodWaylandClient,
               clutter_input_method_wayland_client,
               CLUTTER_TYPE_INPUT_METHOD)

static void
im_wl_focus_in (ClutterInputMethod *im,
                 ClutterInputFocus  *focus)
{
}

static void
im_wl_focus_out (ClutterInputMethod *im)
{
}

static void
im_wl_reset (ClutterInputMethod *im)
{
}

static void
im_wl_set_cursor_location (ClutterInputMethod    *im,
                            const graphene_rect_t *rect)
{
}

static void
im_wl_set_surrounding (ClutterInputMethod *im,
                        const gchar        *text,
                        guint               cursor,
                        guint               anchor)
{
}

static void
im_wl_update_content_hints (ClutterInputMethod           *im,
                             ClutterInputContentHintFlags  hints)
{
}

static void
im_wl_update_content_purpose (ClutterInputMethod         *im,
                               ClutterInputContentPurpose  purpose)
{
}

static gboolean
im_wl_filter_key_event (ClutterInputMethod *im,
                         const ClutterEvent *key)
{
    return FALSE;
}

static void
clutter_input_method_wayland_client_class_init (ClutterInputMethodWaylandClientClass *klass)
{
    ClutterInputMethodClass *im_class = CLUTTER_INPUT_METHOD_CLASS (klass);

    im_class->focus_in = im_wl_focus_in;
    im_class->focus_out = im_wl_focus_out;
    im_class->reset = im_wl_reset;
    im_class->set_cursor_location = im_wl_set_cursor_location;
    im_class->set_surrounding = im_wl_set_surrounding;
    im_class->update_content_hints = im_wl_update_content_hints;
    im_class->update_content_purpose = im_wl_update_content_purpose;
    im_class->filter_key_event = im_wl_filter_key_event;
}

static void
clutter_input_method_wayland_client_init (ClutterInputMethodWaylandClient *im)
{
}

G_DEFINE_TYPE (ClutterBackendWaylandClient,
               clutter_backend_wayland_client,
               CLUTTER_TYPE_BACKEND)

/* Forward declarations */
static const struct wl_seat_listener seat_listener;
static const struct wl_keyboard_listener keyboard_listener;

/* Registry handlers */
static void
registry_global (void *data,
                 struct wl_registry *registry,
                 uint32_t name,
                 const char *interface,
                 uint32_t version)
{
    ClutterBackendWaylandClient *backend = data;

    CLUTTER_NOTE (BACKEND, "Wayland registry: %s (v%d)", interface, version);

    if (strcmp (interface, wl_compositor_interface.name) == 0)
    {
        backend->wl_compositor = wl_registry_bind (registry, name,
                                                   &wl_compositor_interface,
                                                   MIN (version, 4));
    }
    else if (strcmp (interface, wl_shm_interface.name) == 0)
    {
        backend->wl_shm = wl_registry_bind (registry, name,
                                            &wl_shm_interface,
                                            MIN (version, 1));
    }
    else if (strcmp (interface, wl_seat_interface.name) == 0)
    {
        backend->wl_seat = wl_registry_bind (registry, name,
                                             &wl_seat_interface,
                                             MIN (version, 5));
        wl_seat_add_listener (backend->wl_seat, &seat_listener, backend);
    }
    else if (strcmp (interface, wl_output_interface.name) == 0)
    {
        if (!backend->wl_output)
        {
            backend->wl_output = wl_registry_bind (registry, name,
                                                   &wl_output_interface,
                                                   MIN (version, 2));
        }
    }
    else if (strcmp (interface, zwlr_layer_shell_v1_interface.name) == 0)
    {
        backend->layer_shell = wl_registry_bind (registry, name,
                                                 &zwlr_layer_shell_v1_interface,
                                                 MIN (version, 4));
    }
}

static void
registry_global_remove (void *data,
                        struct wl_registry *registry,
                        uint32_t name)
{
    /* TODO: Handle output removal, etc. */
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

/* Surface-to-stage mapping helpers */
void
clutter_backend_wayland_client_register_surface (ClutterBackendWaylandClient *backend,
                                                  struct wl_surface           *surface,
                                                  gpointer                     stage)
{
    g_return_if_fail (CLUTTER_IS_BACKEND_WAYLAND_CLIENT (backend));
    g_return_if_fail (surface != NULL);

    if (!backend->surface_to_stage)
        backend->surface_to_stage = g_hash_table_new (g_direct_hash, g_direct_equal);

    g_hash_table_insert (backend->surface_to_stage, surface, stage);
}

void
clutter_backend_wayland_client_unregister_surface (ClutterBackendWaylandClient *backend,
                                                    struct wl_surface           *surface)
{
    g_return_if_fail (CLUTTER_IS_BACKEND_WAYLAND_CLIENT (backend));

    if (backend->surface_to_stage && surface)
        g_hash_table_remove (backend->surface_to_stage, surface);
}

static ClutterStageWaylandClient *
find_stage_for_surface (ClutterBackendWaylandClient *backend,
                        struct wl_surface           *surface)
{
    if (!backend->surface_to_stage)
        return NULL;

    return g_hash_table_lookup (backend->surface_to_stage, surface);
}

/* wl_pointer listener */
static uint32_t
wl_button_to_clutter_button (uint32_t wl_button)
{
    switch (wl_button)
    {
        case BTN_LEFT:   return 1;
        case BTN_MIDDLE: return 2;
        case BTN_RIGHT:  return 3;
        default:         return wl_button - BTN_LEFT + 1;
    }
}

static void
pointer_handle_enter (void               *data,
                      struct wl_pointer  *pointer,
                      uint32_t            serial,
                      struct wl_surface  *surface,
                      wl_fixed_t          sx,
                      wl_fixed_t          sy)
{
    ClutterBackendWaylandClient *backend_wl = data;
    ClutterSeatWaylandClient *seat_wl = CLUTTER_SEAT_WAYLAND_CLIENT (backend_wl->seat);
    ClutterStageWaylandClient *stage_wl;
    ClutterStage *stage;
    ClutterEvent *event;
    float x, y;

    stage_wl = find_stage_for_surface (backend_wl, surface);
    if (!stage_wl)
        return;

    stage = CLUTTER_STAGE_COGL (stage_wl)->wrapper;
    if (!stage)
        return;

    x = wl_fixed_to_double (sx);
    y = wl_fixed_to_double (sy);

    backend_wl->pointer_focus_surface = surface;
    backend_wl->pointer_x = x;
    backend_wl->pointer_y = y;

    _clutter_input_device_set_stage (seat_wl->pointer_device, stage);
    _clutter_input_device_set_coords (seat_wl->pointer_device, NULL, x, y, stage);

    event = clutter_event_new (CLUTTER_ENTER);
    event->crossing.time = g_get_monotonic_time () / 1000;
    event->crossing.flags = 0;
    event->crossing.stage = stage;
    event->crossing.source = CLUTTER_ACTOR (stage);
    event->crossing.x = x;
    event->crossing.y = y;
    event->crossing.related = NULL;
    clutter_event_set_device (event, seat_wl->pointer_device);
    clutter_event_set_source_device (event, seat_wl->pointer_device);
    clutter_do_event (event);
    clutter_event_free (event);
}

static void
pointer_handle_leave (void               *data,
                      struct wl_pointer  *pointer,
                      uint32_t            serial,
                      struct wl_surface  *surface)
{
    ClutterBackendWaylandClient *backend_wl = data;
    ClutterSeatWaylandClient *seat_wl = CLUTTER_SEAT_WAYLAND_CLIENT (backend_wl->seat);
    ClutterStageWaylandClient *stage_wl;
    ClutterStage *stage;
    ClutterEvent *event;

    stage_wl = find_stage_for_surface (backend_wl, surface);
    if (!stage_wl)
        return;

    stage = CLUTTER_STAGE_COGL (stage_wl)->wrapper;
    if (!stage)
        return;

    event = clutter_event_new (CLUTTER_LEAVE);
    event->crossing.time = g_get_monotonic_time () / 1000;
    event->crossing.flags = 0;
    event->crossing.stage = stage;
    event->crossing.source = CLUTTER_ACTOR (stage);
    event->crossing.x = backend_wl->pointer_x;
    event->crossing.y = backend_wl->pointer_y;
    event->crossing.related = NULL;
    clutter_event_set_device (event, seat_wl->pointer_device);
    clutter_event_set_source_device (event, seat_wl->pointer_device);
    clutter_do_event (event);
    clutter_event_free (event);

    backend_wl->pointer_focus_surface = NULL;
    _clutter_input_device_set_stage (seat_wl->pointer_device, NULL);
}

static void
pointer_handle_motion (void               *data,
                       struct wl_pointer  *pointer,
                       uint32_t            time,
                       wl_fixed_t          sx,
                       wl_fixed_t          sy)
{
    ClutterBackendWaylandClient *backend_wl = data;
    ClutterSeatWaylandClient *seat_wl = CLUTTER_SEAT_WAYLAND_CLIENT (backend_wl->seat);
    ClutterStageWaylandClient *stage_wl;
    ClutterStage *stage;
    ClutterEvent *event;
    float x, y;

    stage_wl = find_stage_for_surface (backend_wl, backend_wl->pointer_focus_surface);
    if (!stage_wl)
        return;

    stage = CLUTTER_STAGE_COGL (stage_wl)->wrapper;
    if (!stage)
        return;

    x = wl_fixed_to_double (sx);
    y = wl_fixed_to_double (sy);

    backend_wl->pointer_x = x;
    backend_wl->pointer_y = y;

    _clutter_input_device_set_coords (seat_wl->pointer_device, NULL, x, y, stage);

    event = clutter_event_new (CLUTTER_MOTION);
    event->motion.time = time;
    event->motion.flags = 0;
    event->motion.stage = stage;
    event->motion.x = x;
    event->motion.y = y;
    event->motion.modifier_state = backend_wl->modifier_state;
    event->motion.axes = NULL;
    clutter_event_set_device (event, seat_wl->pointer_device);
    clutter_event_set_source_device (event, seat_wl->pointer_device);
    clutter_do_event (event);
    clutter_event_free (event);
}

static void
pointer_handle_button (void               *data,
                       struct wl_pointer  *pointer,
                       uint32_t            serial,
                       uint32_t            time,
                       uint32_t            button,
                       uint32_t            state)
{
    ClutterBackendWaylandClient *backend_wl = data;
    ClutterSeatWaylandClient *seat_wl = CLUTTER_SEAT_WAYLAND_CLIENT (backend_wl->seat);
    ClutterStageWaylandClient *stage_wl;
    ClutterStage *stage;
    ClutterEvent *event;
    ClutterEventType event_type;

    stage_wl = find_stage_for_surface (backend_wl, backend_wl->pointer_focus_surface);
    if (!stage_wl)
        return;

    stage = CLUTTER_STAGE_COGL (stage_wl)->wrapper;
    if (!stage)
        return;

    backend_wl->pointer_button_serial = serial;

    event_type = (state == WL_POINTER_BUTTON_STATE_PRESSED)
        ? CLUTTER_BUTTON_PRESS
        : CLUTTER_BUTTON_RELEASE;

    event = clutter_event_new (event_type);
    event->button.time = time;
    event->button.flags = 0;
    event->button.stage = stage;
    event->button.x = backend_wl->pointer_x;
    event->button.y = backend_wl->pointer_y;
    event->button.modifier_state = backend_wl->modifier_state;
    event->button.button = wl_button_to_clutter_button (button);
    event->button.click_count = 1;
    event->button.axes = NULL;
    clutter_event_set_device (event, seat_wl->pointer_device);
    clutter_event_set_source_device (event, seat_wl->pointer_device);
    clutter_do_event (event);
    clutter_event_free (event);
}

static void
pointer_handle_axis (void               *data,
                     struct wl_pointer  *pointer,
                     uint32_t            time,
                     uint32_t            axis,
                     wl_fixed_t          value)
{
    ClutterBackendWaylandClient *backend_wl = data;
    ClutterSeatWaylandClient *seat_wl = CLUTTER_SEAT_WAYLAND_CLIENT (backend_wl->seat);
    ClutterStageWaylandClient *stage_wl;
    ClutterStage *stage;
    ClutterEvent *event;
    ClutterScrollDirection direction;
    double val;

    stage_wl = find_stage_for_surface (backend_wl, backend_wl->pointer_focus_surface);
    if (!stage_wl)
        return;

    stage = CLUTTER_STAGE_COGL (stage_wl)->wrapper;
    if (!stage)
        return;

    val = wl_fixed_to_double (value);

    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        direction = (val > 0) ? CLUTTER_SCROLL_DOWN : CLUTTER_SCROLL_UP;
    else
        direction = (val > 0) ? CLUTTER_SCROLL_RIGHT : CLUTTER_SCROLL_LEFT;

    event = clutter_event_new (CLUTTER_SCROLL);
    event->scroll.time = time;
    event->scroll.flags = 0;
    event->scroll.stage = stage;
    event->scroll.x = backend_wl->pointer_x;
    event->scroll.y = backend_wl->pointer_y;
    event->scroll.direction = direction;
    event->scroll.modifier_state = backend_wl->modifier_state;
    event->scroll.axes = NULL;
    event->scroll.scroll_source = CLUTTER_SCROLL_SOURCE_UNKNOWN;
    event->scroll.finish_flags = CLUTTER_SCROLL_FINISHED_NONE;
    clutter_event_set_device (event, seat_wl->pointer_device);
    clutter_event_set_source_device (event, seat_wl->pointer_device);
    clutter_do_event (event);
    clutter_event_free (event);
}

static void
pointer_handle_frame (void *data, struct wl_pointer *pointer)
{
}

static void
pointer_handle_axis_source (void *data, struct wl_pointer *pointer, uint32_t axis_source)
{
}

static void
pointer_handle_axis_stop (void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis)
{
}

static void
pointer_handle_axis_discrete (void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete)
{
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_handle_enter,
    .leave = pointer_handle_leave,
    .motion = pointer_handle_motion,
    .button = pointer_handle_button,
    .axis = pointer_handle_axis,
    .frame = pointer_handle_frame,
    .axis_source = pointer_handle_axis_source,
    .axis_stop = pointer_handle_axis_stop,
    .axis_discrete = pointer_handle_axis_discrete,
};

/* wl_keyboard listener */
static void
keyboard_handle_keymap (void               *data,
                        struct wl_keyboard *keyboard,
                        uint32_t            format,
                        int32_t             fd,
                        uint32_t            size)
{
    ClutterBackendWaylandClient *backend_wl = data;
    ClutterSeatWaylandClient *seat_wl = CLUTTER_SEAT_WAYLAND_CLIENT (backend_wl->seat);
    char *map_str;

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
    {
        close (fd);
        return;
    }

    map_str = mmap (NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map_str == MAP_FAILED)
    {
        close (fd);
        return;
    }

    if (!backend_wl->xkb_context)
        backend_wl->xkb_context = xkb_context_new (XKB_CONTEXT_NO_FLAGS);

    g_clear_pointer (&backend_wl->xkb_keymap, xkb_keymap_unref);
    g_clear_pointer (&backend_wl->xkb_state, xkb_state_unref);

    backend_wl->xkb_keymap = xkb_keymap_new_from_string (backend_wl->xkb_context,
                                                          map_str,
                                                          XKB_KEYMAP_FORMAT_TEXT_V1,
                                                          XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap (map_str, size);
    close (fd);

    if (!backend_wl->xkb_keymap)
    {
        g_warning ("Failed to compile XKB keymap");
        return;
    }

    backend_wl->xkb_state = xkb_state_new (backend_wl->xkb_keymap);

    if (seat_wl->keymap)
        clutter_keymap_wayland_client_set_xkb_state (
            CLUTTER_KEYMAP_WAYLAND_CLIENT (seat_wl->keymap),
            backend_wl->xkb_state);

    CLUTTER_NOTE (BACKEND, "Wayland keyboard keymap updated");
}

static void
keyboard_handle_enter (void               *data,
                       struct wl_keyboard *keyboard,
                       uint32_t            serial,
                       struct wl_surface  *surface,
                       struct wl_array    *keys)
{
    ClutterBackendWaylandClient *backend_wl = data;
    ClutterSeatWaylandClient *seat_wl = CLUTTER_SEAT_WAYLAND_CLIENT (backend_wl->seat);
    ClutterStageWaylandClient *stage_wl;
    ClutterStage *stage;

    stage_wl = find_stage_for_surface (backend_wl, surface);
    if (!stage_wl)
        return;

    stage = CLUTTER_STAGE_COGL (stage_wl)->wrapper;
    if (!stage)
        return;

    backend_wl->keyboard_focus_surface = surface;
    _clutter_input_device_set_stage (seat_wl->keyboard_device, stage);

    CLUTTER_NOTE (BACKEND, "Keyboard focus entered surface");
}

static void
keyboard_handle_leave (void               *data,
                       struct wl_keyboard *keyboard,
                       uint32_t            serial,
                       struct wl_surface  *surface)
{
    ClutterBackendWaylandClient *backend_wl = data;
    ClutterSeatWaylandClient *seat_wl = CLUTTER_SEAT_WAYLAND_CLIENT (backend_wl->seat);

    backend_wl->keyboard_focus_surface = NULL;
    _clutter_input_device_set_stage (seat_wl->keyboard_device, NULL);

    CLUTTER_NOTE (BACKEND, "Keyboard focus left surface");
}

static void
keyboard_handle_key (void               *data,
                     struct wl_keyboard *keyboard,
                     uint32_t            serial,
                     uint32_t            time,
                     uint32_t            key,
                     uint32_t            state)
{
    ClutterBackendWaylandClient *backend_wl = data;
    ClutterSeatWaylandClient *seat_wl = CLUTTER_SEAT_WAYLAND_CLIENT (backend_wl->seat);
    ClutterStageWaylandClient *stage_wl;
    ClutterStage *stage;
    ClutterEvent *event;
    xkb_keysym_t sym;
    const xkb_keysym_t *syms;
    xkb_keycode_t xkb_key;
    char buffer[8];
    int n;

    if (!backend_wl->xkb_state)
        return;

    stage_wl = find_stage_for_surface (backend_wl, backend_wl->keyboard_focus_surface);
    if (!stage_wl)
        return;

    stage = CLUTTER_STAGE_COGL (stage_wl)->wrapper;
    if (!stage)
        return;

    /* Wayland sends evdev keycodes (base 0), XKB expects base 8 */
    xkb_key = key + 8;

    n = xkb_key_get_syms (backend_wl->xkb_state, xkb_key, &syms);
    if (n == 1)
        sym = syms[0];
    else
        sym = XKB_KEY_NoSymbol;

    if (state)
        event = clutter_event_new (CLUTTER_KEY_PRESS);
    else
        event = clutter_event_new (CLUTTER_KEY_RELEASE);

    event->key.stage = stage;
    event->key.time = time;
    event->key.hardware_keycode = xkb_key;
    event->key.keyval = sym;

    _clutter_event_set_state_full (event,
                                   0,
                                   xkb_state_serialize_mods (backend_wl->xkb_state,
                                                             XKB_STATE_MODS_DEPRESSED),
                                   xkb_state_serialize_mods (backend_wl->xkb_state,
                                                             XKB_STATE_MODS_LATCHED),
                                   xkb_state_serialize_mods (backend_wl->xkb_state,
                                                             XKB_STATE_MODS_LOCKED),
                                   xkb_state_serialize_mods (backend_wl->xkb_state,
                                                             XKB_STATE_MODS_EFFECTIVE));

    event->key.modifier_state = xkb_state_serialize_mods (backend_wl->xkb_state,
                                                           XKB_STATE_MODS_EFFECTIVE);

    clutter_event_set_device (event, seat_wl->keyboard_device);
    clutter_event_set_source_device (event, seat_wl->keyboard_device);

    n = xkb_keysym_to_utf8 (sym, buffer, sizeof (buffer));
    if (n == 0)
    {
        event->key.unicode_value = (gunichar) '\0';
    }
    else
    {
        event->key.unicode_value = g_utf8_get_char_validated (buffer, n);
        if (event->key.unicode_value == (gunichar) -1 ||
            event->key.unicode_value == (gunichar) -2)
            event->key.unicode_value = (gunichar) '\0';
    }

    /* Update xkb_state for the key press/release so that subsequent
     * modifier queries are accurate (this matters for key sequences
     * like Shift+a). The compositor also sends explicit modifier
     * updates via the modifiers callback, but updating here keeps
     * state consistent between key and modifier events. */
    xkb_state_update_key (backend_wl->xkb_state, xkb_key,
                          state ? XKB_KEY_DOWN : XKB_KEY_UP);

    backend_wl->modifier_state = xkb_state_serialize_mods (backend_wl->xkb_state,
                                                            XKB_STATE_MODS_EFFECTIVE);

    clutter_do_event (event);
    clutter_event_free (event);
}

static void
keyboard_handle_modifiers (void               *data,
                           struct wl_keyboard *keyboard,
                           uint32_t            serial,
                           uint32_t            mods_depressed,
                           uint32_t            mods_latched,
                           uint32_t            mods_locked,
                           uint32_t            group)
{
    ClutterBackendWaylandClient *backend_wl = data;

    if (!backend_wl->xkb_state)
        return;

    xkb_state_update_mask (backend_wl->xkb_state,
                           mods_depressed, mods_latched, mods_locked,
                           0, 0, group);

    backend_wl->modifier_state = xkb_state_serialize_mods (backend_wl->xkb_state,
                                                            XKB_STATE_MODS_EFFECTIVE);
}

static void
keyboard_handle_repeat_info (void               *data,
                             struct wl_keyboard *keyboard,
                             int32_t             rate,
                             int32_t             delay)
{
    CLUTTER_NOTE (BACKEND, "Keyboard repeat: rate=%d delay=%d", rate, delay);
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_handle_keymap,
    .enter = keyboard_handle_enter,
    .leave = keyboard_handle_leave,
    .key = keyboard_handle_key,
    .modifiers = keyboard_handle_modifiers,
    .repeat_info = keyboard_handle_repeat_info,
};

/* wl_seat listener */
static void
seat_handle_capabilities (void            *data,
                          struct wl_seat  *seat,
                          uint32_t         capabilities)
{
    ClutterBackendWaylandClient *backend_wl = data;

    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !backend_wl->wl_pointer)
    {
        backend_wl->wl_pointer = wl_seat_get_pointer (backend_wl->wl_seat);
        wl_pointer_add_listener (backend_wl->wl_pointer, &pointer_listener, backend_wl);
        CLUTTER_NOTE (BACKEND, "Wayland pointer acquired");
    }
    else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && backend_wl->wl_pointer)
    {
        wl_pointer_destroy (backend_wl->wl_pointer);
        backend_wl->wl_pointer = NULL;
        CLUTTER_NOTE (BACKEND, "Wayland pointer lost");
    }

    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !backend_wl->wl_keyboard)
    {
        backend_wl->wl_keyboard = wl_seat_get_keyboard (backend_wl->wl_seat);
        wl_keyboard_add_listener (backend_wl->wl_keyboard, &keyboard_listener, backend_wl);
        CLUTTER_NOTE (BACKEND, "Wayland keyboard acquired");
    }
    else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && backend_wl->wl_keyboard)
    {
        wl_keyboard_destroy (backend_wl->wl_keyboard);
        backend_wl->wl_keyboard = NULL;
        CLUTTER_NOTE (BACKEND, "Wayland keyboard lost");
    }
}

static void
seat_handle_name (void *data, struct wl_seat *seat, const char *name)
{
    CLUTTER_NOTE (BACKEND, "Wayland seat name: %s", name);
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

/* GSource for Wayland display events */
typedef struct {
    GSource source;
    ClutterBackendWaylandClient *backend;
    GPollFD pfd;
} WaylandEventSource;

static gboolean
wayland_event_source_prepare (GSource *source,
                              gint    *timeout)
{
    WaylandEventSource *wl_source = (WaylandEventSource *) source;
    struct wl_display *display = wl_source->backend->wl_display;

    *timeout = -1;

    if (!display)
        return FALSE;

    /* Dispatch any events already in the default queue but not yet
     * dispatched. This is critical: eglSwapBuffers (called during redraw)
     * internally reads from the Wayland fd to process buffer release
     * events on Cogl's private event queue. That read also pulls in our
     * input events, placing them in the default queue without dispatching
     * them. Without this call those events would sit undispatched until
     * new data arrives on the fd. */
    if (wl_display_prepare_read (display) != 0)
    {
        wl_display_dispatch_pending (display);
        wl_display_flush (display);
        return FALSE;
    }

    wl_display_flush (display);
    wl_display_cancel_read (display);

    return FALSE;
}

static gboolean
wayland_event_source_check (GSource *source)
{
    WaylandEventSource *wl_source = (WaylandEventSource *) source;

    return (wl_source->pfd.revents & G_IO_IN) != 0;
}

static gboolean
wayland_event_source_dispatch (GSource     *source,
                               GSourceFunc  callback,
                               gpointer     user_data)
{
    WaylandEventSource *wl_source = (WaylandEventSource *) source;
    struct wl_display *display = wl_source->backend->wl_display;

    if (wl_source->pfd.revents & (G_IO_ERR | G_IO_HUP))
    {
        g_warning ("Wayland display error");
        return G_SOURCE_REMOVE;
    }

    if (wl_source->pfd.revents & G_IO_IN)
    {
        if (wl_display_dispatch (display) == -1)
        {
            g_warning ("Wayland display dispatch failed: %s", strerror (errno));
            return G_SOURCE_REMOVE;
        }
    }

    return G_SOURCE_CONTINUE;
}

static void
wayland_event_source_finalize (GSource *source)
{
}

static GSourceFuncs wayland_event_source_funcs = {
    .prepare = wayland_event_source_prepare,
    .check = wayland_event_source_check,
    .dispatch = wayland_event_source_dispatch,
    .finalize = wayland_event_source_finalize,
};

static GSource *
wayland_event_source_new (ClutterBackendWaylandClient *backend)
{
    GSource *source;
    WaylandEventSource *wl_source;

    source = g_source_new (&wayland_event_source_funcs, sizeof (WaylandEventSource));
    g_source_set_name (source, "Wayland Event Source");

    wl_source = (WaylandEventSource *) source;
    wl_source->backend = backend;
    wl_source->pfd.fd = wl_display_get_fd (backend->wl_display);
    wl_source->pfd.events = G_IO_IN | G_IO_ERR | G_IO_HUP;

    g_source_add_poll (source, &wl_source->pfd);

    return source;
}

/* Backend vfuncs */
static gboolean
clutter_backend_wayland_client_post_parse (ClutterBackend *backend,
                                           GError        **error)
{
    ClutterBackendWaylandClient *backend_wl = CLUTTER_BACKEND_WAYLAND_CLIENT (backend);
    const char *display_name;

    display_name = g_getenv ("WAYLAND_DISPLAY");
    if (!display_name)
        display_name = "wayland-0";

    CLUTTER_NOTE (BACKEND, "Connecting to Wayland display '%s'", display_name);

    backend_wl->wl_display = wl_display_connect (display_name);
    if (!backend_wl->wl_display)
    {
        g_set_error (error, CLUTTER_INIT_ERROR,
                     CLUTTER_INIT_ERROR_BACKEND,
                     "Failed to connect to Wayland display '%s': %s",
                     display_name, strerror (errno));
        return FALSE;
    }

    /* Get registry and bind globals */
    backend_wl->wl_registry = wl_display_get_registry (backend_wl->wl_display);
    wl_registry_add_listener (backend_wl->wl_registry, &registry_listener, backend_wl);
    wl_display_roundtrip (backend_wl->wl_display);

    /* Check required interfaces */
    if (!backend_wl->wl_compositor)
    {
        g_set_error_literal (error, CLUTTER_INIT_ERROR,
                             CLUTTER_INIT_ERROR_BACKEND,
                             "wl_compositor not available from Wayland compositor");
        return FALSE;
    }

    if (!backend_wl->layer_shell)
    {
        g_set_error_literal (error, CLUTTER_INIT_ERROR,
                             CLUTTER_INIT_ERROR_BACKEND,
                             "zwlr_layer_shell_v1 not available from Wayland compositor");
        return FALSE;
    }

    CLUTTER_NOTE (BACKEND, "Connected to Wayland display, protocols bound");

    /* Create event source for Wayland events */
    backend_wl->wayland_source = wayland_event_source_new (backend_wl);
    g_source_attach (backend_wl->wayland_source, NULL);

    return TRUE;
}

static CoglRenderer *
clutter_backend_wayland_client_get_renderer (ClutterBackend *backend,
                                             GError        **error)
{
    ClutterBackendWaylandClient *backend_wl = CLUTTER_BACKEND_WAYLAND_CLIENT (backend);
    CoglRenderer *renderer;

    CLUTTER_NOTE (BACKEND, "Creating Cogl renderer for Wayland EGL");

    renderer = cogl_renderer_new ();

    cogl_renderer_set_winsys_id (renderer, COGL_WINSYS_ID_EGL_WAYLAND);
    cogl_wayland_renderer_set_foreign_display (renderer, backend_wl->wl_display);

    if (!cogl_renderer_connect (renderer, error))
    {
        cogl_object_unref (renderer);
        return NULL;
    }

    return renderer;
}

static CoglDisplay *
clutter_backend_wayland_client_get_display (ClutterBackend *backend,
                                            CoglRenderer   *renderer,
                                            CoglSwapChain  *swap_chain,
                                            GError        **error)
{
    CoglOnscreenTemplate *onscreen_template;
    CoglDisplay *display;

    CLUTTER_NOTE (BACKEND, "Creating CoglDisplay for Wayland");

    onscreen_template = cogl_onscreen_template_new (swap_chain);
    cogl_swap_chain_set_has_alpha (swap_chain, TRUE);

    if (!cogl_renderer_check_onscreen_template (renderer, onscreen_template, error))
    {
        cogl_object_unref (onscreen_template);
        return NULL;
    }

    display = cogl_display_new (renderer, onscreen_template);
    cogl_object_unref (onscreen_template);

    return display;
}

static ClutterStageWindow *
clutter_backend_wayland_client_create_stage (ClutterBackend *backend,
                                             ClutterStage   *wrapper,
                                             GError        **error)
{
    CLUTTER_NOTE (BACKEND, "Creating Wayland client stage");

    return g_object_new (CLUTTER_TYPE_STAGE_WAYLAND_CLIENT,
                         "wrapper", wrapper,
                         "backend", backend,
                         NULL);
}

static ClutterFeatureFlags
clutter_backend_wayland_client_get_features (ClutterBackend *backend)
{
    ClutterFeatureFlags flags;

    flags = CLUTTER_BACKEND_CLASS (clutter_backend_wayland_client_parent_class)->get_features (backend);
    flags |= CLUTTER_FEATURE_STAGE_MULTIPLE;

    return flags;
}

static void
clutter_backend_wayland_client_init_events (ClutterBackend *backend)
{
    ClutterBackendWaylandClient *backend_wl = CLUTTER_BACKEND_WAYLAND_CLIENT (backend);
    ClutterInputMethod *im;

    CLUTTER_NOTE (BACKEND, "Initializing Wayland input events");

    backend_wl->seat = clutter_seat_wayland_client_new (CLUTTER_BACKEND (backend_wl));

    im = g_object_new (CLUTTER_TYPE_INPUT_METHOD_WAYLAND_CLIENT, NULL);
    clutter_backend_set_input_method (backend, im);
    g_object_unref (im);
}

static ClutterSeat *
clutter_backend_wayland_client_get_default_seat (ClutterBackend *backend)
{
    ClutterBackendWaylandClient *backend_wl = CLUTTER_BACKEND_WAYLAND_CLIENT (backend);

    return backend_wl->seat;
}

static void
clutter_backend_wayland_client_dispose (GObject *object)
{
    ClutterBackendWaylandClient *backend_wl = CLUTTER_BACKEND_WAYLAND_CLIENT (object);

    g_clear_object (&backend_wl->seat);
    g_clear_object (&backend_wl->xsettings);

    G_OBJECT_CLASS (clutter_backend_wayland_client_parent_class)->dispose (object);
}

static void
clutter_backend_wayland_client_finalize (GObject *object)
{
    ClutterBackendWaylandClient *backend_wl = CLUTTER_BACKEND_WAYLAND_CLIENT (object);

    if (backend_wl->wayland_source)
    {
        g_source_destroy (backend_wl->wayland_source);
        g_source_unref (backend_wl->wayland_source);
    }

    g_clear_pointer (&backend_wl->surface_to_stage, g_hash_table_destroy);

    g_clear_pointer (&backend_wl->xkb_state, xkb_state_unref);
    g_clear_pointer (&backend_wl->xkb_keymap, xkb_keymap_unref);
    g_clear_pointer (&backend_wl->xkb_context, xkb_context_unref);

    if (backend_wl->layer_shell)
        zwlr_layer_shell_v1_destroy (backend_wl->layer_shell);

    if (backend_wl->wl_pointer)
        wl_pointer_destroy (backend_wl->wl_pointer);

    if (backend_wl->wl_keyboard)
        wl_keyboard_destroy (backend_wl->wl_keyboard);

    if (backend_wl->wl_seat)
        wl_seat_destroy (backend_wl->wl_seat);

    if (backend_wl->wl_output)
        wl_output_destroy (backend_wl->wl_output);

    if (backend_wl->wl_shm)
        wl_shm_destroy (backend_wl->wl_shm);

    if (backend_wl->wl_compositor)
        wl_compositor_destroy (backend_wl->wl_compositor);

    if (backend_wl->wl_registry)
        wl_registry_destroy (backend_wl->wl_registry);

    if (backend_wl->wl_display)
        wl_display_disconnect (backend_wl->wl_display);

    G_OBJECT_CLASS (clutter_backend_wayland_client_parent_class)->finalize (object);
}

static void
clutter_backend_wayland_client_class_init (ClutterBackendWaylandClientClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    ClutterBackendClass *backend_class = CLUTTER_BACKEND_CLASS (klass);

    object_class->dispose = clutter_backend_wayland_client_dispose;
    object_class->finalize = clutter_backend_wayland_client_finalize;

    backend_class->post_parse = clutter_backend_wayland_client_post_parse;
    backend_class->get_renderer = clutter_backend_wayland_client_get_renderer;
    backend_class->get_display = clutter_backend_wayland_client_get_display;
    backend_class->create_stage = clutter_backend_wayland_client_create_stage;
    backend_class->get_features = clutter_backend_wayland_client_get_features;
    backend_class->init_events = clutter_backend_wayland_client_init_events;
    backend_class->get_default_seat = clutter_backend_wayland_client_get_default_seat;
}

static void
clutter_backend_wayland_client_init (ClutterBackendWaylandClient *backend)
{
}

ClutterBackend *
clutter_backend_wayland_client_new (void)
{
    return g_object_new (CLUTTER_TYPE_BACKEND_WAYLAND_CLIENT, NULL);
}

/* Accessors */
struct wl_display *
clutter_backend_wayland_client_get_wl_display (ClutterBackendWaylandClient *backend)
{
    g_return_val_if_fail (CLUTTER_IS_BACKEND_WAYLAND_CLIENT (backend), NULL);
    return backend->wl_display;
}

struct wl_compositor *
clutter_backend_wayland_client_get_compositor (ClutterBackendWaylandClient *backend)
{
    g_return_val_if_fail (CLUTTER_IS_BACKEND_WAYLAND_CLIENT (backend), NULL);
    return backend->wl_compositor;
}

struct zwlr_layer_shell_v1 *
clutter_backend_wayland_client_get_layer_shell (ClutterBackendWaylandClient *backend)
{
    g_return_val_if_fail (CLUTTER_IS_BACKEND_WAYLAND_CLIENT (backend), NULL);
    return backend->layer_shell;
}

struct wl_output *
clutter_backend_wayland_client_get_output (ClutterBackendWaylandClient *backend)
{
    g_return_val_if_fail (CLUTTER_IS_BACKEND_WAYLAND_CLIENT (backend), NULL);
    return backend->wl_output;
}
