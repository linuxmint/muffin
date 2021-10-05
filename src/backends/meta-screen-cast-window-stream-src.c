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

#include "backends/meta-screen-cast-window-stream-src.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-screen-cast-session.h"
#include "backends/meta-screen-cast-window.h"
#include "backends/meta-screen-cast-window-stream.h"
#include "compositor/meta-window-actor-private.h"

struct _MetaScreenCastWindowStreamSrc
{
  MetaScreenCastStreamSrc parent;

  MetaScreenCastWindow *screen_cast_window;

  unsigned long screen_cast_window_damaged_handler_id;
  unsigned long screen_cast_window_destroyed_handler_id;
  unsigned long cursor_moved_handler_id;
  unsigned long cursor_changed_handler_id;

  gboolean cursor_bitmap_invalid;
};

G_DEFINE_TYPE (MetaScreenCastWindowStreamSrc,
               meta_screen_cast_window_stream_src,
               META_TYPE_SCREEN_CAST_STREAM_SRC)

static MetaBackend *
get_backend (MetaScreenCastWindowStreamSrc *window_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (window_src);
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaScreenCastSession *session = meta_screen_cast_stream_get_session (stream);
  MetaScreenCast *screen_cast =
    meta_screen_cast_session_get_screen_cast (session);

  return meta_screen_cast_get_backend (screen_cast);
}

static MetaScreenCastWindowStream *
get_window_stream (MetaScreenCastWindowStreamSrc *window_src)
{
  MetaScreenCastStreamSrc *src;
  MetaScreenCastStream *stream;

  src = META_SCREEN_CAST_STREAM_SRC (window_src);
  stream = meta_screen_cast_stream_src_get_stream (src);

  return META_SCREEN_CAST_WINDOW_STREAM (stream);
}

static MetaWindow *
get_window (MetaScreenCastWindowStreamSrc *window_src)
{
  MetaScreenCastWindowStream *window_stream;

  window_stream = get_window_stream (window_src);

  return meta_screen_cast_window_stream_get_window (window_stream);
}

static int
get_stream_width (MetaScreenCastWindowStreamSrc *window_src)
{
  MetaScreenCastWindowStream *window_stream;

  window_stream = get_window_stream (window_src);

  return meta_screen_cast_window_stream_get_width (window_stream);
}

static int
get_stream_height (MetaScreenCastWindowStreamSrc *window_src)
{
  MetaScreenCastWindowStream *window_stream;

  window_stream = get_window_stream (window_src);

  return meta_screen_cast_window_stream_get_height (window_stream);
}

