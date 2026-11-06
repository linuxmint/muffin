/*
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

#include "config.h"

#include "meta/util.h"

#ifdef HAVE_WAYLAND
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-keyboard.h"
#include "wayland/meta-wayland-seat.h"
#endif

/* Whether ~/.xinputrc (written by im-config / mintlocale-im) selects fcitx5.
 * For Wayland sessions we deliberately do not set GTK_IM_MODULE=fcitx
 * (that would route apps to fcitx's toolkit modules and bypass the compositor
 * bridge), so the env is not a reliable signal.
 *
 * The "run_im fcitx5" prefix match (with '#'-comment skip) is duplicated in
 * cinnamon-session main.c and cinnamon's cinnamon-settings bin/util.py -
 * separate processes/languages that can't share this code. If im-config ever
 * changes its line format, all three must change together. */
static gboolean
xinputrc_selects_fcitx (void)
{
  g_autofree char *path = NULL;
  g_autofree char *contents = NULL;
  g_auto (GStrv) lines = NULL;
  int i;

  path = g_build_filename (g_get_home_dir (), ".xinputrc", NULL);
  if (!g_file_get_contents (path, &contents, NULL, NULL))
    return FALSE;

  lines = g_strsplit (contents, "\n", -1);
  for (i = 0; lines[i]; i++)
    {
      const char *line = g_strchug (lines[i]);

      if (line[0] == '#')
        continue;
      if (g_str_has_prefix (line, "run_im fcitx5"))
        return TRUE;
    }

  return FALSE;
}

static gboolean
im_mode_is_fcitx (void)
{
  g_autofree char *fcitx5_path = NULL;

  if (!meta_is_wayland_compositor ())
    return FALSE;

  fcitx5_path = g_find_program_in_path ("fcitx5");
  if (!fcitx5_path)
    return FALSE;

  return xinputrc_selects_fcitx ();
}

gboolean
meta_im_mode_is_fcitx (void)
{
  /* Cached: the mode must stay fixed for the compositor's lifetime — the
   * protocol globals, the launcher and the JS side all key off it, and
   * re-reading ~/.xinputrc mid-session could give them different answers. */
  static gsize once_init = 0;
  static gboolean mode_is_fcitx = FALSE;

  if (g_once_init_enter (&once_init))
    {
      mode_is_fcitx = im_mode_is_fcitx ();
      g_once_init_leave (&once_init, 1);
    }

  return mode_is_fcitx;
}

/**
 * meta_wayland_type_keysym:
 * @keysym: an X11/xkb keysym
 *
 * Types a keysym that the current keyboard layout cannot produce, by briefly
 * swapping the seat's keymap for a temporary one containing it. This reaches
 * every client type - including XWayland - unlike input-method commits.
 *
 * Returns: %TRUE if the keysym was typed this way. %FALSE when the keysym is
 * reachable through the current layout (type it with a virtual input device
 * instead, keeping normal key semantics), or on X11 sessions.
 */
gboolean
meta_wayland_type_keysym (unsigned int keysym)
{
#ifdef HAVE_WAYLAND
  MetaWaylandCompositor *compositor;

  if (!meta_is_wayland_compositor ())
    return FALSE;

  compositor = meta_wayland_compositor_get_default ();
  if (!compositor->seat || !compositor->seat->keyboard)
    return FALSE;

  return meta_wayland_keyboard_maybe_type_keysym (compositor->seat->keyboard,
                                                  keysym);
#else
  return FALSE;
#endif
}
