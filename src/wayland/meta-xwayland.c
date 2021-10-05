/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * X Wayland Support
 *
 * Copyright (C) 2013 Intel Corporation
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

#include "wayland/meta-xwayland.h"
#include "wayland/meta-xwayland-private.h"

#include <errno.h>
#include <glib-unix.h>
#include <glib.h>
#include <sys/socket.h>
#include <sys/un.h>
#if defined(HAVE_SYS_RANDOM)
#include <sys/random.h>
#elif defined(HAVE_LINUX_RANDOM)
#include <linux/random.h>
#endif
#include <unistd.h>
#include <X11/Xauth.h>

#include "core/main-private.h"
#include "meta/main.h"
#include "wayland/meta-xwayland-surface.h"
#include "x11/meta-x11-display-private.h"

static int display_number_override = -1;

static void meta_xwayland_stop_xserver (MetaXWaylandManager *manager);

void
meta_xwayland_associate_window_with_surface (MetaWindow          *window,
                                             MetaWaylandSurface  *surface)
{
  MetaDisplay *display = window->display;
  MetaXwaylandSurface *xwayland_surface;

  if (!meta_wayland_surface_assign_role (surface,
                                         META_TYPE_XWAYLAND_SURFACE,
                                         NULL))
    {
      wl_resource_post_error (surface->resource,
                              WL_DISPLAY_ERROR_INVALID_OBJECT,
                              "wl_surface@%d already has a different role",
                              wl_resource_get_id (surface->resource));
      return;
    }

  xwayland_surface = META_XWAYLAND_SURFACE (surface->role);
  meta_xwayland_surface_associate_with_window (xwayland_surface, window);

  /* Now that we have a surface check if it should have focus. */
  meta_display_sync_wayland_input_focus (display);
}

static gboolean
associate_window_with_surface_id (MetaXWaylandManager *manager,
                                  MetaWindow          *window,
                                  guint32              surface_id)
{
  struct wl_resource *resource;

  resource = wl_client_get_object (manager->client, surface_id);
  if (resource)
    {
      MetaWaylandSurface *surface = wl_resource_get_user_data (resource);
      meta_xwayland_associate_window_with_surface (window, surface);
      return TRUE;
    }
  else
    return FALSE;
}

void
meta_xwayland_handle_wl_surface_id (MetaWindow *window,
                                    guint32     surface_id)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaXWaylandManager *manager = &compositor->xwayland_manager;

  if (!associate_window_with_surface_id (manager, window, surface_id))
    {
      /* No surface ID yet, schedule this association for whenever the
       * surface is made known.
       */
      meta_wayland_compositor_schedule_surface_association (compositor,
                                                            surface_id, window);
    }
}

gboolean
meta_xwayland_is_xwayland_surface (MetaWaylandSurface *surface)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaXWaylandManager *manager = &compositor->xwayland_manager;

  return wl_resource_get_client (surface->resource) == manager->client;
}

static gboolean
try_display (int    display,
             char **filename_out,
             int   *fd_out)
{
  gboolean ret = FALSE;
  char *filename;
  int fd;

  filename = g_strdup_printf ("/tmp/.X%d-lock", display);

 again:
  fd = open (filename, O_WRONLY | O_CLOEXEC | O_CREAT | O_EXCL, 0444);

  if (fd < 0 && errno == EEXIST)
    {
      char pid[11];
      char *end;
      pid_t other;

      fd = open (filename, O_CLOEXEC, O_RDONLY);
      if (fd < 0 || read (fd, pid, 11) != 11)
        {
          g_warning ("can't read lock file %s: %m", filename);
          goto out;
        }
      close (fd);
      fd = -1;

      pid[10] = '\0';
      other = strtol (pid, &end, 0);
      if (end != pid + 10)
        {
          g_warning ("can't parse lock file %s", filename);
          goto out;
        }

      if (kill (other, 0) < 0 && errno == ESRCH)
        {
          /* Process is dead. Try unlinking the lock file and trying again. */
          if (unlink (filename) < 0)
            {
              g_warning ("failed to unlink stale lock file %s: %m", filename);
              goto out;
            }

          goto again;
        }

      goto out;
    }
  else if (fd < 0)
    {
      g_warning ("failed to create lock file %s: %m", filename);
      goto out;
    }

  ret = TRUE;

 out:
  if (!ret)
    {
      g_free (filename);
      filename = NULL;

      if (fd >= 0)
        {
          close (fd);
          fd = -1;
        }
    }

  *filename_out = filename;
  *fd_out = fd;
  return ret;
}

