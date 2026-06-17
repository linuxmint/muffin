/*
 * Copyright (C) 2026 Linux Mint
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "wayland/meta-wayland-xdg-toplevel-icon.h"

#include <cairo.h>
#include <string.h>
#include <wayland-server.h>

#include "core/window-private.h"
#include "meta/common.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-surface.h"
#include "wayland/meta-wayland-versions.h"
#include "wayland/meta-wayland-xdg-shell.h"
#include "wayland/meta-window-wayland.h"

#include "xdg-toplevel-icon-v1-server-protocol.h"

/* Sizes (in pixels) we ask scalable clients to provide buffers for. */
static const int icon_sizes[] = { 16, 24, 32, 48, 64, 96 };

typedef struct _MetaWaylandXdgToplevelIconBuffer
{
  cairo_surface_t *surface;
  int size;
  int scale;
  struct wl_list link;
} MetaWaylandXdgToplevelIconBuffer;

typedef struct _MetaWaylandXdgToplevelIcon
{
  char *icon_name;
  struct wl_list buffers;
  gboolean immutable;
} MetaWaylandXdgToplevelIcon;

static gboolean
shm_format_to_cairo (uint32_t        format,
                     cairo_format_t *cairo_format)
{
  switch (format)
    {
    case WL_SHM_FORMAT_ARGB8888:
      *cairo_format = CAIRO_FORMAT_ARGB32;
      return TRUE;
    case WL_SHM_FORMAT_XRGB8888:
      *cairo_format = CAIRO_FORMAT_RGB24;
      return TRUE;
    default:
      return FALSE;
    }
}

static cairo_surface_t *
surface_from_shm_buffer (struct wl_shm_buffer *shm_buffer)
{
  int width, height, stride, dst_stride, y;
  cairo_format_t cairo_format;
  cairo_surface_t *surface;
  unsigned char *dst, *src;

  if (!shm_format_to_cairo (wl_shm_buffer_get_format (shm_buffer), &cairo_format))
    return NULL;

  width = wl_shm_buffer_get_width (shm_buffer);
  height = wl_shm_buffer_get_height (shm_buffer);
  stride = wl_shm_buffer_get_stride (shm_buffer);

  surface = cairo_image_surface_create (cairo_format, width, height);
  if (cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy (surface);
      return NULL;
    }

  dst_stride = cairo_image_surface_get_stride (surface);
  dst = cairo_image_surface_get_data (surface);

  wl_shm_buffer_begin_access (shm_buffer);
  src = wl_shm_buffer_get_data (shm_buffer);
  for (y = 0; y < height; y++)
    memcpy (dst + y * dst_stride, src + y * stride, MIN (stride, dst_stride));
  wl_shm_buffer_end_access (shm_buffer);

  cairo_surface_mark_dirty (surface);
  return surface;
}

static int
buffer_scale (const MetaWaylandXdgToplevelIconBuffer *buffer)
{
  return buffer->scale > 0 ? buffer->scale : 1;
}

/* A buffer's logical size is its pixel size divided by the scale the client
 * supplied it for: a 64px buffer added at scale 2 is a 32px icon's worth of
 * hidpi pixel data. */
static int
buffer_logical_size (const MetaWaylandXdgToplevelIconBuffer *buffer)
{
  return buffer->size / buffer_scale (buffer);
}

/* Produce an icon surface of logical edge @target_size from @buffer, rendered
 * at the buffer's full device resolution (target_size * scale pixels) and
 * tagged with a matching cairo device scale, so hidpi clients keep their crisp
 * pixel data while consumers still see a target_size-logical icon. */
static cairo_surface_t *
make_icon_surface (const MetaWaylandXdgToplevelIconBuffer *buffer,
                   int                                     target_size)
{
  cairo_surface_t *src = buffer->surface;
  int scale = buffer_scale (buffer);
  int target_px = target_size * scale;
  int width, height;
  cairo_surface_t *dst;
  cairo_t *cr;

  width = cairo_image_surface_get_width (src);
  height = cairo_image_surface_get_height (src);

  if (width == target_px && height == target_px && scale == 1)
    return cairo_surface_reference (src);

  dst = cairo_image_surface_create (cairo_image_surface_get_format (src),
                                    target_px, target_px);
  cr = cairo_create (dst);
  cairo_scale (cr, (double) target_px / width, (double) target_px / height);
  cairo_set_source_surface (cr, src, 0, 0);
  cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_GOOD);
  cairo_paint (cr);
  cairo_destroy (cr);

  if (scale != 1)
    cairo_surface_set_device_scale (dst, scale, scale);

  return dst;
}

