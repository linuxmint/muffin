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

#include "clutter-keymap-wayland-client.h"

G_DEFINE_TYPE (ClutterKeymapWaylandClient,
               clutter_keymap_wayland_client,
               CLUTTER_TYPE_KEYMAP)

static gboolean
clutter_keymap_wayland_client_get_num_lock_state (ClutterKeymap *keymap)
{
    ClutterKeymapWaylandClient *keymap_wl = CLUTTER_KEYMAP_WAYLAND_CLIENT (keymap);

    if (keymap_wl->xkb_state)
        return xkb_state_mod_name_is_active (keymap_wl->xkb_state,
                                             XKB_MOD_NAME_NUM,
                                             XKB_STATE_MODS_LOCKED);

    return FALSE;
}

static gboolean
clutter_keymap_wayland_client_get_caps_lock_state (ClutterKeymap *keymap)
{
    ClutterKeymapWaylandClient *keymap_wl = CLUTTER_KEYMAP_WAYLAND_CLIENT (keymap);

    if (keymap_wl->xkb_state)
        return xkb_state_mod_name_is_active (keymap_wl->xkb_state,
                                             XKB_MOD_NAME_CAPS,
                                             XKB_STATE_MODS_LOCKED);

    return FALSE;
}

static PangoDirection
clutter_keymap_wayland_client_get_direction (ClutterKeymap *keymap)
{
    return PANGO_DIRECTION_LTR;
}

static void
clutter_keymap_wayland_client_class_init (ClutterKeymapWaylandClientClass *klass)
{
    ClutterKeymapClass *keymap_class = CLUTTER_KEYMAP_CLASS (klass);

    keymap_class->get_num_lock_state = clutter_keymap_wayland_client_get_num_lock_state;
    keymap_class->get_caps_lock_state = clutter_keymap_wayland_client_get_caps_lock_state;
    keymap_class->get_direction = clutter_keymap_wayland_client_get_direction;
}

static void
clutter_keymap_wayland_client_init (ClutterKeymapWaylandClient *keymap)
{
}

ClutterKeymap *
clutter_keymap_wayland_client_new (void)
{
    return g_object_new (CLUTTER_TYPE_KEYMAP_WAYLAND_CLIENT, NULL);
}

void
clutter_keymap_wayland_client_set_xkb_state (ClutterKeymapWaylandClient *keymap,
                                              struct xkb_state           *xkb_state)
{
    g_return_if_fail (CLUTTER_IS_KEYMAP_WAYLAND_CLIENT (keymap));

    keymap->xkb_state = xkb_state;
}
