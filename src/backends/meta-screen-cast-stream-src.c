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

#include "backends/meta-screen-cast-stream-src.h"

#include <errno.h>
#include <fcntl.h>
#include <pipewire/pipewire.h>
#include <spa/param/props.h>
#include <spa/param/format-utils.h>
#include <spa/param/video/format-utils.h>
#include <spa/utils/result.h>
#include <stdint.h>
#include <sys/mman.h>

#include "backends/meta-screen-cast-session.h"
#include "backends/meta-screen-cast-stream.h"
#include "clutter/clutter-mutter.h"
#include "core/meta-fraction.h"
#include "meta/boxes.h"

#define PRIVATE_OWNER_FROM_FIELD(TypeName, field_ptr, field_name) \
  (TypeName *)((guint8 *)(field_ptr) - G_PRIVATE_OFFSET (TypeName, field_name))

#define CURSOR_META_SIZE(width, height) \
  (sizeof (struct spa_meta_cursor) + \
   sizeof (struct spa_meta_bitmap) + width * height * 4)

enum
{
  PROP_0,

  PROP_STREAM,
};

enum
{
  READY,
  CLOSED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

typedef struct _MetaPipeWireSource
{
  GSource base;

  MetaScreenCastStreamSrc *src;
  struct pw_loop *pipewire_loop;
} MetaPipeWireSource;

typedef struct _MetaScreenCastStreamSrcPrivate
{
  MetaScreenCastStream *stream;

  struct pw_context *pipewire_context;
  struct pw_core *pipewire_core;
  MetaPipeWireSource *pipewire_source;
  struct spa_hook pipewire_core_listener;

  gboolean is_enabled;
  gboolean emit_closed_after_dispatch;

  struct pw_stream *pipewire_stream;
  struct spa_hook pipewire_stream_listener;
  uint32_t node_id;

  struct spa_video_info_raw video_format;
  int video_stride;

  int64_t last_frame_timestamp_us;
  guint follow_up_frame_source_id;

  GHashTable *dmabuf_handles;

  int stream_width;
  int stream_height;
} MetaScreenCastStreamSrcPrivate;

static void
meta_screen_cast_stream_src_init_initable_iface (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaScreenCastStreamSrc,
                         meta_screen_cast_stream_src,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                meta_screen_cast_stream_src_init_initable_iface)
                         G_ADD_PRIVATE (MetaScreenCastStreamSrc))

static inline uint32_t
us2ms (uint64_t us)
{
  return (uint32_t) (us / 1000);
}

static void
meta_screen_cast_stream_src_get_specs (MetaScreenCastStreamSrc *src,
                                       int                     *width,
                                       int                     *height,
                                       float                   *frame_rate)
{
  MetaScreenCastStreamSrcClass *klass =
    META_SCREEN_CAST_STREAM_SRC_GET_CLASS (src);

  klass->get_specs (src, width, height, frame_rate);
}

static gboolean
meta_screen_cast_stream_src_get_videocrop (MetaScreenCastStreamSrc *src,
                                           MetaRectangle           *crop_rect)
{
  MetaScreenCastStreamSrcClass *klass =
    META_SCREEN_CAST_STREAM_SRC_GET_CLASS (src);

  if (klass->get_videocrop)
    return klass->get_videocrop (src, crop_rect);

  return FALSE;
}

static gboolean
meta_screen_cast_stream_src_record_to_buffer (MetaScreenCastStreamSrc  *src,
                                              uint8_t                  *data,
                                              GError                  **error)
{
  MetaScreenCastStreamSrcClass *klass =
    META_SCREEN_CAST_STREAM_SRC_GET_CLASS (src);

  return klass->record_to_buffer (src, data, error);
}

static gboolean
meta_screen_cast_stream_src_record_to_framebuffer (MetaScreenCastStreamSrc  *src,
                                                   CoglFramebuffer          *framebuffer,
                                                   GError                  **error)
{
  MetaScreenCastStreamSrcClass *klass =
    META_SCREEN_CAST_STREAM_SRC_GET_CLASS (src);

  return klass->record_to_framebuffer (src, framebuffer, error);
}

static void
meta_screen_cast_stream_src_record_follow_up (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastStreamSrcClass *klass =
    META_SCREEN_CAST_STREAM_SRC_GET_CLASS (src);

  klass->record_follow_up (src);
}

static void
meta_screen_cast_stream_src_set_cursor_metadata (MetaScreenCastStreamSrc *src,
                                                 struct spa_meta_cursor  *spa_meta_cursor)
{
  MetaScreenCastStreamSrcClass *klass =
    META_SCREEN_CAST_STREAM_SRC_GET_CLASS (src);