/* Pick the best stored buffer for a target logical size: prefer the smallest
 * buffer (by logical size) that is at least as large as the target, otherwise
 * the largest available. Comparing logical sizes means a client's hidpi
 * buffers are matched to the logical icon size rather than looking oversized. */
static MetaWaylandXdgToplevelIconBuffer *
pick_best_buffer (MetaWaylandXdgToplevelIcon *icon,
                  int                         target_size)
{
  MetaWaylandXdgToplevelIconBuffer *iter;
  MetaWaylandXdgToplevelIconBuffer *best = NULL;
  int best_size = 0;

  wl_list_for_each (iter, &icon->buffers, link)
    {
      int size = buffer_logical_size (iter);

      if (best == NULL ||
          (best_size < target_size && size > best_size) ||
          (size >= target_size && size < best_size))
        {
          best = iter;
          best_size = size;
        }
    }

  return best;
}

static void
apply_icon_to_window (MetaWaylandXdgToplevelIcon *icon,
                      MetaWindow                 *window)
{
  MetaWaylandXdgToplevelIconBuffer *buffer;
  cairo_surface_t *full_icon = NULL;
  cairo_surface_t *mini_icon = NULL;

  if (icon == NULL)
    {
      meta_window_set_icon_name (window, NULL);
      meta_window_wayland_set_custom_icon (window, NULL, NULL);
      return;
    }

  meta_window_set_icon_name (window, icon->icon_name);

  buffer = pick_best_buffer (icon, META_ICON_WIDTH);
  if (buffer)
    full_icon = make_icon_surface (buffer, META_ICON_WIDTH);

  buffer = pick_best_buffer (icon, META_MINI_ICON_WIDTH);
  if (buffer)
    mini_icon = make_icon_surface (buffer, META_MINI_ICON_WIDTH);

  meta_window_wayland_set_custom_icon (window, full_icon, mini_icon);

  g_clear_pointer (&full_icon, cairo_surface_destroy);
  g_clear_pointer (&mini_icon, cairo_surface_destroy);
}

static MetaWaylandXdgToplevelIcon *
icon_from_resource (struct wl_resource *resource)
{
  return wl_resource_get_user_data (resource);
}

