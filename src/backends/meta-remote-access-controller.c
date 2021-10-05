/*
 * Copyright (C) 2018 Red Hat Inc.
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

#include "backends/meta-remote-access-controller-private.h"

enum
{
  HANDLE_STOPPED,

  N_HANDLE_SIGNALS
};

static int handle_signals[N_HANDLE_SIGNALS];

enum
{
  CONTROLLER_NEW_HANDLE,

  N_CONTROLLER_SIGNALS
};

static int controller_signals[N_CONTROLLER_SIGNALS];

typedef struct _MetaRemoteAccessHandlePrivate
{
  gboolean has_stopped;

  gboolean disable_animations;
} MetaRemoteAccessHandlePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaRemoteAccessHandle,
                            meta_remote_access_handle,
                            G_TYPE_OBJECT)

struct _MetaRemoteAccessController
{
  GObject parent;
};

G_DEFINE_TYPE (MetaRemoteAccessController,
               meta_remote_access_controller,
               G_TYPE_OBJECT)

/**
 * meta_remote_access_handle_stop:
 * @handle: A #MetaRemoteAccessHandle
 *
 * Stop the associated remote access session.
 */
void
meta_remote_access_handle_stop (MetaRemoteAccessHandle *handle)
{
  MetaRemoteAccessHandlePrivate *priv =
    meta_remote_access_handle_get_instance_private (handle);

  if (priv->has_stopped)
    return;

  META_REMOTE_ACCESS_HANDLE_GET_CLASS (handle)->stop (handle);
}

/**
 * meta_remote_access_get_disable_animations:
 * @handle: A #MetaRemoteAccessHandle
 *
 * Returns: %TRUE if the remote access requested that animations should be
 * disabled.
 */
gboolean
meta_remote_access_handle_get_disable_animations (MetaRemoteAccessHandle *handle)
{
  MetaRemoteAccessHandlePrivate *priv =
    meta_remote_access_handle_get_instance_private (handle);

  return priv->disable_animations;
}

void
meta_remote_access_handle_set_disable_animations (MetaRemoteAccessHandle *handle,
                                                  gboolean                disable_animations)
{
  MetaRemoteAccessHandlePrivate *priv =
    meta_remote_access_handle_get_instance_private (handle);

  priv->disable_animations = disable_animations;
}

void
meta_remote_access_handle_notify_stopped (MetaRemoteAccessHandle *handle)
{
  MetaRemoteAccessHandlePrivate *priv =
    meta_remote_access_handle_get_instance_private (handle);

  priv->has_stopped = TRUE;
  g_signal_emit (handle, handle_signals[HANDLE_STOPPED], 0);
}

void
meta_remote_access_controller_notify_new_handle (MetaRemoteAccessController *controller,
                                                 MetaRemoteAccessHandle     *handle)
{
  g_signal_emit (controller, controller_signals[CONTROLLER_NEW_HANDLE], 0,
                 handle);
}

static void
meta_remote_access_handle_init (MetaRemoteAccessHandle *handle)
{
}

static void
meta_remote_access_handle_class_init (MetaRemoteAccessHandleClass *klass)
{
  handle_signals[HANDLE_STOPPED] =
    g_signal_new ("stopped",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
meta_remote_access_controller_init (MetaRemoteAccessController *controller)
{
}

static void
meta_remote_access_controller_class_init (MetaRemoteAccessControllerClass *klass)
{
  controller_signals[CONTROLLER_NEW_HANDLE] =
    g_signal_new ("new-handle",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  META_TYPE_REMOTE_ACCESS_HANDLE);
}
