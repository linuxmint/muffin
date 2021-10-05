/*
 * Copyright (C) 2013-2019 Red Hat
 * Copyright (C) 2018 DisplayLink (UK) Ltd.
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

#include "backends/native/meta-kms-plane-private.h"

#include <drm_fourcc.h>
#include <stdio.h>

#include "backends/meta-monitor-transform.h"
#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-impl-device.h"
#include "backends/native/meta-kms-update-private.h"

struct _MetaKmsPlane
{
  GObject parent;

  MetaKmsPlaneType type;
  gboolean is_fake;

  uint32_t id;

  uint32_t possible_crtcs;

  uint32_t rotation_prop_id;
  uint32_t rotation_map[META_MONITOR_N_TRANSFORMS];
  uint32_t all_hw_transforms;

  /*
   * primary plane's supported formats and maybe modifiers
   * key: GUINT_TO_POINTER (format)
   * value: owned GArray* (uint64_t modifier), or NULL
   */
  GHashTable *formats_modifiers;

  MetaKmsDevice *device;
};

G_DEFINE_TYPE (MetaKmsPlane, meta_kms_plane, G_TYPE_OBJECT)

MetaKmsDevice *
meta_kms_plane_get_device (MetaKmsPlane *plane)
{
  return plane->device;
}

uint32_t
meta_kms_plane_get_id (MetaKmsPlane *plane)
{
  g_return_val_if_fail (!plane->is_fake, 0);

  return plane->id;
}

MetaKmsPlaneType
meta_kms_plane_get_plane_type (MetaKmsPlane *plane)
{
  return plane->type;
}

void
meta_kms_plane_update_set_rotation (MetaKmsPlane           *plane,
                                    MetaKmsPlaneAssignment *plane_assignment,
                                    MetaMonitorTransform    transform)
{
  g_return_if_fail (meta_kms_plane_is_transform_handled (plane, transform));

  meta_kms_plane_assignment_set_plane_property (plane_assignment,
                                                plane->rotation_prop_id,
                                                plane->rotation_map[transform]);
}

gboolean
meta_kms_plane_is_transform_handled (MetaKmsPlane         *plane,
                                     MetaMonitorTransform  transform)
{
  switch (transform)
    {
    case META_MONITOR_TRANSFORM_NORMAL:
    case META_MONITOR_TRANSFORM_180:
    case META_MONITOR_TRANSFORM_FLIPPED:
    case META_MONITOR_TRANSFORM_FLIPPED_180:
      break;
    case META_MONITOR_TRANSFORM_90:
    case META_MONITOR_TRANSFORM_270:
    case META_MONITOR_TRANSFORM_FLIPPED_90:
    case META_MONITOR_TRANSFORM_FLIPPED_270:
      /*
       * Blacklist these transforms as testing shows that they don't work
       * anyway, e.g. due to the wrong buffer modifiers. They might as well be
       * less optimal due to the complexity dealing with rotation at scan-out,
       * potentially resulting in higher power consumption.
       */
      return FALSE;
    }
  return plane->all_hw_transforms & (1 << transform);
}

GArray *
meta_kms_plane_get_modifiers_for_format (MetaKmsPlane *plane,
                                         uint32_t      format)
{
  return g_hash_table_lookup (plane->formats_modifiers,
                              GUINT_TO_POINTER (format));
}

GArray *
meta_kms_plane_copy_drm_format_list (MetaKmsPlane *plane)
{
  GArray *formats;
  GHashTableIter it;
  gpointer key;
  unsigned int n_formats_modifiers;

  n_formats_modifiers = g_hash_table_size (plane->formats_modifiers);
  formats = g_array_sized_new (FALSE, FALSE,
                               sizeof (uint32_t),
                               n_formats_modifiers);
  g_hash_table_iter_init (&it, plane->formats_modifiers);
  while (g_hash_table_iter_next (&it, &key, NULL))
    {
      uint32_t drm_format = GPOINTER_TO_UINT (key);

      g_array_append_val (formats, drm_format);
    }

  return formats;
}

gboolean
meta_kms_plane_is_format_supported (MetaKmsPlane *plane,
                                    uint32_t      drm_format)
{
  return g_hash_table_lookup_extended (plane->formats_modifiers,
                                       GUINT_TO_POINTER (drm_format),
                                       NULL, NULL);
}

