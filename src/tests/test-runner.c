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

#include "config.h"

#include <gio/gio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "compositor/meta-plugin-manager.h"
#include "core/window-private.h"
#include "meta/main.h"
#include "meta/util.h"
#include "meta/window.h"
#include "tests/test-utils.h"
#include "ui/ui.h"
#include "wayland/meta-wayland.h"
#include "x11/meta-x11-display-private.h"

typedef struct {
  GHashTable *clients;
  AsyncWaiter *waiter;
  GString *warning_messages;
  GMainLoop *loop;
} TestCase;

static gboolean
test_case_alarm_filter (MetaX11Display        *x11_display,
                        XSyncAlarmNotifyEvent *event,
                        gpointer               data)
{
  TestCase *test = data;
  GHashTableIter iter;
  gpointer key, value;

  if (async_waiter_alarm_filter (x11_display, event, test->waiter))
    return TRUE;

  g_hash_table_iter_init (&iter, test->clients);
  while (g_hash_table_iter_next (&iter, &key, &value))
    if (test_client_alarm_filter (x11_display, event, value))
      return TRUE;

  return FALSE;
}

static TestCase *
test_case_new (void)
{
  TestCase *test = g_new0 (TestCase, 1);

  test_wait_for_x11_display ();

  meta_x11_display_set_alarm_filter (meta_get_display ()->x11_display,
                                     test_case_alarm_filter, test);

  test->clients = g_hash_table_new (g_str_hash, g_str_equal);
  test->waiter = async_waiter_new ();
  test->loop = g_main_loop_new (NULL, FALSE);

  return test;
}

static gboolean
test_case_loop_quit (gpointer data)
{
  TestCase *test = data;

  g_main_loop_quit (test->loop);

  return FALSE;
}

static gboolean
test_case_dispatch (TestCase *test,
                    GError  **error)
{
  /* Wait until we've done any outstanding queued up work.
   * Though we add this as BEFORE_REDRAW, the iteration that runs the
   * BEFORE_REDRAW idles will proceed on and do the redraw, so we're
   * waiting until after *all* frame processing.
   */
  meta_later_add (META_LATER_BEFORE_REDRAW,
                  test_case_loop_quit,
                  test,
                  NULL);
  g_main_loop_run (test->loop);

  return TRUE;
}

static gboolean
test_case_wait (TestCase *test,
                GError  **error)
{
  GHashTableIter iter;
  gpointer key, value;

  /* First have each client set a XSync counter, and wait until
   * we receive the resulting event - so we know we've received
   * everything that the client have sent us.
   */
  g_hash_table_iter_init (&iter, test->clients);
  while (g_hash_table_iter_next (&iter, &key, &value))
    if (!test_client_wait (value, error))
      return FALSE;

  /* Then wait until we've done any outstanding queued up work. */
  test_case_dispatch (test, error);

  /* Then set an XSync counter ourselves and and wait until
   * we receive the resulting event - this makes sure that we've
   * received back any X events we generated.
   */
  async_waiter_set_and_wait (test->waiter);
  return TRUE;
}

static gboolean
test_case_sleep (TestCase  *test,
                 guint32    interval,
                 GError   **error)
{
  g_timeout_add_full (G_PRIORITY_LOW, interval, test_case_loop_quit, test, NULL);
  g_main_loop_run (test->loop);

  return TRUE;
}

#define BAD_COMMAND(...)                                                \
  G_STMT_START {                                                        \
      g_set_error (error,                                               \
                   TEST_RUNNER_ERROR, TEST_RUNNER_ERROR_BAD_COMMAND,    \
                   __VA_ARGS__);                                        \
      return FALSE;                                                     \
  } G_STMT_END

static TestClient *
test_case_lookup_client (TestCase *test,
                         char     *client_id,
                         GError  **error)
{
  TestClient *client = g_hash_table_lookup (test->clients, client_id);
  if (!client)
    g_set_error (error, TEST_RUNNER_ERROR, TEST_RUNNER_ERROR_BAD_COMMAND,
                 "No such client %s", client_id);

  return client;
}

