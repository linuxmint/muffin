/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2017 Red Hat Inc.
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

#include "backends/meta-screen-cast-monitor-stream.h"

#include "backends/meta-logical-monitor.h"
#include "backends/meta-screen-cast-monitor-stream-src.h"

enum
{
  PROP_0,

  PROP_MONITOR,
};

struct _MetaScreenCastMonitorStream
{
  MetaScreenCastStream parent;

  ClutterStage *stage;

  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;
};

G_DEFINE_TYPE (MetaScreenCastMonitorStream,
               meta_screen_cast_monitor_stream,
               META_TYPE_SCREEN_CAST_STREAM)

static gboolean
update_monitor (MetaScreenCastMonitorStream *monitor_stream,
                MetaMonitor                 *new_monitor)
{
  MetaLogicalMonitor *new_logical_monitor;

  new_logical_monitor = meta_monitor_get_logical_monitor (new_monitor);
  if (!new_logical_monitor)
    return FALSE;

  if (!meta_rectangle_equal (&new_logical_monitor->rect,
                             &monitor_stream->logical_monitor->rect))
    return FALSE;

  g_set_object (&monitor_stream->monitor, new_monitor);
  g_set_object (&monitor_stream->logical_monitor, new_logical_monitor);

  return TRUE;
}

static void
on_monitors_changed (MetaMonitorManager          *monitor_manager,
                     MetaScreenCastMonitorStream *monitor_stream)
{
  MetaMonitor *new_monitor = NULL;
  GList *monitors;
  GList *l;

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  for (l = monitors; l; l = l->next)
    {
      MetaMonitor *other_monitor = l->data;

      if (meta_monitor_is_same_as (monitor_stream->monitor, other_monitor))
        {
          new_monitor = other_monitor;
          break;
        }
    }

  if (!new_monitor || !update_monitor (monitor_stream, new_monitor))
    meta_screen_cast_stream_close (META_SCREEN_CAST_STREAM (monitor_stream));
}

ClutterStage *
meta_screen_cast_monitor_stream_get_stage (MetaScreenCastMonitorStream *monitor_stream)
{
  return monitor_stream->stage;
}

MetaMonitor *
meta_screen_cast_monitor_stream_get_monitor (MetaScreenCastMonitorStream *monitor_stream)
{
  return monitor_stream->monitor;
}

MetaScreenCastMonitorStream *
meta_screen_cast_monitor_stream_new (MetaScreenCastSession     *session,
                                     GDBusConnection           *connection,
                                     MetaMonitor               *monitor,
                                     ClutterStage              *stage,
                                     MetaScreenCastCursorMode   cursor_mode,
                                     GError                   **error)
{
  MetaGpu *gpu = meta_monitor_get_gpu (monitor);
  MetaBackend *backend = meta_gpu_get_backend (gpu);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaScreenCastMonitorStream *monitor_stream;

  if (!meta_monitor_is_active (monitor))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Monitor not active");
      return NULL;
    }

  monitor_stream = g_initable_new (META_TYPE_SCREEN_CAST_MONITOR_STREAM,
                                   NULL,
                                   error,
                                   "session", session,
                                   "connection", connection,
                                   "cursor-mode", cursor_mode,
                                   "monitor", monitor,
                                   NULL);
  if (!monitor_stream)
    return NULL;

  monitor_stream->stage = stage;

  g_signal_connect_object (monitor_manager, "monitors-changed-internal",
                           G_CALLBACK (on_monitors_changed),
                           monitor_stream, 0);

  return monitor_stream;
}

static MetaScreenCastStreamSrc *
meta_screen_cast_monitor_stream_create_src (MetaScreenCastStream  *stream,
                                            GError               **error)
{
  MetaScreenCastMonitorStream *monitor_stream =
    META_SCREEN_CAST_MONITOR_STREAM (stream);
  MetaScreenCastMonitorStreamSrc *monitor_stream_src;

  monitor_stream_src = meta_screen_cast_monitor_stream_src_new (monitor_stream,
                                                                error);
  if (!monitor_stream_src)
    return NULL;

  return META_SCREEN_CAST_STREAM_SRC (monitor_stream_src);
}

