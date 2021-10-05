/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat Inc.
 * Copyright (C) 2017 Intel Corporation
 * Copyright (C) 2018,2019 DisplayLink (UK) Ltd.
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
 *     Jonas Ådahl <jadahl@gmail.com>
 *     Daniel Stone <daniels@collabora.com>
 */

/**
 * SECTION:meta-wayland-dma-buf
 * @title: MetaWaylandDmaBuf
 * @short_description: Handles passing DMA-BUFs in Wayland
 *
 * The MetaWaylandDmaBuf namespace contains several objects and functions to
 * handle DMA-BUF buffers that are passed through from clients in Wayland (e.g.
 * using the linux_dmabuf_unstable_v1 protocol).
 */

#include "config.h"

#include "wayland/meta-wayland-dma-buf.h"

#include <drm_fourcc.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-egl-ext.h"
#include "backends/meta-egl.h"
#include "cogl/cogl-egl.h"
#include "cogl/cogl.h"
#include "meta/meta-backend.h"
#include "wayland/meta-wayland-buffer.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-versions.h"

#include "linux-dmabuf-unstable-v1-server-protocol.h"

#ifndef DRM_FORMAT_MOD_INVALID
#define DRM_FORMAT_MOD_INVALID ((1ULL << 56) - 1)
#endif

#define META_WAYLAND_DMA_BUF_MAX_FDS 4

struct _MetaWaylandDmaBufBuffer
{
  GObject parent;

  int width;
  int height;
  uint32_t drm_format;
  uint64_t drm_modifier;
  bool is_y_inverted;
  int fds[META_WAYLAND_DMA_BUF_MAX_FDS];
  uint32_t offsets[META_WAYLAND_DMA_BUF_MAX_FDS];
  uint32_t strides[META_WAYLAND_DMA_BUF_MAX_FDS];
};

G_DEFINE_TYPE (MetaWaylandDmaBufBuffer, meta_wayland_dma_buf_buffer, G_TYPE_OBJECT);

static gboolean
meta_wayland_dma_buf_realize_texture (MetaWaylandBuffer  *buffer,
                                      GError            **error)
{
  MetaBackend *backend = meta_get_backend ();
  MetaEgl *egl = meta_backend_get_egl (backend);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  EGLDisplay egl_display = cogl_egl_context_get_egl_display (cogl_context);
  MetaWaylandDmaBufBuffer *dma_buf = buffer->dma_buf.dma_buf;
  uint32_t n_planes;
  uint64_t modifiers[META_WAYLAND_DMA_BUF_MAX_FDS];
  CoglPixelFormat cogl_format;
  EGLImageKHR egl_image;
  CoglEglImageFlags flags;
  CoglTexture2D *texture;

  if (buffer->dma_buf.texture)
    return TRUE;

  switch (dma_buf->drm_format)
    {
    /*
     * NOTE: The cogl_format here is only used for texture color channel
     * swizzling as compared to COGL_PIXEL_FORMAT_ARGB. It is *not* used
     * for accessing the buffer memory. EGL will access the buffer
     * memory according to the DRM fourcc code. Cogl will not mmap
     * and access the buffer memory at all.
     */
    case DRM_FORMAT_XRGB8888:
      cogl_format = COGL_PIXEL_FORMAT_RGB_888;
      break;
    case DRM_FORMAT_ARGB8888:
      cogl_format = COGL_PIXEL_FORMAT_ARGB_8888_PRE;
      break;
    case DRM_FORMAT_ARGB2101010:
      cogl_format = COGL_PIXEL_FORMAT_ARGB_2101010_PRE;
      break;
    case DRM_FORMAT_RGB565:
      cogl_format = COGL_PIXEL_FORMAT_RGB_565;
      break;
    default:
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Unsupported buffer format %d", dma_buf->drm_format);
      return FALSE;
    }

  for (n_planes = 0; n_planes < META_WAYLAND_DMA_BUF_MAX_FDS; n_planes++)
    {
      if (dma_buf->fds[n_planes] < 0)
        break;

      modifiers[n_planes] = dma_buf->drm_modifier;
    }

  egl_image = meta_egl_create_dmabuf_image (egl,
                                            egl_display,
                                            dma_buf->width,
                                            dma_buf->height,
                                            dma_buf->drm_format,
                                            n_planes,
                                            dma_buf->fds,
                                            dma_buf->strides,
                                            dma_buf->offsets,
                                            modifiers,
                                            error);
  if (egl_image == EGL_NO_IMAGE_KHR)
    return FALSE;

  flags = COGL_EGL_IMAGE_FLAG_NO_GET_DATA;
  texture = cogl_egl_texture_2d_new_from_image (cogl_context,
                                                dma_buf->width,
                                                dma_buf->height,
                                                cogl_format,
                                                egl_image,
                                                flags,
                                                error);

  meta_egl_destroy_image (egl, egl_display, egl_image, NULL);

  if (!texture)
    return FALSE;

  buffer->dma_buf.texture = COGL_TEXTURE (texture);
  buffer->is_y_inverted = dma_buf->is_y_inverted;

  return TRUE;
}