static void
maybe_draw_cursor_sprite (MetaScreenCastWindowStreamSrc *window_src,
                          uint8_t                       *data,
                          MetaRectangle                 *stream_rect)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (window_src);
  MetaBackend *backend = get_backend (window_src);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  MetaCursorSprite *cursor_sprite;
  CoglTexture *cursor_texture;
  MetaScreenCastWindow *screen_cast_window;
  graphene_point_t cursor_position;
  graphene_point_t relative_cursor_position;
  cairo_surface_t *cursor_surface;
  uint8_t *cursor_surface_data;
  GError *error = NULL;
  cairo_surface_t *stream_surface;
  int width, height;
  float scale;
  int hotspot_x, hotspot_y;
  cairo_t *cr;

  cursor_sprite = meta_cursor_renderer_get_cursor (cursor_renderer);
  if (!cursor_sprite)
    return;

  cursor_texture = meta_cursor_sprite_get_cogl_texture (cursor_sprite);
  if (!cursor_texture)
    return;

  screen_cast_window = window_src->screen_cast_window;
  cursor_position = meta_cursor_renderer_get_position (cursor_renderer);
  if (!meta_screen_cast_window_transform_cursor_position (screen_cast_window,
                                                          cursor_sprite,
                                                          &cursor_position,
                                                          &scale,
                                                          &relative_cursor_position))
    return;

  meta_cursor_sprite_get_hotspot (cursor_sprite, &hotspot_x, &hotspot_y);

  width = cogl_texture_get_width (cursor_texture) * scale;
  height = cogl_texture_get_height (cursor_texture) * scale;
  cursor_surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                               width, height);

  cursor_surface_data = cairo_image_surface_get_data (cursor_surface);
  if (!meta_screen_cast_stream_src_draw_cursor_into (src,
                                                     cursor_texture,
                                                     scale,
                                                     cursor_surface_data,
                                                     &error))
    {
      g_warning ("Failed to draw cursor: %s", error->message);
      g_error_free (error);
      cairo_surface_destroy (cursor_surface);
      return;
    }

  stream_surface =
    cairo_image_surface_create_for_data (data, CAIRO_FORMAT_ARGB32,
                                         stream_rect->width,
                                         stream_rect->height,
                                         stream_rect->width * 4);

  cr = cairo_create (stream_surface);
  cairo_surface_mark_dirty (cursor_surface);
  cairo_surface_flush (cursor_surface);
  cairo_set_source_surface (cr, cursor_surface,
                            relative_cursor_position.x - hotspot_x * scale,
                            relative_cursor_position.y - hotspot_y * scale);
  cairo_paint (cr);
  cairo_destroy (cr);
  cairo_surface_destroy (stream_surface);
  cairo_surface_destroy (cursor_surface);
}

static void
maybe_blit_cursor_sprite (MetaScreenCastWindowStreamSrc *window_src,
                          CoglFramebuffer               *framebuffer,
                          MetaRectangle                 *stream_rect)
{
  MetaBackend *backend = get_backend (window_src);
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_get_default_backend ());
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  MetaScreenCastWindow *screen_cast_window;
  MetaCursorSprite *cursor_sprite;
  graphene_point_t relative_cursor_position;
  graphene_point_t cursor_position;
  CoglTexture *cursor_texture;
  CoglPipeline *pipeline;
  int width, height;
  float scale;
  int hotspot_x, hotspot_y;
  float x, y;

  cursor_sprite = meta_cursor_renderer_get_cursor (cursor_renderer);
  if (!cursor_sprite)
    return;

  cursor_texture = meta_cursor_sprite_get_cogl_texture (cursor_sprite);
  if (!cursor_texture)
    return;

  screen_cast_window = window_src->screen_cast_window;
  cursor_position = meta_cursor_renderer_get_position (cursor_renderer);
  if (!meta_screen_cast_window_transform_cursor_position (screen_cast_window,
                                                          cursor_sprite,
                                                          &cursor_position,
                                                          &scale,
                                                          &relative_cursor_position))
    return;

  meta_cursor_sprite_get_hotspot (cursor_sprite, &hotspot_x, &hotspot_y);

  x = (relative_cursor_position.x - hotspot_x) * scale;
  y = (relative_cursor_position.y - hotspot_y) * scale;
  width = cogl_texture_get_width (cursor_texture);
  height = cogl_texture_get_height (cursor_texture);

  pipeline = cogl_pipeline_new (cogl_context);
  cogl_pipeline_set_layer_texture (pipeline, 0, cursor_texture);
  cogl_pipeline_set_layer_filters (pipeline, 0,
                                   COGL_PIPELINE_FILTER_LINEAR,
                                   COGL_PIPELINE_FILTER_LINEAR);

  cogl_framebuffer_draw_rectangle (framebuffer,
                                   pipeline,
                                   x, y,
                                   x + width, y + height);

  cogl_object_unref (pipeline);
}

