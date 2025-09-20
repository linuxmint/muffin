/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2005 Elijah Newren
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

/**
 * SECTION:util
 * @title: Utility functions
 * @short_description: Miscellaneous utility functions
 */

#define _POSIX_C_SOURCE 200112L /* for fdopen() */

#include "config.h"

#include "core/util-private.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <X11/Xlib.h>   /* must explicitly be included for Solaris; #326746 */
#include <X11/Xutil.h>  /* Just for the definition of the various gravities */

#include "clutter/clutter.h"
#include "cogl/cogl.h"
#include "meta/common.h"
#include "meta/main.h"

#ifdef WITH_VERBOSE_MODE
static void
meta_topic_real_valist (MetaDebugTopic topic,
                        const char    *format,
                        va_list        args) G_GNUC_PRINTF(2, 0);
#endif

static gint verbose_topics = 0;
static gboolean is_debugging = FALSE;
static gboolean replace_current = FALSE;
static int no_prefix = 0;
static gboolean is_wayland_compositor = FALSE;

#ifdef WITH_VERBOSE_MODE
static FILE* logfile = NULL;

static void
ensure_logfile (void)
{
  if (logfile == NULL && g_getenv ("MUFFIN_USE_LOGFILE"))
    {
      char *filename = NULL;
      char *tmpl;
      int fd;
      GError *err;

      tmpl = g_strdup_printf ("muffin-%d-debug-log-XXXXXX",
                              (int) getpid ());

      err = NULL;
      fd = g_file_open_tmp (tmpl,
                            &filename,
                            &err);

      g_free (tmpl);

      if (err != NULL)
        {
          meta_warning ("Failed to open debug log: %s\n",
                        err->message);
          g_error_free (err);
          return;
        }

      logfile = fdopen (fd, "w");

      if (logfile == NULL)
        {
          meta_warning ("Failed to fdopen() log file %s: %s\n",
                        filename, strerror (errno));
          close (fd);
        }
      else
        {
          g_printerr ("Opened log file %s\n", filename);
        }

      g_free (filename);
    }
}
#endif

gboolean
meta_is_verbose (void)
{
  return verbose_topics != 0;
}

void
meta_set_verbose (gboolean setting)
{
#ifndef WITH_VERBOSE_MODE
  if (setting)
    meta_fatal (_("Mutter was compiled without support for verbose mode\n"));
#else
  if (setting)
    ensure_logfile ();
#endif

  if (setting)
    meta_add_verbose_topic (META_DEBUG_VERBOSE);
  else
    meta_remove_verbose_topic (META_DEBUG_VERBOSE);
}

/**
 * meta_add_verbose_topic:
 * @topic: Topic for which logging will be started
 *
 * Ensure log messages for the given topic @topic
 * will be printed.
 */
void
meta_add_verbose_topic (MetaDebugTopic topic)
{
  if (verbose_topics == META_DEBUG_VERBOSE)
    return;
  if (topic == META_DEBUG_VERBOSE)
    verbose_topics = META_DEBUG_VERBOSE;
  else
    verbose_topics |= topic;
}

/**
 * meta_remove_verbose_topic:
 * @topic: Topic for which logging will be stopped
 *
 * Stop printing log messages for the given topic @topic.  Note
 * that this method does not stack with meta_add_verbose_topic();
 * i.e. if two calls to meta_add_verbose_topic() for the same
 * topic are made, one call to meta_remove_verbose_topic() will
 * remove it.
 */
void
meta_remove_verbose_topic (MetaDebugTopic topic)
{
  if (topic == META_DEBUG_VERBOSE)
    verbose_topics = 0;
  else
    verbose_topics &= ~topic;
}

gboolean
meta_is_debugging (void)
{
  return is_debugging;
}

void
meta_set_debugging (gboolean setting)
{
#ifdef WITH_VERBOSE_MODE
  if (setting)
    ensure_logfile ();
#endif

  is_debugging = setting;
}

gboolean
meta_get_replace_current_wm (void)
{
  return replace_current;
}

void
meta_set_replace_current_wm (gboolean setting)
{
  replace_current = setting;
}

gboolean
meta_is_wayland_compositor (void)
{
  return is_wayland_compositor;
}

