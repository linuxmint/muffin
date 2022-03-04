/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2015-2017 Red Hat Inc.
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
 *
 */

#include "config.h"

#include "backends/meta-screen-cast.h"

#include <pipewire/pipewire.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-remote-desktop-session.h"
#include "backends/meta-screen-cast-session.h"

#define META_SCREEN_CAST_DBUS_SERVICE "org.gnome.Mutter.ScreenCast"
#define META_SCREEN_CAST_DBUS_PATH "/org/gnome/Mutter/ScreenCast"
#define META_SCREEN_CAST_API_VERSION 3

struct _MetaScreenCast
{
  MetaDBusScreenCastSkeleton parent;

  int dbus_name_id;

  GList *sessions;

  MetaDbusSessionWatcher *session_watcher;
  MetaBackend *backend;
};

static void
meta_screen_cast_init_iface (MetaDBusScreenCastIface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaScreenCast, meta_screen_cast,
                         META_DBUS_TYPE_SCREEN_CAST_SKELETON,
                         G_IMPLEMENT_INTERFACE (META_DBUS_TYPE_SCREEN_CAST,
                                                meta_screen_cast_init_iface))

GDBusConnection *
meta_screen_cast_get_connection (MetaScreenCast *screen_cast)
{
  GDBusInterfaceSkeleton *interface_skeleton =
    G_DBUS_INTERFACE_SKELETON (screen_cast);

  return g_dbus_interface_skeleton_get_connection (interface_skeleton);
}

MetaBackend *
meta_screen_cast_get_backend (MetaScreenCast *screen_cast)
{
  return screen_cast->backend;
}

static gboolean
register_remote_desktop_screen_cast_session (MetaScreenCastSession  *session,
                                             const char             *remote_desktop_session_id,
                                             GError                **error)
{
  MetaScreenCast *screen_cast =
    meta_screen_cast_session_get_screen_cast (session);
  MetaBackend *backend = meta_screen_cast_get_backend (screen_cast);
  MetaRemoteDesktop *remote_desktop = meta_backend_get_remote_desktop (backend);
  MetaRemoteDesktopSession *remote_desktop_session;

  remote_desktop_session =
    meta_remote_desktop_get_session (remote_desktop, remote_desktop_session_id);
  if (!remote_desktop_session)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No remote desktop session found");
      return FALSE;
    }

  if (!meta_remote_desktop_session_register_screen_cast (remote_desktop_session,
                                                         session,
                                                         error))
    return FALSE;

  return TRUE;
}

static void
on_session_closed (MetaScreenCastSession *session,
                   MetaScreenCast        *screen_cast)
{
  screen_cast->sessions = g_list_remove (screen_cast->sessions, session);
}

static gboolean
handle_create_session (MetaDBusScreenCast    *skeleton,
                       GDBusMethodInvocation *invocation,
                       GVariant              *properties)
{
  MetaScreenCast *screen_cast = META_SCREEN_CAST (skeleton);
  const char *peer_name;
  MetaScreenCastSession *session;
  GError *error = NULL;
  const char *session_path;
  const char *client_dbus_name;
  char *remote_desktop_session_id = NULL;
  gboolean disable_animations;
  MetaScreenCastSessionType session_type;

  g_variant_lookup (properties, "remote-desktop-session-id", "s",
                    &remote_desktop_session_id);

  if (remote_desktop_session_id)
    session_type = META_SCREEN_CAST_SESSION_TYPE_REMOTE_DESKTOP;
  else
    session_type = META_SCREEN_CAST_SESSION_TYPE_NORMAL;

  peer_name = g_dbus_method_invocation_get_sender (invocation);
  session = meta_screen_cast_session_new (screen_cast,
                                          session_type,
                                          peer_name,
                                          &error);
  if (!session)
    {
      g_warning ("Failed to create screen cast session: %s",
                 error->message);

      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Failed to create session: %s",
                                             error->message);
      g_error_free (error);

      return TRUE;
    }

  if (remote_desktop_session_id)
    {
      if (!register_remote_desktop_screen_cast_session (session,
                                                        remote_desktop_session_id,
                                                        &error))
        {
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                 G_DBUS_ERROR_FAILED,
                                                 "%s", error->message);
          g_error_free (error);
          g_object_unref (session);
          return TRUE;
        }
    }

  if (g_variant_lookup (properties, "disable-animations", "b",
                        &disable_animations))
    {
      meta_screen_cast_session_set_disable_animations (session,
                                                       disable_animations);
    }

  client_dbus_name = g_dbus_method_invocation_get_sender (invocation);
  meta_dbus_session_watcher_watch_session (screen_cast->session_watcher,
                                           client_dbus_name,
                                           META_DBUS_SESSION (session));

  session_path = meta_screen_cast_session_get_object_path (session);
  meta_dbus_screen_cast_complete_create_session (skeleton,
                                                 invocation,
                                                 session_path);

  screen_cast->sessions = g_list_append (screen_cast->sessions, session);

  g_signal_connect (session, "session-closed",
                    G_CALLBACK (on_session_closed),
                    screen_cast);

  return TRUE;
}

