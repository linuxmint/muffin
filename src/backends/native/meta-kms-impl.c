/*
 * Copyright (C) 2018 Red Hat
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

#include "backends/native/meta-kms-impl.h"

enum
{
  PROP_0,

  PROP_KMS,
};

typedef struct _MetaKmsImplPrivate
{
  MetaKms *kms;
} MetaKmsImplPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (MetaKmsImpl, meta_kms_impl, G_TYPE_OBJECT)

MetaKms *
meta_kms_impl_get_kms (MetaKmsImpl *impl)
{
  MetaKmsImplPrivate *priv = meta_kms_impl_get_instance_private (impl);

  return priv->kms;
}

MetaKmsFeedback *
meta_kms_impl_process_update (MetaKmsImpl   *impl,
                              MetaKmsUpdate *update)
{
  return META_KMS_IMPL_GET_CLASS (impl)->process_update (impl, update);
}

void
meta_kms_impl_handle_page_flip_callback (MetaKmsImpl         *impl,
                                         MetaKmsPageFlipData *page_flip_data)
{
  META_KMS_IMPL_GET_CLASS (impl)->handle_page_flip_callback (impl,
                                                             page_flip_data);
}

void
meta_kms_impl_discard_pending_page_flips (MetaKmsImpl *impl)
{
  META_KMS_IMPL_GET_CLASS (impl)->discard_pending_page_flips (impl);
}

void
meta_kms_impl_dispatch_idle (MetaKmsImpl *impl)
{
  META_KMS_IMPL_GET_CLASS (impl)->dispatch_idle (impl);
}

void
meta_kms_impl_notify_device_created (MetaKmsImpl   *impl,
                                     MetaKmsDevice *device)
{
  META_KMS_IMPL_GET_CLASS (impl)->notify_device_created (impl, device);
}

static void
meta_kms_impl_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  MetaKmsImpl *impl = META_KMS_IMPL (object);
  MetaKmsImplPrivate *priv = meta_kms_impl_get_instance_private (impl);

  switch (prop_id)
    {
    case PROP_KMS:
      priv->kms = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_kms_impl_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  MetaKmsImpl *impl = META_KMS_IMPL (object);
  MetaKmsImplPrivate *priv = meta_kms_impl_get_instance_private (impl);

  switch (prop_id)
    {
    case PROP_KMS:
      g_value_set_object (value, priv->kms);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_kms_impl_init (MetaKmsImpl *kms_impl)
{
}

static void
meta_kms_impl_class_init (MetaKmsImplClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *pspec;

  object_class->set_property = meta_kms_impl_set_property;
  object_class->get_property = meta_kms_impl_get_property;

  pspec = g_param_spec_object ("kms",
                               "kms",
                               "MetaKms",
                               META_TYPE_KMS,
                               G_PARAM_READWRITE |
                               G_PARAM_STATIC_STRINGS |
                               G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class,
                                   PROP_KMS,
                                   pspec);
}