static gboolean
capture_into (MetaScreenCastWindowStreamSrc *window_src,
              uint8_t                       *data)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (window_src);
  MetaRectangle stream_rect;
  MetaScreenCastStream *stream;

  stream_rect.x = 0;
  stream_rect.y = 0;
  stream_rect.width = get_stream_width (window_src);
  stream_rect.height = get_stream_height (window_src);

  meta_screen_cast_window_capture_into (window_src->screen_cast_window,
                                        &stream_rect, data);

  stream = meta_screen_cast_stream_src_get_stream (src);
  switch (meta_screen_cast_stream_get_cursor_mode (stream))
    {
    case META_SCREEN_CAST_CURSOR_MODE_EMBEDDED:
      maybe_draw_cursor_sprite (window_src, data, &stream_rect);
      break;
    case META_SCREEN_CAST_CURSOR_MODE_METADATA:
    case META_SCREEN_CAST_CURSOR_MODE_HIDDEN:
      break;
    }

  return TRUE;
}

static void
meta_screen_cast_window_stream_src_get_specs (MetaScreenCastStreamSrc *src,
                                              int                     *width,
                                              int                     *height,
                                              float                   *frame_rate)
{
  MetaScreenCastWindowStreamSrc *window_src =
    META_SCREEN_CAST_WINDOW_STREAM_SRC (src);

  *width = get_stream_width (window_src);
  *height = get_stream_height (window_src);
  *frame_rate = 60.0f;
}

static gboolean
meta_screen_cast_window_stream_src_get_videocrop (MetaScreenCastStreamSrc *src,
                                                  MetaRectangle           *crop_rect)
{
  MetaScreenCastWindowStreamSrc *window_src =
    META_SCREEN_CAST_WINDOW_STREAM_SRC (src);
  MetaRectangle stream_rect;

  meta_screen_cast_window_get_buffer_bounds (window_src->screen_cast_window,
                                             crop_rect);

  stream_rect.x = 0;
  stream_rect.y = 0;
  stream_rect.width = get_stream_width (window_src);
  stream_rect.height = get_stream_height (window_src);

  meta_rectangle_intersect (crop_rect, &stream_rect, crop_rect);

  return TRUE;
}

static void
meta_screen_cast_window_stream_src_stop (MetaScreenCastWindowStreamSrc *window_src)

{
  MetaBackend *backend = get_backend (window_src);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);

  if (!window_src->screen_cast_window)
    return;

  g_clear_signal_handler (&window_src->screen_cast_window_damaged_handler_id,
                          window_src->screen_cast_window);
  g_clear_signal_handler (&window_src->screen_cast_window_destroyed_handler_id,
                          window_src->screen_cast_window);
  g_clear_signal_handler (&window_src->cursor_moved_handler_id,
                          cursor_tracker);
  g_clear_signal_handler (&window_src->cursor_changed_handler_id,
                          cursor_tracker);
}

static void
screen_cast_window_damaged (MetaWindowActor               *actor,
                            MetaScreenCastWindowStreamSrc *window_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (window_src);
  MetaScreenCastRecordFlag flags;

  flags = META_SCREEN_CAST_RECORD_FLAG_CURSOR_ONLY;
  meta_screen_cast_stream_src_maybe_record_frame (src, flags);
}

static void
screen_cast_window_destroyed (MetaWindowActor               *actor,
                              MetaScreenCastWindowStreamSrc *window_src)
{
  meta_screen_cast_window_stream_src_stop (window_src);
  window_src->screen_cast_window = NULL;
}

static void
sync_cursor_state (MetaScreenCastWindowStreamSrc *window_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (window_src);
  MetaScreenCastRecordFlag flags;

  if (meta_screen_cast_window_has_damage (window_src->screen_cast_window))
    return;

  flags = META_SCREEN_CAST_RECORD_FLAG_CURSOR_ONLY;
  meta_screen_cast_stream_src_maybe_record_frame (src, flags);
}

static void
cursor_moved (MetaCursorTracker             *cursor_tracker,
              float                          x,
              float                          y,
              MetaScreenCastWindowStreamSrc *window_src)
{
  sync_cursor_state (window_src);
}

