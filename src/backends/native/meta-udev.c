/*
 * Copyright (C) 2018 Red Hat
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

#include "backends/native/meta-udev.h"

#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-launcher.h"

#define DRM_CARD_UDEV_DEVICE_TYPE "drm_minor"

enum
{
  HOTPLUG,
  DEVICE_ADDED,
  DEVICE_REMOVED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

struct _MetaUdev
{
  GObject parent;

  MetaBackendNative *backend_native;

  GUdevClient *gudev_client;

  gulong uevent_handler_id;
};

G_DEFINE_TYPE (MetaUdev, meta_udev, G_TYPE_OBJECT)

gboolean
meta_is_udev_device_platform_device (GUdevDevice *device)
{
  g_autoptr (GUdevDevice) platform_device = NULL;

  platform_device = g_udev_device_get_parent_with_subsystem (device,
                                                             "platform",
                                                             NULL);
  return !!platform_device;
}

gboolean
meta_is_udev_device_boot_vga (GUdevDevice *device)
{
  g_autoptr (GUdevDevice) pci_device = NULL;

  pci_device = g_udev_device_get_parent_with_subsystem (device, "pci", NULL);
  if (!pci_device)
    return FALSE;

  return g_udev_device_get_sysfs_attr_as_int (pci_device, "boot_vga") == 1;
}

gboolean
meta_udev_is_drm_device (MetaUdev    *udev,
                         GUdevDevice *device)
{
  MetaLauncher *launcher =
    meta_backend_native_get_launcher (udev->backend_native);
  const char *seat_id;
  const char *device_type;
  const char *device_seat;

  /* Filter out devices that are not character device, like card0-VGA-1. */
  if (g_udev_device_get_device_type (device) != G_UDEV_DEVICE_TYPE_CHAR)
    return FALSE;

  device_type = g_udev_device_get_property (device, "DEVTYPE");
  if (g_strcmp0 (device_type, DRM_CARD_UDEV_DEVICE_TYPE) != 0)
    return FALSE;

  device_seat = g_udev_device_get_property (device, "ID_SEAT");
  if (!device_seat)
    {
      /* When ID_SEAT is not set, it means seat0. */
      device_seat = "seat0";
    }

  /* Skip devices that do not belong to our seat. */
  seat_id = meta_launcher_get_seat_id (launcher);
  if (g_strcmp0 (seat_id, device_seat))
    return FALSE;

  return TRUE;
}

GList *
meta_udev_list_drm_devices (MetaUdev  *udev,
                            GError   **error)
{
  g_autoptr (GUdevEnumerator) enumerator = NULL;
  GList *devices;
  GList *l;

  enumerator = g_udev_enumerator_new (udev->gudev_client);

  g_udev_enumerator_add_match_name (enumerator, "card*");
  g_udev_enumerator_add_match_tag (enumerator, "seat");

  /*
   * We need to explicitly match the subsystem for now.
   * https://bugzilla.gnome.org/show_bug.cgi?id=773224
   */
  g_udev_enumerator_add_match_subsystem (enumerator, "drm");

  devices = g_udev_enumerator_execute (enumerator);
  if (!devices)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "No drm devices found");
      return FALSE;
    }

  for (l = devices; l;)
    {
      GUdevDevice *device = l->data;
      GList *l_next = l->next;

      if (!meta_udev_is_drm_device (udev, device))
        {
          g_object_unref (device);
          devices = g_list_delete_link (devices, l);
        }

      l = l_next;
    }

  return devices;
}

static void
on_uevent (GUdevClient *client,
           const char  *action,
           GUdevDevice *device,
           gpointer     user_data)
{
  MetaUdev *udev = META_UDEV (user_data);

  if (!g_udev_device_get_device_file (device))
    return;

  if (g_str_equal (action, "add"))
    g_signal_emit (udev, signals[DEVICE_ADDED], 0, device);
  else if (g_str_equal (action, "remove"))
    g_signal_emit (udev, signals[DEVICE_REMOVED], 0, device);

  if (g_udev_device_get_property_as_boolean (device, "HOTPLUG"))
    g_signal_emit (udev, signals[HOTPLUG], 0);
}

MetaUdev *
meta_udev_new (MetaBackendNative *backend_native)
{
  MetaUdev *udev;

  udev = g_object_new (META_TYPE_UDEV, NULL);
  udev->backend_native = backend_native;

  return udev;
}

static void
meta_udev_finalize (GObject *object)
{
  MetaUdev *udev = META_UDEV (object);

  g_clear_signal_handler (&udev->uevent_handler_id, udev->gudev_client);
  g_clear_object (&udev->gudev_client);

  G_OBJECT_CLASS (meta_udev_parent_class)->finalize (object);
}

static void
meta_udev_init (MetaUdev *udev)
{
  const char *subsystems[] = { "drm", NULL };

  udev->gudev_client = g_udev_client_new (subsystems);
  udev->uevent_handler_id = g_signal_connect (udev->gudev_client,
                                              "uevent",
                                              G_CALLBACK (on_uevent), udev);
}

static void
meta_udev_class_init (MetaUdevClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_udev_finalize;

  signals[HOTPLUG] =
    g_signal_new ("hotplug",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
  signals[DEVICE_ADDED] =
    g_signal_new ("device-added",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  G_UDEV_TYPE_DEVICE);
  signals[DEVICE_REMOVED] =
    g_signal_new ("device-removed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  G_UDEV_TYPE_DEVICE);
}
