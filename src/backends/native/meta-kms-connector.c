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

#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-connector-private.h"

#include <errno.h>

#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-impl-device.h"
#include "backends/native/meta-kms-update-private.h"

struct _MetaKmsConnector
{
  GObject parent;

  MetaKmsDevice *device;

  uint32_t id;
  MetaConnectorType type;
  uint32_t type_id;
  char *name;

  MetaKmsConnectorState *current_state;

  uint32_t dpms_prop_id;
  uint32_t underscan_prop_id;
  uint32_t underscan_hborder_prop_id;
  uint32_t underscan_vborder_prop_id;
  uint32_t edid_blob_id;
  uint32_t tile_blob_id;
};

G_DEFINE_TYPE (MetaKmsConnector, meta_kms_connector, G_TYPE_OBJECT)

MetaKmsDevice *
meta_kms_connector_get_device (MetaKmsConnector *connector)
{
  return connector->device;
}

void
meta_kms_connector_update_set_dpms_state (MetaKmsConnector *connector,
                                          MetaKmsUpdate    *update,
                                          uint64_t          state)
{
  meta_kms_update_set_connector_property (update,
                                          connector,
                                          connector->dpms_prop_id,
                                          state);
}

void
meta_kms_connector_set_underscanning (MetaKmsConnector *connector,
                                      MetaKmsUpdate    *update,
                                      uint64_t          hborder,
                                      uint64_t          vborder)
{
  meta_kms_update_set_connector_property (update,
                                          connector,
                                          connector->underscan_prop_id,
                                          1);
  meta_kms_update_set_connector_property (update,
                                          connector,
                                          connector->underscan_hborder_prop_id,
                                          hborder);
  meta_kms_update_set_connector_property (update,
                                          connector,
                                          connector->underscan_vborder_prop_id,
                                          vborder);
}

void
meta_kms_connector_unset_underscanning (MetaKmsConnector *connector,
                                        MetaKmsUpdate    *update)
{
  meta_kms_update_set_connector_property (update,
                                          connector,
                                          connector->underscan_prop_id,
                                          0);
}

MetaConnectorType
meta_kms_connector_get_connector_type (MetaKmsConnector *connector)
{
  return connector->type;
}

uint32_t
meta_kms_connector_get_id (MetaKmsConnector *connector)
{
  return connector->id;
}

const char *
meta_kms_connector_get_name (MetaKmsConnector *connector)
{
  return connector->name;
}

gboolean
meta_kms_connector_can_clone (MetaKmsConnector *connector,
                              MetaKmsConnector *other_connector)
{
  MetaKmsConnectorState *state = connector->current_state;
  MetaKmsConnectorState *other_state = other_connector->current_state;

  if (state->common_possible_clones == 0 ||
      other_state->common_possible_clones == 0)
    return FALSE;

  if (state->encoder_device_idxs != other_state->encoder_device_idxs)
    return FALSE;

  return TRUE;
}

const MetaKmsConnectorState *
meta_kms_connector_get_current_state (MetaKmsConnector *connector)
{
  return connector->current_state;
}

gboolean
meta_kms_connector_is_underscanning_supported (MetaKmsConnector *connector)
{
  return connector->underscan_prop_id != 0;
}

static void
set_panel_orientation (MetaKmsConnectorState *state,
                       drmModePropertyPtr     prop,
                       uint64_t               orientation)
{
  const char *name;

  name = prop->enums[orientation].name;
  if (strcmp (name, "Upside Down") == 0)
    {
      state->panel_orientation_transform = META_MONITOR_TRANSFORM_180;
    }
  else if (strcmp (name, "Left Side Up") == 0)
    {
      /* Left side up, rotate 90 degrees counter clockwise to correct */
      state->panel_orientation_transform = META_MONITOR_TRANSFORM_90;
    }
  else if (strcmp (name, "Right Side Up") == 0)
    {
      /* Right side up, rotate 270 degrees counter clockwise to correct */
      state->panel_orientation_transform = META_MONITOR_TRANSFORM_270;
    }
  else
    {
      state->panel_orientation_transform = META_MONITOR_TRANSFORM_NORMAL;
    }
}

