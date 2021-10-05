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

#include <gio/gunixinputstream.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkwayland.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <X11/extensions/sync.h>

const char *client_id = "0";
static gboolean wayland;
GHashTable *windows;
GQuark event_source_quark;
GQuark event_handlers_quark;
GQuark can_take_focus_quark;

typedef void (*XEventHandler) (GtkWidget *window, XEvent *event);

static void read_next_line (GDataInputStream *in);

static void
window_export_handle_cb (GdkWindow  *window,
                         const char *handle_str,
                         gpointer    user_data)
{
  GdkWindow *gdk_window = gtk_widget_get_window (GTK_WIDGET (user_data));

  if (!gdk_wayland_window_set_transient_for_exported (gdk_window,
                                                      (gchar *) handle_str))
    g_print ("Fail to set transient_for exported window handle %s", handle_str);
  gdk_window_set_modal_hint (gdk_window, TRUE);
}

static GtkWidget *
lookup_window (const char *window_id)
{
  GtkWidget *window = g_hash_table_lookup (windows, window_id);
  if (!window)
    g_print ("Window %s doesn't exist", window_id);

  return window;
}

typedef struct {
  GSource base;
  GSource **self_ref;
  GPollFD event_poll_fd;
  Display *xdisplay;
} XClientEventSource;

static gboolean
x_event_source_prepare (GSource *source,
                        int     *timeout)
{
  XClientEventSource *x_source = (XClientEventSource *) source;

  *timeout = -1;

  return XPending (x_source->xdisplay);
}

static gboolean
x_event_source_check (GSource *source)
{
  XClientEventSource *x_source = (XClientEventSource *) source;

  return XPending (x_source->xdisplay);
}

static gboolean
x_event_source_dispatch (GSource     *source,
                         GSourceFunc  callback,
                         gpointer     user_data)
{
  XClientEventSource *x_source = (XClientEventSource *) source;

  while (XPending (x_source->xdisplay))
    {
      GHashTableIter iter;
      XEvent event;
      gpointer value;

      XNextEvent (x_source->xdisplay, &event);

      g_hash_table_iter_init (&iter, windows);
      while (g_hash_table_iter_next (&iter, NULL, &value))
        {
          GList *l;
          GtkWidget *window = value;
          GList *handlers =
            g_object_get_qdata (G_OBJECT (window), event_handlers_quark);

          for (l = handlers; l; l = l->next)
            {
              XEventHandler handler = l->data;
              handler (window, &event);
            }
        }
    }

  return TRUE;
}

static void
x_event_source_finalize (GSource *source)
{
  XClientEventSource *x_source = (XClientEventSource *) source;

  *x_source->self_ref = NULL;
}

static GSourceFuncs x_event_funcs = {
  x_event_source_prepare,
  x_event_source_check,
  x_event_source_dispatch,
  x_event_source_finalize,
};

static GSource*
ensure_xsource_handler (GdkDisplay *gdkdisplay)
{
  static GSource *source = NULL;
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdkdisplay);
  XClientEventSource *x_source;

  if (source)
    return g_source_ref (source);

  source = g_source_new (&x_event_funcs, sizeof (XClientEventSource));
  x_source = (XClientEventSource *) source;
  x_source->self_ref = &source;
  x_source->xdisplay = xdisplay;
  x_source->event_poll_fd.fd = ConnectionNumber (xdisplay);
  x_source->event_poll_fd.events = G_IO_IN;
  g_source_add_poll (source, &x_source->event_poll_fd);

  g_source_set_priority (source, GDK_PRIORITY_EVENTS - 1);
  g_source_set_can_recurse (source, TRUE);
  g_source_attach (source, NULL);

  return source;
}

static gboolean
window_has_x11_event_handler (GtkWidget     *window,
                              XEventHandler  handler)
{
  GList *handlers =
    g_object_get_qdata (G_OBJECT (window), event_handlers_quark);

  g_return_val_if_fail (handler, FALSE);
  g_return_val_if_fail (!wayland, FALSE);

  return g_list_find (handlers, handler) != NULL;
}

static void
unref_and_maybe_destroy_gsource (GSource *source)
{
  g_source_unref (source);

  if (source->ref_count == 1)
    g_source_destroy (source);
}

static void
window_add_x11_event_handler (GtkWidget     *window,
                              XEventHandler  handler)
{
  GSource *source;
  GList *handlers =
    g_object_get_qdata (G_OBJECT (window), event_handlers_quark);

  g_return_if_fail (!window_has_x11_event_handler (window, handler));

  source = ensure_xsource_handler (gtk_widget_get_display (window));
  g_object_set_qdata_full (G_OBJECT (window), event_source_quark, source,
                           (GDestroyNotify) unref_and_maybe_destroy_gsource);

  handlers = g_list_append (handlers, handler);
  g_object_set_qdata (G_OBJECT (window), event_handlers_quark, handlers);
}

