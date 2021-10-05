/*
 * Copyright (C) 2019 Red Hat
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

#include "backends/native/meta-kms-impl-device.h"

#include <errno.h>
#include <xf86drm.h>

#include "backends/native/meta-kms-connector-private.h"
#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-crtc-private.h"
#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-impl.h"
#include "backends/native/meta-kms-page-flip-private.h"
#include "backends/native/meta-kms-plane-private.h"
#include "backends/native/meta-kms-plane.h"
#include "backends/native/meta-kms-private.h"
#include "backends/native/meta-kms-update.h"

struct _MetaKmsImplDevice
{
  GObject parent;

  MetaKmsDevice *device;
  MetaKmsImpl *impl;

  int fd;
  GSource *fd_source;

  GList *crtcs;
  GList *connectors;
  GList *planes;

  MetaKmsDeviceCaps caps;
};

G_DEFINE_TYPE (MetaKmsImplDevice, meta_kms_impl_device, G_TYPE_OBJECT)

MetaKmsDevice *
meta_kms_impl_device_get_device (MetaKmsImplDevice *impl_device)
{
  return impl_device->device;
}

GList *
meta_kms_impl_device_copy_connectors (MetaKmsImplDevice *impl_device)
{
  return g_list_copy (impl_device->connectors);
}

GList *
meta_kms_impl_device_copy_crtcs (MetaKmsImplDevice *impl_device)
{
  return g_list_copy (impl_device->crtcs);
}

GList *
meta_kms_impl_device_copy_planes (MetaKmsImplDevice *impl_device)
{
  return g_list_copy (impl_device->planes);
}

const MetaKmsDeviceCaps *
meta_kms_impl_device_get_caps (MetaKmsImplDevice *impl_device)
{
  return &impl_device->caps;
}

static void
page_flip_handler (int           fd,
                   unsigned int  sequence,
                   unsigned int  sec,
                   unsigned int  usec,
                   void         *user_data)
{
  MetaKmsPageFlipData *page_flip_data = user_data;
  MetaKmsImpl *impl;

  meta_kms_page_flip_data_set_timings_in_impl (page_flip_data,
                                               sequence, sec, usec);

  impl = meta_kms_page_flip_data_get_kms_impl (page_flip_data);
  meta_kms_impl_handle_page_flip_callback (impl, page_flip_data);
}

gboolean
meta_kms_impl_device_dispatch (MetaKmsImplDevice  *impl_device,
                               GError            **error)
{
  drmEventContext drm_event_context;

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (impl_device->impl));

  drm_event_context = (drmEventContext) { 0 };
  drm_event_context.version = 2;
  drm_event_context.page_flip_handler = page_flip_handler;

  while (TRUE)
    {
      if (drmHandleEvent (impl_device->fd, &drm_event_context) != 0)
        {
          struct pollfd pfd;
          int ret;

          if (errno != EAGAIN)
            {
              g_set_error_literal (error, G_IO_ERROR,
                                   g_io_error_from_errno (errno),
                                   strerror (errno));
              return FALSE;
            }

          pfd.fd = impl_device->fd;
          pfd.events = POLL_IN | POLL_ERR;
          do
            {
              ret = poll (&pfd, 1, -1);
            }
          while (ret == -1 && errno == EINTR);
        }
      else
        {
          break;
        }
    }

  return TRUE;
}

static gpointer
kms_event_dispatch_in_impl (MetaKmsImpl  *impl,
                            gpointer      user_data,
                            GError      **error)
{
  MetaKmsImplDevice *impl_device = user_data;
  gboolean ret;

  ret = meta_kms_impl_device_dispatch (impl_device, error);
  return GINT_TO_POINTER (ret);
}

drmModePropertyPtr
meta_kms_impl_device_find_property (MetaKmsImplDevice       *impl_device,
                                    drmModeObjectProperties *props,
                                    const char              *prop_name,
                                    int                     *out_idx)
{
  unsigned int i;

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (impl_device->impl));

  for (i = 0; i < props->count_props; i++)
    {
      drmModePropertyPtr prop;

      prop = drmModeGetProperty (impl_device->fd, props->props[i]);
      if (!prop)
        continue;

      if (strcmp (prop->name, prop_name) == 0)
        {
          *out_idx = i;
          return prop;
        }

      drmModeFreeProperty (prop);
    }

  return NULL;
}

static void
init_caps (MetaKmsImplDevice *impl_device)
{
  int fd = impl_device->fd;
  uint64_t cursor_width, cursor_height;

  if (drmGetCap (fd, DRM_CAP_CURSOR_WIDTH, &cursor_width) == 0 &&
      drmGetCap (fd, DRM_CAP_CURSOR_HEIGHT, &cursor_height) == 0)
    {
      impl_device->caps.has_cursor_size = TRUE;
      impl_device->caps.cursor_width = cursor_width;
      impl_device->caps.cursor_height = cursor_height;
    }
}

static void
init_crtcs (MetaKmsImplDevice *impl_device,
            drmModeRes        *drm_resources)
{
  int idx;

  for (idx = 0; idx < drm_resources->count_crtcs; idx++)
    {
      drmModeCrtc *drm_crtc;
      MetaKmsCrtc *crtc;

      drm_crtc = drmModeGetCrtc (impl_device->fd, drm_resources->crtcs[idx]);
      crtc = meta_kms_crtc_new (impl_device, drm_crtc, idx);
      drmModeFreeCrtc (drm_crtc);

      impl_device->crtcs = g_list_prepend (impl_device->crtcs, crtc);
    }
  impl_device->crtcs = g_list_reverse (impl_device->crtcs);
}

static MetaKmsConnector *
find_existing_connector (MetaKmsImplDevice *impl_device,
                         drmModeConnector  *drm_connector)
{
  GList *l;

  for (l = impl_device->connectors; l; l = l->next)
    {
      MetaKmsConnector *connector = l->data;

      if (meta_kms_connector_is_same_as (connector, drm_connector))
        return connector;
    }

  return NULL;
}

static void
update_connectors (MetaKmsImplDevice *impl_device,
                   drmModeRes        *drm_resources)
{
  GList *connectors = NULL;
  unsigned int i;

  for (i = 0; i < drm_resources->count_connectors; i++)
    {
      drmModeConnector *drm_connector;
      MetaKmsConnector *connector;

      drm_connector = drmModeGetConnector (impl_device->fd,
                                           drm_resources->connectors[i]);
      if (!drm_connector)
        continue;

      connector = find_existing_connector (impl_device, drm_connector);
      if (connector)
        connector = g_object_ref (connector);
      else
        connector = meta_kms_connector_new (impl_device, drm_connector,
                                            drm_resources);
      drmModeFreeConnector (drm_connector);

      connectors = g_list_prepend (connectors, connector);
    }

  g_list_free_full (impl_device->connectors, g_object_unref);
  impl_device->connectors = g_list_reverse (connectors);
}

static MetaKmsPlaneType
get_plane_type (MetaKmsImplDevice       *impl_device,
                drmModeObjectProperties *props)
{
  drmModePropertyPtr prop;
  int idx;

  prop = meta_kms_impl_device_find_property (impl_device, props, "type", &idx);
  if (!prop)
    return FALSE;
  drmModeFreeProperty (prop);

  switch (props->prop_values[idx])
    {
    case DRM_PLANE_TYPE_PRIMARY:
      return META_KMS_PLANE_TYPE_PRIMARY;
    case DRM_PLANE_TYPE_CURSOR:
      return META_KMS_PLANE_TYPE_CURSOR;
    case DRM_PLANE_TYPE_OVERLAY:
      return META_KMS_PLANE_TYPE_OVERLAY;
    default:
      g_warning ("Unhandled plane type %" G_GUINT64_FORMAT,
                 props->prop_values[idx]);
      return -1;
    }
}

MetaKmsPlane *
meta_kms_impl_device_add_fake_plane (MetaKmsImplDevice *impl_device,
                                     MetaKmsPlaneType   plane_type,
                                     MetaKmsCrtc       *crtc)
{
  MetaKmsPlane *plane;

  plane = meta_kms_plane_new_fake (plane_type, crtc);
  impl_device->planes = g_list_append (impl_device->planes, plane);

  return plane;
}

static void
init_planes (MetaKmsImplDevice *impl_device)
{
  int fd = impl_device->fd;
  drmModePlaneRes *drm_planes;
  unsigned int i;

  drm_planes = drmModeGetPlaneResources (fd);
  if (!drm_planes)
    return;

  for (i = 0; i < drm_planes->count_planes; i++)
    {
      drmModePlane *drm_plane;
      drmModeObjectProperties *props;

      drm_plane = drmModeGetPlane (fd, drm_planes->planes[i]);
      if (!drm_plane)
        continue;

      props = drmModeObjectGetProperties (fd,
                                          drm_plane->plane_id,
                                          DRM_MODE_OBJECT_PLANE);
      if (props)
        {
          MetaKmsPlaneType plane_type;

          plane_type = get_plane_type (impl_device, props);
          if (plane_type != -1)
            {
              MetaKmsPlane *plane;

              plane = meta_kms_plane_new (plane_type,
                                          impl_device,
                                          drm_plane, props);

              impl_device->planes = g_list_prepend (impl_device->planes, plane);
            }
        }

      g_clear_pointer (&props, drmModeFreeObjectProperties);
      drmModeFreePlane (drm_plane);
    }
  impl_device->planes = g_list_reverse (impl_device->planes);
}

void
meta_kms_impl_device_update_states (MetaKmsImplDevice *impl_device)
{
  drmModeRes *drm_resources;

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (impl_device->impl));

  drm_resources = drmModeGetResources (impl_device->fd);
  if (!drm_resources)
    {
      g_list_free_full (impl_device->planes, g_object_unref);
      g_list_free_full (impl_device->crtcs, g_object_unref);
      g_list_free_full (impl_device->connectors, g_object_unref);
      impl_device->planes = NULL;
      impl_device->crtcs = NULL;
      impl_device->connectors = NULL;
      return;
    }

  update_connectors (impl_device, drm_resources);

  g_list_foreach (impl_device->crtcs, (GFunc) meta_kms_crtc_update_state,
                  NULL);
  g_list_foreach (impl_device->connectors, (GFunc) meta_kms_connector_update_state,
                  drm_resources);
  drmModeFreeResources (drm_resources);
}

void
meta_kms_impl_device_predict_states (MetaKmsImplDevice *impl_device,
                                     MetaKmsUpdate     *update)
{
  g_list_foreach (impl_device->crtcs, (GFunc) meta_kms_crtc_predict_state,
                  update);
  g_list_foreach (impl_device->connectors, (GFunc) meta_kms_connector_predict_state,
                  update);
}

MetaKmsImplDevice *
meta_kms_impl_device_new (MetaKmsDevice  *device,
                          MetaKmsImpl    *impl,
                          int             fd,
                          GError        **error)
{
  MetaKms *kms = meta_kms_impl_get_kms (impl);
  MetaKmsImplDevice *impl_device;
  int ret;
  drmModeRes *drm_resources;

  meta_assert_in_kms_impl (kms);

  ret = drmSetClientCap (fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
  if (ret != 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (-ret),
                   "Failed to activate universal planes: %s",
                   g_strerror (-ret));
      return NULL;
    }

  drm_resources = drmModeGetResources (fd);
  if (!drm_resources)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Failed to activate universal planes: %s",
                   g_strerror (errno));
      return NULL;
    }

  impl_device = g_object_new (META_TYPE_KMS_IMPL_DEVICE, NULL);
  impl_device->device = device;
  impl_device->impl = impl;
  impl_device->fd = fd;

  init_caps (impl_device);

  init_crtcs (impl_device, drm_resources);
  init_planes (impl_device);

  update_connectors (impl_device, drm_resources);

  drmModeFreeResources (drm_resources);

  impl_device->fd_source =
    meta_kms_register_fd_in_impl (kms, fd,
                                  kms_event_dispatch_in_impl,
                                  impl_device);

  return impl_device;
}

int
meta_kms_impl_device_get_fd (MetaKmsImplDevice *impl_device)
{
  meta_assert_in_kms_impl (meta_kms_impl_get_kms (impl_device->impl));

  return impl_device->fd;
}

int
meta_kms_impl_device_leak_fd (MetaKmsImplDevice *impl_device)
{
  return impl_device->fd;
}

int
meta_kms_impl_device_close (MetaKmsImplDevice *impl_device)
{
  int fd;

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (impl_device->impl));

  g_clear_pointer (&impl_device->fd_source, g_source_destroy);
  fd = impl_device->fd;
  impl_device->fd = -1;

  return fd;
}

static void
meta_kms_impl_device_finalize (GObject *object)
{
  MetaKmsImplDevice *impl_device = META_KMS_IMPL_DEVICE (object);

  g_list_free_full (impl_device->planes, g_object_unref);
  g_list_free_full (impl_device->crtcs, g_object_unref);
  g_list_free_full (impl_device->connectors, g_object_unref);

  G_OBJECT_CLASS (meta_kms_impl_device_parent_class)->finalize (object);
}

static void
meta_kms_impl_device_init (MetaKmsImplDevice *device)
{
}

static void
meta_kms_impl_device_class_init (MetaKmsImplDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_kms_impl_device_finalize;
}