void
meta_set_is_wayland_compositor (gboolean value)
{
  is_wayland_compositor = value;
}

char *
meta_g_utf8_strndup (const gchar *src,
                     gsize        n)
{
  const gchar *s = src;
  while (n && *s)
    {
      s = g_utf8_next_char (s);
      n--;
    }

  return g_strndup (src, s - src);
}

static int
utf8_fputs (const char *str,
            FILE       *f)
{
  char *l;
  int retval;

  l = g_locale_from_utf8 (str, -1, NULL, NULL, NULL);

  if (l == NULL)
    retval = fputs (str, f); /* just print it anyway, better than nothing */
  else
    retval = fputs (l, f);

  g_free (l);

  return retval;
}

#ifdef WITH_VERBOSE_MODE
void
meta_debug_spew_real (const char *format, ...)
{
  va_list args;
  gchar *str;
  FILE *out;

  g_return_if_fail (format != NULL);

  if (!is_debugging)
    return;

  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);

  out = logfile ? logfile : stderr;

  if (no_prefix == 0)
    utf8_fputs ("Window manager: ", out);
  utf8_fputs (str, out);

  fflush (out);

  g_free (str);
}
#endif /* WITH_VERBOSE_MODE */

#ifdef WITH_VERBOSE_MODE
void
meta_verbose_real (const char *format, ...)
{
  va_list args;

  va_start (args, format);
  meta_topic_real_valist (META_DEBUG_VERBOSE, format, args);
  va_end (args);
}
#endif /* WITH_VERBOSE_MODE */

#ifdef WITH_VERBOSE_MODE
static const char*
topic_name (MetaDebugTopic topic)
{
  switch (topic)
    {
    case META_DEBUG_FOCUS:
      return "FOCUS";
    case META_DEBUG_WORKAREA:
      return "WORKAREA";
    case META_DEBUG_STACK:
      return "STACK";
    case META_DEBUG_THEMES:
      return "THEMES";
    case META_DEBUG_SM:
      return "SM";
    case META_DEBUG_EVENTS:
      return "EVENTS";
    case META_DEBUG_WINDOW_STATE:
      return "WINDOW_STATE";
    case META_DEBUG_WINDOW_OPS:
      return "WINDOW_OPS";
    case META_DEBUG_PLACEMENT:
      return "PLACEMENT";
    case META_DEBUG_GEOMETRY:
      return "GEOMETRY";
    case META_DEBUG_PING:
      return "PING";
    case META_DEBUG_XINERAMA:
      return "XINERAMA";
    case META_DEBUG_KEYBINDINGS:
      return "KEYBINDINGS";
    case META_DEBUG_SYNC:
      return "SYNC";
    case META_DEBUG_ERRORS:
      return "ERRORS";
    case META_DEBUG_STARTUP:
      return "STARTUP";
    case META_DEBUG_PREFS:
      return "PREFS";
    case META_DEBUG_GROUPS:
      return "GROUPS";
    case META_DEBUG_RESIZING:
      return "RESIZING";
    case META_DEBUG_SHAPES:
      return "SHAPES";
    case META_DEBUG_COMPOSITOR:
      return "COMPOSITOR";
    case META_DEBUG_EDGE_RESISTANCE:
      return "EDGE_RESISTANCE";
    case META_DEBUG_DBUS:
      return "DBUS";
    case META_DEBUG_INPUT:
      return "INPUT";
    case META_DEBUG_VERBOSE:
      return "VERBOSE";
    }

  return "WM";
}

static int sync_count = 0;

static void
meta_topic_real_valist (MetaDebugTopic topic,
                        const char    *format,
                        va_list        args)
{
  gchar *str;
  FILE *out;

  g_return_if_fail (format != NULL);

  if (verbose_topics == 0
      || (topic == META_DEBUG_VERBOSE && verbose_topics != META_DEBUG_VERBOSE)
      || (!(verbose_topics & topic)))
    return;

  str = g_strdup_vprintf (format, args);

  out = logfile ? logfile : stderr;

  if (no_prefix == 0)
    fprintf (out, "%s: ", topic_name (topic));

  if (topic == META_DEBUG_SYNC)
    {
      ++sync_count;
      fprintf (out, "%d: ", sync_count);
    }

  utf8_fputs (str, out);

  fflush (out);

  g_free (str);
}