static void
state_set_properties (MetaKmsConnectorState *state,
                      MetaKmsImplDevice     *impl_device,
                      drmModeConnector      *drm_connector)
{
  int fd;
  int i;

  fd = meta_kms_impl_device_get_fd (impl_device);

  for (i = 0; i < drm_connector->count_props; i++)
    {
      drmModePropertyPtr prop;

      prop = drmModeGetProperty (fd, drm_connector->props[i]);
      if (!prop)
        continue;

      if ((prop->flags & DRM_MODE_PROP_RANGE) &&
          strcmp (prop->name, "suggested X") == 0)
        state->suggested_x = drm_connector->prop_values[i];
      else if ((prop->flags & DRM_MODE_PROP_RANGE) &&
               strcmp (prop->name, "suggested Y") == 0)
        state->suggested_y = drm_connector->prop_values[i];
      else if ((prop->flags & DRM_MODE_PROP_RANGE) &&
               strcmp (prop->name, "hotplug_mode_update") == 0)
        state->hotplug_mode_update = drm_connector->prop_values[i];
      else if (strcmp (prop->name, "scaling mode") == 0)
        state->has_scaling = TRUE;
      else if ((prop->flags & DRM_MODE_PROP_ENUM) &&
               strcmp (prop->name, "panel orientation") == 0)
        set_panel_orientation (state, prop, drm_connector->prop_values[i]);

      drmModeFreeProperty (prop);
    }
}

static CoglSubpixelOrder
drm_subpixel_order_to_cogl_subpixel_order (drmModeSubPixel subpixel)
{
  switch (subpixel)
    {
    case DRM_MODE_SUBPIXEL_NONE:
      return COGL_SUBPIXEL_ORDER_NONE;
      break;
    case DRM_MODE_SUBPIXEL_HORIZONTAL_RGB:
      return COGL_SUBPIXEL_ORDER_HORIZONTAL_RGB;
      break;
    case DRM_MODE_SUBPIXEL_HORIZONTAL_BGR:
      return COGL_SUBPIXEL_ORDER_HORIZONTAL_BGR;
      break;
    case DRM_MODE_SUBPIXEL_VERTICAL_RGB:
      return COGL_SUBPIXEL_ORDER_VERTICAL_RGB;
      break;
    case DRM_MODE_SUBPIXEL_VERTICAL_BGR:
      return COGL_SUBPIXEL_ORDER_VERTICAL_BGR;
      break;
    case DRM_MODE_SUBPIXEL_UNKNOWN:
      return COGL_SUBPIXEL_ORDER_UNKNOWN;
    }
  return COGL_SUBPIXEL_ORDER_UNKNOWN;
}

static void
state_set_edid (MetaKmsConnectorState *state,
                MetaKmsConnector      *connector,
                MetaKmsImplDevice     *impl_device,
                uint32_t               blob_id)
{
  int fd;
  drmModePropertyBlobPtr edid_blob;
  GBytes *edid_data;

  fd = meta_kms_impl_device_get_fd (impl_device);
  edid_blob = drmModeGetPropertyBlob (fd, blob_id);
  if (!edid_blob)
    {
      g_warning ("Failed to read EDID of connector %s: %s",
                 connector->name, g_strerror (errno));
      return;
    }

   edid_data = g_bytes_new (edid_blob->data, edid_blob->length);
   drmModeFreePropertyBlob (edid_blob);

   state->edid_data = edid_data;
}

static void
state_set_tile_info (MetaKmsConnectorState *state,
                     MetaKmsConnector      *connector,
                     MetaKmsImplDevice     *impl_device,
                     uint32_t               blob_id)
{
  int fd;
  drmModePropertyBlobPtr tile_blob;

  state->tile_info = (MetaTileInfo) { 0 };

  fd = meta_kms_impl_device_get_fd (impl_device);
  tile_blob = drmModeGetPropertyBlob (fd, blob_id);
  if (!tile_blob)
    {
      g_warning ("Failed to read TILE of connector %s: %s",
                 connector->name, strerror (errno));
      return;
    }

  if (tile_blob->length > 0)
    {
      if (sscanf ((char *) tile_blob->data, "%d:%d:%d:%d:%d:%d:%d:%d",
                  &state->tile_info.group_id,
                  &state->tile_info.flags,
                  &state->tile_info.max_h_tiles,
                  &state->tile_info.max_v_tiles,
                  &state->tile_info.loc_h_tile,
                  &state->tile_info.loc_v_tile,
                  &state->tile_info.tile_w,
                  &state->tile_info.tile_h) != 8)
        {
          g_warning ("Couldn't understand TILE property blob of connector %s",
                     connector->name);
          state->tile_info = (MetaTileInfo) { 0 };
        }
    }

  drmModeFreePropertyBlob (tile_blob);
}

static void
state_set_blobs (MetaKmsConnectorState *state,
                 MetaKmsConnector      *connector,
                 MetaKmsImplDevice     *impl_device,
                 drmModeConnector      *drm_connector)
{
  int fd;
  int i;

  fd = meta_kms_impl_device_get_fd (impl_device);

  for (i = 0; i < drm_connector->count_props; i++)
    {
      drmModePropertyPtr prop;

      prop = drmModeGetProperty (fd, drm_connector->props[i]);
      if (!prop)
        continue;

      if (prop->flags & DRM_MODE_PROP_BLOB)
        {
          uint32_t blob_id;

          blob_id = drm_connector->prop_values[i];

          if (blob_id)
            {
              if (strcmp (prop->name, "EDID") == 0)
                state_set_edid (state, connector, impl_device, blob_id);
              else if (strcmp (prop->name, "TILE") == 0)
                state_set_tile_info (state, connector, impl_device, blob_id);
            }
        }

      drmModeFreeProperty (prop);
    }
}

