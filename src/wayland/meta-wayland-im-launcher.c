/*
 * Wayland Support
 *
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

/* Launches fcitx5 as a compositor-owned child, the way muffin launches
 * XWayland: a socketpair provides a private wl connection whose server end
 * is turned into a known wl_client via wl_client_create(), and the client end
 * is handed to fcitx as WAYLAND_SOCKET. Because the compositor holds that
 * exact wl_client, the global filter can advertise the input-method /
 * virtual-keyboard globals to fcitx alone (see meta-wayland-input-method.c and
 * the filter in meta-wayland.c). fcitx is supervised: reaped on shutdown and
 * relaunched if it crashes.
 */

#include "config.h"

#include "wayland/meta-wayland-im-launcher.h"

#include <errno.h>
#include <gio/gio.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include "wayland/meta-wayland-private.h"

#define IM_LAUNCHER_MAX_BACKOFF_SECS 30
#define IM_LAUNCHER_STABLE_RUN_USECS (60 * G_USEC_PER_SEC)
#define IM_LAUNCHER_MAX_CRASHES 5

struct _MetaWaylandImLauncher
{
  MetaWaylandCompositor *compositor;

  struct wl_client *client;
  struct wl_listener client_destroy_listener;

  GSubprocess *proc;
  GCancellable *cancellable;

  guint restart_source_id;
  guint backoff_seconds;
  guint crash_count;
  gint64 last_start_us;

  gboolean wait_pending;
  gboolean shutting_down;
};

static void spawn_fcitx (MetaWaylandImLauncher *launcher);

static void
on_client_destroyed (struct wl_listener *listener,
                     void               *data)
{
  MetaWaylandImLauncher *launcher =
    wl_container_of (listener, launcher, client_destroy_listener);

  launcher->client = NULL;
}

static gboolean
restart_timeout_cb (gpointer user_data)
{
  MetaWaylandImLauncher *launcher = user_data;

  launcher->restart_source_id = 0;

  if (!launcher->shutting_down)
    spawn_fcitx (launcher);

  return G_SOURCE_REMOVE;
}

static void
schedule_restart (MetaWaylandImLauncher *launcher)
{
  guint delay;

  /* A run that lasted long enough to be considered stable resets the
   * penalties; otherwise keep doubling the delay (capped) and give up
   * entirely after a run of quick successive crashes. */
  if (g_get_monotonic_time () - launcher->last_start_us > IM_LAUNCHER_STABLE_RUN_USECS)
    {
      launcher->backoff_seconds = 1;
      launcher->crash_count = 0;
    }
  else if (++launcher->crash_count >= IM_LAUNCHER_MAX_CRASHES)
    {
      g_warning ("fcitx5 crashed %u times in quick succession; giving up",
                 launcher->crash_count);
      return;
    }

  delay = launcher->backoff_seconds;
  launcher->backoff_seconds = MIN (launcher->backoff_seconds * 2,
                                   IM_LAUNCHER_MAX_BACKOFF_SECS);

  g_warning ("fcitx5 crashed; relaunching in %u s", delay);
  launcher->restart_source_id =
    g_timeout_add_seconds (delay, restart_timeout_cb, launcher);
}

static void
on_fcitx_exited (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  MetaWaylandImLauncher *launcher = user_data;
  g_autoptr (GError) error = NULL;
  gboolean finished;

  finished = g_subprocess_wait_finish (G_SUBPROCESS (source), result, &error);

  launcher->wait_pending = FALSE;

  /* meta_wayland_im_launcher_destroy() defers the free to us while a wait
   * is outstanding: cancellation can't unschedule a task that has already
   * resolved, so this callback can still fire (successfully) from a queued
   * completion after teardown. */
  if (launcher->shutting_down)
    {
      g_free (launcher);
      return;
    }

  if (!finished)
    return; /* Cancelled. */

  /* Only a crash (killed by a signal) warrants a relaunch. A normal exit -
   * any status - was fcitx's own decision (e.g. another instance already owns
   * the org.fcitx.Fcitx5 bus name, or a fatal config error) and relaunching
   * would loop on the same outcome. */
  if (!g_subprocess_get_if_signaled (G_SUBPROCESS (source)))
    {
      g_warning ("fcitx5 exited on its own (status %d); not relaunching",
                 g_subprocess_get_if_exited (G_SUBPROCESS (source)) ?
                 g_subprocess_get_exit_status (G_SUBPROCESS (source)) : -1);
      return;
    }

  schedule_restart (launcher);
}

