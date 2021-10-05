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

#include "backends/meta-remote-desktop-session.h"

#include <linux/input.h>
#include <xkbcommon/xkbcommon.h>
#include <stdlib.h>

#include "backends/meta-dbus-session-watcher.h"
#include "backends/meta-screen-cast-session.h"
#include "backends/meta-remote-access-controller-private.h"
#include "backends/x11/meta-backend-x11.h"
#include "cogl/cogl.h"
#include "meta/meta-backend.h"

#include "meta-dbus-remote-desktop.h"

#define META_REMOTE_DESKTOP_SESSION_DBUS_PATH "/org/gnome/Mutter/RemoteDesktop/Session"

enum _MetaRemoteDesktopNotifyAxisFlags
{
  META_REMOTE_DESKTOP_NOTIFY_AXIS_FLAGS_FINISH = 1 << 0,
} MetaRemoteDesktopNotifyAxisFlags;

struct _MetaRemoteDesktopSession
{
  MetaDBusRemoteDesktopSessionSkeleton parent;

  char *peer_name;

  char *session_id;
  char *object_path;

  MetaScreenCastSession *screen_cast_session;
  gulong screen_cast_session_closed_handler_id;
  guint started : 1;

  ClutterVirtualInputDevice *virtual_pointer;
  ClutterVirtualInputDevice *virtual_keyboard;
  ClutterVirtualInputDevice *virtual_touchscreen;

  MetaRemoteDesktopSessionHandle *handle;
};

static void
meta_remote_desktop_session_init_iface (MetaDBusRemoteDesktopSessionIface *iface);

static void
meta_dbus_session_init_iface (MetaDbusSessionInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaRemoteDesktopSession,
                         meta_remote_desktop_session,
                         META_DBUS_TYPE_REMOTE_DESKTOP_SESSION_SKELETON,
                         G_IMPLEMENT_INTERFACE (META_DBUS_TYPE_REMOTE_DESKTOP_SESSION,
                                                meta_remote_desktop_session_init_iface)
                         G_IMPLEMENT_INTERFACE (META_TYPE_DBUS_SESSION,
                                                meta_dbus_session_init_iface))

struct _MetaRemoteDesktopSessionHandle
{
  MetaRemoteAccessHandle parent;

  MetaRemoteDesktopSession *session;
};

G_DEFINE_TYPE (MetaRemoteDesktopSessionHandle,
               meta_remote_desktop_session_handle,
               META_TYPE_REMOTE_ACCESS_HANDLE)

static MetaRemoteDesktopSessionHandle *
meta_remote_desktop_session_handle_new (MetaRemoteDesktopSession *session);

static gboolean
meta_remote_desktop_session_is_running (MetaRemoteDesktopSession *session)
{
  return !!session->virtual_pointer;
}

static void
init_remote_access_handle (MetaRemoteDesktopSession *session)
{
  MetaBackend *backend = meta_get_backend ();
  MetaRemoteAccessController *remote_access_controller;
  MetaRemoteAccessHandle *remote_access_handle;

  session->handle = meta_remote_desktop_session_handle_new (session);

  remote_access_controller = meta_backend_get_remote_access_controller (backend);
  remote_access_handle = META_REMOTE_ACCESS_HANDLE (session->handle);
  meta_remote_access_controller_notify_new_handle (remote_access_controller,
                                                   remote_access_handle);
}

static gboolean
meta_remote_desktop_session_start (MetaRemoteDesktopSession *session,
                                   GError                  **error)
{
  ClutterBackend *backend = clutter_get_default_backend ();
  ClutterSeat *seat = clutter_backend_get_default_seat (backend);

  g_assert (!session->started);

  if (session->screen_cast_session)
    {
      if (!meta_screen_cast_session_start (session->screen_cast_session, error))
        return FALSE;
    }

  session->virtual_pointer =
    clutter_seat_create_virtual_device (seat, CLUTTER_POINTER_DEVICE);
  session->virtual_keyboard =
    clutter_seat_create_virtual_device (seat, CLUTTER_KEYBOARD_DEVICE);
  session->virtual_touchscreen =
    clutter_seat_create_virtual_device (seat, CLUTTER_TOUCHSCREEN_DEVICE);

  init_remote_access_handle (session);
  session->started = TRUE;

  return TRUE;
}

