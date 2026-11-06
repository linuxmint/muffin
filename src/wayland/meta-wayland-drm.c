/*
 * Copyright (C) 2024 Linux Mint
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
 */

#include "config.h"

#include "wayland/meta-wayland-drm.h"

#include "backends/meta-backend-private.h"
#include "wayland/meta-wayland-private.h"

#ifdef HAVE_NATIVE_BACKEND
#include <xf86drm.h>

#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-gpu-kms.h"
#include "backends/native/meta-renderer-native.h"
#endif

#include "wayland-drm-server-protocol.h"

#ifdef HAVE_NATIVE_BACKEND

static void
drm_authenticate (struct wl_client   *client,
                  struct wl_resource *resource,
                  uint32_t            id)
{
  /* We advertise the render node, which doesn't require authentication.
   * Just respond immediately so clients don't stall. */
  wl_drm_send_authenticated (resource);
}

static void
drm_create_buffer (struct wl_client   *client,
                   struct wl_resource *resource,
                   uint32_t            id,
                   uint32_t            name,
                   int32_t             width,
                   int32_t             height,
                   uint32_t            stride,
                   uint32_t            format)
{
  wl_resource_post_error (resource, WL_DRM_ERROR_INVALID_NAME,
                          "flink buffer creation not supported");
}

static void
drm_create_planar_buffer (struct wl_client   *client,
                          struct wl_resource *resource,
                          uint32_t            id,
                          uint32_t            name,
                          int32_t             width,
                          int32_t             height,
                          uint32_t            format,
                          int32_t             offset0,
                          int32_t             stride0,
                          int32_t             offset1,
                          int32_t             stride1,
                          int32_t             offset2,
                          int32_t             stride2)
{
  wl_resource_post_error (resource, WL_DRM_ERROR_INVALID_NAME,
                          "flink buffer creation not supported");
}

static void
drm_create_prime_buffer (struct wl_client   *client,
                         struct wl_resource *resource,
                         uint32_t            id,
                         int32_t             name,
                         int32_t             width,
                         int32_t             height,
                         uint32_t            format,
                         int32_t             offset0,
                         int32_t             stride0,
                         int32_t             offset1,
                         int32_t             stride1,
                         int32_t             offset2,
                         int32_t             stride2)
{
  wl_resource_post_error (resource, WL_DRM_ERROR_INVALID_NAME,
                          "prime buffer creation not supported");
}

static const struct wl_drm_interface drm_implementation = {
  drm_authenticate,
  drm_create_buffer,
  drm_create_planar_buffer,
  drm_create_prime_buffer,
};

static void
drm_bind (struct wl_client *client,
          void             *data,
          uint32_t          version,
          uint32_t          id)
{
  const char *device_path = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client, &wl_drm_interface, version, id);
  wl_resource_set_implementation (resource, &drm_implementation, NULL, NULL);

  wl_drm_send_device (resource, device_path);
  wl_drm_send_capabilities (resource, WL_DRM_CAPABILITY_PRIME);
}

#endif /* HAVE_NATIVE_BACKEND */

void
meta_wayland_drm_init (MetaWaylandCompositor *compositor)
{
#ifdef HAVE_NATIVE_BACKEND
  MetaBackend *backend = meta_get_backend ();
  MetaRendererNative *renderer_native;
  MetaGpuKms *gpu_kms;
  int kms_fd;
  char *render_node_path;

  if (!META_IS_BACKEND_NATIVE (backend))
    return;

  /* Has to be the GPU we actually render on, not just the first one
   * enumerated: on a PRIME system those differ, and handing clients the
   * other GPU's node points them at a device we do not composite with. */
  renderer_native =
    META_RENDERER_NATIVE (meta_backend_get_renderer (backend));
  gpu_kms = meta_renderer_native_get_primary_gpu (renderer_native);
  if (!gpu_kms)
    return;

  kms_fd = meta_gpu_kms_get_fd (gpu_kms);
  render_node_path = drmGetRenderDeviceNameFromFd (kms_fd);

  if (!render_node_path)
    {
      g_warning ("wl_drm: could not get render node path, skipping");
      return;
    }

  wl_global_create (compositor->wayland_display,
                    &wl_drm_interface,
                    2,
                    render_node_path,
                    drm_bind);

  g_message ("wl_drm: advertising render node %s", render_node_path);
  /* render_node_path is intentionally leaked — it lives for the lifetime
   * of the global and there is no teardown path for Wayland globals here. */
#endif /* HAVE_NATIVE_BACKEND */
}