  if (klass->set_cursor_metadata)
    klass->set_cursor_metadata (src, spa_meta_cursor);
}

static gboolean
draw_cursor_sprite_via_offscreen (MetaScreenCastStreamSrc  *src,
                                  CoglTexture              *cursor_texture,
                                  int                       bitmap_width,
                                  int                       bitmap_height,
                                  uint8_t                  *bitmap_data,
                                  GError                  **error)
{
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaScreenCastSession *session = meta_screen_cast_stream_get_session (stream);
  MetaScreenCast *screen_cast =
    meta_screen_cast_session_get_screen_cast (session);
  MetaBackend *backend = meta_screen_cast_get_backend (screen_cast);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  CoglTexture2D *bitmap_texture;
  CoglOffscreen *offscreen;
  CoglFramebuffer *fb;
  CoglPipeline *pipeline;
  CoglColor clear_color;

  bitmap_texture = cogl_texture_2d_new_with_size (cogl_context,
                                                  bitmap_width, bitmap_height);
  cogl_primitive_texture_set_auto_mipmap (COGL_PRIMITIVE_TEXTURE (bitmap_texture),
                                          FALSE);
  if (!cogl_texture_allocate (COGL_TEXTURE (bitmap_texture), error))
    {
      cogl_object_unref (bitmap_texture);
      return FALSE;
    }

  offscreen = cogl_offscreen_new_with_texture (COGL_TEXTURE (bitmap_texture));
  fb = COGL_FRAMEBUFFER (offscreen);
  cogl_object_unref (bitmap_texture);
  if (!cogl_framebuffer_allocate (fb, error))
    {
      cogl_object_unref (fb);
      return FALSE;
    }

  pipeline = cogl_pipeline_new (cogl_context);
  cogl_pipeline_set_layer_texture (pipeline, 0, cursor_texture);
  cogl_pipeline_set_layer_filters (pipeline, 0,
                                   COGL_PIPELINE_FILTER_LINEAR,
                                   COGL_PIPELINE_FILTER_LINEAR);
  cogl_color_init_from_4ub (&clear_color, 0, 0, 0, 0);
  cogl_framebuffer_clear (fb, COGL_BUFFER_BIT_COLOR, &clear_color);
  cogl_framebuffer_draw_rectangle (fb, pipeline,
                                   -1, 1, 1, -1);
  cogl_object_unref (pipeline);

  cogl_framebuffer_read_pixels (fb,
                                0, 0,
                                bitmap_width, bitmap_height,
                                COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                                bitmap_data);
  cogl_object_unref (fb);

  return TRUE;
}

gboolean
meta_screen_cast_stream_src_draw_cursor_into (MetaScreenCastStreamSrc  *src,
                                              CoglTexture              *cursor_texture,
                                              float                     scale,
                                              uint8_t                  *data,
                                              GError                  **error)
{
  int texture_width, texture_height;
  int width, height;

  texture_width = cogl_texture_get_width (cursor_texture);
  texture_height = cogl_texture_get_height (cursor_texture);
  width = texture_width * scale;
  height = texture_height * scale;

  if (texture_width == width &&
      texture_height == height)
    {
      cogl_texture_get_data (cursor_texture,
                             COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                             texture_width * 4,
                             data);
    }
  else
    {
      if (!draw_cursor_sprite_via_offscreen (src,
                                             cursor_texture,
                                             width,
                                             height,
                                             data,
                                             error))
        return FALSE;
    }

  return TRUE;
}

void
meta_screen_cast_stream_src_unset_cursor_metadata (MetaScreenCastStreamSrc *src,
                                                   struct spa_meta_cursor  *spa_meta_cursor)
{
  spa_meta_cursor->id = 0;
}

void
meta_screen_cast_stream_src_set_cursor_position_metadata (MetaScreenCastStreamSrc *src,
                                                          struct spa_meta_cursor  *spa_meta_cursor,
                                                          int                      x,
                                                          int                      y)
{
  spa_meta_cursor->id = 1;
  spa_meta_cursor->position.x = x;
  spa_meta_cursor->position.y = y;
  spa_meta_cursor->hotspot.x = 0;
  spa_meta_cursor->hotspot.y = 0;
  spa_meta_cursor->bitmap_offset = 0;
}