void
meta_topic_real (MetaDebugTopic topic,
                 const char *format,
                 ...)
{
  va_list args;

  va_start (args, format);
  meta_topic_real_valist (topic, format, args);
  va_end (args);
}
#endif /* WITH_VERBOSE_MODE */

void
meta_bug (const char *format, ...)
{
  va_list args;
  gchar *str;
  FILE *out;

  g_return_if_fail (format != NULL);

  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);

#ifdef WITH_VERBOSE_MODE
  out = logfile ? logfile : stderr;
#else
  out = stderr;
#endif

  if (no_prefix == 0)
    utf8_fputs ("Bug in window manager: ", out);
  utf8_fputs (str, out);

  fflush (out);

  g_free (str);

  /* stop us in a debugger */
  abort ();
}

void
meta_warning (const char *format, ...)
{
  va_list args;
  gchar *str;
  FILE *out;

  g_return_if_fail (format != NULL);

  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);

#ifdef WITH_VERBOSE_MODE
  out = logfile ? logfile : stderr;
#else
  out = stderr;
#endif

  if (no_prefix == 0)
    utf8_fputs ("Window manager warning: ", out);
  utf8_fputs (str, out);

  fflush (out);

  g_free (str);
}

void
meta_fatal (const char *format, ...)
{
  va_list args;
  gchar *str;
  FILE *out;

  g_warn_if_fail (format);
  if (!format)
    meta_exit (META_EXIT_ERROR);

  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);

#ifdef WITH_VERBOSE_MODE
  out = logfile ? logfile : stderr;
#else
  out = stderr;
#endif

  if (no_prefix == 0)
    utf8_fputs ("Window manager error: ", out);
  utf8_fputs (str, out);

  fflush (out);

  g_free (str);

  meta_exit (META_EXIT_ERROR);
}

void
meta_push_no_msg_prefix (void)
{
  ++no_prefix;
}

void
meta_pop_no_msg_prefix (void)
{
  g_return_if_fail (no_prefix > 0);

  --no_prefix;
}

void
meta_exit (MetaExitCode code)
{

  exit (code);
}

gint
meta_unsigned_long_equal (gconstpointer v1,
                          gconstpointer v2)
{
  return *((const gulong*) v1) == *((const gulong*) v2);
}

guint
meta_unsigned_long_hash  (gconstpointer v)
{
  gulong val = * (const gulong *) v;

  /* I'm not sure this works so well. */
#if GLIB_SIZEOF_LONG > 4
  return (guint) (val ^ (val >> 32));
#else
  return val;
#endif
}

const char*
meta_gravity_to_string (MetaGravity gravity)
{
  switch (gravity)
    {
    case META_GRAVITY_NORTH_WEST:
      return "META_GRAVITY_NORTH_WEST";
      break;
    case META_GRAVITY_NORTH:
      return "META_GRAVITY_NORTH";
      break;
    case META_GRAVITY_NORTH_EAST:
      return "META_GRAVITY_NORTH_EAST";
      break;
    case META_GRAVITY_WEST:
      return "META_GRAVITY_WEST";
      break;
    case META_GRAVITY_CENTER:
      return "META_GRAVITY_CENTER";
      break;
    case META_GRAVITY_EAST:
      return "META_GRAVITY_EAST";
      break;
    case META_GRAVITY_SOUTH_WEST:
      return "META_GRAVITY_SOUTH_WEST";
      break;
    case META_GRAVITY_SOUTH:
      return "META_GRAVITY_SOUTH";
      break;
    case META_GRAVITY_SOUTH_EAST:
      return "META_GRAVITY_SOUTH_EAST";
      break;
    case META_GRAVITY_STATIC:
      return "META_GRAVITY_STATIC";
      break;
    default:
      return "META_GRAVITY_NORTH_WEST";
      break;
    }
}

char*
meta_external_binding_name_for_action (guint keybinding_action)
{
  return g_strdup_printf ("external-grab-%u", keybinding_action);
}

