/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat, Inc.
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

/*
 * SECTION:restart
 * @short_description: Smoothly restart the compositor
 *
 * There are some cases where we need to restart Muffin in order
 * to deal with changes in state - the particular case inspiring
 * this is enabling or disabling stereo output. To make this
 * fairly smooth for the user, we need to do two things:
 *
 *  - Display a message to the user and make sure that it is
 *    actually painted before we exit.
 *  - Use a helper program so that the Composite Overlay Window
 *    isn't unmapped and mapped.
 *
 * This handles both of these.
 */

#include <config.h>

#include <clutter/clutter.h>
#include <gio/gunixinputstream.h>

#include <meta/main.h>
#include "ui.h"
#include <meta/util.h>
#include "display-private.h"

static gboolean restart_helper_started = FALSE;
static gboolean restart_stage_shown = FALSE;

static void
restart_check_ready (void)
{
  if (restart_helper_started && restart_stage_shown)
    meta_display_restart (meta_get_display ());
}

static void
restart_helper_read_line_callback (GObject      *source_object,
                                   GAsyncResult *res,
                                   gpointer      user_data)
{
  GError *error = NULL;
  gsize length;
  char *line = g_data_input_stream_read_line_finish_utf8 (G_DATA_INPUT_STREAM (source_object),
                                                          res,
                                                          &length, &error);
  if (line == NULL)
    {
      meta_warning ("Failed to read output from restart helper%s%s\n",
                    error ? ": " : NULL,
                    error ? error->message : NULL);
    }
  else
    g_free (line); /* We don't actually care what the restart helper outputs */

  g_object_unref (source_object);

  restart_helper_started = TRUE;
  restart_check_ready ();
}

static gboolean
restart_stage_painted (gpointer data)
{
  restart_stage_shown = TRUE;
  restart_check_ready ();

  return FALSE;
}

/**
 * meta_restart:
 *
 * Starts the process of restarting the compositor.
 */
void
meta_restart (void)
{
  MetaDisplay *display = meta_get_display();
  GInputStream *unix_stream;
  GDataInputStream *data_stream;
  GError *error = NULL;
  int helper_out_fd;

  static const char * const helper_argv[] = {
    MUFFIN_LIBEXECDIR "/muffin-restart-helper", NULL
  };

  meta_display_notify_restart (display);

  /* Wait until the stage was painted */
  clutter_threads_add_repaint_func_full (CLUTTER_REPAINT_FLAGS_POST_PAINT,
                                         restart_stage_painted,
                                         NULL, NULL);

  /* We also need to wait for the restart helper to get its
   * reference to the Composite Overlay Window.
   */
  if (!g_spawn_async_with_pipes (NULL, /* working directory */
                                 (char **)helper_argv,
                                 NULL, /* envp */
                                 G_SPAWN_DEFAULT,
                                 NULL, NULL, /* child_setup */
                                 NULL, /* child_pid */
                                 NULL, /* standard_input */
                                 &helper_out_fd,
                                 NULL, /* standard_error */
                                 &error))
    {
      meta_warning ("Failed to start restart helper: %s\n", error->message);
      goto error;
    }

  unix_stream = g_unix_input_stream_new (helper_out_fd, TRUE);
  data_stream = g_data_input_stream_new (unix_stream);
  g_object_unref (unix_stream);

  g_data_input_stream_read_line_async (data_stream, G_PRIORITY_DEFAULT,
                                       NULL, restart_helper_read_line_callback,
                                       &error);
  if (error != NULL)
    {
      meta_warning ("Failed to read from restart helper: %s\n", error->message);
      g_object_unref (data_stream);
      goto error;
    }

  return;

 error:
  /* If starting the restart helper fails, then we just go ahead and restart
   * immediately. We won't get a smooth transition, since the overlay window
   * will be destroyed and recreated, but otherwise it will work fine.
   */
  restart_helper_started = TRUE;
  restart_check_ready ();

  return;
}