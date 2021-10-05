/*
 * Copyright © 2020  Red Hat Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "backends/meta-backend-private.h"
#include "meta-input-device-private.h"

typedef struct _MetaInputDevicePrivate MetaInputDevicePrivate;

struct _MetaInputDevicePrivate
{
#ifdef HAVE_LIBWACOM
  WacomDevice *wacom_device;
#else
  /* Just something to have non-zero sized struct otherwise */
  gpointer wacom_device;
#endif
};

enum
{
  PROP_WACOM_DEVICE = 1,
  N_PROPS
};

static GParamSpec *props[N_PROPS] = { 0 };

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (MetaInputDevice,
                                     meta_input_device,
                                     CLUTTER_TYPE_INPUT_DEVICE)

static void
meta_input_device_init (MetaInputDevice *input_device)
{
}

static void
meta_input_device_constructed (GObject *object)
{
  MetaInputDevice *input_device = META_INPUT_DEVICE (object);
#ifdef HAVE_LIBWACOM
  WacomDeviceDatabase *wacom_db;
  MetaInputDevicePrivate *priv;
  const char *node;
#endif

  G_OBJECT_CLASS (meta_input_device_parent_class)->constructed (object);

#ifdef HAVE_LIBWACOM
  priv = meta_input_device_get_instance_private (input_device);
  wacom_db = meta_backend_get_wacom_database (meta_get_backend ());
  node = clutter_input_device_get_device_node (CLUTTER_INPUT_DEVICE (input_device));
  priv->wacom_device = libwacom_new_from_path (wacom_db, node,
                                               WFALLBACK_NONE, NULL);
#endif /* HAVE_LIBWACOM */
}

static void
meta_input_device_finalize (GObject *object)
{
#ifdef HAVE_LIBWACOM
  MetaInputDevicePrivate *priv;

  priv = meta_input_device_get_instance_private (META_INPUT_DEVICE (object));

  g_clear_pointer (&priv->wacom_device, libwacom_destroy);
#endif /* HAVE_LIBWACOM */

  G_OBJECT_CLASS (meta_input_device_parent_class)->finalize (object);
}

static void
meta_input_device_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  MetaInputDevicePrivate *priv;

  priv = meta_input_device_get_instance_private (META_INPUT_DEVICE (object));

  switch (prop_id)
    {
    case PROP_WACOM_DEVICE:
      g_value_set_pointer (value, priv->wacom_device);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_input_device_class_init (MetaInputDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_input_device_constructed;
  object_class->finalize = meta_input_device_finalize;
  object_class->get_property = meta_input_device_get_property;

  props[PROP_WACOM_DEVICE] =
    g_param_spec_pointer ("wacom-device",
                          "Wacom device",
                          "Wacom device",
                          G_PARAM_READABLE);

  g_object_class_install_properties (object_class, N_PROPS, props);
}

#ifdef HAVE_LIBWACOM
WacomDevice *
meta_input_device_get_wacom_device (MetaInputDevice *input_device)
{
  MetaInputDevicePrivate *priv;

  priv = meta_input_device_get_instance_private (input_device);

  return priv->wacom_device;
}
#endif /* HAVE_LIBWACOM */
