/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Muffin main() */

/* 
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2006 Elijah Newren
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
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 */

/**
 * SECTION:main
 * @short_description: Program startup.
 * 
 * Functions which parse the command-line arguments, create the display,
 * kick everything off and then close down Muffin when it's time to go.
 *
 * 
 * 
 * Muffin - a boring window manager for the adult in you
 *
 * Many window managers are like Marshmallow Froot Loops; Muffin
 * is like Cheerios.
 *
 * The best way to get a handle on how the whole system fits together
 * is discussed in doc/code-overview.txt; if you're looking for functions
 * to investigate, read main(), meta_display_open(), and event_callback().
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define _SVID_SOURCE /* for putenv() and some signal-related functions */

#include <config.h>
#include <meta/main.h>
#include <meta/util.h>
#include "display-private.h"
#include <meta/errors.h>
#include "ui.h"
#include "session.h"
#include <meta/prefs.h>
#include <meta/compositor.h>

#include <glib-object.h>
#include <gdk/gdkx.h>

#include <stdlib.h>
#include <sys/types.h>
#include <wait.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <time.h>
#include <unistd.h>

#include <clutter/clutter.h>
#include <clutter/x11/clutter-x11.h>
#include <GL/gl.h>

#ifdef HAVE_INTROSPECTION
#include <girepository.h>
#endif

/*
 * The exit code we'll return to our parent process when we eventually die.
 */
static MetaExitCode meta_exit_code = META_EXIT_SUCCESS;

/*
 * Handle on the main loop, so that we have an easy way of shutting Muffin
 * down.
 */
static GMainLoop *meta_main_loop = NULL;

static void prefs_changed_callback (MetaPreference pref,
                                    gpointer       data);

/*
 * Prints log messages. If Muffin was compiled with backtrace support,
 * also prints a backtrace (see meta_print_backtrace()).
 *
 * \param log_domain  the domain the error occurred in (we ignore this)
 * \param log_level   the log level so that we can filter out less
 *                    important messages
 * \param message     the message to log
 * \param user_data   arbitrary data (we ignore this)
 */
static void
log_handler (const gchar   *log_domain,
             GLogLevelFlags log_level,
             const gchar   *message,
             gpointer       user_data)
{
  meta_warning ("Log level %d: %s\n", log_level, message);
  meta_print_backtrace ();
}

/*
 * Prints a list of which configure script options were used to
 * build this copy of Muffin. This is actually always called
 * on startup, but it's all no-op unless we're in verbose mode
 * (see meta_set_verbose).
 */
static void
meta_print_compilation_info (void)
{
#ifdef HAVE_SHAPE
  meta_verbose ("Compiled with shape extension\n");
#else
  meta_verbose ("Compiled without shape extension\n");
#endif
#ifdef HAVE_XINERAMA
  meta_topic (META_DEBUG_XINERAMA, "Compiled with Xinerama extension\n");
#else
  meta_topic (META_DEBUG_XINERAMA, "Compiled without Xinerama extension\n");
#endif
#ifdef HAVE_XFREE_XINERAMA
  meta_topic (META_DEBUG_XINERAMA, " (using XFree86 Xinerama)\n");
#else
  meta_topic (META_DEBUG_XINERAMA, " (not using XFree86 Xinerama)\n");
#endif
#ifdef HAVE_SOLARIS_XINERAMA
  meta_topic (META_DEBUG_XINERAMA, " (using Solaris Xinerama)\n");
#else
  meta_topic (META_DEBUG_XINERAMA, " (not using Solaris Xinerama)\n");
#endif
#ifdef HAVE_XSYNC
  meta_verbose ("Compiled with sync extension\n");
#else
  meta_verbose ("Compiled without sync extension\n");
#endif
#ifdef HAVE_RANDR
  meta_verbose ("Compiled with randr extension\n");
#else
  meta_verbose ("Compiled without randr extension\n");
#endif
#ifdef HAVE_STARTUP_NOTIFICATION
  meta_verbose ("Compiled with startup notification\n");
#else
  meta_verbose ("Compiled without startup notification\n");
#endif
}

/*
 * Prints the version number, the current timestamp (not the
 * build date), the locale, the character encoding, and a list
 * of configure script options that were used to build this
 * copy of Muffin. This is actually always called
 * on startup, but it's all no-op unless we're in verbose mode
 * (see meta_set_verbose).
 */