static gboolean
test_case_parse_window_id (TestCase    *test,
                           const char  *client_and_window_id,
                           TestClient **client,
                           const char **window_id,
                           GError     **error)
{
  const char *slash = strchr (client_and_window_id, '/');
  char *tmp;
  if (slash == NULL)
    BAD_COMMAND ("client/window ID %s doesnt' contain a /", client_and_window_id);

  *window_id = slash + 1;

  tmp = g_strndup (client_and_window_id, slash - client_and_window_id);
  *client = test_case_lookup_client (test, tmp, error);
  g_free (tmp);

  return client != NULL;
}

static gboolean
test_case_assert_stacking (TestCase *test,
                           char    **expected_windows,
                           int       n_expected_windows,
                           GError  **error)
{
  MetaDisplay *display = meta_get_display ();
  guint64 *windows;
  int n_windows;
  GString *stack_string = g_string_new (NULL);
  GString *expected_string = g_string_new (NULL);
  int i;

  meta_stack_tracker_get_stack (display->stack_tracker, &windows, &n_windows);
  for (i = 0; i < n_windows; i++)
    {
      MetaWindow *window = meta_display_lookup_stack_id (display, windows[i]);
      if (window != NULL && window->title)
        {
          /* See comment in meta_ui_new() about why the dummy window for GTK+ theming
           * is managed as a MetaWindow.
           */
          if (META_STACK_ID_IS_X11 (windows[i]) &&
              meta_ui_window_is_dummy (display->x11_display->ui, windows[i]))
            continue;

          if (stack_string->len > 0)
            g_string_append_c (stack_string, ' ');

          if (g_str_has_prefix (window->title, "test/"))
            g_string_append (stack_string, window->title + 5);
          else
            g_string_append_printf (stack_string, "(%s)", window->title);
        }
      else if (windows[i] == display->x11_display->guard_window)
        {
          if (stack_string->len > 0)
            g_string_append_c (stack_string, ' ');

          g_string_append_c (stack_string, '|');
        }
    }

  for (i = 0; i < n_expected_windows; i++)
    {
      if (expected_string->len > 0)
        g_string_append_c (expected_string, ' ');

      g_string_append (expected_string, expected_windows[i]);
    }

  /* Don't require '| ' as a prefix if there are no hidden windows - we
   * remove the prefix from the actual string instead of adding it to the
   * expected string for clarity of the error message
   */
  if (index (expected_string->str, '|') == NULL && stack_string->str[0] == '|')
    {
      g_string_erase (stack_string,
                      0, stack_string->str[1] == ' ' ? 2 : 1);
    }

  if (strcmp (expected_string->str, stack_string->str) != 0)
    {
      g_set_error (error, TEST_RUNNER_ERROR, TEST_RUNNER_ERROR_ASSERTION_FAILED,
                   "stacking: expected='%s', actual='%s'",
                   expected_string->str, stack_string->str);
    }

  g_string_free (stack_string, TRUE);
  g_string_free (expected_string, TRUE);

  return *error == NULL;
}

static gboolean
test_case_assert_focused (TestCase    *test,
                          const char  *expected_window,
                          GError     **error)
{
  MetaDisplay *display = meta_get_display ();

  if (!display->focus_window)
    {
      if (g_strcmp0 (expected_window, "none") != 0)
        {
          g_set_error (error, TEST_RUNNER_ERROR, TEST_RUNNER_ERROR_ASSERTION_FAILED,
                       "focus: expected='%s', actual='none'", expected_window);
        }
    }
  else
    {
      const char *focused = display->focus_window->title;

      if (g_str_has_prefix (focused, "test/"))
        focused += 5;

      if (g_strcmp0 (focused, expected_window) != 0)
        g_set_error (error, TEST_RUNNER_ERROR, TEST_RUNNER_ERROR_ASSERTION_FAILED,
                     "focus: expected='%s', actual='%s'",
                     expected_window, focused);
    }

  return *error == NULL;
}

