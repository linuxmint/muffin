/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter main */

/*
 * Copyright (C) 2001 Havoc Pennington
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef META_MAIN_H
#define META_MAIN_H

#include <glib.h>

#include <meta/common.h>

META_EXPORT
GOptionContext *meta_get_option_context     (void);

META_EXPORT
void            meta_init                   (void);

META_EXPORT
int             meta_run                    (void);

META_EXPORT
void            meta_register_with_session  (void);

META_EXPORT
gboolean        meta_activate_session       (void);  /* Actually defined in meta-backend.c */

META_EXPORT
gboolean        meta_get_replace_current_wm (void);  /* Actually defined in util.c */

META_EXPORT
void            meta_set_wm_name              (const char *wm_name);

META_EXPORT
void            meta_set_gnome_wm_keybindings (const char *wm_keybindings);

META_EXPORT
void            meta_restart                (const char *message);

META_EXPORT
gboolean        meta_is_restart             (void);

/**
 * MetaExitCode:
 * @META_EXIT_SUCCESS: Success
 * @META_EXIT_ERROR: Error
 */
typedef enum
{
  META_EXIT_SUCCESS,
  META_EXIT_ERROR
} MetaExitCode;

/* exit immediately */
META_EXPORT
void meta_exit (MetaExitCode code) G_GNUC_NORETURN;

/* g_main_loop_quit() then fall out of main() */
META_EXPORT
void meta_quit (MetaExitCode code);

META_EXPORT
void            meta_test_init               (void);


#endif