static void
window_remove_x11_event_handler (GtkWidget     *window,
                                 XEventHandler  handler)
{
  GList *handlers =
    g_object_get_qdata (G_OBJECT (window), event_handlers_quark);

  g_return_if_fail (window_has_x11_event_handler (window, handler));

  g_object_set_qdata (G_OBJECT (window), event_source_quark, NULL);

  handlers = g_list_remove (handlers, handler);
  g_object_set_qdata (G_OBJECT (window), event_handlers_quark, handlers);
}

static void
handle_take_focus (GtkWidget *window,
                   XEvent    *xevent)
{
  GdkWindow *gdkwindow = gtk_widget_get_window (window);
  GdkDisplay *display = gtk_widget_get_display (window);
  Atom wm_protocols =
    gdk_x11_get_xatom_by_name_for_display (display, "WM_PROTOCOLS");
  Atom wm_take_focus =
    gdk_x11_get_xatom_by_name_for_display (display, "WM_TAKE_FOCUS");

  if (xevent->xany.type != ClientMessage ||
      xevent->xany.window != GDK_WINDOW_XID (gdkwindow))
    return;

  if (xevent->xclient.message_type == wm_protocols &&
      xevent->xclient.data.l[0] == wm_take_focus)
    {
      XSetInputFocus (xevent->xany.display,
                      GDK_WINDOW_XID (gdkwindow),
                      RevertToParent,
                      xevent->xclient.data.l[1]);
    }
}