static gboolean
test_case_check_xserver_stacking (TestCase *test,
                                  GError  **error)
{
  MetaDisplay *display = meta_get_display ();
  GString *local_string = g_string_new (NULL);
  GString *x11_string = g_string_new (NULL);
  int i;

  guint64 *windows;
  int n_windows;
  meta_stack_tracker_get_stack (display->stack_tracker, &windows, &n_windows);

  for (i = 0; i < n_windows; i++)
    {
      if (META_STACK_ID_IS_X11 (windows[i]))
        {
          if (local_string->len > 0)
            g_string_append_c (local_string, ' ');

          g_string_append_printf (local_string, "%#lx", (Window)windows[i]);
        }
    }

  Window root;
  Window parent;
  Window *children;
  unsigned int n_children;
  XQueryTree (display->x11_display->xdisplay,
              display->x11_display->xroot,
              &root, &parent, &children, &n_children);

  for (i = 0; i < (int)n_children; i++)
    {
      if (x11_string->len > 0)
        g_string_append_c (x11_string, ' ');

      g_string_append_printf (x11_string, "%#lx", (Window)children[i]);
    }

  if (strcmp (x11_string->str, local_string->str) != 0)
    g_set_error (error, TEST_RUNNER_ERROR, TEST_RUNNER_ERROR_ASSERTION_FAILED,
                 "xserver stacking: x11='%s', local='%s'",
                 x11_string->str, local_string->str);

  XFree (children);

  g_string_free (local_string, TRUE);
  g_string_free (x11_string, TRUE);

  return *error == NULL;
}

