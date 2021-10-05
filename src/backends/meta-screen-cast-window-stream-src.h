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

#ifndef META_SCREEN_CAST_WINDOW_STREAM_SRC_H
#define META_SCREEN_CAST_WINDOW_STREAM_SRC_H

#include "backends/meta-screen-cast-stream-src.h"

typedef struct _MetaScreenCastWindowStream MetaScreenCastWindowStream;

#define META_TYPE_SCREEN_CAST_WINDOW_STREAM_SRC (meta_screen_cast_window_stream_src_get_type ())
G_DECLARE_FINAL_TYPE (MetaScreenCastWindowStreamSrc,
                      meta_screen_cast_window_stream_src,
                      META, SCREEN_CAST_WINDOW_STREAM_SRC,
                      MetaScreenCastStreamSrc)

MetaScreenCastWindowStreamSrc * meta_screen_cast_window_stream_src_new (MetaScreenCastWindowStream  *window_stream,
                                                                        GError                     **error);

#endif /* META_SCREEN_CAST_WINDOW_STREAM_SRC_H */