void
meta_screen_cast_stream_src_set_empty_cursor_sprite_metadata (MetaScreenCastStreamSrc *src,
                                                              struct spa_meta_cursor  *spa_meta_cursor,
                                                              int                      x,
                                                              int                      y)
{
  struct spa_meta_bitmap *spa_meta_bitmap;

  spa_meta_cursor->id = 1;
  spa_meta_cursor->position.x = x;
  spa_meta_cursor->position.y = y;

  spa_meta_cursor->bitmap_offset = sizeof (struct spa_meta_cursor);

  spa_meta_bitmap = SPA_MEMBER (spa_meta_cursor,
                                spa_meta_cursor->bitmap_offset,
                                struct spa_meta_bitmap);
  spa_meta_bitmap->format = SPA_VIDEO_FORMAT_RGBA;
  spa_meta_bitmap->offset = sizeof (struct spa_meta_bitmap);

  spa_meta_cursor->hotspot.x = 0;
  spa_meta_cursor->hotspot.y = 0;

  *spa_meta_bitmap = (struct spa_meta_bitmap) { 0 };
}

void
meta_screen_cast_stream_src_set_cursor_sprite_metadata (MetaScreenCastStreamSrc *src,
                                                        struct spa_meta_cursor  *spa_meta_cursor,
                                                        MetaCursorSprite        *cursor_sprite,
                                                        int                      x,
                                                        int                      y,
                                                        float                    scale)
{
  CoglTexture *cursor_texture;
  struct spa_meta_bitmap *spa_meta_bitmap;
  int hotspot_x, hotspot_y;
  int texture_width, texture_height;
  int bitmap_width, bitmap_height;
  uint8_t *bitmap_data;
  GError *error = NULL;

  cursor_texture = meta_cursor_sprite_get_cogl_texture (cursor_sprite);
  if (!cursor_texture)
    {
      meta_screen_cast_stream_src_set_empty_cursor_sprite_metadata (src,
                                                                    spa_meta_cursor,
                                                                    x, y);
      return;
    }

  spa_meta_cursor->id = 1;
  spa_meta_cursor->position.x = x;
  spa_meta_cursor->position.y = y;

  spa_meta_cursor->bitmap_offset = sizeof (struct spa_meta_cursor);

  spa_meta_bitmap = SPA_MEMBER (spa_meta_cursor,
                                spa_meta_cursor->bitmap_offset,
                                struct spa_meta_bitmap);
  spa_meta_bitmap->format = SPA_VIDEO_FORMAT_RGBA;
  spa_meta_bitmap->offset = sizeof (struct spa_meta_bitmap);

  meta_cursor_sprite_get_hotspot (cursor_sprite, &hotspot_x, &hotspot_y);
  spa_meta_cursor->hotspot.x = (int32_t) roundf (hotspot_x * scale);
  spa_meta_cursor->hotspot.y = (int32_t) roundf (hotspot_y * scale);

  texture_width = cogl_texture_get_width (cursor_texture);
  texture_height = cogl_texture_get_height (cursor_texture);
  bitmap_width = texture_width * scale;
  bitmap_height = texture_height * scale;

  spa_meta_bitmap->size.width = bitmap_width;
  spa_meta_bitmap->size.height = bitmap_height;
  spa_meta_bitmap->stride = bitmap_width * 4;

  bitmap_data = SPA_MEMBER (spa_meta_bitmap,
                            spa_meta_bitmap->offset,
                            uint8_t);

  if (!meta_screen_cast_stream_src_draw_cursor_into (src,
                                                     cursor_texture,
                                                     scale,
                                                     bitmap_data,
                                                     &error))
    {
      g_warning ("Failed to draw cursor: %s", error->message);
      g_error_free (error);
      spa_meta_cursor->id = 0;
    }
}

static void
add_cursor_metadata (MetaScreenCastStreamSrc *src,
                     struct spa_buffer       *spa_buffer)
{
  struct spa_meta_cursor *spa_meta_cursor;

  spa_meta_cursor = spa_buffer_find_meta_data (spa_buffer, SPA_META_Cursor,
                                               sizeof (*spa_meta_cursor));
  if (spa_meta_cursor)
    meta_screen_cast_stream_src_set_cursor_metadata (src, spa_meta_cursor);
}

static void
maybe_record_cursor (MetaScreenCastStreamSrc *src,
                     struct spa_buffer       *spa_buffer)
{
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);

  switch (meta_screen_cast_stream_get_cursor_mode (stream))
    {
    case META_SCREEN_CAST_CURSOR_MODE_HIDDEN:
    case META_SCREEN_CAST_CURSOR_MODE_EMBEDDED:
      return;
    case META_SCREEN_CAST_CURSOR_MODE_METADATA:
      add_cursor_metadata (src, spa_buffer);
      return;
    }

  g_assert_not_reached ();
}

