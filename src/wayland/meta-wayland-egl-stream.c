/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat Inc.
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
 */

#include "config.h"

#include "wayland/meta-wayland-egl-stream.h"

#include <dlfcn.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-egl-ext.h"
#include "backends/meta-egl.h"
#include "cogl/cogl-egl.h"
#include "meta/meta-backend.h"
#include "wayland/meta-wayland-buffer.h"
#include "wayland/meta-wayland-private.h"

#include "wayland-eglstream-controller-server-protocol.h"

static struct wl_interface *wl_eglstream_controller_interface_ptr = NULL;

static void
attach_eglstream_consumer (struct wl_client   *client,
                           struct wl_resource *resource,
                           struct wl_resource *wl_surface,
                           struct wl_resource *wl_eglstream)
{
  MetaWaylandBuffer *buffer = meta_wayland_buffer_from_resource (wl_eglstream);

  if (!meta_wayland_buffer_is_realized (buffer))
    meta_wayland_buffer_realize (buffer);
}

static const struct wl_eglstream_controller_interface
meta_eglstream_controller_interface = {
  attach_eglstream_consumer
};

static void
bind_eglstream_controller (struct wl_client *client,
                           void             *data,
                           uint32_t          version,
                           uint32_t          id)
{
  struct wl_resource *resource;

  g_assert (wl_eglstream_controller_interface_ptr != NULL);

  resource = wl_resource_create (client,
                                 wl_eglstream_controller_interface_ptr,
                                 version,
                                 id);

  if (resource == NULL)
    {
      wl_client_post_no_memory(client);
      return;
    }

  wl_resource_set_implementation (resource,
                                  &meta_eglstream_controller_interface,
                                  data,
                                  NULL);
}

gboolean
meta_wayland_eglstream_controller_init (MetaWaylandCompositor *compositor)
{
  /*
   * wl_eglstream_controller_interface is provided by
   * libnvidia-egl-wayland.so.1
   *
   * Since it might not be available on the
   * system, dynamically load it at runtime and resolve the needed
   * symbols. If available, it should be found under any of the search
   * directories of dlopen()
   *
   * Failure to initialize wl_eglstream_controller is non-fatal
   */

  void *lib = dlopen ("libnvidia-egl-wayland.so.1", RTLD_NOW | RTLD_LAZY);
  if (!lib)
    goto fail;

  wl_eglstream_controller_interface_ptr =
    dlsym (lib, "wl_eglstream_controller_interface");

  if (!wl_eglstream_controller_interface_ptr)
    goto fail;

  if (wl_global_create (compositor->wayland_display,
                        wl_eglstream_controller_interface_ptr, 1,
                        NULL,
                        bind_eglstream_controller) == NULL)
    goto fail;

  g_debug ("WL: loaded libnvidia-egl-wayland.so.1:wl_eglstream_controller.");

  return TRUE;

fail:
  if (lib)
    dlclose(lib);

  g_debug ("WL: Unable to initialize wl_eglstream_controller.");

  return FALSE;
}

struct _MetaWaylandEglStream
{
  GObject parent;

  EGLStreamKHR egl_stream;
  MetaWaylandBuffer *buffer;
  CoglTexture2D *texture;
  gboolean is_y_inverted;
  CoglSnippet *snippet;
};

G_DEFINE_TYPE (MetaWaylandEglStream, meta_wayland_egl_stream,
               G_TYPE_OBJECT)

MetaWaylandEglStream *
meta_wayland_egl_stream_new (MetaWaylandBuffer *buffer,
                             GError           **error)
{
  MetaBackend *backend = meta_get_backend ();
  MetaEgl *egl = meta_backend_get_egl (backend);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  EGLDisplay egl_display = cogl_egl_context_get_egl_display (cogl_context);
  EGLAttrib stream_attribs[] = {
    EGL_WAYLAND_EGLSTREAM_WL, (EGLAttrib) buffer->resource,
    EGL_NONE
  };
  EGLStreamKHR egl_stream;
  MetaWaylandEglStream *stream;

  egl_stream = meta_egl_create_stream_attrib (egl, egl_display, stream_attribs,
                                              error);
  if (egl_stream == EGL_NO_STREAM_KHR)
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Failed to create stream from wl_buffer resource");
      return NULL;
    }

  stream = g_object_new (META_TYPE_WAYLAND_EGL_STREAM, NULL);
  stream->egl_stream = egl_stream;
  stream->buffer = buffer;

  return stream;
}

static void
stream_texture_destroyed (gpointer data)
{
  MetaWaylandEglStream *stream = data;

  stream->texture = NULL;

  g_object_unref (stream);
}

static gboolean
alloc_egl_stream_texture (CoglTexture2D *texture,
                          gpointer       user_data,
                          GError       **error)
{
  MetaBackend *backend = meta_get_backend ();
  MetaEgl *egl = meta_backend_get_egl (backend);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  EGLDisplay egl_display = cogl_egl_context_get_egl_display (cogl_context);
  MetaWaylandEglStream *stream = user_data;

  return meta_egl_stream_consumer_gl_texture_external (egl, egl_display,
                                                       stream->egl_stream,
                                                       error);
}