static void
state_set_physical_dimensions (MetaKmsConnectorState *state,
                               drmModeConnector      *drm_connector)
{
  state->width_mm = drm_connector->mmWidth;
  state->height_mm = drm_connector->mmHeight;
}

static void
state_set_modes (MetaKmsConnectorState *state,
                 drmModeConnector      *drm_connector)
{
  state->modes =
    g_memdup (drm_connector->modes,
              drm_connector->count_modes * sizeof (drmModeModeInfo));
  state->n_modes = drm_connector->count_modes;
}

static void
set_encoder_device_idx_bit (uint32_t          *encoder_device_idxs,
                            uint32_t           encoder_id,
                            MetaKmsImplDevice *impl_device,
                            drmModeRes        *drm_resources)
{
  int fd;
  int i;

  fd = meta_kms_impl_device_get_fd (impl_device);

  for (i = 0; i < drm_resources->count_encoders; i++)
    {
      drmModeEncoder *drm_encoder;

      drm_encoder = drmModeGetEncoder (fd, drm_resources->encoders[i]);
      if (!drm_encoder)
        continue;

      if (drm_encoder->encoder_id == encoder_id)
        {
          *encoder_device_idxs |= (1 << i);
          drmModeFreeEncoder (drm_encoder);
          break;
        }

      drmModeFreeEncoder (drm_encoder);
    }
}

static void
state_set_crtc_state (MetaKmsConnectorState *state,
                      drmModeConnector      *drm_connector,
                      MetaKmsImplDevice     *impl_device,
                      drmModeRes            *drm_resources)
{
  int fd;
  int i;
  uint32_t common_possible_crtcs;
  uint32_t common_possible_clones;
  uint32_t encoder_device_idxs;

  fd = meta_kms_impl_device_get_fd (impl_device);

  common_possible_crtcs = UINT32_MAX;
  common_possible_clones = UINT32_MAX;
  encoder_device_idxs = 0;
  for (i = 0; i < drm_connector->count_encoders; i++)
    {
      drmModeEncoder *drm_encoder;

      drm_encoder = drmModeGetEncoder (fd, drm_connector->encoders[i]);
      if (!drm_encoder)
        continue;

      common_possible_crtcs &= drm_encoder->possible_crtcs;
      common_possible_clones &= drm_encoder->possible_clones;

      set_encoder_device_idx_bit (&encoder_device_idxs,
                                  drm_encoder->encoder_id,
                                  impl_device,
                                  drm_resources);

      if (drm_connector->encoder_id == drm_encoder->encoder_id)
        state->current_crtc_id = drm_encoder->crtc_id;

      drmModeFreeEncoder (drm_encoder);
    }

  state->common_possible_crtcs = common_possible_crtcs;
  state->common_possible_clones = common_possible_clones;
  state->encoder_device_idxs = encoder_device_idxs;
}

static MetaKmsConnectorState *
meta_kms_connector_state_new (void)
{
  MetaKmsConnectorState *state;

  state = g_new0 (MetaKmsConnectorState, 1);
  state->suggested_x = -1;
  state->suggested_y = -1;

  return state;
}

static void
meta_kms_connector_state_free (MetaKmsConnectorState *state)
{
  g_clear_pointer (&state->edid_data, g_bytes_unref);
  g_free (state->modes);
  g_free (state);
}

static void
meta_kms_connector_read_state (MetaKmsConnector  *connector,
                               MetaKmsImplDevice *impl_device,
                               drmModeConnector  *drm_connector,
                               drmModeRes        *drm_resources)
{
  MetaKmsConnectorState *state;

  g_clear_pointer (&connector->current_state, meta_kms_connector_state_free);

  if (!drm_connector || drm_connector->connection != DRM_MODE_CONNECTED)
    return;

  state = meta_kms_connector_state_new ();

  state_set_blobs (state, connector, impl_device, drm_connector);

  state_set_properties (state, impl_device, drm_connector);

  state->subpixel_order =
    drm_subpixel_order_to_cogl_subpixel_order (drm_connector->subpixel);

  state_set_physical_dimensions (state, drm_connector);

  state_set_modes (state, drm_connector);

  state_set_crtc_state (state, drm_connector, impl_device, drm_resources);

  connector->current_state = state;
}