static void
process_line (const char *line)
{
  GError *error = NULL;
  int argc;
  char **argv;

  if (!g_shell_parse_argv (line, &argc, &argv, &error))
    {
      g_print ("error parsing command: %s", error->message);
      g_error_free (error);
      return;
    }

  if (argc < 1)
    {
      g_print ("Empty command");
      goto out;
    }

  if (strcmp (argv[0], "create") == 0)
    {
      int i;

      if (argc  < 2)
        {
          g_print ("usage: create <id> [override|csd]");
          goto out;
        }

      if (g_hash_table_lookup (windows, argv[1]))
        {
          g_print ("window %s already exists", argv[1]);
          goto out;
        }

      gboolean override = FALSE;
      gboolean csd = FALSE;
      for (i = 2; i < argc; i++)
        {
          if (strcmp (argv[i], "override") == 0)
            override = TRUE;
          if (strcmp (argv[i], "csd") == 0)
            csd = TRUE;
        }

      if (override && csd)
        {
          g_print ("override and csd keywords are exclusie");
          goto out;
        }

      GtkWidget *window = gtk_window_new (override ? GTK_WINDOW_POPUP : GTK_WINDOW_TOPLEVEL);
      g_hash_table_insert (windows, g_strdup (argv[1]), window);

      if (csd)
        {
          GtkWidget *headerbar = gtk_header_bar_new ();
          gtk_window_set_titlebar (GTK_WINDOW (window), headerbar);
          gtk_widget_show (headerbar);
        }

      gtk_window_set_default_size (GTK_WINDOW (window), 100, 100);

      gchar *title = g_strdup_printf ("test/%s/%s", client_id, argv[1]);
      gtk_window_set_title (GTK_WINDOW (window), title);
      g_free (title);

      g_object_set_qdata (G_OBJECT (window), can_take_focus_quark,
                          GUINT_TO_POINTER (TRUE));

      gtk_widget_realize (window);

      if (!wayland)
        {
          /* The cairo xlib backend creates a window when initialized, which
           * confuses our testing if it happens asynchronously the first
           * time a window is painted. By creating an Xlib surface and
           * destroying it, we force initialization at a more predictable time.
           */
          GdkWindow *window_gdk = gtk_widget_get_window (window);
          cairo_surface_t *surface = gdk_window_create_similar_surface (window_gdk,
                                                                        CAIRO_CONTENT_COLOR,
                                                                        1, 1);
          cairo_surface_destroy (surface);
        }

    }
  else if (strcmp (argv[0], "set_parent") == 0)
    {
      if (argc != 3)
        {
          g_print ("usage: set_parent <window-id> <parent-id>");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        {
          g_print ("unknown window %s", argv[1]);
          goto out;
        }

      GtkWidget *parent_window = lookup_window (argv[2]);
      if (!parent_window)
        {
          g_print ("unknown parent window %s", argv[2]);
          goto out;
        }

      gtk_window_set_transient_for (GTK_WINDOW (window),
                                    GTK_WINDOW (parent_window));
    }
  else if (strcmp (argv[0], "set_parent_exported") == 0)
    {
      if (argc != 3)
        {
          g_print ("usage: set_parent_exported <window-id> <parent-id>");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        {
          g_print ("unknown window %s", argv[1]);
          goto out;
        }

      GtkWidget *parent_window = lookup_window (argv[2]);
      if (!parent_window)
        {
          g_print ("unknown parent window %s", argv[2]);
          goto out;
        }

      GdkWindow *parent_gdk_window = gtk_widget_get_window (parent_window);
      if (!gdk_wayland_window_export_handle (parent_gdk_window,
                                             window_export_handle_cb,
                                             window,
                                             NULL))
        g_print ("Fail to export handle for window id %s", argv[2]);
    }
  else if (strcmp (argv[0], "accept_focus") == 0)
    {
      if (argc != 3)
        {
          g_print ("usage: %s <window-id> [true|false]", argv[0]);
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        {
          g_print ("unknown window %s", argv[1]);
          goto out;
        }

      if (!wayland &&
          window_has_x11_event_handler (window, handle_take_focus))
        {
          g_print ("Impossible to use %s for windows accepting take focus",
                   argv[1]);
          goto out;
        }

      gboolean enabled = g_ascii_strcasecmp (argv[2], "true") == 0;
      gtk_window_set_accept_focus (GTK_WINDOW (window), enabled);
    }
  else if (strcmp (argv[0], "can_take_focus") == 0)
    {
      if (argc != 3)
        {
          g_print ("usage: %s <window-id> [true|false]", argv[0]);
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        {
          g_print ("unknown window %s", argv[1]);
          goto out;
        }

      if (wayland)
        {
          g_print ("%s not supported under wayland", argv[0]);
          goto out;
        }

      if (window_has_x11_event_handler (window, handle_take_focus))
        {
          g_print ("Impossible to change %s for windows accepting take focus",
                   argv[1]);
          goto out;
        }

      GdkDisplay *display = gdk_display_get_default ();
      GdkWindow *gdkwindow = gtk_widget_get_window (window);
      Display *xdisplay = gdk_x11_display_get_xdisplay (display);
      Window xwindow = GDK_WINDOW_XID (gdkwindow);
      Atom wm_take_focus = gdk_x11_get_xatom_by_name_for_display (display, "WM_TAKE_FOCUS");
      gboolean add = g_ascii_strcasecmp(argv[2], "true") == 0;
      Atom *protocols = NULL;
      Atom *new_protocols;
      int n_protocols = 0;
      int i, n = 0;

      gdk_display_sync (display);
      XGetWMProtocols (xdisplay, xwindow, &protocols, &n_protocols);
      new_protocols = g_new0 (Atom, n_protocols + (add ? 1 : 0));

      for (i = 0; i < n_protocols; ++i)
        {
          if (protocols[i] != wm_take_focus)
            new_protocols[n++] = protocols[i];
        }

      if (add)
        new_protocols[n++] = wm_take_focus;

      XSetWMProtocols (xdisplay, xwindow, new_protocols, n);
      g_object_set_qdata (G_OBJECT (window), can_take_focus_quark,
                          GUINT_TO_POINTER (add));

      XFree (new_protocols);
      XFree (protocols);
    }
  else if (strcmp (argv[0], "accept_take_focus") == 0)
    {
      if (argc != 3)
        {
          g_print ("usage: %s <window-id> [true|false]", argv[0]);
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        {
          g_print ("unknown window %s", argv[1]);
          goto out;
        }

      if (wayland)
        {
          g_print ("%s not supported under wayland", argv[0]);
          goto out;
        }

      if (gtk_window_get_accept_focus (GTK_WINDOW (window)))
        {
          g_print ("%s not supported for input windows", argv[0]);
          goto out;
        }

      if (!g_object_get_qdata (G_OBJECT (window), can_take_focus_quark))
        {
          g_print ("%s not supported for windows with no WM_TAKE_FOCUS set",
                   argv[0]);
          goto out;
        }

      if (g_ascii_strcasecmp (argv[2], "true") == 0)
        window_add_x11_event_handler (window, handle_take_focus);
      else
        window_remove_x11_event_handler (window, handle_take_focus);
    }
  else if (strcmp (argv[0], "show") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: show <id>");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gtk_widget_show (window);
      gdk_display_sync (gdk_display_get_default ());
    }
  else if (strcmp (argv[0], "hide") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: hide <id>");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gtk_widget_hide (window);
    }
  else if (strcmp (argv[0], "activate") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: activate <id>");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gtk_window_present (GTK_WINDOW (window));
    }
  else if (strcmp (argv[0], "resize") == 0)
    {
      if (argc != 4)
        {
          g_print ("usage: resize <id> <width> <height>");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      int width = atoi (argv[2]);
      int height = atoi (argv[3]);
      gtk_window_resize (GTK_WINDOW (window), width, height);
    }
  else if (strcmp (argv[0], "raise") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: raise <id>");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gdk_window_raise (gtk_widget_get_window (window));
    }
  else if (strcmp (argv[0], "lower") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: lower <id>");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gdk_window_lower (gtk_widget_get_window (window));
    }
  else if (strcmp (argv[0], "destroy") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: destroy <id>");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      g_hash_table_remove (windows, argv[1]);
      gtk_widget_destroy (window);
    }
  else if (strcmp (argv[0], "destroy_all") == 0)
    {
      if (argc != 1)
        {
          g_print ("usage: destroy_all");
          goto out;
        }

      GHashTableIter iter;
      gpointer key, value;

      g_hash_table_iter_init (&iter, windows);
      while (g_hash_table_iter_next (&iter, &key, &value))
        gtk_widget_destroy (value);

      g_hash_table_remove_all (windows);
    }
  else if (strcmp (argv[0], "sync") == 0)
    {
      if (argc != 1)
        {
          g_print ("usage: sync");
          goto out;
        }

      gdk_display_sync (gdk_display_get_default ());
    }
  else if (strcmp (argv[0], "set_counter") == 0)
    {
      XSyncCounter counter;
      int value;

      if (argc != 3)
        {
          g_print ("usage: set_counter <counter> <value>");
          goto out;
        }

      if (wayland)
        {
          g_print ("usage: set_counter can only be used for X11");
          goto out;
        }

      counter = strtoul(argv[1], NULL, 10);
      value = atoi(argv[2]);
      XSyncValue sync_value;
      XSyncIntToValue (&sync_value, value);

      XSyncSetCounter (gdk_x11_display_get_xdisplay (gdk_display_get_default ()),
                       counter, sync_value);
    }
  else if (strcmp (argv[0], "minimize") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: minimize <id>");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gtk_window_iconify (GTK_WINDOW (window));
    }
  else if (strcmp (argv[0], "unminimize") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: unminimize <id>");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gtk_window_deiconify (GTK_WINDOW (window));
    }
  else
    {
      g_print ("Unknown command %s", argv[0]);
      goto out;
    }

  g_print ("OK\n");

 out:
  g_strfreev (argv);
}

static void
on_line_received (GObject      *source,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  GDataInputStream *in = G_DATA_INPUT_STREAM (source);
  GError *error = NULL;
  gsize length;
  char *line = g_data_input_stream_read_line_finish_utf8 (in, result, &length, &error);

  if (line == NULL)
    {
      if (error != NULL)
        g_printerr ("Error reading from stdin: %s\n", error->message);
      gtk_main_quit ();
      return;
    }

  process_line (line);
  g_free (line);
  read_next_line (in);
}

static void
read_next_line (GDataInputStream *in)
{
  g_data_input_stream_read_line_async (in, G_PRIORITY_DEFAULT, NULL,
                                       on_line_received, NULL);
}

const GOptionEntry options[] = {
  {
    "wayland", 0, 0, G_OPTION_ARG_NONE,
    &wayland,
    "Create a wayland client, not an X11 one",
    NULL
  },
  {
    "client-id", 0, 0, G_OPTION_ARG_STRING,
    &client_id,
    "Identifier used in Window titles for this client",
    "CLIENT_ID",
  },
  { NULL }
};

int
main(int argc, char **argv)
{
  GOptionContext *context = g_option_context_new (NULL);
  GError *error = NULL;

  g_option_context_add_main_entries (context, options, NULL);

  if (!g_option_context_parse (context,
                               &argc, &argv, &error))
    {
      g_printerr ("%s", error->message);
      return 1;
    }

  if (wayland)
    gdk_set_allowed_backends ("wayland");
  else
    gdk_set_allowed_backends ("x11");

  gtk_init (NULL, NULL);

  windows = g_hash_table_new_full (g_str_hash, g_str_equal,
                                   g_free, NULL);
  event_source_quark = g_quark_from_static_string ("event-source");
  event_handlers_quark = g_quark_from_static_string ("event-handlers");
  can_take_focus_quark = g_quark_from_static_string ("can-take-focus");

  GInputStream *raw_in = g_unix_input_stream_new (0, FALSE);
  GDataInputStream *in = g_data_input_stream_new (raw_in);

  read_next_line (in);

  gtk_main ();

  return 0;
}