void
meta_remote_desktop_session_close (MetaRemoteDesktopSession *session)
{
  MetaDBusRemoteDesktopSession *skeleton =
    META_DBUS_REMOTE_DESKTOP_SESSION (session);

  session->started = FALSE;

  if (session->screen_cast_session)
    {
      g_clear_signal_handler (&session->screen_cast_session_closed_handler_id,
                              session->screen_cast_session);
      meta_screen_cast_session_close (session->screen_cast_session);
      session->screen_cast_session = NULL;
    }

  g_clear_object (&session->virtual_pointer);
  g_clear_object (&session->virtual_keyboard);
  g_clear_object (&session->virtual_touchscreen);

  meta_dbus_session_notify_closed (META_DBUS_SESSION (session));
  meta_dbus_remote_desktop_session_emit_closed (skeleton);
  g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (session));

  if (session->handle)
    {
      MetaRemoteAccessHandle *remote_access_handle =
        META_REMOTE_ACCESS_HANDLE (session->handle);

      meta_remote_access_handle_notify_stopped (remote_access_handle);
    }

  g_object_unref (session);
}

char *
meta_remote_desktop_session_get_object_path (MetaRemoteDesktopSession *session)
{
  return session->object_path;
}

char *
meta_remote_desktop_session_get_session_id (MetaRemoteDesktopSession *session)
{
  return session->session_id;
}

static void
on_screen_cast_session_closed (MetaScreenCastSession    *screen_cast_session,
                               MetaRemoteDesktopSession *session)
{
  session->screen_cast_session = NULL;
  meta_remote_desktop_session_close (session);
}

gboolean
meta_remote_desktop_session_register_screen_cast (MetaRemoteDesktopSession  *session,
                                                  MetaScreenCastSession     *screen_cast_session,
                                                  GError                   **error)
{
  if (session->screen_cast_session)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Remote desktop session already have an associated "
                   "screen cast session");
      return FALSE;
    }

  session->screen_cast_session = screen_cast_session;
  session->screen_cast_session_closed_handler_id =
    g_signal_connect (screen_cast_session, "session-closed",
                      G_CALLBACK (on_screen_cast_session_closed),
                      session);

  return TRUE;
}

MetaRemoteDesktopSession *
meta_remote_desktop_session_new (MetaRemoteDesktop  *remote_desktop,
                                 const char         *peer_name,
                                 GError            **error)
{
  GDBusInterfaceSkeleton *interface_skeleton;
  MetaRemoteDesktopSession *session;
  GDBusConnection *connection;

  session = g_object_new (META_TYPE_REMOTE_DESKTOP_SESSION, NULL);

  session->peer_name = g_strdup (peer_name);

  interface_skeleton = G_DBUS_INTERFACE_SKELETON (session);
  connection = meta_remote_desktop_get_connection (remote_desktop);
  if (!g_dbus_interface_skeleton_export (interface_skeleton,
                                         connection,
                                         session->object_path,
                                         error))
    {
      g_object_unref (session);
      return NULL;
    }

  return session;
}

static gboolean
check_permission (MetaRemoteDesktopSession *session,
                  GDBusMethodInvocation    *invocation)
{
  return g_strcmp0 (session->peer_name,
                    g_dbus_method_invocation_get_sender (invocation)) == 0;
}

static gboolean
meta_remote_desktop_session_check_can_notify (MetaRemoteDesktopSession *session,
                                              GDBusMethodInvocation    *invocation)
{
  if (!session->started)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Session not started");
      return FALSE;
    }

  if (!check_permission (session, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return FALSE;
    }

  return TRUE;
}