void
meta_kms_connector_update_state (MetaKmsConnector *connector,
                                 drmModeRes       *drm_resources)
{
  MetaKmsImplDevice *impl_device;
  drmModeConnector *drm_connector;

  impl_device = meta_kms_device_get_impl_device (connector->device);
  drm_connector = drmModeGetConnector (meta_kms_impl_device_get_fd (impl_device),
                                       connector->id);
  meta_kms_connector_read_state (connector, impl_device,
                                 drm_connector,
                                 drm_resources);
  if (drm_connector)
    drmModeFreeConnector (drm_connector);
}

void
meta_kms_connector_predict_state (MetaKmsConnector *connector,
                                  MetaKmsUpdate    *update)
{
  GList *mode_sets;
  GList *l;

  if (!connector->current_state)
    return;

  mode_sets = meta_kms_update_get_mode_sets (update);
  for (l = mode_sets; l; l = l->next)
    {
      MetaKmsModeSet *mode_set = l->data;
      MetaKmsCrtc *crtc;

      if (!g_list_find (mode_set->connectors, connector))
        continue;

      crtc = mode_set->crtc;
      if (crtc)
        connector->current_state->current_crtc_id = meta_kms_crtc_get_id (crtc);
      else
        connector->current_state->current_crtc_id = 0;

      break;
    }
}

static void
find_property_ids (MetaKmsConnector  *connector,
                   MetaKmsImplDevice *impl_device,
                   drmModeConnector  *drm_connector)
{
  int fd;
  int i;

  fd = meta_kms_impl_device_get_fd (impl_device);

  for (i = 0; i < drm_connector->count_props; i++)
    {
      drmModePropertyPtr prop;

      prop = drmModeGetProperty (fd, drm_connector->props[i]);
      if (!prop)
        continue;

      if ((prop->flags & DRM_MODE_PROP_ENUM) &&
          strcmp (prop->name, "DPMS") == 0)
        connector->dpms_prop_id = prop->prop_id;
      else if ((prop->flags & DRM_MODE_PROP_ENUM) &&
               strcmp (prop->name, "underscan") == 0)
        connector->underscan_prop_id = prop->prop_id;
      else if ((prop->flags & DRM_MODE_PROP_RANGE) &&
               strcmp (prop->name, "underscan hborder") == 0)
        connector->underscan_hborder_prop_id = prop->prop_id;
      else if ((prop->flags & DRM_MODE_PROP_RANGE) &&
               strcmp (prop->name, "underscan vborder") == 0)
        connector->underscan_vborder_prop_id = prop->prop_id;

      drmModeFreeProperty (prop);
    }
}

static char *
make_connector_name (drmModeConnector *drm_connector)
{
  static const char * const connector_type_names[] = {
    "None",
    "VGA",
    "DVI-I",
    "DVI-D",
    "DVI-A",
    "Composite",
    "SVIDEO",
    "LVDS",
    "Component",
    "DIN",
    "DP",
    "HDMI",
    "HDMI-B",
    "TV",
    "eDP",
    "Virtual",
    "DSI",
  };

  if (drm_connector->connector_type < G_N_ELEMENTS (connector_type_names))
    return g_strdup_printf ("%s-%d",
                            connector_type_names[drm_connector->connector_type],
                            drm_connector->connector_type_id);
  else
    return g_strdup_printf ("Unknown%d-%d",
                            drm_connector->connector_type,
                            drm_connector->connector_type_id);
}

gboolean
meta_kms_connector_is_same_as (MetaKmsConnector *connector,
                               drmModeConnector *drm_connector)
{
  return (connector->id == drm_connector->connector_id &&
          connector->type == drm_connector->connector_type &&
          connector->type_id == drm_connector->connector_type_id);
}

MetaKmsConnector *
meta_kms_connector_new (MetaKmsImplDevice *impl_device,
                        drmModeConnector  *drm_connector,
                        drmModeRes        *drm_resources)
{
  MetaKmsConnector *connector;

  connector = g_object_new (META_TYPE_KMS_CONNECTOR, NULL);
  connector->device = meta_kms_impl_device_get_device (impl_device);
  connector->id = drm_connector->connector_id;
  connector->type = (MetaConnectorType) drm_connector->connector_type;
  connector->type_id = drm_connector->connector_type_id;
  connector->name = make_connector_name (drm_connector);

  find_property_ids (connector, impl_device, drm_connector);

  meta_kms_connector_read_state (connector, impl_device,
                                 drm_connector,
                                 drm_resources);

  return connector;
}

static void
meta_kms_connector_finalize (GObject *object)
{
  MetaKmsConnector *connector = META_KMS_CONNECTOR (object);

  g_clear_pointer (&connector->current_state, meta_kms_connector_state_free);
  g_free (connector->name);

  G_OBJECT_CLASS (meta_kms_connector_parent_class)->finalize (object);
}

static void
meta_kms_connector_init (MetaKmsConnector *connector)
{
}

static void
meta_kms_connector_class_init (MetaKmsConnectorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_kms_connector_finalize;
}