static void
spawn_fcitx (MetaWaylandImLauncher *launcher)
{
  int fds[2];
  g_autoptr (GSubprocessLauncher) sub_launcher = NULL;
  g_autoptr (GError) error = NULL;

  if (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) < 0)
    {
      g_warning ("fcitx5 socketpair failed: %s", g_strerror (errno));
      return;
    }

  sub_launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);
  g_subprocess_launcher_take_fd (sub_launcher, fds[1], 3);
  g_subprocess_launcher_setenv (sub_launcher, "WAYLAND_SOCKET", "3", TRUE);

  /* Make sure fcitx itself never loads client-side IM modules (its own
   * config dialogs would loop input back into itself). cinnamon-session
   * clears these for Wayland sessions anyway; unsetting here keeps the
   * child correct regardless of what we inherited. XMODIFIERS is deliberately
   * NOT in this list: fcitx names its XIM server after it (guess_server_name()
   * in fcitx5's xim.cpp) and XWayland clients look the server up by their own
   * inherited XMODIFIERS, so both must derive from the same session value. */
  {
    static const char * const im_env_vars[] = {
      "GTK_IM_MODULE", "CLUTTER_IM_MODULE", "QT_IM_MODULE",
      "QT_IM_MODULES", "SDL_IM_MODULE",
    };
    guint i;

    for (i = 0; i < G_N_ELEMENTS (im_env_vars); i++)
      g_subprocess_launcher_unsetenv (sub_launcher, im_env_vars[i]);
  }

  g_clear_object (&launcher->proc);
  launcher->proc = g_subprocess_launcher_spawn (sub_launcher, &error,
                                                "fcitx5", NULL);
  if (!launcher->proc)
    {
      g_warning ("Failed to launch fcitx5: %s", error->message);
      close (fds[0]);
      return;
    }

  launcher->last_start_us = g_get_monotonic_time ();

  launcher->wait_pending = TRUE;
  g_subprocess_wait_async (launcher->proc, launcher->cancellable,
                           on_fcitx_exited, launcher);

  /* The destroy-listener struct is reused across respawns; if a previous
   * client is somehow still alive, unlink from it first or the re-add below
   * would corrupt both clients' listener lists. */
  if (launcher->client)
    {
      wl_list_remove (&launcher->client_destroy_listener.link);
      launcher->client = NULL;
    }

  /* wl_client_create takes ownership of fds[0] on success. */
  launcher->client = wl_client_create (launcher->compositor->wayland_display,
                                       fds[0]);
  if (!launcher->client)
    {
      /* fcitx keeps running (it can still serve XWayland clients over XIM),
       * but without this wl_client the privileged globals are never
       * advertised, so no compositor bridge this session. */
      g_warning ("Failed to create a wl_client for fcitx5");
      close (fds[0]);
      return;
    }

  wl_client_add_destroy_listener (launcher->client,
                                  &launcher->client_destroy_listener);
}

MetaWaylandImLauncher *
meta_wayland_im_launcher_new (MetaWaylandCompositor *compositor)
{
  MetaWaylandImLauncher *launcher;

  launcher = g_new0 (MetaWaylandImLauncher, 1);
  launcher->compositor = compositor;
  launcher->cancellable = g_cancellable_new ();
  launcher->backoff_seconds = 1;
  launcher->client_destroy_listener.notify = on_client_destroyed;

  return launcher;
}

void
meta_wayland_im_launcher_start (MetaWaylandImLauncher *launcher)
{
  spawn_fcitx (launcher);
}

void
meta_wayland_im_launcher_destroy (MetaWaylandImLauncher *launcher)
{
  if (!launcher)
    return;

  launcher->shutting_down = TRUE;
  g_cancellable_cancel (launcher->cancellable);
  g_clear_handle_id (&launcher->restart_source_id, g_source_remove);

  /* The client can outlive the launcher (it is torn down with the display);
   * unlink the destroy listener so it can't fire on freed memory. */
  if (launcher->client)
    {
      wl_list_remove (&launcher->client_destroy_listener.link);
      launcher->client = NULL;
    }

  if (launcher->proc)
    {
      g_subprocess_send_signal (launcher->proc, SIGTERM);
      g_clear_object (&launcher->proc);
    }

  g_clear_object (&launcher->cancellable);

  if (launcher->wait_pending)
    return; /* on_fcitx_exited frees the launcher. */

  g_free (launcher);
}

struct wl_client *
meta_wayland_im_launcher_get_client (MetaWaylandImLauncher *launcher)
{
  if (!launcher)
    return NULL;

  return launcher->client;
}