gboolean
meta_wayland_dma_buf_buffer_attach (MetaWaylandBuffer  *buffer,
                                    CoglTexture       **texture,
                                    GError            **error)
{
  if (!meta_wayland_dma_buf_realize_texture (buffer, error))
    return FALSE;

  cogl_clear_object (texture);
  *texture = cogl_object_ref (buffer->dma_buf.texture);
  return TRUE;
}

static void
buffer_params_add (struct wl_client   *client,
                   struct wl_resource *resource,
                   int32_t             fd,
                   uint32_t            plane_idx,
                   uint32_t            offset,
                   uint32_t            stride,
                   uint32_t            drm_modifier_hi,
                   uint32_t            drm_modifier_lo)
{
  MetaWaylandDmaBufBuffer *dma_buf;
  uint64_t drm_modifier;

  drm_modifier = ((uint64_t) drm_modifier_hi) << 32;
  drm_modifier |= ((uint64_t) drm_modifier_lo) & 0xffffffff;

  dma_buf = wl_resource_get_user_data (resource);
  if (!dma_buf)
    {
      wl_resource_post_error (resource,
                              ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED,
                              "params already used");
      return;
    }

  if (plane_idx >= META_WAYLAND_DMA_BUF_MAX_FDS)
    {
      wl_resource_post_error (resource,
                              ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX,
                              "out-of-bounds plane index %d",
                              plane_idx);
      return;
    }

  if (dma_buf->fds[plane_idx] != -1)
    {
      wl_resource_post_error (resource,
                              ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_SET,
                              "plane index %d already set",
                              plane_idx);
      return;
    }

  if (dma_buf->drm_modifier != DRM_FORMAT_MOD_INVALID &&
      dma_buf->drm_modifier != drm_modifier)
    {
      wl_resource_post_error (resource,
                              ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_WL_BUFFER,
                              "mismatching modifier between planes");
      return;
    }

  dma_buf->drm_modifier = drm_modifier;
  dma_buf->fds[plane_idx] = fd;
  dma_buf->offsets[plane_idx] = offset;
  dma_buf->strides[plane_idx] = stride;
}