static void
meta_print_self_identity (void)
{
  char buf[256];
  GDate d;
  const char *charset;

  /* Version and current date. */
  g_date_clear (&d, 1);
  g_date_set_time_t (&d, time (NULL));
  g_date_strftime (buf, sizeof (buf), "%x", &d);
  meta_verbose ("Muffin version %s running on %s\n",
    VERSION, buf);
  
  /* Locale and encoding. */
  g_get_charset (&charset);
  meta_verbose ("Running in locale \"%s\" with encoding \"%s\"\n",
    setlocale (LC_ALL, NULL), charset);

  /* Compilation settings. */
  meta_print_compilation_info ();
}

/*
 * The set of possible options that can be set on Muffin's
 * command line.
 */
static gchar    *opt_save_file;
static gchar    *opt_display_name;
static gchar    *opt_client_id;
static gboolean  opt_replace_wm;
static gboolean  opt_disable_sm;
static gboolean  opt_sync;

static GOptionEntry meta_options[] = {
  {
    "sm-disable", 0, 0, G_OPTION_ARG_NONE,
    &opt_disable_sm,
    N_("Disable connection to session manager"),
    NULL
  },
  {
    "replace", 0, 0, G_OPTION_ARG_NONE,
    &opt_replace_wm,
    N_("Replace the running window manager"),
    NULL
  },
  {
    "sm-client-id", 0, 0, G_OPTION_ARG_STRING,
    &opt_client_id,
    N_("Specify session management ID"),
    "ID"
  },
  {
    "display", 'd', 0, G_OPTION_ARG_STRING,
    &opt_display_name, N_("X Display to use"),
    "DISPLAY"
  },
  {
    "sm-save-file", 0, 0, G_OPTION_ARG_FILENAME,
    &opt_save_file,
    N_("Initialize session from savefile"),
    "FILE"
  },
  {
    "sync", 0, 0, G_OPTION_ARG_NONE,
    &opt_sync,
    N_("Make X calls synchronous"),
    NULL
  },
  {NULL}
};

/**
 * meta_get_option_context: (skip)
 *
 * Returns a #GOptionContext initialized with muffin-related options.
 * Parse the command-line args with this before calling meta_init().
 *
 * Return value: the #GOptionContext
 */
GOptionContext *
meta_get_option_context (void)
{
  GOptionContext *ctx;

  if (setlocale (LC_ALL, "") == NULL)
    meta_warning ("Locale not understood by C library, internationalization will not work\n");
  bindtextdomain (GETTEXT_PACKAGE, MUFFIN_LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

  ctx = g_option_context_new (NULL);
  g_option_context_add_main_entries (ctx, meta_options, GETTEXT_PACKAGE);
  g_option_context_add_group (ctx, clutter_get_option_group_without_init ());
  g_option_context_add_group (ctx, cogl_get_option_group ());

  return ctx;
}

/* Muffin is responsible for pulling events off the X queue, so Clutter
 * doesn't need (and shouldn't) run its normal event source which polls
 * the X fd, but we do have to deal with dispatching events that accumulate
 * in the clutter queue. This happens, for example, when clutter generate
 * enter/leave events on mouse motion - several events are queued in the
 * clutter queue but only one dispatched. It could also happen because of
 * explicit calls to clutter_event_put(). We add a very simple custom
 * event loop source which is simply responsible for pulling events off
 * of the queue and dispatching them before we block for new events.
 */

static gboolean 
event_prepare (GSource    *source,
               gint       *timeout_)
{
  *timeout_ = -1;

  return clutter_events_pending ();
}

static gboolean 
event_check (GSource *source)
{
  return clutter_events_pending ();
}

static gboolean
event_dispatch (GSource    *source,
                GSourceFunc callback,
                gpointer    user_data)
{
  ClutterEvent *event = clutter_event_get ();
  if (event)
    clutter_do_event (event);

  return TRUE;
}

static GSourceFuncs event_funcs = {
  event_prepare,
  event_check,
  event_dispatch
};

static void
meta_clutter_init (void)
{
  clutter_x11_set_display (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()));
  clutter_x11_disable_event_retrieval ();
  
  if (CLUTTER_INIT_SUCCESS == clutter_init (NULL, NULL))
    {
      GSource *source = g_source_new (&event_funcs, sizeof (GSource));
      g_source_attach (source, NULL);
      g_source_unref (source);
    }
  else
    {
	  meta_fatal ("Unable to initialize Clutter.\n");
    }
}

/*
 * Selects which display Muffin should use. It first tries to use
 * display_name as the display. If display_name is NULL then
 * try to use the environment variable MUFFIN_DISPLAY. If that
 * also is NULL, use the default - :0.0
 */