static gboolean
do_record_frame (MetaScreenCastStreamSrc  *src,
                 struct spa_buffer        *spa_buffer,
                 uint8_t                  *data,
                 GError                  **error)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  if (spa_buffer->datas[0].data ||
      spa_buffer->datas[0].type == SPA_DATA_MemFd)
    {
      return meta_screen_cast_stream_src_record_to_buffer (src, data, error);
    }
  else if (spa_buffer->datas[0].type == SPA_DATA_DmaBuf)
    {
      CoglDmaBufHandle *dmabuf_handle =
        g_hash_table_lookup (priv->dmabuf_handles,
                             GINT_TO_POINTER (spa_buffer->datas[0].fd));
      CoglFramebuffer *dmabuf_fbo =
        cogl_dma_buf_handle_get_framebuffer (dmabuf_handle);

      return meta_screen_cast_stream_src_record_to_framebuffer (src,
                                                                dmabuf_fbo,
                                                                error);
    }

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
               "Unknown SPA buffer type %u", spa_buffer->datas[0].type);
  return FALSE;
}

gboolean
meta_screen_cast_stream_src_pending_follow_up_frame (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  return priv->follow_up_frame_source_id != 0;
}

static gboolean
follow_up_frame_cb (gpointer user_data)
{
  MetaScreenCastStreamSrc *src = user_data;
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  priv->follow_up_frame_source_id = 0;
  meta_screen_cast_stream_src_record_follow_up (src);

  return G_SOURCE_REMOVE;
}

static void
maybe_schedule_follow_up_frame (MetaScreenCastStreamSrc *src,
                                int64_t                  timeout_us)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  if (priv->follow_up_frame_source_id)
    return;

  priv->follow_up_frame_source_id = g_timeout_add (us2ms (timeout_us),
                                                   follow_up_frame_cb,
                                                   src);
}

void
meta_screen_cast_stream_src_maybe_record_frame (MetaScreenCastStreamSrc  *src,
                                                MetaScreenCastRecordFlag  flags)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  MetaRectangle crop_rect;
  struct pw_buffer *buffer;
  struct spa_buffer *spa_buffer;
  uint8_t *data = NULL;
  uint64_t now_us;
  g_autoptr (GError) error = NULL;

  now_us = g_get_monotonic_time ();
  if (priv->video_format.max_framerate.num > 0 &&
      priv->last_frame_timestamp_us != 0)
    {
      int64_t min_interval_us;
      int64_t time_since_last_frame_us;

      min_interval_us =
        ((G_USEC_PER_SEC * priv->video_format.max_framerate.denom) /
         priv->video_format.max_framerate.num);

      time_since_last_frame_us = now_us - priv->last_frame_timestamp_us;
      if (time_since_last_frame_us < min_interval_us)
        {
          int64_t timeout_us;

          timeout_us = min_interval_us - time_since_last_frame_us;
          maybe_schedule_follow_up_frame (src, timeout_us);
          return;
        }
    }

  if (!priv->pipewire_stream)
    return;

  buffer = pw_stream_dequeue_buffer (priv->pipewire_stream);
  if (!buffer)
    return;

  spa_buffer = buffer->buffer;
  data = spa_buffer->datas[0].data;

  if (spa_buffer->datas[0].type != SPA_DATA_DmaBuf && !data)
    {
      g_critical ("Invalid buffer data");
      return;
    }

  if (!(flags & META_SCREEN_CAST_RECORD_FLAG_CURSOR_ONLY))
    {
      g_clear_handle_id (&priv->follow_up_frame_source_id, g_source_remove);
      if (do_record_frame (src, spa_buffer, data, &error))
        {
          struct spa_meta_region *spa_meta_video_crop;

          spa_buffer->datas[0].chunk->size = spa_buffer->datas[0].maxsize;
          spa_buffer->datas[0].chunk->stride = priv->video_stride;

          /* Update VideoCrop if needed */
          spa_meta_video_crop =
            spa_buffer_find_meta_data (spa_buffer, SPA_META_VideoCrop,
                                       sizeof (*spa_meta_video_crop));
          if (spa_meta_video_crop)
            {
              if (meta_screen_cast_stream_src_get_videocrop (src, &crop_rect))
                {
                  spa_meta_video_crop->region.position.x = crop_rect.x;
                  spa_meta_video_crop->region.position.y = crop_rect.y;
                  spa_meta_video_crop->region.size.width = crop_rect.width;
                  spa_meta_video_crop->region.size.height = crop_rect.height;
                }
              else
                {
                  spa_meta_video_crop->region.position.x = 0;
                  spa_meta_video_crop->region.position.y = 0;
                  spa_meta_video_crop->region.size.width = priv->stream_width;
                  spa_meta_video_crop->region.size.height = priv->stream_height;
                }
            }
        }
      else
        {
          g_warning ("Failed to record screen cast frame: %s", error->message);
          spa_buffer->datas[0].chunk->size = 0;
        }
    }
  else
    {
      spa_buffer->datas[0].chunk->size = 0;
    }

  maybe_record_cursor (src, spa_buffer);

  priv->last_frame_timestamp_us = now_us;

  pw_stream_queue_buffer (priv->pipewire_stream, buffer);
}