static void
buffer_params_destroy (struct wl_client   *client,
                       struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
buffer_params_destructor (struct wl_resource *resource)
{
  MetaWaylandDmaBufBuffer *dma_buf;

  /* The user-data for our MetaWaylandBuffer is only valid in between adding
   * FDs and creating the buffer; once it is created, we free it out into
   * the wild, where the ref is considered transferred to the wl_buffer. */
  dma_buf = wl_resource_get_user_data (resource);
  if (dma_buf)
    g_object_unref (dma_buf);
}

static void
buffer_destroy (struct wl_client   *client,
                struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct wl_buffer_interface dma_buf_buffer_impl =
{
  buffer_destroy,
};

/**
 * meta_wayland_dma_buf_from_buffer:
 * @buffer: A #MetaWaylandBuffer object
 *
 * Fetches the associated #MetaWaylandDmaBufBuffer from the wayland buffer.
 * This does not *create* a new object, as this happens in the create_params
 * request of linux_dmabuf_unstable_v1.
 *
 * Returns: (transfer none): The corresponding #MetaWaylandDmaBufBuffer (or
 * %NULL if it wasn't a dma_buf-based wayland buffer)
 */
MetaWaylandDmaBufBuffer *
meta_wayland_dma_buf_from_buffer (MetaWaylandBuffer *buffer)
{
  if (wl_resource_instance_of (buffer->resource, &wl_buffer_interface,
                               &dma_buf_buffer_impl))
      return wl_resource_get_user_data (buffer->resource);

  return NULL;
}

static void
buffer_params_create_common (struct wl_client   *client,
                             struct wl_resource *params_resource,
                             uint32_t            buffer_id,
                             int32_t             width,
                             int32_t             height,
                             uint32_t            drm_format,
                             uint32_t            flags)
{
  MetaWaylandDmaBufBuffer *dma_buf;
  MetaWaylandBuffer *buffer;
  struct wl_resource *buffer_resource;
  GError *error = NULL;

  dma_buf = wl_resource_get_user_data (params_resource);
  if (!dma_buf)
    {
      wl_resource_post_error (params_resource,
                              ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED,
                              "params already used");
      return;
    }

  /* Calling the 'create' method is the point of no return: after that point,
   * the params object cannot be used. This method must either transfer the
   * ownership of the MetaWaylandDmaBufBuffer to a MetaWaylandBuffer, or
   * destroy it. */
  wl_resource_set_user_data (params_resource, NULL);

  if (dma_buf->fds[0] == -1)
    {
      wl_resource_post_error (params_resource,
                              ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE,
                              "no planes added to params");
      g_object_unref (dma_buf);
      return;
    }

  if ((dma_buf->fds[3] >= 0 || dma_buf->fds[2] >= 0) &&
      (dma_buf->fds[2] == -1 || dma_buf->fds[1] == -1))
    {
      wl_resource_post_error (params_resource,
                              ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE,
                              "gap in planes added to params");
      g_object_unref (dma_buf);
      return;
    }

  dma_buf->width = width;
  dma_buf->height = height;
  dma_buf->drm_format = drm_format;
  dma_buf->is_y_inverted = !(flags & ZWP_LINUX_BUFFER_PARAMS_V1_FLAGS_Y_INVERT);

  if (flags & ~ZWP_LINUX_BUFFER_PARAMS_V1_FLAGS_Y_INVERT)
    {
      wl_resource_post_error (params_resource,
                              ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_WL_BUFFER,
                              "unknown flags 0x%x supplied", flags);
      g_object_unref (dma_buf);
      return;
    }

  /* Create a new MetaWaylandBuffer wrapping our dmabuf, and immediately try
   * to realize it, so we can give the client success/fail feedback for the
   * import. */
  buffer_resource =
    wl_resource_create (client, &wl_buffer_interface, 1, buffer_id);
  wl_resource_set_implementation (buffer_resource, &dma_buf_buffer_impl,
                                  dma_buf, NULL);
  buffer = meta_wayland_buffer_from_resource (buffer_resource);

  meta_wayland_buffer_realize (buffer);
  if (!meta_wayland_dma_buf_realize_texture (buffer, &error))
    {
      if (buffer_id == 0)
        {
          zwp_linux_buffer_params_v1_send_failed (params_resource);
        }
      else
        {
          wl_resource_post_error (params_resource,
                                  ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_WL_BUFFER,
                                  "failed to import supplied dmabufs: %s",
                                  error ? error->message : "unknown error");
        }

        /* will unref the MetaWaylandBuffer */
        wl_resource_destroy (buffer->resource);
        return;
    }

    /* If buffer_id is 0, we are using the non-immediate interface, so
     * need to send a success event with our buffer. */
    if (buffer_id == 0)
      zwp_linux_buffer_params_v1_send_created (params_resource,
                                               buffer->resource);
}

static void
buffer_params_create (struct wl_client   *client,
                      struct wl_resource *params_resource,
                      int32_t             width,
                      int32_t             height,
                      uint32_t            format,
                      uint32_t            flags)
{
  buffer_params_create_common (client, params_resource, 0, width, height,
                               format, flags);
}

static void
buffer_params_create_immed (struct wl_client   *client,
                            struct wl_resource *params_resource,
                            uint32_t            buffer_id,
                            int32_t             width,
                            int32_t             height,
                            uint32_t            format,
                            uint32_t            flags)
{
  buffer_params_create_common (client, params_resource, buffer_id, width,
                               height, format, flags);
}

static const struct zwp_linux_buffer_params_v1_interface buffer_params_implementation =
{
  buffer_params_destroy,
  buffer_params_add,
  buffer_params_create,
  buffer_params_create_immed,
};

static void
dma_buf_handle_destroy (struct wl_client   *client,
                        struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
dma_buf_handle_create_buffer_params (struct wl_client   *client,
                                     struct wl_resource *dma_buf_resource,
                                     uint32_t            params_id)
{
  struct wl_resource *params_resource;
  MetaWaylandDmaBufBuffer *dma_buf;

  dma_buf = g_object_new (META_TYPE_WAYLAND_DMA_BUF_BUFFER, NULL);

  params_resource =
    wl_resource_create (client,
                        &zwp_linux_buffer_params_v1_interface,
                        wl_resource_get_version (dma_buf_resource),
                        params_id);
  wl_resource_set_implementation (params_resource,
                                  &buffer_params_implementation,
                                  dma_buf,
                                  buffer_params_destructor);
}

static const struct zwp_linux_dmabuf_v1_interface dma_buf_implementation =
{
  dma_buf_handle_destroy,
  dma_buf_handle_create_buffer_params,
};

static void
send_modifiers (struct wl_resource *resource,
                uint32_t            format)
{
  MetaBackend *backend = meta_get_backend ();
  MetaEgl *egl = meta_backend_get_egl (backend);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  EGLDisplay egl_display = cogl_egl_context_get_egl_display (cogl_context);
  EGLint num_modifiers;
  EGLuint64KHR *modifiers;
  GError *error = NULL;
  gboolean ret;
  int i;

  zwp_linux_dmabuf_v1_send_format (resource, format);

  /* The modifier event was only added in v3; v1 and v2 only have the format
   * event. */
  if (wl_resource_get_version (resource) < ZWP_LINUX_DMABUF_V1_MODIFIER_SINCE_VERSION)
    return;

  /* First query the number of available modifiers, then allocate an array,
   * then fill the array. */
  ret = meta_egl_query_dma_buf_modifiers (egl, egl_display, format, 0, NULL,
                                          NULL, &num_modifiers, NULL);
  if (!ret)
    return;

  if (num_modifiers == 0)
    {
      zwp_linux_dmabuf_v1_send_modifier (resource, format,
                                         DRM_FORMAT_MOD_INVALID >> 32,
                                         DRM_FORMAT_MOD_INVALID & 0xffffffff);
      return;
    }

  modifiers = g_new0 (uint64_t, num_modifiers);
  ret = meta_egl_query_dma_buf_modifiers (egl, egl_display, format,
                                          num_modifiers, modifiers, NULL,
                                          &num_modifiers, &error);
  if (!ret)
    {
      g_warning ("Failed to query modifiers for format 0x%" PRIu32 ": %s",
                 format, error ? error->message : "unknown error");
      g_free (modifiers);
      return;
    }

  for (i = 0; i < num_modifiers; i++)
    {
      zwp_linux_dmabuf_v1_send_modifier (resource, format,
                                         modifiers[i] >> 32,
                                         modifiers[i] & 0xffffffff);
    }

  g_free (modifiers);
}

static void
dma_buf_bind (struct wl_client *client,
              void             *data,
              uint32_t          version,
              uint32_t          id)
{
  MetaWaylandCompositor *compositor = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client, &zwp_linux_dmabuf_v1_interface,
                                 version, id);
  wl_resource_set_implementation (resource, &dma_buf_implementation,
                                  compositor, NULL);
  send_modifiers (resource, DRM_FORMAT_ARGB8888);
  send_modifiers (resource, DRM_FORMAT_XRGB8888);
  send_modifiers (resource, DRM_FORMAT_ARGB2101010);
  send_modifiers (resource, DRM_FORMAT_RGB565);
}

/**
 * meta_wayland_dma_buf_init:
 * @compositor: The #MetaWaylandCompositor
 *
 * Creates the global Wayland object that exposes the linux-dmabuf protocol.
 *
 * Returns: Whether the initialization was succesfull. If this is %FALSE,
 * clients won't be able to use the linux-dmabuf protocol to pass buffers.
 */
gboolean
meta_wayland_dma_buf_init (MetaWaylandCompositor *compositor)
{
  MetaBackend *backend = meta_get_backend ();
  MetaEgl *egl = meta_backend_get_egl (backend);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  EGLDisplay egl_display = cogl_egl_context_get_egl_display (cogl_context);

  g_assert (backend && egl && clutter_backend && cogl_context && egl_display);

  if (!meta_egl_has_extensions (egl, egl_display, NULL,
                                "EGL_EXT_image_dma_buf_import_modifiers",
                                NULL))
    return FALSE;

  if (!wl_global_create (compositor->wayland_display,
                         &zwp_linux_dmabuf_v1_interface,
                         META_ZWP_LINUX_DMABUF_V1_VERSION,
                         compositor,
                         dma_buf_bind))
    return FALSE;

  return TRUE;
}

static void
meta_wayland_dma_buf_buffer_finalize (GObject *object)
{
  MetaWaylandDmaBufBuffer *dma_buf = META_WAYLAND_DMA_BUF_BUFFER (object);
  int i;

  for (i = 0; i < META_WAYLAND_DMA_BUF_MAX_FDS; i++)
    {
      if (dma_buf->fds[i] != -1)
        close (dma_buf->fds[i]);
    }

  G_OBJECT_CLASS (meta_wayland_dma_buf_buffer_parent_class)->finalize (object);
}

static void
meta_wayland_dma_buf_buffer_init (MetaWaylandDmaBufBuffer *dma_buf)
{
  int i;

  dma_buf->drm_modifier = DRM_FORMAT_MOD_INVALID;

  for (i = 0; i < META_WAYLAND_DMA_BUF_MAX_FDS; i++)
    dma_buf->fds[i] = -1;
}

static void
meta_wayland_dma_buf_buffer_class_init (MetaWaylandDmaBufBufferClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_wayland_dma_buf_buffer_finalize;
}