static void
meta_select_display (gchar *display_name)
{
  gchar *envVar = "";
  if (display_name)
    envVar = g_strconcat ("DISPLAY=", display_name, NULL);
  else if (g_getenv ("MUFFIN_DISPLAY"))
    envVar = g_strconcat ("DISPLAY=",
      g_getenv ("MUFFIN_DISPLAY"), NULL);
  /* DO NOT FREE envVar, putenv() sucks */
  putenv (envVar);
}

static void
meta_finalize (void)
{
  MetaDisplay *display = meta_get_display ();

  if (display)
    meta_display_close (display,
                        CurrentTime); /* I doubt correct timestamps matter here */
}

static int sigterm_pipe_fds[2] = { -1, -1 };

static void
sigterm_handler (int signum)
{
  if (sigterm_pipe_fds[1] >= 0)
    {
      int G_GNUC_UNUSED dummy;

      dummy = write (sigterm_pipe_fds[1], "", 1);
      close (sigterm_pipe_fds[1]);
      sigterm_pipe_fds[1] = -1;
    }
}

static gboolean
on_sigterm (void)
{
  meta_quit (META_EXIT_SUCCESS);
  return FALSE;
}

/**
 * meta_init: (skip)
 *
 * Initialize muffin. Call this after meta_get_option_context() and
 * meta_plugin_type_register(), and before meta_run().
 */
void
meta_init (void)
{
  struct sigaction act;
  sigset_t empty_mask;
  GIOChannel *channel;

  g_type_init ();
  
  sigemptyset (&empty_mask);
  act.sa_handler = SIG_IGN;
  act.sa_mask    = empty_mask;
  act.sa_flags   = 0;
  if (sigaction (SIGPIPE,  &act, NULL) < 0)
    g_printerr ("Failed to register SIGPIPE handler: %s\n",
                g_strerror (errno));
#ifdef SIGXFSZ
  if (sigaction (SIGXFSZ,  &act, NULL) < 0)
    g_printerr ("Failed to register SIGXFSZ handler: %s\n",
                g_strerror (errno));
#endif

  if (pipe (sigterm_pipe_fds) != 0)
    g_printerr ("Failed to create SIGTERM pipe: %s\n",
                g_strerror (errno));

  channel = g_io_channel_unix_new (sigterm_pipe_fds[0]);
  g_io_channel_set_flags (channel, G_IO_FLAG_NONBLOCK, NULL);
  g_io_add_watch (channel, G_IO_IN, (GIOFunc) on_sigterm, NULL);
  g_io_channel_set_close_on_unref (channel, TRUE);
  g_io_channel_unref (channel);

  act.sa_handler = &sigterm_handler;
  if (sigaction (SIGTERM, &act, NULL) < 0)
    g_printerr ("Failed to register SIGTERM handler: %s\n",
		g_strerror (errno));

  if (g_getenv ("MUFFIN_VERBOSE"))
    meta_set_verbose (TRUE);
  if (g_getenv ("MUFFIN_DEBUG"))
    meta_set_debugging (TRUE);

  if (g_get_home_dir ())
    if (chdir (g_get_home_dir ()) < 0)
      meta_warning ("Could not change to home directory %s.\n",
                    g_get_home_dir ());

  meta_print_self_identity ();
  
#ifdef HAVE_INTROSPECTION
  g_irepository_prepend_search_path (MUFFIN_PKGLIBDIR);
#endif

  meta_set_syncing (opt_sync || (g_getenv ("MUFFIN_SYNC") != NULL));

  meta_select_display (opt_display_name);
  
  if (opt_replace_wm)
    meta_set_replace_current_wm (TRUE);

  if (opt_save_file && opt_client_id)
    meta_fatal ("Can't specify both SM save file and SM client id\n");
  
  meta_main_loop = g_main_loop_new (NULL, FALSE);
  
  meta_ui_init ();

  /*
   * Clutter can only be initialized after the UI.
   */
  meta_clutter_init ();
  
  const char *renderer = (const char *) glGetString (GL_RENDERER);
  if (strstr (renderer, "llvmpipe") ||
      strstr (renderer, "Rasterizer") ||
      strstr (renderer, "softpipe"))
  {
	/* Clutter envs not set, since they won't work after Clutter init */
    g_setenv ("CINNAMON_SOFTWARE_RENDERING", "1", FALSE);
    g_setenv ("CINNAMON_SLOWDOWN_FACTOR", "0.0001", FALSE);
    g_setenv ("MUFFIN_NO_SHADOWS", "1", FALSE);
    meta_warning ("Software rendering detected: %s\n", renderer);
  }
}

/**
 * meta_run: (skip)
 *
 * Runs muffin. Call this after completing your own initialization.
 *
 * Return value: muffin's exit status
 */