static gboolean
meta_screen_cast_stream_src_is_enabled (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  return priv->is_enabled;
}

static void
meta_screen_cast_stream_src_enable (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  META_SCREEN_CAST_STREAM_SRC_GET_CLASS (src)->enable (src);

  priv->is_enabled = TRUE;
}

static void
meta_screen_cast_stream_src_disable (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  META_SCREEN_CAST_STREAM_SRC_GET_CLASS (src)->disable (src);

  g_clear_handle_id (&priv->follow_up_frame_source_id, g_source_remove);

  priv->is_enabled = FALSE;
}

static void
on_stream_state_changed (void                 *data,
                         enum pw_stream_state  old,
                         enum pw_stream_state  state,
                         const char           *error_message)
{
  MetaScreenCastStreamSrc *src = data;
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  switch (state)
    {
    case PW_STREAM_STATE_ERROR:
      g_warning ("pipewire stream error: %s", error_message);
      if (meta_screen_cast_stream_src_is_enabled (src))
        meta_screen_cast_stream_src_disable (src);
      priv->emit_closed_after_dispatch = TRUE;
      break;
    case PW_STREAM_STATE_PAUSED:
      if (priv->node_id == SPA_ID_INVALID && priv->pipewire_stream)
        {
          priv->node_id = pw_stream_get_node_id (priv->pipewire_stream);
          g_signal_emit (src, signals[READY], 0, (unsigned int) priv->node_id);
        }
      if (meta_screen_cast_stream_src_is_enabled (src))
        meta_screen_cast_stream_src_disable (src);
      break;
    case PW_STREAM_STATE_STREAMING:
      if (!meta_screen_cast_stream_src_is_enabled (src))
        meta_screen_cast_stream_src_enable (src);
      break;
    case PW_STREAM_STATE_UNCONNECTED:
    case PW_STREAM_STATE_CONNECTING:
      break;
    }
}

static void
on_stream_param_changed (void                 *data,
                         uint32_t              id,
                         const struct spa_pod *format)
{
  MetaScreenCastStreamSrc *src = data;
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  uint8_t params_buffer[1024];
  int32_t width, height, stride, size;
  struct spa_pod_builder pod_builder;
  const struct spa_pod *params[3];
  const int bpp = 4;

  if (!format || id != SPA_PARAM_Format)
    return;

  spa_format_video_raw_parse (format,
                              &priv->video_format);

  width = priv->video_format.size.width;
  height = priv->video_format.size.height;
  stride = SPA_ROUND_UP_N (width * bpp, 4);
  size = height * stride;

  priv->video_stride = stride;

  pod_builder = SPA_POD_BUILDER_INIT (params_buffer, sizeof (params_buffer));

  params[0] = spa_pod_builder_add_object (
    &pod_builder,
    SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
    SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int (16, 2, 16),
    SPA_PARAM_BUFFERS_blocks, SPA_POD_Int (1),
    SPA_PARAM_BUFFERS_size, SPA_POD_Int (size),
    SPA_PARAM_BUFFERS_stride, SPA_POD_Int (stride),
    SPA_PARAM_BUFFERS_align, SPA_POD_Int (16));

  params[1] = spa_pod_builder_add_object (
    &pod_builder,
    SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
    SPA_PARAM_META_type, SPA_POD_Id (SPA_META_VideoCrop),
    SPA_PARAM_META_size, SPA_POD_Int (sizeof (struct spa_meta_region)));

  params[2] = spa_pod_builder_add_object (
    &pod_builder,
    SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
    SPA_PARAM_META_type, SPA_POD_Id (SPA_META_Cursor),
    SPA_PARAM_META_size, SPA_POD_Int (CURSOR_META_SIZE (64, 64)));

  pw_stream_update_params (priv->pipewire_stream, params, G_N_ELEMENTS (params));
}