static void
cursor_changed (MetaCursorTracker             *cursor_tracker,
                MetaScreenCastWindowStreamSrc *window_src)
{
  window_src->cursor_bitmap_invalid = TRUE;
  sync_cursor_state (window_src);
}

static void
meta_screen_cast_window_stream_src_enable (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastWindowStreamSrc *window_src =
    META_SCREEN_CAST_WINDOW_STREAM_SRC (src);
  MetaBackend *backend = get_backend (window_src);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  MetaWindowActor *window_actor;
  MetaScreenCastStream *stream;
  MetaScreenCastRecordFlag flags;

  window_actor = meta_window_actor_from_window (get_window (window_src));
  if (!window_actor)
    return;

  window_src->screen_cast_window = META_SCREEN_CAST_WINDOW (window_actor);

  window_src->screen_cast_window_damaged_handler_id =
    g_signal_connect (window_src->screen_cast_window,
                      "damaged",
                      G_CALLBACK (screen_cast_window_damaged),
                      window_src);

  window_src->screen_cast_window_destroyed_handler_id =
    g_signal_connect (window_src->screen_cast_window,
                      "destroy",
                      G_CALLBACK (screen_cast_window_destroyed),
                      window_src);

  stream = meta_screen_cast_stream_src_get_stream (src);
  switch (meta_screen_cast_stream_get_cursor_mode (stream))
    {
    case META_SCREEN_CAST_CURSOR_MODE_METADATA:
    case META_SCREEN_CAST_CURSOR_MODE_EMBEDDED:
      window_src->cursor_moved_handler_id =
        g_signal_connect_after (cursor_tracker, "cursor-moved",
                                G_CALLBACK (cursor_moved),
                                window_src);
      window_src->cursor_changed_handler_id =
        g_signal_connect_after (cursor_tracker, "cursor-changed",
                                G_CALLBACK (cursor_changed),
                                window_src);
      break;
    case META_SCREEN_CAST_CURSOR_MODE_HIDDEN:
      break;
    }

  flags = META_SCREEN_CAST_RECORD_FLAG_NONE;
  meta_screen_cast_stream_src_maybe_record_frame (src, flags);
}

static void
meta_screen_cast_window_stream_src_disable (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastWindowStreamSrc *window_src =
    META_SCREEN_CAST_WINDOW_STREAM_SRC (src);

  meta_screen_cast_window_stream_src_stop (window_src);
}

static gboolean
meta_screen_cast_window_stream_src_record_to_buffer (MetaScreenCastStreamSrc  *src,
                                                     uint8_t                  *data,
                                                     GError                  **error)
{
  MetaScreenCastWindowStreamSrc *window_src =
    META_SCREEN_CAST_WINDOW_STREAM_SRC (src);

  capture_into (window_src, data);

  return TRUE;
}

static gboolean
meta_screen_cast_window_stream_src_record_to_framebuffer (MetaScreenCastStreamSrc  *src,
                                                          CoglFramebuffer          *framebuffer,
                                                          GError                  **error)
{
  MetaScreenCastWindowStreamSrc *window_src =
    META_SCREEN_CAST_WINDOW_STREAM_SRC (src);
  MetaScreenCastStream *stream;
  MetaRectangle stream_rect;

  stream_rect.x = 0;
  stream_rect.y = 0;
  stream_rect.width = get_stream_width (window_src);
  stream_rect.height = get_stream_height (window_src);

  if (!meta_screen_cast_window_blit_to_framebuffer (window_src->screen_cast_window,
                                                    &stream_rect,
                                                    framebuffer))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to blit window content to framebuffer");
      return FALSE;
    }

  stream = meta_screen_cast_stream_src_get_stream (src);
  switch (meta_screen_cast_stream_get_cursor_mode (stream))
    {
    case META_SCREEN_CAST_CURSOR_MODE_EMBEDDED:
      maybe_blit_cursor_sprite (window_src, framebuffer, &stream_rect);
      break;
    case META_SCREEN_CAST_CURSOR_MODE_METADATA:
    case META_SCREEN_CAST_CURSOR_MODE_HIDDEN:
      break;
    }

  cogl_framebuffer_finish (framebuffer);

  return TRUE;
}