static char *
create_lock_file (int display, int *display_out)
{
  char *filename;
  int fd;

  char pid[12];
  int size;
  int number_of_tries = 0;

  while (!try_display (display, &filename, &fd))
    {
      display++;
      number_of_tries++;

      /* If we can't get a display after 50 times, then something's wrong. Just
       * abort in this case. */
      if (number_of_tries >= 50)
        return NULL;
    }

  /* Subtle detail: we use the pid of the wayland compositor, not the xserver
   * in the lock file. Another subtlety: snprintf returns the number of bytes
   * it _would've_ written without either the NUL or the size clamping, hence
   * the disparity in size. */
  size = snprintf (pid, 12, "%10d\n", getpid ());
  if (size != 11 || write (fd, pid, 11) != 11)
    {
      unlink (filename);
      close (fd);
      g_warning ("failed to write pid to lock file %s", filename);
      g_free (filename);
      return NULL;
    }

  close (fd);

  *display_out = display;
  return filename;
}

static int
bind_to_abstract_socket (int       display,
                         gboolean *fatal)
{
  struct sockaddr_un addr;
  socklen_t size, name_size;
  int fd;

  fd = socket (PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    {
      *fatal = TRUE;
      g_warning ("Failed to create socket: %m");
      return -1;
    }

  addr.sun_family = AF_LOCAL;
  name_size = snprintf (addr.sun_path, sizeof addr.sun_path,
                        "%c/tmp/.X11-unix/X%d", 0, display);
  size = offsetof (struct sockaddr_un, sun_path) + name_size;
  if (bind (fd, (struct sockaddr *) &addr, size) < 0)
    {
      *fatal = errno != EADDRINUSE;
      g_warning ("failed to bind to @%s: %m", addr.sun_path + 1);
      close (fd);
      return -1;
    }

  if (listen (fd, 1) < 0)
    {
      *fatal = errno != EADDRINUSE;
      g_warning ("Failed to listen on abstract socket @%s: %m",
                 addr.sun_path + 1);
      close (fd);
      return -1;
    }

  return fd;
}

static int
bind_to_unix_socket (int display)
{
  struct sockaddr_un addr;
  socklen_t size, name_size;
  int fd;

  fd = socket (PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    return -1;

  addr.sun_family = AF_LOCAL;
  name_size = snprintf (addr.sun_path, sizeof addr.sun_path,
                        "/tmp/.X11-unix/X%d", display) + 1;
  size = offsetof (struct sockaddr_un, sun_path) + name_size;
  unlink (addr.sun_path);
  if (bind (fd, (struct sockaddr *) &addr, size) < 0)
    {
      g_warning ("failed to bind to %s: %m\n", addr.sun_path);
      close (fd);
      return -1;
    }

  if (listen (fd, 1) < 0)
    {
      unlink (addr.sun_path);
      close (fd);
      return -1;
    }

  return fd;
}

static void
xserver_died (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
  GSubprocess *proc = G_SUBPROCESS (source);
  MetaDisplay *display = meta_get_display ();
  g_autoptr (GError) error = NULL;

  if (!g_subprocess_wait_finish (proc, result, &error))
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      g_warning ("Failed to finish waiting for Xwayland: %s", error->message);
    }
  else if (!g_subprocess_get_successful (proc))
    {
      if (meta_get_x11_display_policy () == META_DISPLAY_POLICY_MANDATORY)
        g_warning ("X Wayland crashed; exiting");
      else
        g_warning ("X Wayland crashed; attempting to recover");
    }

  if (meta_get_x11_display_policy () == META_DISPLAY_POLICY_MANDATORY)
    {
      meta_exit (META_EXIT_ERROR);
    }
  else if (meta_get_x11_display_policy () == META_DISPLAY_POLICY_ON_DEMAND)
    {
      MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();

      if (display->x11_display)
        meta_display_shutdown_x11 (display);

      if (!meta_xwayland_init (&compositor->xwayland_manager,
                               compositor->wayland_display))
        g_warning ("Failed to init X sockets");
    }
}

static gboolean
shutdown_xwayland_cb (gpointer data)
{
  MetaXWaylandManager *manager = data;

  meta_verbose ("Shutting down Xwayland");
  manager->xserver_grace_period_id = 0;
  meta_display_shutdown_x11 (meta_get_display ());
  meta_xwayland_stop_xserver (manager);
  return G_SOURCE_REMOVE;
}

static int
x_io_error (Display *display)
{
  g_warning ("Connection to xwayland lost");

  if (meta_get_x11_display_policy () == META_DISPLAY_POLICY_MANDATORY)
    meta_exit (META_EXIT_ERROR);

  return 0;
}

void
meta_xwayland_override_display_number (int number)
{
  display_number_override = number;
}

static gboolean
open_display_sockets (MetaXWaylandManager *manager,
                      int                  display_index,
                      int                 *abstract_fd_out,
                      int                 *unix_fd_out,
                      gboolean            *fatal)
{
  int abstract_fd, unix_fd;

  abstract_fd = bind_to_abstract_socket (display_index,
                                         fatal);
  if (abstract_fd < 0)
    return FALSE;

  unix_fd = bind_to_unix_socket (display_index);
  if (unix_fd < 0)
    {
      *fatal = FALSE;
      close (abstract_fd);
      return FALSE;
    }

  *abstract_fd_out = abstract_fd;
  *unix_fd_out = unix_fd;

  return TRUE;
}

static gboolean
choose_xdisplay (MetaXWaylandManager    *manager,
                 MetaXWaylandConnection *connection)
{
  int display = 0;
  char *lock_file = NULL;
  gboolean fatal = FALSE;

  if (display_number_override != -1)
    display = display_number_override;
  else if (g_getenv ("RUNNING_UNDER_GDM"))
    display = 1024;

  do
    {
      lock_file = create_lock_file (display, &display);
      if (!lock_file)
        {
          g_warning ("Failed to create an X lock file");
          return FALSE;
        }

      if (!open_display_sockets (manager, display,
                                 &connection->abstract_fd,
                                 &connection->unix_fd,
                                 &fatal))
        {
          unlink (lock_file);

          if (!fatal)
            {
              display++;
              continue;
            }
          else
            {
              g_warning ("Failed to bind X11 socket");
              return FALSE;
            }
        }

      break;
    }
  while (1);

  connection->display_index = display;
  connection->name = g_strdup_printf (":%d", connection->display_index);
  connection->lock_file = lock_file;

  return TRUE;
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (FILE, fclose)

static gboolean
prepare_auth_file (MetaXWaylandManager *manager)
{
  Xauth auth_entry = { 0 };
  g_autoptr (FILE) fp = NULL;
  char auth_data[16];
  int fd;

  manager->auth_file = g_build_filename (g_get_user_runtime_dir (),
                                         ".mutter-Xwaylandauth.XXXXXX",
                                         NULL);

  if (getrandom (auth_data, sizeof (auth_data), 0) != sizeof (auth_data))
    {
      g_warning ("Failed to get random data: %s", g_strerror (errno));
      return FALSE;
    }

  auth_entry.family = FamilyLocal;
  auth_entry.address = (char *) g_get_host_name ();
  auth_entry.address_length = strlen (auth_entry.address);
  auth_entry.name = (char *) "MIT-MAGIC-COOKIE-1";
  auth_entry.name_length = strlen (auth_entry.name);
  auth_entry.data = auth_data;
  auth_entry.data_length = sizeof (auth_data);

  fd = g_mkstemp (manager->auth_file);
  if (fd < 0)
    {
      g_warning ("Failed to open Xauthority file: %s", g_strerror (errno));
      return FALSE;
    }

  fp = fdopen (fd, "w+");
  if (!fp)
    {
      g_warning ("Failed to open Xauthority stream: %s", g_strerror (errno));
      close (fd);
      return FALSE;
    }

  if (!XauWriteAuth (fp, &auth_entry))
    {
      g_warning ("Error writing to Xauthority file: %s", g_strerror (errno));
      return FALSE;
    }

  auth_entry.family = FamilyWild;
  if (!XauWriteAuth (fp, &auth_entry))
    {
      g_warning ("Error writing to Xauthority file: %s", g_strerror (errno));
      return FALSE;
    }

  if (fflush (fp) == EOF)
    {
      g_warning ("Error writing to Xauthority file: %s", g_strerror (errno));
      return FALSE;
    }

  return TRUE;
}

static void
add_local_user_to_xhost (Display *xdisplay)
{
  XHostAddress host_entry;
  XServerInterpretedAddress siaddr;

  siaddr.type = (char *) "localuser";
  siaddr.typelength = strlen (siaddr.type);
  siaddr.value = (char *) g_get_user_name();
  siaddr.valuelength = strlen (siaddr.value);

  host_entry.family = FamilyServerInterpreted;
  host_entry.address = (char *) &siaddr;

  XAddHost (xdisplay, &host_entry);
}

static void
on_init_x11_cb (MetaDisplay  *display,
                GAsyncResult *result,
                gpointer      user_data)
{
  g_autoptr (GError) error = NULL;

  if (!meta_display_init_x11_finish (display, result, &error))
    g_warning ("Failed to initialize X11 display: %s\n", error->message);
}

static gboolean
on_displayfd_ready (int          fd,
                    GIOCondition condition,
                    gpointer     user_data)
{
  GTask *task = user_data;

  /* The server writes its display name to the displayfd
   * socket when it's ready. We don't care about the data
   * in the socket, just that it wrote something, since
   * that means it's ready. */
  g_task_return_boolean (task, TRUE);
  g_object_unref (task);

  return G_SOURCE_REMOVE;
}

void
meta_xwayland_start_xserver (MetaXWaylandManager *manager,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
  int xwayland_client_fd[2];
  int displayfd[2];
  g_autoptr(GSubprocessLauncher) launcher = NULL;
  GSubprocessFlags flags;
  GError *error = NULL;
  g_autoptr (GTask) task = NULL;

  task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_source_tag (task, meta_xwayland_start_xserver);
  g_task_set_task_data (task, manager, NULL);

  /* We want xwayland to be a wayland client so we make a socketpair to setup a
   * wayland protocol connection. */
  if (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, xwayland_client_fd) < 0)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               g_io_error_from_errno (errno),
                               "xwayland_client_fd socketpair failed");
      return;
    }

  if (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, displayfd) < 0)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               g_io_error_from_errno (errno),
                               "displayfd socketpair failed");
      return;
    }

  /* xwayland, please. */
  flags = G_SUBPROCESS_FLAGS_NONE;

  if (getenv ("XWAYLAND_STFU"))
    {
      flags |= G_SUBPROCESS_FLAGS_STDOUT_SILENCE;
      flags |= G_SUBPROCESS_FLAGS_STDERR_SILENCE;
    }

  launcher = g_subprocess_launcher_new (flags);

  g_subprocess_launcher_take_fd (launcher, xwayland_client_fd[1], 3);
  g_subprocess_launcher_take_fd (launcher, manager->public_connection.abstract_fd, 4);
  g_subprocess_launcher_take_fd (launcher, manager->public_connection.unix_fd, 5);
  g_subprocess_launcher_take_fd (launcher, displayfd[1], 6);
  g_subprocess_launcher_take_fd (launcher, manager->private_connection.abstract_fd, 7);

  g_subprocess_launcher_setenv (launcher, "WAYLAND_SOCKET", "3", TRUE);

  manager->proc = g_subprocess_launcher_spawn (launcher, &error,
                                               XWAYLAND_PATH,
                                               manager->public_connection.name,
                                               "-rootless",
                                               "-noreset",
                                               "-accessx",
                                               "-core",
                                               "-auth", manager->auth_file,
                                               "-listen", "4",
                                               "-listen", "5",
                                               "-displayfd", "6",
#ifdef HAVE_XWAYLAND_INITFD
                                               "-initfd", "7",
#else
                                               "-listen", "7",
#endif
                                               NULL);

  if (!manager->proc)
    {
      g_task_return_error (task, error);
      return;
    }

  manager->xserver_died_cancellable = g_cancellable_new ();
  g_subprocess_wait_async (manager->proc, manager->xserver_died_cancellable,
                           xserver_died, NULL);
  g_unix_fd_add (displayfd[0], G_IO_IN, on_displayfd_ready,
                 g_steal_pointer (&task));
  manager->client = wl_client_create (manager->wayland_display,
                                      xwayland_client_fd[0]);
}