static void
on_stream_add_buffer (void             *data,
                      struct pw_buffer *buffer)
{
  MetaScreenCastStreamSrc *src = data;
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  CoglContext *context =
    clutter_backend_get_cogl_context (clutter_get_default_backend ());
  CoglRenderer *renderer = cogl_context_get_renderer (context);
  g_autoptr (GError) error = NULL;
  CoglDmaBufHandle *dmabuf_handle;
  struct spa_buffer *spa_buffer = buffer->buffer;
  struct spa_data *spa_data = spa_buffer->datas;
  const int bpp = 4;
  int stride;

  stride = SPA_ROUND_UP_N (priv->video_format.size.width * bpp, 4);

  spa_data[0].mapoffset = 0;
  spa_data[0].maxsize = stride * priv->video_format.size.height;

  dmabuf_handle = cogl_renderer_create_dma_buf (renderer,
                                                priv->stream_width,
                                                priv->stream_height,
                                                &error);

  if (error)
    g_debug ("Error exporting DMA buffer handle: %s", error->message);

  if (dmabuf_handle)
    {
      spa_data[0].type = SPA_DATA_DmaBuf;
      spa_data[0].flags = SPA_DATA_FLAG_READWRITE;
      spa_data[0].fd = cogl_dma_buf_handle_get_fd (dmabuf_handle);
      spa_data[0].data = NULL;

      g_hash_table_insert (priv->dmabuf_handles,
                           GINT_TO_POINTER (spa_data[0].fd),
                           dmabuf_handle);
    }
  else
    {
      unsigned int seals;

      /* Fallback to a memfd buffer */
      spa_data[0].type = SPA_DATA_MemFd;
      spa_data[0].flags = SPA_DATA_FLAG_READWRITE;
      spa_data[0].fd = memfd_create ("mutter-screen-cast-memfd",
                                     MFD_CLOEXEC | MFD_ALLOW_SEALING);
      if (spa_data[0].fd == -1)
        {
          g_critical ("Can't create memfd: %m");
          return;
        }
      spa_data[0].mapoffset = 0;
      spa_data[0].maxsize = stride * priv->video_format.size.height;

      if (ftruncate (spa_data[0].fd, spa_data[0].maxsize) < 0)
        {
          g_critical ("Can't truncate to %d: %m", spa_data[0].maxsize);
          return;
        }

      seals = F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL;
      if (fcntl (spa_data[0].fd, F_ADD_SEALS, seals) == -1)
        g_warning ("Failed to add seals: %m");

      spa_data[0].data = mmap (NULL,
                               spa_data[0].maxsize,
                               PROT_READ | PROT_WRITE,
                               MAP_SHARED,
                               spa_data[0].fd,
                               spa_data[0].mapoffset);
      if (spa_data[0].data == MAP_FAILED)
        {
          g_critical ("Failed to mmap memory: %m");
          return;
        }
    }
}

static void
on_stream_remove_buffer (void             *data,
                         struct pw_buffer *buffer)
{
  MetaScreenCastStreamSrc *src = data;
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  struct spa_buffer *spa_buffer = buffer->buffer;
  struct spa_data *spa_data = spa_buffer->datas;

  if (spa_data[0].type == SPA_DATA_DmaBuf)
    {
      if (!g_hash_table_remove (priv->dmabuf_handles, GINT_TO_POINTER (spa_data[0].fd)))
        g_critical ("Failed to remove non-exported DMA buffer");
    }
  else if (spa_data[0].type == SPA_DATA_MemFd)
    {
      munmap (spa_data[0].data, spa_data[0].maxsize);
      close (spa_data[0].fd);
    }
}

static const struct pw_stream_events stream_events = {
  PW_VERSION_STREAM_EVENTS,
  .state_changed = on_stream_state_changed,
  .param_changed = on_stream_param_changed,
  .add_buffer = on_stream_add_buffer,
  .remove_buffer = on_stream_remove_buffer,
};