static gboolean
handle_start (MetaDBusRemoteDesktopSession *skeleton,
              GDBusMethodInvocation        *invocation)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);
  GError *error = NULL;

  if (session->started)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Already started");
      return TRUE;
    }

  if (!check_permission (session, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return TRUE;
    }

  if (!meta_remote_desktop_session_start (session, &error))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Failed to start remote desktop: %s",
                                             error->message);
      g_error_free (error);

      meta_remote_desktop_session_close (session);

      return TRUE;
    }

  meta_dbus_remote_desktop_session_complete_start (skeleton, invocation);

  return TRUE;
}

static gboolean
handle_stop (MetaDBusRemoteDesktopSession *skeleton,
             GDBusMethodInvocation        *invocation)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);

  if (!session->started)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Session not started");
      return TRUE;
    }

  if (!check_permission (session, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return TRUE;
    }

  meta_remote_desktop_session_close (session);

  meta_dbus_remote_desktop_session_complete_stop (skeleton, invocation);

  return TRUE;
}

static gboolean
handle_notify_keyboard_keycode (MetaDBusRemoteDesktopSession *skeleton,
                                GDBusMethodInvocation        *invocation,
                                unsigned int                  keycode,
                                gboolean                      pressed)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);
  ClutterKeyState state;

  if (!meta_remote_desktop_session_check_can_notify (session, invocation))
    return TRUE;

  if (pressed)
    state = CLUTTER_KEY_STATE_PRESSED;
  else
    state = CLUTTER_KEY_STATE_RELEASED;

  clutter_virtual_input_device_notify_key (session->virtual_keyboard,
                                           CLUTTER_CURRENT_TIME,
                                           keycode,
                                           state);

  meta_dbus_remote_desktop_session_complete_notify_keyboard_keycode (skeleton,
                                                                     invocation);
  return TRUE;
}

static gboolean
handle_notify_keyboard_keysym (MetaDBusRemoteDesktopSession *skeleton,
                               GDBusMethodInvocation        *invocation,
                               unsigned int                  keysym,
                               gboolean                      pressed)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);
  ClutterKeyState state;

  if (!meta_remote_desktop_session_check_can_notify (session, invocation))
    return TRUE;

  if (pressed)
    state = CLUTTER_KEY_STATE_PRESSED;
  else
    state = CLUTTER_KEY_STATE_RELEASED;

  clutter_virtual_input_device_notify_keyval (session->virtual_keyboard,
                                              CLUTTER_CURRENT_TIME,
                                              keysym,
                                              state);

  meta_dbus_remote_desktop_session_complete_notify_keyboard_keysym (skeleton,
                                                                    invocation);
  return TRUE;
}

/* Translation taken from the clutter evdev backend. */
static int
translate_to_clutter_button (int button)
{
  switch (button)
    {
    case BTN_LEFT:
      return CLUTTER_BUTTON_PRIMARY;
    case BTN_RIGHT:
      return CLUTTER_BUTTON_SECONDARY;
    case BTN_MIDDLE:
      return CLUTTER_BUTTON_MIDDLE;
    default:
      /*
       * For compatibility reasons, all additional buttons go after the old
       * 4-7 scroll ones.
       */
      return button - (BTN_LEFT - 1) + 4;
    }
}

static gboolean
handle_notify_pointer_button (MetaDBusRemoteDesktopSession *skeleton,
                              GDBusMethodInvocation        *invocation,
                              int                           button_code,
                              gboolean                      pressed)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);
  uint32_t button;
  ClutterButtonState state;

  if (!meta_remote_desktop_session_check_can_notify (session, invocation))
    return TRUE;

  button = translate_to_clutter_button (button_code);

  if (pressed)
    state = CLUTTER_BUTTON_STATE_PRESSED;
  else
    state = CLUTTER_BUTTON_STATE_RELEASED;

  clutter_virtual_input_device_notify_button (session->virtual_pointer,
                                              CLUTTER_CURRENT_TIME,
                                              button,
                                              state);

  meta_dbus_remote_desktop_session_complete_notify_pointer_button (skeleton,
                                                                   invocation);

  return TRUE;
}