CoglTexture2D *
meta_wayland_egl_stream_create_texture (MetaWaylandEglStream *stream,
                                        GError              **error)
{
  MetaBackend *backend = meta_get_backend ();
  MetaEgl *egl = meta_backend_get_egl (backend);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  EGLDisplay egl_display = cogl_egl_context_get_egl_display (cogl_context);
  CoglTexture2D *texture;
  int width, height;
  int y_inverted;

  if (!meta_egl_query_wayland_buffer (egl, egl_display,
                                      stream->buffer->resource,
                                      EGL_WIDTH, &width,
                                      error))
    return NULL;

  if (!meta_egl_query_wayland_buffer (egl, egl_display,
                                      stream->buffer->resource,
                                      EGL_HEIGHT, &height,
                                      error))
    return NULL;

  if (!meta_egl_query_wayland_buffer (egl, egl_display,
                                      stream->buffer->resource,
                                      EGL_WAYLAND_Y_INVERTED_WL, &y_inverted,
                                      NULL))
    y_inverted = EGL_TRUE;

  texture =
    cogl_texture_2d_new_from_egl_image_external (cogl_context,
                                                 width, height,
                                                 alloc_egl_stream_texture,
                                                 g_object_ref (stream),
                                                 stream_texture_destroyed,
                                                 error);
  if (!texture)
    {
      g_object_unref (stream);
      return NULL;
    }

  if (!cogl_texture_allocate (COGL_TEXTURE (texture), error))
    {
      cogl_object_unref (texture);
      return NULL;
    }

  stream->texture = texture;
  stream->is_y_inverted = !!y_inverted;

  return texture;
}

gboolean
meta_wayland_egl_stream_attach (MetaWaylandEglStream *stream,
                                GError              **error)
{
  MetaBackend *backend = meta_get_backend ();
  MetaEgl *egl = meta_backend_get_egl (backend);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  EGLDisplay egl_display = cogl_egl_context_get_egl_display (cogl_context);
  EGLint stream_state;

  if (!meta_egl_query_stream (egl, egl_display, stream->egl_stream,
                              EGL_STREAM_STATE_KHR, &stream_state,
                              error))
    return FALSE;

  if (stream_state == EGL_STREAM_STATE_NEW_FRAME_AVAILABLE_KHR)
    {
      if (!meta_egl_stream_consumer_acquire (egl, egl_display,
                                             stream->egl_stream,
                                             error))
        return FALSE;
    }

  return TRUE;
}

gboolean
meta_wayland_egl_stream_is_y_inverted (MetaWaylandEglStream *stream)
{
  return stream->is_y_inverted;
}

CoglSnippet *
meta_wayland_egl_stream_create_snippet (MetaWaylandEglStream *stream)
{
  if (!stream->snippet)
    {
      CoglSnippet *snippet;

      snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_TEXTURE_LOOKUP,
                                  "uniform samplerExternalOES tex_external;",
                                  NULL);
      cogl_snippet_set_replace (snippet,
                                "cogl_texel = texture2D (tex_external,\n"
                                "                        cogl_tex_coord.xy);");
      stream->snippet = snippet;
    }

  return cogl_object_ref (stream->snippet);
}

gboolean
meta_wayland_is_egl_stream_buffer (MetaWaylandBuffer *buffer)
{
  MetaBackend *backend = meta_get_backend ();
  MetaEgl *egl = meta_backend_get_egl (backend);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  EGLDisplay egl_display = cogl_egl_context_get_egl_display (cogl_context);
  int stream_fd;

  if (!meta_egl_has_extensions (egl, egl_display, NULL,
                                "EGL_KHR_stream_consumer_gltexture",
                                "EGL_KHR_stream_cross_process_fd",
                                NULL))
    return FALSE;

  if (!meta_egl_query_wayland_buffer (egl, egl_display, buffer->resource,
                                      EGL_WAYLAND_BUFFER_WL, &stream_fd,
                                      NULL))
    return FALSE;

  return TRUE;
}

static void
meta_wayland_egl_stream_finalize (GObject *object)
{
  MetaWaylandEglStream *stream = META_WAYLAND_EGL_STREAM (object);
  MetaBackend *backend = meta_get_backend ();
  MetaEgl *egl = meta_backend_get_egl (backend);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  EGLDisplay egl_display = cogl_egl_context_get_egl_display (cogl_context);

  g_assert (!stream->texture);

  meta_egl_destroy_stream (egl, egl_display, stream->egl_stream, NULL);

  cogl_clear_object (&stream->snippet);

  G_OBJECT_CLASS (meta_wayland_egl_stream_parent_class)->finalize (object);
}

static void
meta_wayland_egl_stream_init (MetaWaylandEglStream *stream)
{
}

static void
meta_wayland_egl_stream_class_init (MetaWaylandEglStreamClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_wayland_egl_stream_finalize;
}