static struct pw_stream *
create_pipewire_stream (MetaScreenCastStreamSrc  *src,
                        GError                  **error)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  struct pw_stream *pipewire_stream;
  uint8_t buffer[1024];
  struct spa_pod_builder pod_builder =
    SPA_POD_BUILDER_INIT (buffer, sizeof (buffer));
  float frame_rate;
  MetaFraction frame_rate_fraction;
  struct spa_fraction max_framerate;
  struct spa_fraction min_framerate;
  const struct spa_pod *params[1];
  int result;

  priv->node_id = SPA_ID_INVALID;

  pipewire_stream = pw_stream_new (priv->pipewire_core,
                                   "meta-screen-cast-src",
                                   NULL);
  if (!pipewire_stream)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create PipeWire stream: %s",
                   strerror (errno));
      return NULL;
    }

  meta_screen_cast_stream_src_get_specs (src,
                                         &priv->stream_width,
                                         &priv->stream_height,
                                         &frame_rate);
  frame_rate_fraction = meta_fraction_from_double (frame_rate);

  min_framerate = SPA_FRACTION (1, 1);
  max_framerate = SPA_FRACTION (frame_rate_fraction.num,
                                frame_rate_fraction.denom);

  params[0] = spa_pod_builder_add_object (
    &pod_builder,
    SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
    SPA_FORMAT_mediaType, SPA_POD_Id (SPA_MEDIA_TYPE_video),
    SPA_FORMAT_mediaSubtype, SPA_POD_Id (SPA_MEDIA_SUBTYPE_raw),
    SPA_FORMAT_VIDEO_format, SPA_POD_Id (SPA_VIDEO_FORMAT_BGRx),
    SPA_FORMAT_VIDEO_size, SPA_POD_Rectangle (&SPA_RECTANGLE (priv->stream_width,
                                                              priv->stream_height)),
    SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction (&SPA_FRACTION (0, 1)),
    SPA_FORMAT_VIDEO_maxFramerate, SPA_POD_CHOICE_RANGE_Fraction (&max_framerate,
                                                                  &min_framerate,
                                                                  &max_framerate));

  pw_stream_add_listener (pipewire_stream,
                          &priv->pipewire_stream_listener,
                          &stream_events,
                          src);

  result = pw_stream_connect (pipewire_stream,
                              PW_DIRECTION_OUTPUT,
                              SPA_ID_INVALID,
                              (PW_STREAM_FLAG_DRIVER |
                               PW_STREAM_FLAG_ALLOC_BUFFERS),
                              params, G_N_ELEMENTS (params));
  if (result != 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Could not connect: %s", spa_strerror (result));
      return NULL;
    }

  return pipewire_stream;
}

static void
on_core_error (void       *data,
               uint32_t    id,
	       int         seq,
	       int         res,
	       const char *message)
{
  MetaScreenCastStreamSrc *src = data;
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  g_warning ("pipewire remote error: id:%u %s", id, message);

  if (id == PW_ID_CORE && res == -EPIPE)
    {
      if (meta_screen_cast_stream_src_is_enabled (src))
        meta_screen_cast_stream_src_disable (src);
      priv->emit_closed_after_dispatch = TRUE;
    }
}

static gboolean
pipewire_loop_source_prepare (GSource *base,
                              int     *timeout)
{
  *timeout = -1;
  return FALSE;
}

static gboolean
pipewire_loop_source_dispatch (GSource     *source,
                               GSourceFunc  callback,
                               gpointer     user_data)
{
  MetaPipeWireSource *pipewire_source = (MetaPipeWireSource *) source;
  MetaScreenCastStreamSrc *src = pipewire_source->src;
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);
  int result;

  result = pw_loop_iterate (pipewire_source->pipewire_loop, 0);
  if (result < 0)
    g_warning ("pipewire_loop_iterate failed: %s", spa_strerror (result));

  if (priv->emit_closed_after_dispatch)
    g_signal_emit (src, signals[CLOSED], 0);

  return TRUE;
}

static void
pipewire_loop_source_finalize (GSource *source)
{
  MetaPipeWireSource *pipewire_source = (MetaPipeWireSource *) source;

  pw_loop_leave (pipewire_source->pipewire_loop);
  pw_loop_destroy (pipewire_source->pipewire_loop);
}

static GSourceFuncs pipewire_source_funcs =
{
  pipewire_loop_source_prepare,
  NULL,
  pipewire_loop_source_dispatch,
  pipewire_loop_source_finalize
};

static MetaPipeWireSource *
create_pipewire_source (MetaScreenCastStreamSrc *src)
{
  MetaPipeWireSource *pipewire_source;

  pipewire_source =
    (MetaPipeWireSource *) g_source_new (&pipewire_source_funcs,
                                         sizeof (MetaPipeWireSource));
  pipewire_source->src = src;
  pipewire_source->pipewire_loop = pw_loop_new (NULL);
  if (!pipewire_source->pipewire_loop)
    {
      g_source_unref ((GSource *) pipewire_source);
      return NULL;
    }

  g_source_add_unix_fd (&pipewire_source->base,
                        pw_loop_get_fd (pipewire_source->pipewire_loop),
                        G_IO_IN | G_IO_ERR);

  pw_loop_enter (pipewire_source->pipewire_loop);
  g_source_attach (&pipewire_source->base, NULL);

  return pipewire_source;
}

