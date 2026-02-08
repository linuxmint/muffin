/*
 * Copyright (C) 2022 Red Hat Inc.
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

#include "wayland/meta-wayland-single-pixel-buffer.h"

#include "backends/meta-backend-private.h"
#include "wayland/meta-wayland-buffer.h"
#include "wayland/meta-wayland-private.h"

#include "single-pixel-buffer-v1-server-protocol.h"

struct _MetaWaylandSinglePixelBuffer
{
  uint32_t r;
  uint32_t g;
  uint32_t b;
  uint32_t a;
};

static void
buffer_destroy (struct wl_client   *client,
                struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct wl_buffer_interface single_pixel_buffer_implementation =
{
  buffer_destroy,
};

static void
single_pixel_buffer_manager_destroy (struct wl_client *client,
                                     struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
single_pixel_buffer_manager_create_1px_rgba32_buffer (struct wl_client   *client,
                                                      struct wl_resource *resource,
                                                      uint32_t            buffer_id,
                                                      uint32_t            r,
                                                      uint32_t            g,
                                                      uint32_t            b,
                                                      uint32_t            a)
{
  MetaWaylandSinglePixelBuffer *single_pixel_buffer;
  struct wl_resource *buffer_resource;

  single_pixel_buffer = g_new0 (MetaWaylandSinglePixelBuffer, 1);
  single_pixel_buffer->r = r;
  single_pixel_buffer->g = g;
  single_pixel_buffer->b = b;
  single_pixel_buffer->a = a;

  buffer_resource =
    wl_resource_create (client, &wl_buffer_interface, 1, buffer_id);
  wl_resource_set_implementation (buffer_resource,
                                  &single_pixel_buffer_implementation,
                                  single_pixel_buffer, NULL);
  meta_wayland_buffer_from_resource (buffer_resource);
}

static const struct wp_single_pixel_buffer_manager_v1_interface
  single_pixel_buffer_manager_implementation =
{
  single_pixel_buffer_manager_destroy,
  single_pixel_buffer_manager_create_1px_rgba32_buffer,
};

static void
single_pixel_buffer_manager_bind (struct wl_client *client,
                                  void             *user_data,
                                  uint32_t          version,
                                  uint32_t          id)
{
  MetaWaylandCompositor *compositor = user_data;
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &wp_single_pixel_buffer_manager_v1_interface,
                                 version, id);
  wl_resource_set_implementation (resource,
                                  &single_pixel_buffer_manager_implementation,
                                  compositor, NULL);
}

gboolean
meta_wayland_single_pixel_buffer_attach (MetaWaylandBuffer  *buffer,
                                         CoglTexture       **texture,
                                         GError            **error)
{
  MetaBackend *backend = meta_get_backend ();
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  MetaWaylandSinglePixelBuffer *single_pixel_buffer =
    wl_resource_get_user_data (buffer->resource);
  uint8_t data[4];
  CoglPixelFormat pixel_format;
  CoglTexture2D *tex_2d;

  if (buffer->single_pixel.texture)
    {
      *texture = g_object_ref (buffer->single_pixel.texture);
      return TRUE;
    }

  data[0] = single_pixel_buffer->b / (UINT32_MAX / 0xff);
  data[1] = single_pixel_buffer->g / (UINT32_MAX / 0xff);
  data[2] = single_pixel_buffer->r / (UINT32_MAX / 0xff);
  data[3] = single_pixel_buffer->a / (UINT32_MAX / 0xff);

  if (data[3] == UINT8_MAX)
    pixel_format = COGL_PIXEL_FORMAT_BGR_888;
  else
    pixel_format = COGL_PIXEL_FORMAT_BGRA_8888_PRE;

  tex_2d = cogl_texture_2d_new_from_data (cogl_context,
                                          1, 1,
                                          pixel_format,
                                          4, data,
                                          error);
  if (!tex_2d)
    return FALSE;

  buffer->single_pixel.texture = COGL_TEXTURE (tex_2d);

  cogl_clear_object (texture);
  *texture = cogl_object_ref (buffer->single_pixel.texture);
  return TRUE;
}

MetaWaylandSinglePixelBuffer *
meta_wayland_single_pixel_buffer_from_buffer (MetaWaylandBuffer *buffer)
{
  if (!buffer->resource)
    return NULL;

  if (wl_resource_instance_of (buffer->resource, &wl_buffer_interface,
                               &single_pixel_buffer_implementation))
    return wl_resource_get_user_data (buffer->resource);

  return NULL;
}

void
meta_wayland_single_pixel_buffer_free (MetaWaylandSinglePixelBuffer *single_pixel_buffer)
{
  g_free (single_pixel_buffer);
}

gboolean
meta_wayland_single_pixel_buffer_is_opaque_black (MetaWaylandSinglePixelBuffer *single_pixel_buffer)
{
  return (single_pixel_buffer->a == UINT32_MAX &&
  single_pixel_buffer->r == 0x0 &&
  single_pixel_buffer->g == 0x0 &&
  single_pixel_buffer->b == 0x0);
}

void
meta_wayland_init_single_pixel_buffer_manager (MetaWaylandCompositor *compositor)
{
  if (!wl_global_create (compositor->wayland_display,
                         &wp_single_pixel_buffer_manager_v1_interface,
                         META_WP_SINGLE_PIXEL_BUFFER_V1_VERSION,
                         compositor,
                         single_pixel_buffer_manager_bind))
    g_warning ("Failed to create wp_single_pixel_buffer_manager_v1 global");
}