gboolean
meta_kms_plane_is_usable_with (MetaKmsPlane *plane,
                               MetaKmsCrtc  *crtc)
{
  return !!(plane->possible_crtcs & (1 << meta_kms_crtc_get_idx (crtc)));
}

static void
parse_rotations (MetaKmsPlane       *plane,
                 MetaKmsImplDevice  *impl_device,
                 drmModePropertyPtr  prop)
{
  int i;

  for (i = 0; i < prop->count_enums; i++)
    {
      MetaMonitorTransform transform = -1;

      if (strcmp (prop->enums[i].name, "rotate-0") == 0)
        transform = META_MONITOR_TRANSFORM_NORMAL;
      else if (strcmp (prop->enums[i].name, "rotate-90") == 0)
        transform = META_MONITOR_TRANSFORM_90;
      else if (strcmp (prop->enums[i].name, "rotate-180") == 0)
        transform = META_MONITOR_TRANSFORM_180;
      else if (strcmp (prop->enums[i].name, "rotate-270") == 0)
        transform = META_MONITOR_TRANSFORM_270;

      if (transform != -1)
        {
          plane->all_hw_transforms |= 1 << transform;
          plane->rotation_map[transform] = 1 << prop->enums[i].value;
        }
    }
}

static void
init_rotations (MetaKmsPlane            *plane,
                MetaKmsImplDevice       *impl_device,
                drmModeObjectProperties *drm_plane_props)
{
  drmModePropertyPtr prop;
  int idx;

  prop = meta_kms_impl_device_find_property (impl_device, drm_plane_props,
                                             "rotation", &idx);
  if (prop)
    {
      plane->rotation_prop_id = drm_plane_props->props[idx];
      parse_rotations (plane, impl_device, prop);
      drmModeFreeProperty (prop);
    }
}

static inline uint32_t *
drm_formats_ptr (struct drm_format_modifier_blob *blob)
{
  return (uint32_t *) (((char *) blob) + blob->formats_offset);
}

static inline struct drm_format_modifier *
drm_modifiers_ptr (struct drm_format_modifier_blob *blob)
{
  return (struct drm_format_modifier *) (((char *) blob) +
                                         blob->modifiers_offset);
}

static void
free_modifier_array (GArray *array)
{
  if (!array)
    return;

  g_array_free (array, TRUE);
}

static void
parse_formats (MetaKmsPlane      *plane,
               MetaKmsImplDevice *impl_device,
               uint32_t           blob_id)
{
  int fd;
  drmModePropertyBlobPtr blob;
  struct drm_format_modifier_blob *blob_fmt;
  uint32_t *formats;
  struct drm_format_modifier *drm_modifiers;
  unsigned int fmt_i, mod_i;

  g_return_if_fail (g_hash_table_size (plane->formats_modifiers) == 0);

  if (blob_id == 0)
    return;

  fd = meta_kms_impl_device_get_fd (impl_device);
  blob = drmModeGetPropertyBlob (fd, blob_id);
  if (!blob)
    return;

  if (blob->length < sizeof (struct drm_format_modifier_blob))
    {
      drmModeFreePropertyBlob (blob);
      return;
    }

  blob_fmt = blob->data;

  formats = drm_formats_ptr (blob_fmt);
  drm_modifiers = drm_modifiers_ptr (blob_fmt);

  for (fmt_i = 0; fmt_i < blob_fmt->count_formats; fmt_i++)
    {
      GArray *modifiers = g_array_new (FALSE, FALSE, sizeof (uint64_t));

      for (mod_i = 0; mod_i < blob_fmt->count_modifiers; mod_i++)
        {
          struct drm_format_modifier *drm_modifier = &drm_modifiers[mod_i];

          /*
           * The modifier advertisement blob is partitioned into groups of
           * 64 formats.
           */
          if (fmt_i < drm_modifier->offset || fmt_i > drm_modifier->offset + 63)
            continue;

          if (!(drm_modifier->formats & (1 << (fmt_i - drm_modifier->offset))))
            continue;

          g_array_append_val (modifiers, drm_modifier->modifier);
        }

      if (modifiers->len == 0)
        {
          free_modifier_array (modifiers);
          modifiers = NULL;
        }

      g_hash_table_insert (plane->formats_modifiers,
                           GUINT_TO_POINTER (formats[fmt_i]),
                           modifiers);
    }

  drmModeFreePropertyBlob (blob);
}