static gboolean
handle_notify_pointer_axis (MetaDBusRemoteDesktopSession *skeleton,
                            GDBusMethodInvocation        *invocation,
                            double                        dx,
                            double                        dy,
                            uint32_t                      flags)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);
  ClutterScrollFinishFlags finish_flags = CLUTTER_SCROLL_FINISHED_NONE;

  if (!meta_remote_desktop_session_check_can_notify (session, invocation))
    return TRUE;

  if (flags & META_REMOTE_DESKTOP_NOTIFY_AXIS_FLAGS_FINISH)
    {
      finish_flags |= (CLUTTER_SCROLL_FINISHED_HORIZONTAL |
                       CLUTTER_SCROLL_FINISHED_VERTICAL);
    }

  clutter_virtual_input_device_notify_scroll_continuous (session->virtual_pointer,
                                                         CLUTTER_CURRENT_TIME,
                                                         dx, dy,
                                                         CLUTTER_SCROLL_SOURCE_FINGER,
                                                         finish_flags);

  meta_dbus_remote_desktop_session_complete_notify_pointer_axis (skeleton,
                                                                 invocation);

  return TRUE;
}

static ClutterScrollDirection
discrete_steps_to_scroll_direction (unsigned int axis,
                                    int          steps)
{
  if (axis == 0 && steps < 0)
    return CLUTTER_SCROLL_UP;
  if (axis == 0 && steps > 0)
    return CLUTTER_SCROLL_DOWN;
  if (axis == 1 && steps < 0)
    return CLUTTER_SCROLL_LEFT;
  if (axis == 1 && steps > 0)
    return CLUTTER_SCROLL_RIGHT;

  g_assert_not_reached ();
  return 0;
}

static gboolean
handle_notify_pointer_axis_discrete (MetaDBusRemoteDesktopSession *skeleton,
                                     GDBusMethodInvocation        *invocation,
                                     unsigned int                  axis,
                                     int                           steps)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);
  ClutterScrollDirection direction;
  int step_count;

  if (!meta_remote_desktop_session_check_can_notify (session, invocation))
    return TRUE;

  if (axis > 1)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Invalid axis value");
      return TRUE;
    }

  if (steps == 0)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Invalid axis steps value");
      return TRUE;
    }

  /*
   * We don't have the actual scroll source, but only know they should be
   * considered as discrete steps. The device that produces such scroll events
   * is the scroll wheel, so pretend that is the scroll source.
   */
  direction = discrete_steps_to_scroll_direction (axis, steps);

  for (step_count = 0; step_count < abs (steps); step_count++)
    clutter_virtual_input_device_notify_discrete_scroll (session->virtual_pointer,
                                                         CLUTTER_CURRENT_TIME,
                                                         direction,
                                                         CLUTTER_SCROLL_SOURCE_WHEEL);

  meta_dbus_remote_desktop_session_complete_notify_pointer_axis_discrete (skeleton,
                                                                          invocation);

  return TRUE;
}

static gboolean
handle_notify_pointer_motion_relative (MetaDBusRemoteDesktopSession *skeleton,
                                       GDBusMethodInvocation        *invocation,
                                       double                        dx,
                                       double                        dy)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);

  if (!meta_remote_desktop_session_check_can_notify (session, invocation))
    return TRUE;

  clutter_virtual_input_device_notify_relative_motion (session->virtual_pointer,
                                                       CLUTTER_CURRENT_TIME,
                                                       dx, dy);

  meta_dbus_remote_desktop_session_complete_notify_pointer_motion_relative (skeleton,
                                                                            invocation);

  return TRUE;
}