gboolean
meta_xwayland_start_xserver_finish (MetaXWaylandManager  *manager,
                                    GAsyncResult         *result,
                                    GError              **error)
{
  g_assert (g_task_get_source_tag (G_TASK (result)) ==
            meta_xwayland_start_xserver);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static gboolean
xdisplay_connection_activity_cb (gint         fd,
                                 GIOCondition cond,
                                 gpointer     user_data)
{
  MetaDisplay *display = meta_get_display ();

  meta_display_init_x11 (display, NULL,
                         (GAsyncReadyCallback) on_init_x11_cb, NULL);

  return G_SOURCE_REMOVE;
}

static void
meta_xwayland_stop_xserver_timeout (MetaXWaylandManager *manager)
{
  if (manager->xserver_grace_period_id)
    return;

  manager->xserver_grace_period_id =
    g_timeout_add_seconds (10, shutdown_xwayland_cb, manager);
}

static void
window_unmanaged_cb (MetaWindow          *window,
                     MetaXWaylandManager *manager)
{
  manager->x11_windows = g_list_remove (manager->x11_windows, window);
  g_signal_handlers_disconnect_by_func (window,
                                        window_unmanaged_cb,
                                        manager);
  if (!manager->x11_windows)
    {
      meta_verbose ("All X11 windows gone, setting shutdown timeout");
      meta_xwayland_stop_xserver_timeout (manager);
    }
}

static void
window_created_cb (MetaDisplay         *display,
                   MetaWindow          *window,
                   MetaXWaylandManager *manager)
{
  /* Ignore all internal windows */
  if (!window->xwindow ||
      meta_window_get_client_pid (window) == getpid ())
    return;

  manager->x11_windows = g_list_prepend (manager->x11_windows, window);
  g_signal_connect (window, "unmanaged",
                    G_CALLBACK (window_unmanaged_cb), manager);

  g_clear_handle_id (&manager->xserver_grace_period_id, g_source_remove);
}

static void
meta_xwayland_stop_xserver (MetaXWaylandManager *manager)
{
  if (manager->proc)
    g_subprocess_send_signal (manager->proc, SIGTERM);
  g_signal_handlers_disconnect_by_func (meta_get_display (),
                                        window_created_cb,
                                        manager);
  g_clear_object (&manager->xserver_died_cancellable);
  g_clear_object (&manager->proc);
}

gboolean
meta_xwayland_init (MetaXWaylandManager *manager,
                    struct wl_display   *wl_display)
{
  MetaDisplayPolicy policy;
  gboolean fatal;

  if (!manager->public_connection.name)
    {
      if (!choose_xdisplay (manager, &manager->public_connection))
        return FALSE;
      if (!choose_xdisplay (manager, &manager->private_connection))
        return FALSE;

      if (!prepare_auth_file (manager))
        return FALSE;
    }
  else
    {
      if (!open_display_sockets (manager,
                                 manager->public_connection.display_index,
                                 &manager->public_connection.abstract_fd,
                                 &manager->public_connection.unix_fd,
                                 &fatal))
        return FALSE;

      if (!open_display_sockets (manager,
                                 manager->private_connection.display_index,
                                 &manager->private_connection.abstract_fd,
                                 &manager->private_connection.unix_fd,
                                 &fatal))
        return FALSE;
    }

  manager->wayland_display = wl_display;
  policy = meta_get_x11_display_policy ();

  if (policy == META_DISPLAY_POLICY_ON_DEMAND)
    {
      g_unix_fd_add (manager->public_connection.abstract_fd, G_IO_IN,
                     xdisplay_connection_activity_cb, manager);
    }

  return TRUE;
}

static void
on_x11_display_closing (MetaDisplay *display)
{
  Display *xdisplay = meta_x11_display_get_xdisplay (display->x11_display);

  meta_xwayland_shutdown_dnd (xdisplay);
  g_signal_handlers_disconnect_by_func (display,
                                        on_x11_display_closing,
                                        NULL);
}

/* To be called right after connecting */
void
meta_xwayland_complete_init (MetaDisplay *display,
                             Display     *xdisplay)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaXWaylandManager *manager = &compositor->xwayland_manager;

  /* We install an X IO error handler in addition to the child watch,
     because after Xlib connects our child watch may not be called soon
     enough, and therefore we won't crash when X exits (and most important
     we won't reset the tty).
  */
  XSetIOErrorHandler (x_io_error);

  g_signal_connect (display, "x11-display-closing",
                    G_CALLBACK (on_x11_display_closing), NULL);
  meta_xwayland_init_dnd (xdisplay);
  add_local_user_to_xhost (xdisplay);

  if (meta_get_x11_display_policy () == META_DISPLAY_POLICY_ON_DEMAND)
    {
      meta_xwayland_stop_xserver_timeout (manager);
      g_signal_connect (meta_get_display (), "window-created",
                        G_CALLBACK (window_created_cb), manager);
    }
}

static void
meta_xwayland_connection_release (MetaXWaylandConnection *connection)
{
  unlink (connection->lock_file);
  g_clear_pointer (&connection->lock_file, g_free);
}

void
meta_xwayland_shutdown (MetaXWaylandManager *manager)
{
  char path[256];

  g_cancellable_cancel (manager->xserver_died_cancellable);

  snprintf (path, sizeof path, "/tmp/.X11-unix/X%d", manager->public_connection.display_index);
  unlink (path);

  snprintf (path, sizeof path, "/tmp/.X11-unix/X%d", manager->private_connection.display_index);
  unlink (path);

  g_clear_pointer (&manager->public_connection.name, g_free);
  g_clear_pointer (&manager->private_connection.name, g_free);

  meta_xwayland_connection_release (&manager->public_connection);
  meta_xwayland_connection_release (&manager->private_connection);

  if (manager->auth_file)
    {
      unlink (manager->auth_file);
      g_clear_pointer (&manager->auth_file, g_free);
    }
}