static void
meta_screen_cast_init_iface (MetaDBusScreenCastIface *iface)
{
  iface->handle_create_session = handle_create_session;
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
  MetaScreenCast *screen_cast = user_data;
  GDBusInterfaceSkeleton *interface_skeleton =
    G_DBUS_INTERFACE_SKELETON (screen_cast);
  GError *error = NULL;

  if (!g_dbus_interface_skeleton_export (interface_skeleton,
                                         connection,
                                         META_SCREEN_CAST_DBUS_PATH,
                                         &error))
    g_warning ("Failed to export remote desktop object: %s\n", error->message);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const char      *name,
                  gpointer         user_data)
{
  g_info ("Acquired name %s\n", name);
}

static void
on_name_lost (GDBusConnection *connection,
              const char      *name,
              gpointer         user_data)
{
  g_warning ("Lost or failed to acquire name %s\n", name);
}

static void
meta_screen_cast_constructed (GObject *object)
{
  MetaScreenCast *screen_cast = META_SCREEN_CAST (object);

  screen_cast->dbus_name_id =
    g_bus_own_name (G_BUS_TYPE_SESSION,
                    META_SCREEN_CAST_DBUS_SERVICE,
                    G_BUS_NAME_OWNER_FLAGS_NONE,
                    on_bus_acquired,
                    on_name_acquired,
                    on_name_lost,
                    screen_cast,
                    NULL);
}

static void
meta_screen_cast_finalize (GObject *object)
{
  MetaScreenCast *screen_cast = META_SCREEN_CAST (object);

  if (screen_cast->dbus_name_id)
    g_bus_unown_name (screen_cast->dbus_name_id);

  while (screen_cast->sessions)
    {
      MetaScreenCastSession *session = screen_cast->sessions->data;

      meta_screen_cast_session_close (session);
    }

  G_OBJECT_CLASS (meta_screen_cast_parent_class)->finalize (object);
}

MetaScreenCast *
meta_screen_cast_new (MetaBackend            *backend,
                      MetaDbusSessionWatcher *session_watcher)
{
  MetaScreenCast *screen_cast;

  screen_cast = g_object_new (META_TYPE_SCREEN_CAST, NULL);
  screen_cast->backend = backend;
  screen_cast->session_watcher = session_watcher;

  return screen_cast;
}


static void
meta_screen_cast_init (MetaScreenCast *screen_cast)
{
  static gboolean is_pipewire_initialized = FALSE;

  if (!is_pipewire_initialized)
    {
      pw_init (NULL, NULL);
      is_pipewire_initialized = TRUE;
    }

  meta_dbus_screen_cast_set_version (META_DBUS_SCREEN_CAST (screen_cast),
                                     META_SCREEN_CAST_API_VERSION);
}

static void
meta_screen_cast_class_init (MetaScreenCastClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_screen_cast_constructed;
  object_class->finalize = meta_screen_cast_finalize;
}
