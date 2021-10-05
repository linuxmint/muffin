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

#ifndef META_SCREEN_CAST_STREAM_SRC_H
#define META_SCREEN_CAST_STREAM_SRC_H

#include <glib-object.h>
#include <spa/param/video/format-utils.h>
#include <spa/buffer/meta.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-cursor-renderer.h"
#include "backends/meta-cursor.h"
#include "backends/meta-renderer.h"
#include "clutter/clutter.h"
#include "cogl/cogl.h"
#include "meta/boxes.h"

typedef struct _MetaScreenCastStream MetaScreenCastStream;

typedef enum _MetaScreenCastRecordFlag
{
  META_SCREEN_CAST_RECORD_FLAG_NONE = 0,
  META_SCREEN_CAST_RECORD_FLAG_CURSOR_ONLY = 1 << 0,
} MetaScreenCastRecordFlag;

#define META_TYPE_SCREEN_CAST_STREAM_SRC (meta_screen_cast_stream_src_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaScreenCastStreamSrc,
                          meta_screen_cast_stream_src,
                          META, SCREEN_CAST_STREAM_SRC,
                          GObject)

struct _MetaScreenCastStreamSrcClass
{
  GObjectClass parent_class;

  void (* get_specs) (MetaScreenCastStreamSrc *src,
                      int                     *width,
                      int                     *height,
                      float                   *frame_rate);
  void (* enable) (MetaScreenCastStreamSrc *src);
  void (* disable) (MetaScreenCastStreamSrc *src);
  gboolean (* record_to_buffer) (MetaScreenCastStreamSrc  *src,
                                 uint8_t                  *data,
                                 GError                  **error);
  gboolean (* record_to_framebuffer) (MetaScreenCastStreamSrc  *src,
                                      CoglFramebuffer          *framebuffer,
                                      GError                  **error);
  void (* record_follow_up) (MetaScreenCastStreamSrc *src);

  gboolean (* get_videocrop) (MetaScreenCastStreamSrc *src,
                              MetaRectangle           *crop_rect);
  void (* set_cursor_metadata) (MetaScreenCastStreamSrc *src,
                                struct spa_meta_cursor  *spa_meta_cursor);
};

void meta_screen_cast_stream_src_maybe_record_frame (MetaScreenCastStreamSrc  *src,
                                                     MetaScreenCastRecordFlag  flags);

gboolean meta_screen_cast_stream_src_pending_follow_up_frame (MetaScreenCastStreamSrc *src);

MetaScreenCastStream * meta_screen_cast_stream_src_get_stream (MetaScreenCastStreamSrc *src);

gboolean meta_screen_cast_stream_src_draw_cursor_into (MetaScreenCastStreamSrc  *src,
                                                       CoglTexture              *cursor_texture,
                                                       float                     scale,
                                                       uint8_t                  *data,
                                                       GError                  **error);

void meta_screen_cast_stream_src_unset_cursor_metadata (MetaScreenCastStreamSrc *src,
                                                        struct spa_meta_cursor  *spa_meta_cursor);

void meta_screen_cast_stream_src_set_cursor_position_metadata (MetaScreenCastStreamSrc *src,
                                                               struct spa_meta_cursor  *spa_meta_cursor,
                                                               int                      x,
                                                               int                      y);

void meta_screen_cast_stream_src_set_empty_cursor_sprite_metadata (MetaScreenCastStreamSrc *src,
                                                                   struct spa_meta_cursor  *spa_meta_cursor,
                                                                   int                      x,
                                                                   int                      y);

void meta_screen_cast_stream_src_set_cursor_sprite_metadata (MetaScreenCastStreamSrc *src,
                                                             struct spa_meta_cursor  *spa_meta_cursor,
                                                             MetaCursorSprite        *cursor_sprite,
                                                             int                      x,
                                                             int                      y,
                                                             float                    scale);

#endif /* META_SCREEN_CAST_STREAM_SRC_H */