static void
set_formats_from_array (MetaKmsPlane   *plane,
                        const uint32_t *formats,
                        size_t          n_formats)
{
  size_t i;

  for (i = 0; i < n_formats; i++)
    {
      g_hash_table_insert (plane->formats_modifiers,
                           GUINT_TO_POINTER (formats[i]), NULL);
    }
}

/*
 * In case the DRM driver does not expose a format list for the
 * primary plane (does not support universal planes nor
 * IN_FORMATS property), hardcode something that is probably supported.
 */
static const uint32_t drm_default_formats[] =
  {
    /* The format everything should always support by convention */
    DRM_FORMAT_XRGB8888,
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
    /* OpenGL GL_RGBA, GL_UNSIGNED_BYTE format, hopefully supported */
    DRM_FORMAT_XBGR8888
#endif
  };

static void
init_formats (MetaKmsPlane            *plane,
              MetaKmsImplDevice       *impl_device,
              drmModePlane            *drm_plane,
              drmModeObjectProperties *drm_plane_props)
{
  drmModePropertyPtr prop;
  int idx;

  prop = meta_kms_impl_device_find_property (impl_device, drm_plane_props,
                                             "IN_FORMATS", &idx);
  if (prop)
    {
      uint32_t blob_id;

      blob_id = drm_plane_props->prop_values[idx];
      parse_formats (plane, impl_device, blob_id);
      drmModeFreeProperty (prop);
    }

  if (g_hash_table_size (plane->formats_modifiers) == 0)
    {
      set_formats_from_array (plane,
                              drm_plane->formats,
                              drm_plane->count_formats);
    }

  /* final formats fallback to something hardcoded */
  if (g_hash_table_size (plane->formats_modifiers) == 0)
    {
      set_formats_from_array (plane,
                              drm_default_formats,
                              G_N_ELEMENTS (drm_default_formats));
    }
}

MetaKmsPlane *
meta_kms_plane_new (MetaKmsPlaneType         type,
                    MetaKmsImplDevice       *impl_device,
                    drmModePlane            *drm_plane,
                    drmModeObjectProperties *drm_plane_props)
{
  MetaKmsPlane *plane;

  plane = g_object_new (META_TYPE_KMS_PLANE, NULL);
  plane->type = type;
  plane->id = drm_plane->plane_id;
  plane->possible_crtcs = drm_plane->possible_crtcs;
  plane->device = meta_kms_impl_device_get_device (impl_device);

  init_rotations (plane, impl_device, drm_plane_props);
  init_formats (plane, impl_device, drm_plane, drm_plane_props);

  return plane;
}

MetaKmsPlane *
meta_kms_plane_new_fake (MetaKmsPlaneType  type,
                         MetaKmsCrtc      *crtc)
{
  MetaKmsPlane *plane;

  static const uint32_t fake_plane_drm_formats[] =
    {
      DRM_FORMAT_XRGB8888,
      DRM_FORMAT_ARGB8888,
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      /* OpenGL GL_RGBA, GL_UNSIGNED_BYTE format, hopefully supported */
      DRM_FORMAT_XBGR8888,
      DRM_FORMAT_ABGR8888
#endif
    };

  plane = g_object_new (META_TYPE_KMS_PLANE, NULL);
  plane->type = type;
  plane->is_fake = TRUE;
  plane->possible_crtcs = 1 << meta_kms_crtc_get_idx (crtc);
  plane->device = meta_kms_crtc_get_device (crtc);

  set_formats_from_array (plane,
                          fake_plane_drm_formats,
                          G_N_ELEMENTS (fake_plane_drm_formats));

  return plane;
}

static void
meta_kms_plane_finalize (GObject *object)
{
  MetaKmsPlane *plane = META_KMS_PLANE (object);

  g_hash_table_destroy (plane->formats_modifiers);

  G_OBJECT_CLASS (meta_kms_plane_parent_class)->finalize (object);
}

static void
meta_kms_plane_init (MetaKmsPlane *plane)
{
  plane->formats_modifiers =
    g_hash_table_new_full (g_direct_hash,
                           g_direct_equal,
                           NULL,
                           (GDestroyNotify) free_modifier_array);
}

static void
meta_kms_plane_class_init (MetaKmsPlaneClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_kms_plane_finalize;
}