static void
meta_screen_cast_monitor_stream_set_parameters (MetaScreenCastStream *stream,
                                                GVariantBuilder      *parameters_builder)
{
  MetaScreenCastMonitorStream *monitor_stream =
    META_SCREEN_CAST_MONITOR_STREAM (stream);
  MetaRectangle logical_monitor_layout;

  logical_monitor_layout =
    meta_logical_monitor_get_layout (monitor_stream->logical_monitor);

  g_variant_builder_add (parameters_builder, "{sv}",
                         "position",
                         g_variant_new ("(ii)",
                                        logical_monitor_layout.x,
                                        logical_monitor_layout.y));
  g_variant_builder_add (parameters_builder, "{sv}",
                         "size",
                         g_variant_new ("(ii)",
                                        logical_monitor_layout.width,
                                        logical_monitor_layout.height));
}

static void
meta_screen_cast_monitor_stream_transform_position (MetaScreenCastStream *stream,
                                                    double                stream_x,
                                                    double                stream_y,
                                                    double               *x,
                                                    double               *y)
{
  MetaScreenCastMonitorStream *monitor_stream =
    META_SCREEN_CAST_MONITOR_STREAM (stream);
  MetaRectangle logical_monitor_layout;

  logical_monitor_layout =
    meta_logical_monitor_get_layout (monitor_stream->logical_monitor);

  *x = logical_monitor_layout.x + stream_x;
  *y = logical_monitor_layout.y + stream_y;
}

static void
meta_screen_cast_monitor_stream_set_property (GObject      *object,
                                              guint         prop_id,
                                              const GValue *value,
                                              GParamSpec   *pspec)
{
  MetaScreenCastMonitorStream *monitor_stream =
    META_SCREEN_CAST_MONITOR_STREAM (object);
  MetaLogicalMonitor *logical_monitor;

  switch (prop_id)
    {
    case PROP_MONITOR:
      g_set_object (&monitor_stream->monitor, g_value_get_object (value));
      logical_monitor = meta_monitor_get_logical_monitor (monitor_stream->monitor);
      g_set_object (&monitor_stream->logical_monitor, logical_monitor);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_screen_cast_monitor_stream_get_property (GObject    *object,
                                              guint       prop_id,
                                              GValue     *value,
                                              GParamSpec *pspec)
{
  MetaScreenCastMonitorStream *monitor_stream =
    META_SCREEN_CAST_MONITOR_STREAM (object);

  switch (prop_id)
    {
    case PROP_MONITOR:
      g_value_set_object (value, monitor_stream->monitor);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_screen_cast_monitor_stream_finalize (GObject *object)
{
  MetaScreenCastMonitorStream *monitor_stream =
    META_SCREEN_CAST_MONITOR_STREAM (object);

  g_clear_object (&monitor_stream->monitor);
  g_clear_object (&monitor_stream->logical_monitor);

  G_OBJECT_CLASS (meta_screen_cast_monitor_stream_parent_class)->finalize (object);
}

static void
meta_screen_cast_monitor_stream_init (MetaScreenCastMonitorStream *monitor_stream)
{
}

static void
meta_screen_cast_monitor_stream_class_init (MetaScreenCastMonitorStreamClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaScreenCastStreamClass *stream_class =
    META_SCREEN_CAST_STREAM_CLASS (klass);

  object_class->set_property = meta_screen_cast_monitor_stream_set_property;
  object_class->get_property = meta_screen_cast_monitor_stream_get_property;
  object_class->finalize = meta_screen_cast_monitor_stream_finalize;

  stream_class->create_src = meta_screen_cast_monitor_stream_create_src;
  stream_class->set_parameters = meta_screen_cast_monitor_stream_set_parameters;
  stream_class->transform_position = meta_screen_cast_monitor_stream_transform_position;

  g_object_class_install_property (object_class,
                                   PROP_MONITOR,
                                   g_param_spec_object ("monitor",
                                                        "monitor",
                                                        "MetaMonitor",
                                                        META_TYPE_MONITOR,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}