static gboolean
test_case_do (TestCase *test,
              int       argc,
              char    **argv,
              GError  **error)
{
  if (strcmp (argv[0], "new_client") == 0)
    {
      MetaWindowClientType type;
      TestClient *client;

      if (argc != 3)
        BAD_COMMAND("usage: new_client <client-id> [wayland|x11]");

      if (strcmp (argv[2], "x11") == 0)
        type = META_WINDOW_CLIENT_TYPE_X11;
      else if (strcmp (argv[2], "wayland") == 0)
        type = META_WINDOW_CLIENT_TYPE_WAYLAND;
      else
        BAD_COMMAND("usage: new_client <client-id> [wayland|x11]");

      if (g_hash_table_lookup (test->clients, argv[1]))
        BAD_COMMAND("client %s already exists", argv[1]);

      client = test_client_new (argv[1], type, error);
      if (!client)
        return FALSE;

      g_hash_table_insert (test->clients, test_client_get_id (client), client);
    }
  else if (strcmp (argv[0], "quit_client") == 0)
    {
      if (argc != 2)
        BAD_COMMAND("usage: quit_client <client-id>");

      TestClient *client = test_case_lookup_client (test, argv[1], error);
      if (!client)
        return FALSE;

      if (!test_client_quit (client, error))
        return FALSE;

      g_hash_table_remove (test->clients, test_client_get_id (client));
      test_client_destroy (client);
    }
  else if (strcmp (argv[0], "create") == 0)
    {
      if (!(argc == 2 ||
            (argc == 3 && strcmp (argv[2], "override") == 0) ||
            (argc == 3 && strcmp (argv[2], "csd") == 0)))
        BAD_COMMAND("usage: %s <client-id>/<window-id > [override|csd]", argv[0]);

      TestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      if (!test_client_do (client, error,
                           "create", window_id,
                           argc == 3 ? argv[2] : NULL,
                           NULL))
        return FALSE;

      if (!test_client_wait (client, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "set_parent") == 0 ||
           strcmp (argv[0], "set_parent_exported") == 0)
    {
      if (argc != 3)
        BAD_COMMAND("usage: %s <client-id>/<window-id> <parent-window-id>",
                    argv[0]);

      TestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      if (!test_client_do (client, error,
                           argv[0], window_id,
                           argv[2],
                           NULL))
        return FALSE;
    }
  else if (strcmp (argv[0], "accept_focus") == 0)
    {
      if (argc != 3 ||
          (g_ascii_strcasecmp (argv[2], "true") != 0 &&
           g_ascii_strcasecmp (argv[2], "false") != 0))
        BAD_COMMAND("usage: %s <client-id>/<window-id> [true|false]",
                    argv[0]);

      TestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      if (!test_client_do (client, error,
                           argv[0], window_id,
                           argv[2],
                           NULL))
        return FALSE;
    }
  else if (strcmp (argv[0], "can_take_focus") == 0)
    {
      if (argc != 3 ||
          (g_ascii_strcasecmp (argv[2], "true") != 0 &&
           g_ascii_strcasecmp (argv[2], "false") != 0))
        BAD_COMMAND("usage: %s <client-id>/<window-id> [true|false]",
                    argv[0]);

      TestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      if (!test_client_do (client, error,
                           argv[0], window_id,
                           argv[2],
                           NULL))
        return FALSE;
    }
  else if (strcmp (argv[0], "accept_take_focus") == 0)
    {
      if (argc != 3 ||
          (g_ascii_strcasecmp (argv[2], "true") != 0 &&
           g_ascii_strcasecmp (argv[2], "false") != 0))
        BAD_COMMAND("usage: %s <client-id>/<window-id> [true|false]",
                    argv[0]);

      TestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      if (!test_client_do (client, error,
                           argv[0], window_id,
                           argv[2],
                           NULL))
        return FALSE;
    }
  else if (strcmp (argv[0], "show") == 0)
    {
      if (argc != 2)
        BAD_COMMAND("usage: %s <client-id>/<window-id>", argv[0]);

      TestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      if (!test_client_do (client, error, argv[0], window_id, NULL))
        return FALSE;

      MetaWindow *window = test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      test_client_wait_for_window_shown (client, window);
    }
  else if (strcmp (argv[0], "hide") == 0 ||
           strcmp (argv[0], "activate") == 0 ||
           strcmp (argv[0], "raise") == 0 ||
           strcmp (argv[0], "lower") == 0 ||
           strcmp (argv[0], "minimize") == 0 ||
           strcmp (argv[0], "unminimize") == 0 ||
           strcmp (argv[0], "destroy") == 0)
    {
      if (argc != 2)
        BAD_COMMAND("usage: %s <client-id>/<window-id>", argv[0]);

      TestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      if (!test_client_do (client, error, argv[0], window_id, NULL))
        return FALSE;
    }
  else if (strcmp (argv[0], "local_activate") == 0)
    {
      if (argc != 2)
        BAD_COMMAND("usage: %s <client-id>/<window-id>", argv[0]);

      TestClient *client;
      const char *window_id;
      if (!test_case_parse_window_id (test, argv[1], &client, &window_id, error))
        return FALSE;

      MetaWindow *window = test_client_find_window (client, window_id, error);
      if (!window)
        return FALSE;

      meta_window_activate (window, 0);
    }
  else if (strcmp (argv[0], "wait") == 0)
    {
      if (argc != 1)
        BAD_COMMAND("usage: %s", argv[0]);

      if (!test_case_wait (test, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "dispatch") == 0)
    {
      if (argc != 1)
        BAD_COMMAND("usage: %s", argv[0]);

      if (!test_case_dispatch (test, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "sleep") == 0)
    {
      guint64 interval;

      if (argc != 2)
        BAD_COMMAND("usage: %s <milliseconds>", argv[0]);

      if (!g_ascii_string_to_unsigned (argv[1], 10, 0, G_MAXUINT32,
                                       &interval, error))
        return FALSE;

      if (!test_case_sleep (test, (guint32) interval, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "assert_stacking") == 0)
    {
      if (!test_case_assert_stacking (test, argv + 1, argc - 1, error))
        return FALSE;

      if (!test_case_check_xserver_stacking (test, error))
        return FALSE;
    }
  else if (strcmp (argv[0], "assert_focused") == 0)
    {
      if (!test_case_assert_focused (test, argv[1], error))
        return FALSE;
    }
  else
    {
      BAD_COMMAND("Unknown command %s", argv[0]);
    }

  return TRUE;
}

static gboolean
test_case_destroy (TestCase *test,
                   GError  **error)
{
  /* Failures when cleaning up the test case aren't recoverable, since we'll
   * pollute the subsequent test cases, so we just return the error, and
   * skip the rest of the cleanup.
   */
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, test->clients);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      if (!test_client_do (value, error, "destroy_all", NULL))
        return FALSE;

    }

  if (!test_case_wait (test, error))
    return FALSE;

  if (!test_case_assert_stacking (test, NULL, 0, error))
    return FALSE;

  g_hash_table_iter_init (&iter, test->clients);
  while (g_hash_table_iter_next (&iter, &key, &value))
    test_client_destroy (value);

  async_waiter_destroy (test->waiter);

  meta_x11_display_set_alarm_filter (meta_get_display ()->x11_display,
                                     NULL, NULL);

  g_hash_table_destroy (test->clients);
  g_free (test);

  return TRUE;
}

/**********************************************************************/

static gboolean
run_test (const char *filename,
          int         index)
{
  TestCase *test = test_case_new ();
  GError *error = NULL;

  GFile *file = g_file_new_for_path (filename);

  GDataInputStream *in = NULL;

  GFileInputStream *in_raw = g_file_read (file, NULL, &error);
  g_object_unref (file);
  if (in_raw == NULL)
    goto out;

  in = g_data_input_stream_new (G_INPUT_STREAM (in_raw));
  g_object_unref (in_raw);

  int line_no = 0;
  while (error == NULL)
    {
      char *line = g_data_input_stream_read_line_utf8 (in, NULL, NULL, &error);
      if (line == NULL)
        break;

      line_no++;

      int argc;
      char **argv = NULL;
      if (!g_shell_parse_argv (line, &argc, &argv, &error))
        {
          if (g_error_matches (error, G_SHELL_ERROR, G_SHELL_ERROR_EMPTY_STRING))
            {
              g_clear_error (&error);
              goto next;
            }

          goto next;
        }

      test_case_do (test, argc, argv, &error);

    next:
      if (error)
        g_prefix_error (&error, "%d: ", line_no);

      g_free (line);
      g_strfreev (argv);
    }

  {
    GError *tmp_error = NULL;
    if (!g_input_stream_close (G_INPUT_STREAM (in), NULL, &tmp_error))
      {
        if (error != NULL)
          g_clear_error (&tmp_error);
        else
          g_propagate_error (&error, tmp_error);
      }
  }

 out:
  if (in != NULL)
    g_object_unref (in);

  GError *cleanup_error = NULL;
  test_case_destroy (test, &cleanup_error);

  const char *testspos = strstr (filename, "tests/");
  char *pretty_name;
  if (testspos)
    pretty_name = g_strdup (testspos + strlen("tests/"));
  else
    pretty_name = g_strdup (filename);

  if (error || cleanup_error)
    {
      g_print ("not ok %d %s\n", index, pretty_name);

      if (error)
        g_print ("   %s\n", error->message);

      if (cleanup_error)
        {
          g_print ("   Fatal Error During Cleanup\n");
          g_print ("   %s\n", cleanup_error->message);
          exit (1);
        }
    }
  else
    {
      g_print ("ok %d %s\n", index, pretty_name);
    }

  g_free (pretty_name);

  gboolean success = error == NULL;

  g_clear_error (&error);
  g_clear_error (&cleanup_error);

  return success;
}

typedef struct {
  int n_tests;
  char **tests;
} RunTestsInfo;

static gboolean
run_tests (gpointer data)
{
  RunTestsInfo *info = data;
  int i;
  gboolean success = TRUE;

  g_print ("1..%d\n", info->n_tests);

  for (i = 0; i < info->n_tests; i++)
    if (!run_test (info->tests[i], i + 1))
      success = FALSE;

  meta_quit (success ? 0 : 1);

  return FALSE;
}

/**********************************************************************/

static gboolean
find_metatests_in_directory (GFile     *directory,
                             GPtrArray *results,
                             GError   **error)
{
  GFileEnumerator *enumerator = g_file_enumerate_children (directory,
                                                           "standard::name,standard::type",
                                                           G_FILE_QUERY_INFO_NONE,
                                                           NULL, error);
  if (!enumerator)
    return FALSE;

  while (*error == NULL)
    {
      GFileInfo *info = g_file_enumerator_next_file (enumerator, NULL, error);
      if (info == NULL)
        break;

      GFile *child = g_file_enumerator_get_child (enumerator, info);
      switch (g_file_info_get_file_type (info))
        {
        case G_FILE_TYPE_REGULAR:
          {
            const char *name = g_file_info_get_name (info);
            if (g_str_has_suffix (name, ".metatest"))
              g_ptr_array_add (results, g_file_get_path (child));
            break;
          }
        case G_FILE_TYPE_DIRECTORY:
          find_metatests_in_directory (child, results, error);
          break;
        default:
          break;
        }

      g_object_unref (child);
      g_object_unref (info);
    }

  {
    GError *tmp_error = NULL;
    if (!g_file_enumerator_close (enumerator, NULL, &tmp_error))
      {
        if (*error != NULL)
          g_clear_error (&tmp_error);
        else
          g_propagate_error (error, tmp_error);
      }
  }

  g_object_unref (enumerator);
  return *error == NULL;
}

static gboolean all_tests = FALSE;

const GOptionEntry options[] = {
  {
    "all", 0, 0, G_OPTION_ARG_NONE,
    &all_tests,
    "Run all installed tests",
    NULL
  },
  { NULL }
};

int
main (int argc, char **argv)
{
  GOptionContext *ctx;
  GError *error = NULL;

  /* First parse the arguments that are passed to us */

  ctx = g_option_context_new (NULL);
  g_option_context_add_main_entries (ctx, options, NULL);

  if (!g_option_context_parse (ctx,
                               &argc, &argv, &error))
    {
      g_printerr ("%s", error->message);
      return 1;
    }

  g_option_context_free (ctx);

  test_init (&argc, &argv);

  GPtrArray *tests = g_ptr_array_new ();

  if (all_tests)
    {
      GFile *test_dir = g_file_new_for_path (MUTTER_PKGDATADIR "/tests");

      if (!find_metatests_in_directory (test_dir, tests, &error))
        {
          g_printerr ("Error enumerating tests: %s\n", error->message);
          return 1;
        }
    }
  else
    {
      int i;
      char *curdir = g_get_current_dir ();

      for (i = 1; i < argc; i++)
        {
          if (g_path_is_absolute (argv[i]))
            g_ptr_array_add (tests, g_strdup (argv[i]));
          else
            g_ptr_array_add (tests, g_build_filename (curdir, argv[i], NULL));
        }

      g_free (curdir);
    }

  /* Then initalize mutter with a different set of arguments */

  char *fake_args[] = { NULL, (char *)"--wayland", (char *)"--nested" };
  fake_args[0] = argv[0];
  char **fake_argv = fake_args;
  int fake_argc = G_N_ELEMENTS (fake_args);

  ctx = meta_get_option_context ();
  if (!g_option_context_parse (ctx, &fake_argc, &fake_argv, &error))
    {
      g_printerr ("mutter: %s\n", error->message);
      exit (1);
    }
  g_option_context_free (ctx);

  meta_plugin_manager_load (test_get_plugin_name ());

  meta_init ();
  meta_register_with_session ();

  RunTestsInfo info;
  info.tests = (char **)tests->pdata;
  info.n_tests = tests->len;

  g_idle_add (run_tests, &info);

  return meta_run ();
}