/* Command line arguments are passed in the locale encoding; in almost
 * all cases, we'd hope that is UTF-8 and no conversion is necessary.
 * If it's not UTF-8, then it's possible that the message isn't
 * representable in the locale encoding.
 */
static void
append_argument (GPtrArray  *args,
                 const char *arg)
{
  char *locale_arg = g_locale_from_utf8 (arg, -1, NULL, NULL, NULL);

  /* This is cheesy, but it's better to have a few ???'s in the dialog
   * for an unresponsive application than no dialog at all appear */
  if (!locale_arg)
    locale_arg = g_strdup ("???");

  g_ptr_array_add (args, locale_arg);
}

/**
 * meta_show_dialog: (skip)
 * @message: message
 * @timeout: timeout
 * @display: display
 * @ok_text: text for Ok button
 * @cancel_text: text for Cancel button
 * @transient_for: window XID of parent
 * @columns: columns
 * @entries: entries
 *
 */
GPid
meta_show_dialog (const char *type,
                  const char *message,
                  const char *timeout,
                  const char *display,
                  const char *ok_text,
                  const char *cancel_text,
                  const int transient_for,
                  GSList *columns,
                  GSList *entries)
{
  GError *error = NULL;
  GSList *tmp;
  GPid child_pid;
  GPtrArray *args;

  args = g_ptr_array_new ();

  append_argument (args, "zenity");
  append_argument (args, type);

  if (display)
    {
      append_argument (args, "--display");
      append_argument (args, display);
    }

  append_argument (args, "--title");
  append_argument (args, "");
  append_argument (args, "--text");
  append_argument (args, message);

  if (timeout)
    {
      append_argument (args, "--timeout");
      append_argument (args, timeout);
    }

  if (ok_text)
    {
      append_argument (args, "--ok-label");
      append_argument (args, ok_text);
     }

  if (cancel_text)
    {
      append_argument (args, "--cancel-label");
      append_argument (args, cancel_text);
    }

  tmp = columns;
  while (tmp)
    {
      append_argument (args, "--column");
      append_argument (args, tmp->data);
      tmp = tmp->next;
    }

  tmp = entries;
  while (tmp)
    {
      append_argument (args, tmp->data);
      tmp = tmp->next;
    }

  if (transient_for)
    {
      gchar *env = g_strdup_printf("%d", transient_for);
      setenv ("WINDOWID", env, 1);
      g_free (env);

      append_argument (args, "--modal");
    }

  g_ptr_array_add (args, NULL); /* NULL-terminate */

  g_spawn_async (
                 "/",
                 (gchar**) args->pdata,
                 NULL,
                 G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                 NULL, NULL,
                 &child_pid,
                 &error
                 );

  if (transient_for)
    unsetenv ("WINDOWID");

  g_ptr_array_free (args, TRUE);

  if (error)
    {
      meta_warning ("%s\n", error->message);
      g_error_free (error);
    }

  return child_pid;
}

MetaLocaleDirection
meta_get_locale_direction (void)
{
  switch (gtk_get_locale_direction ())
    {
    case GTK_TEXT_DIR_LTR:
      return META_LOCALE_DIRECTION_LTR;
    case GTK_TEXT_DIR_RTL:
      return META_LOCALE_DIRECTION_RTL;
    default:
      g_assert_not_reached ();
      return 0;
    }
}

char *
meta_generate_random_id (GRand *rand,
                         int    length)
{
  char *id;
  int i;

  /* Generate a random string of printable ASCII characters. */

  id = g_new0 (char, length + 1);
  for (i = 0; i < length; i++)
    id[i] = (char) g_rand_int_range (rand, 32, 127);

  return id;
}


void
meta_add_clutter_debug_flags (ClutterDebugFlag     debug_flags,
                              ClutterDrawDebugFlag draw_flags,
                              ClutterPickDebugFlag pick_flags)
{
  clutter_add_debug_flags (debug_flags, draw_flags, pick_flags);
}

void
meta_remove_clutter_debug_flags (ClutterDebugFlag     debug_flags,
                                 ClutterDrawDebugFlag draw_flags,
                                 ClutterPickDebugFlag pick_flags)
{
  clutter_remove_debug_flags (debug_flags, draw_flags, pick_flags);
}

/* eof util.c */