static void
meta_screen_cast_window_stream_record_follow_up (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastRecordFlag flags;

  flags = META_SCREEN_CAST_RECORD_FLAG_NONE;
  meta_screen_cast_stream_src_maybe_record_frame (src, flags);
}

static void
meta_screen_cast_window_stream_src_set_cursor_metadata (MetaScreenCastStreamSrc *src,
                                                        struct spa_meta_cursor  *spa_meta_cursor)
{
  MetaScreenCastWindowStreamSrc *window_src =
    META_SCREEN_CAST_WINDOW_STREAM_SRC (src);
  MetaBackend *backend = get_backend (window_src);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  MetaScreenCastWindow *screen_cast_window = window_src->screen_cast_window;
  MetaCursorSprite *cursor_sprite;
  graphene_point_t cursor_position;
  float scale;
  graphene_point_t relative_cursor_position;
  int x, y;

  cursor_sprite = meta_cursor_renderer_get_cursor (cursor_renderer);
  cursor_position = meta_cursor_renderer_get_position (cursor_renderer);

  if (!meta_screen_cast_window_transform_cursor_position (screen_cast_window,
                                                          cursor_sprite,
                                                          &cursor_position,
                                                          &scale,
                                                          &relative_cursor_position))
    {
      meta_screen_cast_stream_src_unset_cursor_metadata (src,
                                                         spa_meta_cursor);
      return;
    }

  x = (int) roundf (relative_cursor_position.x);
  y = (int) roundf (relative_cursor_position.y);

  if (window_src->cursor_bitmap_invalid)
    {
      if (cursor_sprite)
        {
          meta_screen_cast_stream_src_set_cursor_sprite_metadata (src,
                                                                  spa_meta_cursor,
                                                                  cursor_sprite,
                                                                  x, y,
                                                                  scale);
        }
      else
        {
          meta_screen_cast_stream_src_set_empty_cursor_sprite_metadata (src,
                                                                        spa_meta_cursor,
                                                                        x, y);
        }
      window_src->cursor_bitmap_invalid = FALSE;
    }
  else
    {
      meta_screen_cast_stream_src_set_cursor_position_metadata (src,
                                                                spa_meta_cursor,
                                                                x, y);
    }
}

MetaScreenCastWindowStreamSrc *
meta_screen_cast_window_stream_src_new (MetaScreenCastWindowStream  *window_stream,
                                        GError                     **error)
{
  return g_initable_new (META_TYPE_SCREEN_CAST_WINDOW_STREAM_SRC, NULL, error,
                         "stream", window_stream,
                         NULL);
}

static void
meta_screen_cast_window_stream_src_init (MetaScreenCastWindowStreamSrc *window_src)
{
  window_src->cursor_bitmap_invalid = TRUE;
}

static void
meta_screen_cast_window_stream_src_class_init (MetaScreenCastWindowStreamSrcClass *klass)
{
  MetaScreenCastStreamSrcClass *src_class =
    META_SCREEN_CAST_STREAM_SRC_CLASS (klass);

  src_class->get_specs = meta_screen_cast_window_stream_src_get_specs;
  src_class->enable = meta_screen_cast_window_stream_src_enable;
  src_class->disable = meta_screen_cast_window_stream_src_disable;
  src_class->record_to_buffer =
    meta_screen_cast_window_stream_src_record_to_buffer;
  src_class->record_to_framebuffer =
    meta_screen_cast_window_stream_src_record_to_framebuffer;
  src_class->record_follow_up =
    meta_screen_cast_window_stream_record_follow_up;
  src_class->get_videocrop = meta_screen_cast_window_stream_src_get_videocrop;
  src_class->set_cursor_metadata = meta_screen_cast_window_stream_src_set_cursor_metadata;
}
