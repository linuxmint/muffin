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

#include "backends/meta-screen-cast-stream.h"

#include "backends/meta-screen-cast-session.h"

#define META_SCREEN_CAST_STREAM_DBUS_IFACE "org.gnome.Mutter.ScreenCast.Stream"
#define META_SCREEN_CAST_STREAM_DBUS_PATH "/org/gnome/Mutter/ScreenCast/Stream"

enum
{
  PROP_0,

  PROP_SESSION,
  PROP_CONNECTION,
  PROP_CURSOR_MODE,
};

enum
{
  CLOSED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

typedef struct _MetaScreenCastStreamPrivate
{
  MetaScreenCastSession *session;

  GDBusConnection *connection;
  char *object_path;

  MetaScreenCastCursorMode cursor_mode;

  MetaScreenCastStreamSrc *src;
} MetaScreenCastStreamPrivate;

static void
meta_screen_cast_stream_init_initable_iface (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaScreenCastStream,
                         meta_screen_cast_stream,
                         META_DBUS_TYPE_SCREEN_CAST_STREAM_SKELETON,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                meta_screen_cast_stream_init_initable_iface)
                         G_ADD_PRIVATE (MetaScreenCastStream))

static MetaScreenCastStreamSrc *
meta_screen_cast_stream_create_src (MetaScreenCastStream  *stream,
                                    GError               **error)
{
  return META_SCREEN_CAST_STREAM_GET_CLASS (stream)->create_src (stream,
                                                                 error);
}

static void
meta_screen_cast_stream_set_parameters (MetaScreenCastStream *stream,
                                        GVariantBuilder      *parameters_builder)
{
  META_SCREEN_CAST_STREAM_GET_CLASS (stream)->set_parameters (stream,
                                                              parameters_builder);
}

static void
on_stream_src_closed (MetaScreenCastStreamSrc *src,
                      MetaScreenCastStream    *stream)
{
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (stream);

  if (priv->src)
    meta_screen_cast_stream_close (stream);
}

static void
on_stream_src_ready (MetaScreenCastStreamSrc *src,
                     uint32_t                 node_id,
                     MetaScreenCastStream    *stream)
{
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (stream);
  GDBusConnection *connection = priv->connection;
  char *peer_name;

  peer_name = meta_screen_cast_session_get_peer_name (priv->session);
  g_dbus_connection_emit_signal (connection,
                                 peer_name,
                                 priv->object_path,
                                 META_SCREEN_CAST_STREAM_DBUS_IFACE,
                                 "PipeWireStreamAdded",
                                 g_variant_new ("(u)", node_id),
                                 NULL);
}

MetaScreenCastSession *
meta_screen_cast_stream_get_session (MetaScreenCastStream *stream)
{
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (stream);

  return priv->session;
}

gboolean
meta_screen_cast_stream_start (MetaScreenCastStream  *stream,
                               GError               **error)
{
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (stream);
  MetaScreenCastStreamSrc *src;

  src = meta_screen_cast_stream_create_src (stream, error);
  if (!src)
    return FALSE;

  priv->src = src;
  g_signal_connect (src, "ready", G_CALLBACK (on_stream_src_ready), stream);
  g_signal_connect (src, "closed", G_CALLBACK (on_stream_src_closed), stream);

  return TRUE;
}

void
meta_screen_cast_stream_close (MetaScreenCastStream *stream)
{
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (stream);

  g_clear_object (&priv->src);

  g_signal_emit (stream, signals[CLOSED], 0);
}

char *
meta_screen_cast_stream_get_object_path (MetaScreenCastStream *stream)
{
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (stream);

  return priv->object_path;
}

void
meta_screen_cast_stream_transform_position (MetaScreenCastStream *stream,
                                            double                stream_x,
                                            double                stream_y,
                                            double               *x,
                                            double               *y)
{
  META_SCREEN_CAST_STREAM_GET_CLASS (stream)->transform_position (stream,
                                                                  stream_x,
                                                                  stream_y,
                                                                  x,
                                                                  y);
}

MetaScreenCastCursorMode
meta_screen_cast_stream_get_cursor_mode (MetaScreenCastStream *stream)
{
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (stream);

  return priv->cursor_mode;
}