static void
icon_destroy (struct wl_client   *client,
              struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
icon_set_name (struct wl_client   *client,
               struct wl_resource *resource,
               const char         *icon_name)
{
  MetaWaylandXdgToplevelIcon *icon = icon_from_resource (resource);

  if (icon->immutable)
    {
      wl_resource_post_error (resource, XDG_TOPLEVEL_ICON_V1_ERROR_IMMUTABLE,
                              "the icon has already been assigned to a toplevel");
      return;
    }

  g_free (icon->icon_name);
  icon->icon_name = g_strdup (icon_name);
}

static void
icon_add_buffer (struct wl_client   *client,
                 struct wl_resource *resource,
                 struct wl_resource *buffer_resource,
                 int32_t             scale)
{
  MetaWaylandXdgToplevelIcon *icon = icon_from_resource (resource);
  MetaWaylandXdgToplevelIconBuffer *icon_buffer;
  struct wl_shm_buffer *shm_buffer;
  cairo_surface_t *surface;
  cairo_format_t cairo_format;
  int width, height;

  if (icon->immutable)
    {
      wl_resource_post_error (resource, XDG_TOPLEVEL_ICON_V1_ERROR_IMMUTABLE,
                              "the icon has already been assigned to a toplevel");
      return;
    }

  shm_buffer = wl_shm_buffer_get (buffer_resource);
  if (shm_buffer == NULL)
    {
      wl_resource_post_error (resource, XDG_TOPLEVEL_ICON_V1_ERROR_INVALID_BUFFER,
                              "the provided buffer is not backed by wl_shm");
      return;
    }

  width = wl_shm_buffer_get_width (shm_buffer);
  height = wl_shm_buffer_get_height (shm_buffer);
  if (width != height || width <= 0)
    {
      wl_resource_post_error (resource, XDG_TOPLEVEL_ICON_V1_ERROR_INVALID_BUFFER,
                              "the provided buffer is not square");
      return;
    }

  if (!shm_format_to_cairo (wl_shm_buffer_get_format (shm_buffer), &cairo_format))
    {
      wl_resource_post_error (resource, XDG_TOPLEVEL_ICON_V1_ERROR_INVALID_BUFFER,
                              "the provided buffer has an unsupported format");
      return;
    }

  /* The format and dimensions are valid, so a NULL surface here is an internal
   * (cairo allocation) failure rather than a client error - don't silently
   * drop a buffer the client believes it added. */
  surface = surface_from_shm_buffer (shm_buffer);
  if (surface == NULL)
    {
      g_warning ("xdg-toplevel-icon: failed to create a %dx%d cairo surface "
                 "for an icon buffer", width, height);
      return;
    }

  /* A buffer of the same size and scale overrides the previous one. */
  wl_list_for_each (icon_buffer, &icon->buffers, link)
    {
      if (icon_buffer->size == width && icon_buffer->scale == scale)
        {
          cairo_surface_destroy (icon_buffer->surface);
          icon_buffer->surface = surface;
          return;
        }
    }

  icon_buffer = g_new0 (MetaWaylandXdgToplevelIconBuffer, 1);
  icon_buffer->surface = surface;
  icon_buffer->size = width;
  icon_buffer->scale = scale;
  wl_list_insert (&icon->buffers, &icon_buffer->link);
}

static const struct xdg_toplevel_icon_v1_interface icon_implementation = {
  icon_destroy,
  icon_set_name,
  icon_add_buffer,
};

static void
icon_resource_destroy (struct wl_resource *resource)
{
  MetaWaylandXdgToplevelIcon *icon = icon_from_resource (resource);
  MetaWaylandXdgToplevelIconBuffer *icon_buffer, *tmp;

  wl_list_for_each_safe (icon_buffer, tmp, &icon->buffers, link)
    {
      wl_list_remove (&icon_buffer->link);
      cairo_surface_destroy (icon_buffer->surface);
      g_free (icon_buffer);
    }

  g_free (icon->icon_name);
  g_free (icon);
}

static void
manager_destroy (struct wl_client   *client,
                 struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
manager_create_icon (struct wl_client   *client,
                     struct wl_resource *resource,
                     uint32_t            id)
{
  MetaWaylandXdgToplevelIcon *icon;
  struct wl_resource *icon_resource;

  icon = g_new0 (MetaWaylandXdgToplevelIcon, 1);
  wl_list_init (&icon->buffers);

  icon_resource = wl_resource_create (client,
                                      &xdg_toplevel_icon_v1_interface,
                                      wl_resource_get_version (resource),
                                      id);
  wl_resource_set_implementation (icon_resource, &icon_implementation,
                                  icon, icon_resource_destroy);
}

static void
manager_set_icon (struct wl_client   *client,
                  struct wl_resource *resource,
                  struct wl_resource *toplevel_resource,
                  struct wl_resource *icon_resource)
{
  MetaWaylandXdgToplevel *xdg_toplevel;
  MetaWaylandSurfaceRole *surface_role;
  MetaWaylandSurface *surface;
  MetaWindow *window;
  MetaWaylandXdgToplevelIcon *icon = NULL;

  if (toplevel_resource == NULL)
    return;

  xdg_toplevel = wl_resource_get_user_data (toplevel_resource);
  surface_role = META_WAYLAND_SURFACE_ROLE (xdg_toplevel);
  surface = meta_wayland_surface_role_get_surface (surface_role);
  window = meta_wayland_surface_get_window (surface);

  if (window == NULL)
    return;

  if (icon_resource != NULL)
    {
      icon = icon_from_resource (icon_resource);
      icon->immutable = TRUE;

      /* An icon with neither a name nor any buffer resets to the default. */
      if (icon->icon_name == NULL && wl_list_empty (&icon->buffers))
        icon = NULL;
    }

  apply_icon_to_window (icon, window);
}

static const struct xdg_toplevel_icon_manager_v1_interface manager_implementation = {
  manager_destroy,
  manager_create_icon,
  manager_set_icon,
};

static void
bind_xdg_toplevel_icon (struct wl_client *client,
                        void             *data,
                        uint32_t          version,
                        uint32_t          id)
{
  struct wl_resource *resource;
  size_t i;

  resource = wl_resource_create (client,
                                 &xdg_toplevel_icon_manager_v1_interface,
                                 version, id);
  wl_resource_set_implementation (resource, &manager_implementation, NULL, NULL);

  for (i = 0; i < G_N_ELEMENTS (icon_sizes); i++)
    xdg_toplevel_icon_manager_v1_send_icon_size (resource, icon_sizes[i]);
  xdg_toplevel_icon_manager_v1_send_done (resource);
}

void
meta_wayland_xdg_toplevel_icon_init (MetaWaylandCompositor *compositor)
{
  if (wl_global_create (compositor->wayland_display,
                        &xdg_toplevel_icon_manager_v1_interface,
                        META_XDG_TOPLEVEL_ICON_V1_VERSION,
                        NULL,
                        bind_xdg_toplevel_icon) == NULL)
    g_error ("Failed to register a global xdg-toplevel-icon object");
}
