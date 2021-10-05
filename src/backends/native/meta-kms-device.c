/*
 * Copyright (C) 2019 Red Hat
 * Copyright (C) 2019 DisplayLink (UK) Ltd.
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

#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-device.h"

#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-kms-impl-device.h"
#include "backends/native/meta-kms-impl.h"
#include "backends/native/meta-kms-plane.h"
#include "backends/native/meta-kms-private.h"

struct _MetaKmsDevice
{
  GObject parent;

  MetaKms *kms;

  MetaKmsImplDevice *impl_device;

  MetaKmsDeviceFlag flags;
  char *path;

  GList *crtcs;
  GList *connectors;
  GList *planes;

  MetaKmsDeviceCaps caps;
};

G_DEFINE_TYPE (MetaKmsDevice, meta_kms_device, G_TYPE_OBJECT);

MetaKmsImplDevice *
meta_kms_device_get_impl_device (MetaKmsDevice *device)
{
  return device->impl_device;
}

int
meta_kms_device_leak_fd (MetaKmsDevice *device)
{
  return meta_kms_impl_device_leak_fd (device->impl_device);
}

const char *
meta_kms_device_get_path (MetaKmsDevice *device)
{
  return device->path;
}

MetaKmsDeviceFlag
meta_kms_device_get_flags (MetaKmsDevice *device)
{
  return device->flags;
}

gboolean
meta_kms_device_get_cursor_size (MetaKmsDevice *device,
                                 uint64_t      *out_cursor_width,
                                 uint64_t      *out_cursor_height)
{
  if (device->caps.has_cursor_size)
    {
      *out_cursor_width = device->caps.cursor_width;
      *out_cursor_height = device->caps.cursor_height;
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

GList *
meta_kms_device_get_connectors (MetaKmsDevice *device)
{
  return device->connectors;
}

GList *
meta_kms_device_get_crtcs (MetaKmsDevice *device)
{
  return device->crtcs;
}

static GList *
meta_kms_device_get_planes (MetaKmsDevice *device)
{
  return device->planes;
}

static MetaKmsPlane *
get_plane_with_type_for (MetaKmsDevice    *device,
                         MetaKmsCrtc      *crtc,
                         MetaKmsPlaneType  type)
{
  GList *l;

  for (l = meta_kms_device_get_planes (device); l; l = l->next)
    {
      MetaKmsPlane *plane = l->data;

      if (meta_kms_plane_get_plane_type (plane) != type)
        continue;

      if (meta_kms_plane_is_usable_with (plane, crtc))
        return plane;
    }

  return NULL;
}

MetaKmsPlane *
meta_kms_device_get_primary_plane_for (MetaKmsDevice *device,
                                       MetaKmsCrtc   *crtc)
{
  return get_plane_with_type_for (device, crtc, META_KMS_PLANE_TYPE_PRIMARY);
}

MetaKmsPlane *
meta_kms_device_get_cursor_plane_for (MetaKmsDevice *device,
                                      MetaKmsCrtc   *crtc)
{
  return get_plane_with_type_for (device, crtc, META_KMS_PLANE_TYPE_CURSOR);
}

void
meta_kms_device_update_states_in_impl (MetaKmsDevice *device)
{
  MetaKmsImplDevice *impl_device = meta_kms_device_get_impl_device (device);

  meta_assert_in_kms_impl (device->kms);
  meta_assert_is_waiting_for_kms_impl_task (device->kms);

  meta_kms_impl_device_update_states (impl_device);

  g_list_free (device->crtcs);
  device->crtcs = meta_kms_impl_device_copy_crtcs (impl_device);

  g_list_free (device->connectors);
  device->connectors = meta_kms_impl_device_copy_connectors (impl_device);

  g_list_free (device->planes);
  device->planes = meta_kms_impl_device_copy_planes (impl_device);
}

void
meta_kms_device_predict_states_in_impl (MetaKmsDevice *device,
                                        MetaKmsUpdate *update)
{
  MetaKmsImplDevice *impl_device = meta_kms_device_get_impl_device (device);

  meta_assert_in_kms_impl (device->kms);

  meta_kms_impl_device_predict_states (impl_device, update);
}

static gpointer
dispatch_in_impl (MetaKmsImpl  *impl,
                  gpointer      user_data,
                  GError      **error)
{
  MetaKmsImplDevice *impl_device = META_KMS_IMPL_DEVICE (user_data);
  gboolean ret;

  ret = meta_kms_impl_device_dispatch (impl_device, error);
  return GINT_TO_POINTER (ret);
}

static gpointer
dispatch_idle_in_impl (MetaKmsImpl  *impl,
                       gpointer      user_data,
                       GError      **error)
{
  meta_kms_impl_dispatch_idle (impl);

  return GINT_TO_POINTER (TRUE);
}

int
meta_kms_device_dispatch_sync (MetaKmsDevice  *device,
                               GError        **error)
{
  int callback_count;

  if (!meta_kms_run_impl_task_sync (device->kms,
                                    dispatch_idle_in_impl,
                                    device->impl_device,
                                    error))
    return -1;

  callback_count = meta_kms_flush_callbacks (device->kms);
  if (callback_count > 0)
    return TRUE;

  if (!meta_kms_run_impl_task_sync (device->kms,
                                    dispatch_in_impl,
                                    device->impl_device,
                                    error))
    return -1;

  return meta_kms_flush_callbacks (device->kms);
}

void
meta_kms_device_add_fake_plane_in_impl (MetaKmsDevice    *device,
                                        MetaKmsPlaneType  plane_type,
                                        MetaKmsCrtc      *crtc)
{
  MetaKmsImplDevice *impl_device = device->impl_device;
  MetaKmsPlane *plane;

  meta_assert_in_kms_impl (device->kms);

  plane = meta_kms_impl_device_add_fake_plane (impl_device,
                                               plane_type,
                                               crtc);
  device->planes = g_list_append (device->planes, plane);
}

typedef struct _CreateImplDeviceData
{
  MetaKmsDevice *device;
  int fd;

  MetaKmsImplDevice *out_impl_device;
  GList *out_crtcs;
  GList *out_connectors;
  GList *out_planes;
  MetaKmsDeviceCaps out_caps;
} CreateImplDeviceData;

static gpointer
create_impl_device_in_impl (MetaKmsImpl  *impl,
                            gpointer      user_data,
                            GError      **error)
{
  CreateImplDeviceData *data = user_data;
  MetaKmsImplDevice *impl_device;

  impl_device = meta_kms_impl_device_new (data->device, impl, data->fd, error);
  if (!impl_device)
    return FALSE;

  data->out_impl_device = impl_device;
  data->out_crtcs = meta_kms_impl_device_copy_crtcs (impl_device);
  data->out_connectors = meta_kms_impl_device_copy_connectors (impl_device);
  data->out_planes = meta_kms_impl_device_copy_planes (impl_device);
  data->out_caps = *meta_kms_impl_device_get_caps (impl_device);

  return GINT_TO_POINTER (TRUE);
}

MetaKmsDevice *
meta_kms_device_new (MetaKms            *kms,
                     const char         *path,
                     MetaKmsDeviceFlag   flags,
                     GError            **error)
{
  MetaBackend *backend = meta_kms_get_backend (kms);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaLauncher *launcher = meta_backend_native_get_launcher (backend_native);
  MetaKmsDevice *device;
  CreateImplDeviceData data;
  int fd;

  fd = meta_launcher_open_restricted (launcher, path, error);
  if (fd == -1)
    return NULL;

  device = g_object_new (META_TYPE_KMS_DEVICE, NULL);
  device->kms = kms;

  data = (CreateImplDeviceData) {
    .device = device,
    .fd = fd,
  };
  if (!meta_kms_run_impl_task_sync (kms, create_impl_device_in_impl, &data,
                                    error))
    {
      meta_launcher_close_restricted (launcher, fd);
      g_object_unref (device);
      return NULL;
    }

  device->impl_device = data.out_impl_device;
  device->flags = flags;
  device->path = g_strdup (path);
  device->crtcs = data.out_crtcs;
  device->connectors = data.out_connectors;
  device->planes = data.out_planes;
  device->caps = data.out_caps;

  return device;
}

typedef struct _FreeImplDeviceData
{
  MetaKmsImplDevice *impl_device;

  int out_fd;
} FreeImplDeviceData;

static gpointer
free_impl_device_in_impl (MetaKmsImpl  *impl,
                          gpointer      user_data,
                          GError      **error)
{
  FreeImplDeviceData *data = user_data;
  MetaKmsImplDevice *impl_device = data->impl_device;
  int fd;

  fd = meta_kms_impl_device_close (impl_device);
  g_object_unref (impl_device);

  data->out_fd = fd;

  return GINT_TO_POINTER (TRUE);
}

static void
meta_kms_device_finalize (GObject *object)
{
  MetaKmsDevice *device = META_KMS_DEVICE (object);
  MetaBackend *backend = meta_kms_get_backend (device->kms);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaLauncher *launcher = meta_backend_native_get_launcher (backend_native);

  g_free (device->path);
  g_list_free (device->crtcs);
  g_list_free (device->connectors);
  g_list_free (device->planes);

  if (device->impl_device)
    {
      FreeImplDeviceData data;
      GError *error = NULL;

      data = (FreeImplDeviceData) {
        .impl_device = device->impl_device,
      };
      if (!meta_kms_run_impl_task_sync (device->kms, free_impl_device_in_impl, &data,
                                        &error))
        {
          g_warning ("Failed to close KMS impl device: %s", error->message);
          g_error_free (error);
        }
      else
        {
          meta_launcher_close_restricted (launcher, data.out_fd);
        }
    }
  G_OBJECT_CLASS (meta_kms_device_parent_class)->finalize (object);
}

static void
meta_kms_device_init (MetaKmsDevice *device)
{
}

static void
meta_kms_device_class_init (MetaKmsDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_kms_device_finalize;
}