static gboolean
handle_notify_pointer_motion_absolute (MetaDBusRemoteDesktopSession *skeleton,
                                       GDBusMethodInvocation        *invocation,
                                       const char                   *stream_path,
                                       double                        x,
                                       double                        y)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);
  MetaScreenCastStream *stream;
  double abs_x, abs_y;

  if (!meta_remote_desktop_session_check_can_notify (session, invocation))
    return TRUE;


  if (!session->screen_cast_session)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "No screen cast active");
      return TRUE;
    }

  stream = meta_screen_cast_session_get_stream (session->screen_cast_session,
                                                stream_path);
  if (!stream)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Unknown stream");
      return TRUE;
    }

  meta_screen_cast_stream_transform_position (stream, x, y, &abs_x, &abs_y);

  clutter_virtual_input_device_notify_absolute_motion (session->virtual_pointer,
                                                       CLUTTER_CURRENT_TIME,
                                                       abs_x, abs_y);

  meta_dbus_remote_desktop_session_complete_notify_pointer_motion_absolute (skeleton,
                                                                            invocation);

  return TRUE;
}

static gboolean
handle_notify_touch_down (MetaDBusRemoteDesktopSession *skeleton,
                          GDBusMethodInvocation        *invocation,
                          const char                   *stream_path,
                          unsigned int                  slot,
                          double                        x,
                          double                        y)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);
  MetaScreenCastStream *stream;
  double abs_x, abs_y;

  if (!meta_remote_desktop_session_check_can_notify (session, invocation))
    return TRUE;

  if (!session->screen_cast_session)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "No screen cast active");
      return TRUE;
    }

  stream = meta_screen_cast_session_get_stream (session->screen_cast_session,
                                                stream_path);
  if (!stream)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Unknown stream");
      return TRUE;
    }

  meta_screen_cast_stream_transform_position (stream, x, y, &abs_x, &abs_y);

  clutter_virtual_input_device_notify_touch_down (session->virtual_touchscreen,
                                                  CLUTTER_CURRENT_TIME,
                                                  slot,
                                                  abs_x, abs_y);

  meta_dbus_remote_desktop_session_complete_notify_touch_down (skeleton,
                                                               invocation);

  return TRUE;
}

static gboolean
handle_notify_touch_motion (MetaDBusRemoteDesktopSession *skeleton,
                            GDBusMethodInvocation        *invocation,
                            const char                   *stream_path,
                            unsigned int                  slot,
                            double                        x,
                            double                        y)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);
  MetaScreenCastStream *stream;
  double abs_x, abs_y;

  if (!meta_remote_desktop_session_check_can_notify (session, invocation))
    return TRUE;


  if (!session->screen_cast_session)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "No screen cast active");
      return TRUE;
    }

  stream = meta_screen_cast_session_get_stream (session->screen_cast_session,
                                                stream_path);
  if (!stream)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Unknown stream");
      return TRUE;
    }

  meta_screen_cast_stream_transform_position (stream, x, y, &abs_x, &abs_y);

  clutter_virtual_input_device_notify_touch_motion (session->virtual_touchscreen,
                                                    CLUTTER_CURRENT_TIME,
                                                    slot,
                                                    abs_x, abs_y);

  meta_dbus_remote_desktop_session_complete_notify_touch_motion (skeleton,
                                                                 invocation);

  return TRUE;
}

static gboolean
handle_notify_touch_up (MetaDBusRemoteDesktopSession *skeleton,
                          GDBusMethodInvocation        *invocation,
                          unsigned int                  slot)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);

  if (!meta_remote_desktop_session_check_can_notify (session, invocation))
    return TRUE;

  clutter_virtual_input_device_notify_touch_up (session->virtual_touchscreen,
                                                       CLUTTER_CURRENT_TIME,
                                                       slot);

  meta_dbus_remote_desktop_session_complete_notify_touch_up (skeleton,
                                                             invocation);

  return TRUE;
}