static void
meta_screen_cast_stream_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  MetaScreenCastStream *stream = META_SCREEN_CAST_STREAM (object);
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (stream);

  switch (prop_id)
    {
    case PROP_SESSION:
      priv->session = g_value_get_object (value);
      break;
    case PROP_CONNECTION:
      priv->connection = g_value_get_object (value);
      break;
    case PROP_CURSOR_MODE:
      priv->cursor_mode = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_screen_cast_stream_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  MetaScreenCastStream *stream = META_SCREEN_CAST_STREAM (object);
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (stream);

  switch (prop_id)
    {
    case PROP_SESSION:
      g_value_set_object (value, priv->session);
      break;
    case PROP_CONNECTION:
      g_value_set_object (value, priv->connection);
      break;
    case PROP_CURSOR_MODE:
      g_value_set_uint (value, priv->cursor_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_screen_cast_stream_finalize (GObject *object)
{
  MetaScreenCastStream *stream = META_SCREEN_CAST_STREAM (object);
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (stream);

  if (priv->src)
    meta_screen_cast_stream_close (stream);

  g_clear_pointer (&priv->object_path, g_free);

  G_OBJECT_CLASS (meta_screen_cast_stream_parent_class)->finalize (object);
}

static gboolean
meta_screen_cast_stream_initable_init (GInitable     *initable,
                                       GCancellable  *cancellable,
                                       GError       **error)
{
  MetaScreenCastStream *stream = META_SCREEN_CAST_STREAM (initable);
  MetaDBusScreenCastStream *skeleton = META_DBUS_SCREEN_CAST_STREAM (stream);
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (stream);
  GVariantBuilder parameters_builder;
  GVariant *parameters_variant;
  static unsigned int global_stream_number = 0;

  g_variant_builder_init (&parameters_builder, G_VARIANT_TYPE_VARDICT);
  meta_screen_cast_stream_set_parameters (stream, &parameters_builder);

  parameters_variant = g_variant_builder_end (&parameters_builder);
  meta_dbus_screen_cast_stream_set_parameters (skeleton, parameters_variant);

  priv->object_path =
    g_strdup_printf (META_SCREEN_CAST_STREAM_DBUS_PATH "/u%u",
                     ++global_stream_number);
  if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (stream),
                                         priv->connection,
                                         priv->object_path,
                                         error))
    return FALSE;

  return TRUE;
}

static void
meta_screen_cast_stream_init_initable_iface (GInitableIface *iface)
{
  iface->init = meta_screen_cast_stream_initable_init;
}

static void
meta_screen_cast_stream_init (MetaScreenCastStream *stream)
{
}

static void
meta_screen_cast_stream_class_init (MetaScreenCastStreamClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_screen_cast_stream_finalize;
  object_class->set_property = meta_screen_cast_stream_set_property;
  object_class->get_property = meta_screen_cast_stream_get_property;

  g_object_class_install_property (object_class,
                                   PROP_SESSION,
                                   g_param_spec_object ("session",
                                                        "session",
                                                        "MetaScreenSession",
                                                        META_TYPE_SCREEN_CAST_SESSION,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
                                   PROP_CONNECTION,
                                   g_param_spec_object ("connection",
                                                        "connection",
                                                        "GDBus connection",
                                                        G_TYPE_DBUS_CONNECTION,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
                                   PROP_CURSOR_MODE,
                                   g_param_spec_uint ("cursor-mode",
                                                      "cursor-mode",
                                                      "Cursor mode",
                                                      META_SCREEN_CAST_CURSOR_MODE_HIDDEN,
                                                      META_SCREEN_CAST_CURSOR_MODE_METADATA,
                                                      META_SCREEN_CAST_CURSOR_MODE_HIDDEN,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_CONSTRUCT_ONLY |
                                                      G_PARAM_STATIC_STRINGS));

  signals[CLOSED] = g_signal_new ("closed",
                                  G_TYPE_FROM_CLASS (klass),
                                  G_SIGNAL_RUN_LAST,
                                  0,
                                  NULL, NULL, NULL,
                                  G_TYPE_NONE, 0);
}
