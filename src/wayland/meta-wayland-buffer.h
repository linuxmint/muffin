/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Endless Mobile
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
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#ifndef META_WAYLAND_BUFFER_H
#define META_WAYLAND_BUFFER_H

#include <cairo.h>
#include <wayland-server.h>

#include "cogl/cogl.h"
#include "wayland/meta-wayland-types.h"
#include "wayland/meta-wayland-egl-stream.h"
#include "wayland/meta-wayland-dma-buf.h"

typedef enum _MetaWaylandBufferType
{
  META_WAYLAND_BUFFER_TYPE_UNKNOWN,
  META_WAYLAND_BUFFER_TYPE_SHM,
  META_WAYLAND_BUFFER_TYPE_EGL_IMAGE,
#ifdef HAVE_WAYLAND_EGLSTREAM
  META_WAYLAND_BUFFER_TYPE_EGL_STREAM,
#endif
  META_WAYLAND_BUFFER_TYPE_DMA_BUF,
} MetaWaylandBufferType;

struct _MetaWaylandBuffer
{
  GObject parent;

  struct wl_resource *resource;
  struct wl_listener destroy_listener;

  gboolean is_y_inverted;

  MetaWaylandBufferType type;

  struct {
    CoglTexture *texture;
  } egl_image;

#ifdef HAVE_WAYLAND_EGLSTREAM
  struct {
    MetaWaylandEglStream *stream;
    CoglTexture *texture;
  } egl_stream;
#endif

  struct {
    MetaWaylandDmaBufBuffer *dma_buf;
    CoglTexture *texture;
  } dma_buf;
};

#define META_TYPE_WAYLAND_BUFFER (meta_wayland_buffer_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandBuffer, meta_wayland_buffer,
                      META, WAYLAND_BUFFER, GObject);

MetaWaylandBuffer *     meta_wayland_buffer_from_resource       (struct wl_resource    *resource);
struct wl_resource *    meta_wayland_buffer_get_resource        (MetaWaylandBuffer     *buffer);
gboolean                meta_wayland_buffer_is_realized         (MetaWaylandBuffer     *buffer);
gboolean                meta_wayland_buffer_realize             (MetaWaylandBuffer     *buffer);
gboolean                meta_wayland_buffer_attach              (MetaWaylandBuffer     *buffer,
                                                                 CoglTexture          **texture,
                                                                 GError               **error);
CoglSnippet *           meta_wayland_buffer_create_snippet      (MetaWaylandBuffer     *buffer);
gboolean                meta_wayland_buffer_is_y_inverted       (MetaWaylandBuffer     *buffer);
void                    meta_wayland_buffer_process_damage      (MetaWaylandBuffer     *buffer,
                                                                 CoglTexture           *texture,
                                                                 cairo_region_t        *region);

CoglScanout *           meta_wayland_buffer_try_acquire_scanout (MetaWaylandBuffer     *buffer,
                                                                 CoglOnscreen          *onscreen);

#endif /* META_WAYLAND_BUFFER_H */
