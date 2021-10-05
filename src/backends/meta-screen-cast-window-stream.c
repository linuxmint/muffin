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

#include "backends/meta-screen-cast-window-stream.h"

#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-screen-cast-window.h"
#include "backends/meta-screen-cast-window-stream-src.h"
#include "compositor/meta-window-actor-private.h"
#include "core/window-private.h"

enum
{
  PROP_0,

  PROP_WINDOW,
};

struct _MetaScreenCastWindowStream
{
  MetaScreenCastStream parent;

  MetaWindow *window;

  int stream_width;
  int stream_height;
  int logical_width;
  int logical_height;

  unsigned long window_unmanaged_handler_id;
};

static GInitableIface *initable_parent_iface;

static void
meta_screen_cast_window_stream_init_initable_iface (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaScreenCastWindowStream,
                         meta_screen_cast_window_stream,
                         META_TYPE_SCREEN_CAST_STREAM,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                meta_screen_cast_window_stream_init_initable_iface))

MetaWindow *
meta_screen_cast_window_stream_get_window (MetaScreenCastWindowStream *window_stream)
{
  return window_stream->window;
}

int
meta_screen_cast_window_stream_get_width (MetaScreenCastWindowStream *window_stream)
{
  return window_stream->stream_width;
}

int
meta_screen_cast_window_stream_get_height (MetaScreenCastWindowStream *window_stream)
{
  return window_stream->stream_height;
}

MetaScreenCastWindowStream *
meta_screen_cast_window_stream_new (MetaScreenCastSession     *session,
                                    GDBusConnection           *connection,
                                    MetaWindow                *window,
                                    MetaScreenCastCursorMode   cursor_mode,
                                    GError                   **error)
{
  return g_initable_new (META_TYPE_SCREEN_CAST_WINDOW_STREAM,
                         NULL,
                         error,
                         "session", session,
                         "connection", connection,
                         "cursor-mode", cursor_mode,
                         "window", window,
                         NULL);
}

static MetaScreenCastStreamSrc *
meta_screen_cast_window_stream_create_src (MetaScreenCastStream  *stream,
                                           GError               **error)
{
  MetaScreenCastWindowStream *window_stream =
    META_SCREEN_CAST_WINDOW_STREAM (stream);
  MetaScreenCastWindowStreamSrc *window_stream_src;

  window_stream_src = meta_screen_cast_window_stream_src_new (window_stream,
                                                              error);
  if (!window_stream_src)
    return NULL;

  return META_SCREEN_CAST_STREAM_SRC (window_stream_src);
}

static void
meta_screen_cast_window_stream_set_parameters (MetaScreenCastStream *stream,
                                               GVariantBuilder      *parameters_builder)
{
  MetaScreenCastWindowStream *window_stream =
    META_SCREEN_CAST_WINDOW_STREAM (stream);

  g_variant_builder_add (parameters_builder, "{sv}",
                         "size",
                         g_variant_new ("(ii)",
                                        window_stream->logical_width,
                                        window_stream->logical_height));
}

static void
meta_screen_cast_window_stream_transform_position (MetaScreenCastStream *stream,
                                                   double                stream_x,
                                                   double                stream_y,
                                                   double               *x,
                                                   double               *y)
{
  MetaScreenCastWindowStream *window_stream =
    META_SCREEN_CAST_WINDOW_STREAM (stream);
  MetaScreenCastWindow *screen_cast_window =
    META_SCREEN_CAST_WINDOW (meta_window_actor_from_window (window_stream->window));

  meta_screen_cast_window_transform_relative_position (screen_cast_window,
                                                       stream_x,
                                                       stream_y,
                                                       x,
                                                       y);
}

static void
on_window_unmanaged (MetaScreenCastWindowStream *window_stream)
{
  meta_screen_cast_stream_close (META_SCREEN_CAST_STREAM (window_stream));
}

static void
meta_screen_cast_window_stream_set_property (GObject      *object,
                                             guint         prop_id,
                                             const GValue *value,
                                             GParamSpec   *pspec)
{
  MetaScreenCastWindowStream *window_stream =
    META_SCREEN_CAST_WINDOW_STREAM (object);

  switch (prop_id)
    {
    case PROP_WINDOW:
      window_stream->window = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_screen_cast_window_stream_get_property (GObject    *object,
                                             guint       prop_id,
                                             GValue     *value,
                                             GParamSpec *pspec)
{
  MetaScreenCastWindowStream *window_stream =
    META_SCREEN_CAST_WINDOW_STREAM (object);

  switch (prop_id)
    {
    case PROP_WINDOW:
      g_value_set_object (value, window_stream->window);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_screen_cast_window_stream_finalize (GObject *object)
{
  MetaScreenCastWindowStream *window_stream =
    META_SCREEN_CAST_WINDOW_STREAM (object);

  g_clear_signal_handler (&window_stream->window_unmanaged_handler_id,
                          window_stream->window);

  G_OBJECT_CLASS (meta_screen_cast_window_stream_parent_class)->finalize (object);
}

static gboolean
meta_screen_cast_window_stream_initable_init (GInitable     *initable,
                                              GCancellable  *cancellable,
                                              GError       **error)
{
  MetaScreenCastWindowStream *window_stream =
    META_SCREEN_CAST_WINDOW_STREAM (initable);
  MetaWindow *window = window_stream->window;
  MetaLogicalMonitor *logical_monitor;
  int scale;

  logical_monitor = meta_window_get_main_logical_monitor (window);
  if (!logical_monitor)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Main logical monitor not found");
      return FALSE;
    }

  window_stream->window_unmanaged_handler_id =
    g_signal_connect_swapped (window, "unmanaged",
                              G_CALLBACK (on_window_unmanaged),
                              window_stream);

  if (meta_is_stage_views_scaled ())
    scale = (int) ceilf (meta_logical_monitor_get_scale (logical_monitor));
  else
    scale = 1;

  /* We cannot set the stream size to the exact size of the window, because
   * windows can be resized, whereas streams cannot.
   * So we set a size equals to the size of the logical monitor for the window.
   */
  window_stream->logical_width = logical_monitor->rect.width;
  window_stream->logical_height = logical_monitor->rect.height;
  window_stream->stream_width = logical_monitor->rect.width * scale;
  window_stream->stream_height = logical_monitor->rect.height * scale;

  return initable_parent_iface->init (initable, cancellable, error);
}

static void
meta_screen_cast_window_stream_init_initable_iface (GInitableIface *iface)
{
  initable_parent_iface = g_type_interface_peek_parent (iface);

  iface->init = meta_screen_cast_window_stream_initable_init;
}

static void
meta_screen_cast_window_stream_init (MetaScreenCastWindowStream *window_stream)
{
}

static void
meta_screen_cast_window_stream_class_init (MetaScreenCastWindowStreamClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaScreenCastStreamClass *stream_class =
    META_SCREEN_CAST_STREAM_CLASS (klass);

  object_class->set_property = meta_screen_cast_window_stream_set_property;
  object_class->get_property = meta_screen_cast_window_stream_get_property;
  object_class->finalize = meta_screen_cast_window_stream_finalize;

  stream_class->create_src = meta_screen_cast_window_stream_create_src;
  stream_class->set_parameters = meta_screen_cast_window_stream_set_parameters;
  stream_class->transform_position = meta_screen_cast_window_stream_transform_position;

  g_object_class_install_property (object_class,
                                   PROP_WINDOW,
                                   g_param_spec_object ("window",
                                                        "window",
                                                        "MetaWindow",
                                                        META_TYPE_WINDOW,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}