int
meta_run (void)
{
  const gchar *log_domains[] = {
    NULL, G_LOG_DOMAIN, "Gtk", "Gdk", "GLib",
    "Pango", "GLib-GObject", "GThread"
  };
  guint i;

  /* Load prefs */
  meta_prefs_init ();
  meta_prefs_add_listener (prefs_changed_callback, NULL);

  for (i=0; i<G_N_ELEMENTS(log_domains); i++)
    g_log_set_handler (log_domains[i],
                       G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION,
                       log_handler, NULL);

  if (g_getenv ("MUFFIN_G_FATAL_WARNINGS") != NULL)
    g_log_set_always_fatal (G_LOG_LEVEL_MASK);
  
  meta_ui_set_current_theme (meta_prefs_get_theme (), FALSE);

  /* Try to find some theme that'll work if the theme preference
   * doesn't exist.  First try Simple (the default theme) then just
   * try anything in the themes directory.
   */
  if (!meta_ui_have_a_theme ())
    meta_ui_set_current_theme ("Simple", FALSE);
  
  if (!meta_ui_have_a_theme ())
    {
      const char *dir_entry = NULL;
      GError *err = NULL;
      GDir   *themes_dir = NULL;
      
      if (!(themes_dir = g_dir_open (MUFFIN_DATADIR"/themes", 0, &err)))
        {
          meta_fatal (_("Failed to scan themes directory: %s\n"), err->message);
          g_error_free (err);
        } 
      else 
        {
          while (((dir_entry = g_dir_read_name (themes_dir)) != NULL) && 
                 (!meta_ui_have_a_theme ()))
            {
              meta_ui_set_current_theme (dir_entry, FALSE);
            }
          
          g_dir_close (themes_dir);
        }
    }
  
  if (!meta_ui_have_a_theme ())
    meta_fatal (_("Could not find a theme! Be sure %s exists and contains the usual themes.\n"),
                MUFFIN_DATADIR"/themes");
  
  /* Connect to SM as late as possible - but before managing display,
   * or we might try to manage a window before we have the session
   * info
   */
  if (!opt_disable_sm)
    {
      if (opt_client_id == NULL)
        {
          const gchar *desktop_autostart_id;
  
          desktop_autostart_id = g_getenv ("DESKTOP_AUTOSTART_ID");
 
          if (desktop_autostart_id != NULL)
            opt_client_id = g_strdup (desktop_autostart_id);
        }

      /* Unset DESKTOP_AUTOSTART_ID in order to avoid child processes to
       * use the same client id. */
      g_unsetenv ("DESKTOP_AUTOSTART_ID");

      meta_session_init (opt_client_id, opt_save_file);
    }
  /* Free memory possibly allocated by the argument parsing which are
   * no longer needed.
   */
  g_free (opt_save_file);
  g_free (opt_display_name);
  g_free (opt_client_id);

  if (!meta_display_open ())
    meta_exit (META_EXIT_ERROR);
  
  g_main_loop_run (meta_main_loop);

  meta_finalize ();

  return meta_exit_code;
}

/*
 * Stops Muffin. This tells the event loop to stop processing; it is
 * rather dangerous to use this because this will leave the user with
 * no window manager. We generally do this only if, for example, the
 * session manager asks us to; we assume the session manager knows
 * what it's talking about.
 *
 * \param code The success or failure code to return to the calling process.
 */
void
meta_quit (MetaExitCode code)
{
  if (g_main_loop_is_running (meta_main_loop))
    {
      meta_exit_code = code;
      g_main_loop_quit (meta_main_loop);
    }
}

/*
 * Called on pref changes. (One of several functions of its kind and purpose.)
 *
 * \bug Why are these particular prefs handled in main.c and not others?
 * Should they be?
 *
 * \param pref  Which preference has changed
 * \param data  Arbitrary data (which we ignore)
 */
static void
prefs_changed_callback (MetaPreference pref,
                        gpointer       data)
{
  switch (pref)
    {
    case META_PREF_THEME:
    case META_PREF_DRAGGABLE_BORDER_WIDTH:
      meta_ui_set_current_theme (meta_prefs_get_theme (), FALSE);
      meta_display_retheme_all ();
      break;

    case META_PREF_CURSOR_THEME:
    case META_PREF_CURSOR_SIZE:
      meta_display_set_cursor_theme (meta_prefs_get_cursor_theme (),
				     meta_prefs_get_cursor_size ());
      break;
    default:
      /* handled elsewhere or otherwise */
      break;
    }
}
