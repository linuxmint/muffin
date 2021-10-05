/*
 * Copyright (C) 2018 Red Hat
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include <gio/gdesktopappinfo.h>

#include "core/display-private.h"
#include "meta/meta-launch-context.h"
#include "x11/meta-startup-notification-x11.h"

typedef struct _MetaLaunchContext MetaLaunchContext;

struct _MetaLaunchContext
{
  GAppLaunchContext parent_instance;
  MetaDisplay *display;
  MetaWorkspace *workspace;
  uint32_t timestamp;
};

G_DEFINE_TYPE (MetaLaunchContext, meta_launch_context,
               G_TYPE_APP_LAUNCH_CONTEXT)

enum
{
  PROP_DISPLAY = 1,
  PROP_WORKSPACE,
  PROP_TIMESTAMP,
  N_PROPS
};

static GParamSpec *props[N_PROPS] = { 0, };

static void
meta_launch_context_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  MetaLaunchContext *context = META_LAUNCH_CONTEXT (object);

  switch (prop_id)
    {
    case PROP_DISPLAY:
      context->display = g_value_get_object (value);
      break;
    case PROP_WORKSPACE:
      meta_launch_context_set_workspace (context,
                                         g_value_get_object (value));
      break;
    case PROP_TIMESTAMP:
      meta_launch_context_set_timestamp (context,
                                         g_value_get_uint (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_launch_context_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  MetaLaunchContext *context = META_LAUNCH_CONTEXT (object);

  switch (prop_id)
    {
    case PROP_DISPLAY:
      g_value_set_object (value, context->display);
      break;
    case PROP_WORKSPACE:
      g_value_set_object (value, context->workspace);
      break;
    case PROP_TIMESTAMP:
      g_value_set_uint (value, context->timestamp);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_launch_context_finalize (GObject *object)
{
  G_OBJECT_CLASS (meta_launch_context_parent_class)->finalize (object);
}

static void
meta_launch_context_constructed (GObject *object)
{
  MetaLaunchContext *context = META_LAUNCH_CONTEXT (object);
  const char *x11_display, *wayland_display;

  G_OBJECT_CLASS (meta_launch_context_parent_class)->constructed (object);

  x11_display = getenv ("DISPLAY");
  wayland_display = getenv ("WAYLAND_DISPLAY");

  if (x11_display)
    {
      g_app_launch_context_setenv (G_APP_LAUNCH_CONTEXT (context),
                                   "DISPLAY", x11_display);
    }

  if (wayland_display)
    {
      g_app_launch_context_setenv (G_APP_LAUNCH_CONTEXT (context),
                                   "WAYLAND_DISPLAY", wayland_display);
    }
}

static gchar *
meta_launch_context_get_startup_notify_id (GAppLaunchContext *launch_context,
                                           GAppInfo          *info,
                                           GList             *files)
{
  MetaLaunchContext *context = META_LAUNCH_CONTEXT (launch_context);
  MetaDisplay *display = context->display;
  int workspace_idx = -1;
  char *startup_id = NULL;

  if (context->workspace)
    workspace_idx = meta_workspace_index (context->workspace);

  if (display->x11_display)
    {
      /* If there is a X11 display, we prefer going entirely through
       * libsn, as SnMonitor expects to keep a view of the full lifetime
       * of the startup sequence. We can't avoid it when launching and
       * expect that a "remove" message from a X11 client will be handled.
       */
      startup_id =
        meta_x11_startup_notification_launch (display->x11_display,
                                              info,
                                              context->timestamp,
                                              workspace_idx);
    }

  if (!startup_id)
    {
      const char *application_id = NULL;
      MetaStartupNotification *sn;
      MetaStartupSequence *seq;

      startup_id = g_uuid_string_random ();

      /* Fallback through inserting our own startup sequence, this
       * will be enough for wayland clients.
       */
      if (G_IS_DESKTOP_APP_INFO (info))
        {
          application_id =
            g_desktop_app_info_get_filename (G_DESKTOP_APP_INFO (info));
        }

      sn = meta_display_get_startup_notification (context->display);
      seq = g_object_new (META_TYPE_STARTUP_SEQUENCE,
                          "id", startup_id,
                          "application-id", application_id,
                          "name", g_app_info_get_name (info),
                          "workspace", workspace_idx,
                          "timestamp", context->timestamp,
                          NULL);

      meta_startup_notification_add_sequence (sn, seq);
      g_object_unref (seq);
    }

  return startup_id;
}

static void
meta_launch_context_launch_failed (GAppLaunchContext *launch_context,
                                   const gchar       *startup_notify_id)
{
  MetaLaunchContext *context = META_LAUNCH_CONTEXT (launch_context);
  MetaStartupNotification *sn;
  MetaStartupSequence *seq;

  sn = meta_display_get_startup_notification (context->display);
  seq = meta_startup_notification_lookup_sequence (sn, startup_notify_id);

  if (seq)
    {
      meta_startup_sequence_complete (seq);
      meta_startup_notification_remove_sequence (sn, seq);
    }
}

static void
meta_launch_context_class_init (MetaLaunchContextClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GAppLaunchContextClass *ctx_class = G_APP_LAUNCH_CONTEXT_CLASS (klass);

  object_class->finalize = meta_launch_context_finalize;
  object_class->constructed = meta_launch_context_constructed;
  object_class->set_property = meta_launch_context_set_property;
  object_class->get_property = meta_launch_context_get_property;

  ctx_class->get_startup_notify_id = meta_launch_context_get_startup_notify_id;
  ctx_class->launch_failed = meta_launch_context_launch_failed;

  props[PROP_DISPLAY] =
    g_param_spec_object ("display",
                         "display",
                         "Display",
                         META_TYPE_DISPLAY,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  props[PROP_WORKSPACE] =
    g_param_spec_object ("workspace",
                         "workspace",
                         "Workspace",
                         META_TYPE_WORKSPACE,
                         G_PARAM_READWRITE);
  props[PROP_TIMESTAMP] =
    g_param_spec_uint ("timestamp",
                       "timestamp",
                       "Timestamp",
                       0, G_MAXUINT32, 0,
                       G_PARAM_READWRITE);

  g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
meta_launch_context_init (MetaLaunchContext *context)
{
}

void
meta_launch_context_set_workspace (MetaLaunchContext *context,
                                   MetaWorkspace     *workspace)
{
  g_return_if_fail (META_IS_LAUNCH_CONTEXT (context));
  g_return_if_fail (META_IS_WORKSPACE (workspace));

  g_set_object (&context->workspace, workspace);
}

void
meta_launch_context_set_timestamp (MetaLaunchContext *context,
                                   uint32_t           timestamp)
{
  g_return_if_fail (META_IS_LAUNCH_CONTEXT (context));

  context->timestamp = timestamp;
}