static const struct pw_core_events core_events = {
  PW_VERSION_CORE_EVENTS,
  .error = on_core_error,
};

static gboolean
meta_screen_cast_stream_src_initable_init (GInitable     *initable,
                                           GCancellable  *cancellable,
                                           GError       **error)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (initable);
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  priv->pipewire_source = create_pipewire_source (src);
  if (!priv->pipewire_source)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create PipeWire source");
      return FALSE;
    }

  priv->pipewire_context = pw_context_new (priv->pipewire_source->pipewire_loop,
                                           NULL, 0);
  if (!priv->pipewire_context)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create pipewire context");
      return FALSE;
    }

  priv->pipewire_core = pw_context_connect (priv->pipewire_context, NULL, 0);
  if (!priv->pipewire_core)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Couldn't connect pipewire context");
      return FALSE;
    }

  pw_core_add_listener (priv->pipewire_core,
                        &priv->pipewire_core_listener,
                        &core_events,
                        src);

  priv->pipewire_stream = create_pipewire_stream (src, error);
  if (!priv->pipewire_stream)
    return FALSE;

  return TRUE;
}

static void
meta_screen_cast_stream_src_init_initable_iface (GInitableIface *iface)
{
  iface->init = meta_screen_cast_stream_src_initable_init;
}

MetaScreenCastStream *
meta_screen_cast_stream_src_get_stream (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  return priv->stream;
}

static void
meta_screen_cast_stream_src_finalize (GObject *object)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (object);
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  if (meta_screen_cast_stream_src_is_enabled (src))
    meta_screen_cast_stream_src_disable (src);

  g_clear_pointer (&priv->pipewire_stream, pw_stream_destroy);
  g_clear_pointer (&priv->dmabuf_handles, g_hash_table_destroy);
  g_clear_pointer (&priv->pipewire_core, pw_core_disconnect);
  g_clear_pointer (&priv->pipewire_context, pw_context_destroy);
  g_source_destroy (&priv->pipewire_source->base);
  g_source_unref (&priv->pipewire_source->base);

  G_OBJECT_CLASS (meta_screen_cast_stream_src_parent_class)->finalize (object);
}

static void
meta_screen_cast_stream_src_set_property (GObject      *object,
                                          guint         prop_id,
                                          const GValue *value,
                                          GParamSpec   *pspec)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (object);
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  switch (prop_id)
    {
    case PROP_STREAM:
      priv->stream = g_value_get_object (value);
      break;;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_screen_cast_stream_src_get_property (GObject    *object,
                                          guint       prop_id,
                                          GValue     *value,
                                          GParamSpec *pspec)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (object);
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  switch (prop_id)
    {
    case PROP_STREAM:
      g_value_set_object (value, priv->stream);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_screen_cast_stream_src_init (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastStreamSrcPrivate *priv =
    meta_screen_cast_stream_src_get_instance_private (src);

  priv->dmabuf_handles =
    g_hash_table_new_full (NULL, NULL, NULL,
                           (GDestroyNotify) cogl_dma_buf_handle_free);
}

static void
meta_screen_cast_stream_src_class_init (MetaScreenCastStreamSrcClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_screen_cast_stream_src_finalize;
  object_class->set_property = meta_screen_cast_stream_src_set_property;
  object_class->get_property = meta_screen_cast_stream_src_get_property;

  g_object_class_install_property (object_class,
                                   PROP_STREAM,
                                   g_param_spec_object ("stream",
                                                        "stream",
                                                        "MetaScreenCastStream",
                                                        META_TYPE_SCREEN_CAST_STREAM,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  signals[READY] = g_signal_new ("ready",
                                 G_TYPE_FROM_CLASS (klass),
                                 G_SIGNAL_RUN_LAST,
                                 0,
                                 NULL, NULL, NULL,
                                 G_TYPE_NONE, 1,
                                 G_TYPE_UINT);
  signals[CLOSED] = g_signal_new ("closed",
                                  G_TYPE_FROM_CLASS (klass),
                                  G_SIGNAL_RUN_LAST,
                                  0,
                                  NULL, NULL, NULL,
                                  G_TYPE_NONE, 0);
}