static void
meta_remote_desktop_session_init_iface (MetaDBusRemoteDesktopSessionIface *iface)
{
  iface->handle_start = handle_start;
  iface->handle_stop = handle_stop;
  iface->handle_notify_keyboard_keycode = handle_notify_keyboard_keycode;
  iface->handle_notify_keyboard_keysym = handle_notify_keyboard_keysym;
  iface->handle_notify_pointer_button = handle_notify_pointer_button;
  iface->handle_notify_pointer_axis = handle_notify_pointer_axis;
  iface->handle_notify_pointer_axis_discrete = handle_notify_pointer_axis_discrete;
  iface->handle_notify_pointer_motion_relative = handle_notify_pointer_motion_relative;
  iface->handle_notify_pointer_motion_absolute = handle_notify_pointer_motion_absolute;
  iface->handle_notify_touch_down = handle_notify_touch_down;
  iface->handle_notify_touch_motion = handle_notify_touch_motion;
  iface->handle_notify_touch_up = handle_notify_touch_up;
}

static void
meta_remote_desktop_session_client_vanished (MetaDbusSession *dbus_session)
{
  meta_remote_desktop_session_close (META_REMOTE_DESKTOP_SESSION (dbus_session));
}

static void
meta_dbus_session_init_iface (MetaDbusSessionInterface *iface)
{
  iface->client_vanished = meta_remote_desktop_session_client_vanished;
}

static void
meta_remote_desktop_session_finalize (GObject *object)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (object);

  g_assert (!meta_remote_desktop_session_is_running (session));

  g_clear_object (&session->handle);
  g_free (session->peer_name);
  g_free (session->session_id);
  g_free (session->object_path);

  G_OBJECT_CLASS (meta_remote_desktop_session_parent_class)->finalize (object);
}

static void
meta_remote_desktop_session_init (MetaRemoteDesktopSession *session)
{
  MetaDBusRemoteDesktopSession *skeleton =
    META_DBUS_REMOTE_DESKTOP_SESSION  (session);
  GRand *rand;
  static unsigned int global_session_number = 0;

  rand = g_rand_new ();
  session->session_id = meta_generate_random_id (rand, 32);
  g_rand_free (rand);

  meta_dbus_remote_desktop_session_set_session_id (skeleton, session->session_id);

  session->object_path =
    g_strdup_printf (META_REMOTE_DESKTOP_SESSION_DBUS_PATH "/u%u",
                     ++global_session_number);
}

static void
meta_remote_desktop_session_class_init (MetaRemoteDesktopSessionClass *klass)
{

  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_remote_desktop_session_finalize;
}

static MetaRemoteDesktopSessionHandle *
meta_remote_desktop_session_handle_new (MetaRemoteDesktopSession *session)
{
  MetaRemoteDesktopSessionHandle *handle;

  handle = g_object_new (META_TYPE_REMOTE_DESKTOP_SESSION_HANDLE, NULL);
  handle->session = session;

  return handle;
}

static void
meta_remote_desktop_session_handle_stop (MetaRemoteAccessHandle *handle)
{
  MetaRemoteDesktopSession *session;

  session = META_REMOTE_DESKTOP_SESSION_HANDLE (handle)->session;
  if (!session)
    return;

  meta_remote_desktop_session_close (session);
}

static void
meta_remote_desktop_session_handle_init (MetaRemoteDesktopSessionHandle *handle)
{
}

static void
meta_remote_desktop_session_handle_class_init (MetaRemoteDesktopSessionHandleClass *klass)
{
  MetaRemoteAccessHandleClass *remote_access_handle_class =
    META_REMOTE_ACCESS_HANDLE_CLASS (klass);

  remote_access_handle_class->stop = meta_remote_desktop_session_handle_stop;
}
