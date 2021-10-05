/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat
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

#include "backends/meta-monitor.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-crtc.h"
#include "backends/meta-gpu.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-settings-private.h"
#include "backends/meta-output.h"
#include "core/boxes-private.h"

#define SCALE_FACTORS_PER_INTEGER 4
#define SCALE_FACTORS_STEPS (1.0 / (float) SCALE_FACTORS_PER_INTEGER)
#define MINIMUM_SCALE_FACTOR 1.0f
#define MAXIMUM_SCALE_FACTOR 4.0f
#define MINIMUM_LOGICAL_AREA (800 * 480)
#define MAXIMUM_REFRESH_RATE_DIFF 0.001

typedef struct _MetaMonitorMode
{
  MetaMonitor *monitor;
  char *id;
  MetaMonitorModeSpec spec;
  MetaMonitorCrtcMode *crtc_modes;
} MetaMonitorMode;

typedef struct _MetaMonitorModeTiled
{
  MetaMonitorMode parent;

  gboolean is_tiled;
} MetaMonitorModeTiled;

typedef struct _MetaMonitorPrivate
{
  MetaGpu *gpu;

  GList *outputs;
  GList *modes;
  GHashTable *mode_ids;

  MetaMonitorMode *preferred_mode;
  MetaMonitorMode *current_mode;

  MetaMonitorSpec *spec;

  MetaLogicalMonitor *logical_monitor;

  /*
   * The primary or first output for this monitor, 0 if we can't figure out.
   * It can be matched to a winsys_id of a MetaOutput.
   *
   * This is used as an opaque token on reconfiguration when switching from
   * clone to extened, to decide on what output the windows should go next
   * (it's an attempt to keep windows on the same monitor, and preferably on
   * the primary one).
   */
  uint64_t winsys_id;

  char *display_name;
} MetaMonitorPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaMonitor, meta_monitor, G_TYPE_OBJECT)

struct _MetaMonitorNormal
{
  MetaMonitor parent;
};

G_DEFINE_TYPE (MetaMonitorNormal, meta_monitor_normal, META_TYPE_MONITOR)

struct _MetaMonitorTiled
{
  MetaMonitor parent;

  MetaMonitorManager *monitor_manager;

  uint32_t tile_group_id;

  /* The tile (0, 0) output. */
  MetaOutput *origin_output;

  /* The output enabled even when a non-tiled mode is used. */
  MetaOutput *main_output;
};

G_DEFINE_TYPE (MetaMonitorTiled, meta_monitor_tiled, META_TYPE_MONITOR)

static void
meta_monitor_mode_free (MetaMonitorMode *mode);

MetaMonitorSpec *
meta_monitor_spec_clone (MetaMonitorSpec *monitor_spec)
{
  MetaMonitorSpec *new_monitor_spec;

  new_monitor_spec = g_new0 (MetaMonitorSpec, 1);
  *new_monitor_spec = (MetaMonitorSpec) {
    .connector = g_strdup (monitor_spec->connector),
    .vendor = g_strdup (monitor_spec->vendor),
    .product = g_strdup (monitor_spec->product),
    .serial = g_strdup (monitor_spec->serial),
  };

  return new_monitor_spec;
}

gboolean
meta_monitor_spec_equals (MetaMonitorSpec *monitor_spec,
                          MetaMonitorSpec *other_monitor_spec)
{
  return (g_str_equal (monitor_spec->connector, other_monitor_spec->connector) &&
          g_str_equal (monitor_spec->vendor, other_monitor_spec->vendor) &&
          g_str_equal (monitor_spec->product, other_monitor_spec->product) &&
          g_str_equal (monitor_spec->serial, other_monitor_spec->serial));
}

int
meta_monitor_spec_compare (MetaMonitorSpec *monitor_spec_a,
                           MetaMonitorSpec *monitor_spec_b)
{
  int ret;

  ret = strcmp (monitor_spec_a->connector, monitor_spec_b->connector);
  if (ret != 0)
    return ret;

  ret = strcmp (monitor_spec_a->vendor, monitor_spec_b->vendor);
  if (ret != 0)
    return ret;

  ret = strcmp (monitor_spec_a->product, monitor_spec_b->product);
  if (ret != 0)
    return ret;

  return strcmp (monitor_spec_a->serial, monitor_spec_b->serial);
}

void
meta_monitor_spec_free (MetaMonitorSpec *monitor_spec)
{
  g_free (monitor_spec->connector);
  g_free (monitor_spec->vendor);
  g_free (monitor_spec->product);
  g_free (monitor_spec->serial);
  g_free (monitor_spec);
}

static void
meta_monitor_generate_spec (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);
  MetaOutput *output = meta_monitor_get_main_output (monitor);
  MetaMonitorSpec *monitor_spec;

  monitor_spec = g_new0 (MetaMonitorSpec, 1);
  *monitor_spec = (MetaMonitorSpec) {
    .connector = g_strdup (output->name),
    .vendor = g_strdup (output->vendor),
    .product = g_strdup (output->product),
    .serial = g_strdup (output->serial),
  };

  priv->spec = monitor_spec;
}

static const double known_diagonals[] = {
    12.1,
    13.3,
    15.6
};

static char *
diagonal_to_str (double d)
{
  unsigned int i;

  for (i = 0; i < G_N_ELEMENTS (known_diagonals); i++)
    {
      double delta;

      delta = fabs(known_diagonals[i] - d);
      if (delta < 0.1)
        return g_strdup_printf ("%0.1lf\"", known_diagonals[i]);
    }

  return g_strdup_printf ("%d\"", (int) (d + 0.5));
}

static char *
meta_monitor_make_display_name (MetaMonitor        *monitor,
                                MetaMonitorManager *monitor_manager)
{
  g_autofree char *inches = NULL;
  g_autofree char *vendor_name = NULL;
  const char *vendor = NULL;
  const char *product_name = NULL;
  int width_mm;
  int height_mm;

  meta_monitor_get_physical_dimensions (monitor, &width_mm, &height_mm);

  if (meta_monitor_is_laptop_panel (monitor))
      return g_strdup (_("Built-in display"));

  if (width_mm > 0 && height_mm > 0)
    {
      if (!meta_monitor_has_aspect_as_size (monitor))
        {
          double d = sqrt (width_mm * width_mm +
                           height_mm * height_mm);
          inches = diagonal_to_str (d / 25.4);
        }
      else
        {
          product_name = meta_monitor_get_product (monitor);
        }
    }

  vendor = meta_monitor_get_vendor (monitor);

  if (g_strcmp0 (vendor, "unknown") != 0)
    {
      vendor_name = meta_monitor_manager_get_vendor_name (monitor_manager,
                                                          vendor);

      if (!vendor_name)
        vendor_name = g_strdup (vendor);
    }
  else
    {
      if (inches != NULL)
        vendor_name = g_strdup (_("Unknown"));
      else
        vendor_name = g_strdup (_("Unknown Display"));
    }

  if (inches != NULL)
    {
       /**/
      return g_strdup_printf (C_("This is a monitor vendor name, followed by a "
                                 "size in inches, like 'Dell 15\"'",
                                 "%s %s"),
                              vendor_name, inches);
    }
  else if (product_name != NULL)
    {
      return g_strdup_printf (C_("This is a monitor vendor name followed by "
                                 "product/model name where size in inches "
                                 "could not be calculated, e.g. Dell U2414H",
                                 "%s %s"),
                              vendor_name, product_name);
    }
  else
    {
      return g_strdup (vendor_name);
    }
}

MetaGpu *
meta_monitor_get_gpu (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  return priv->gpu;
}

GList *
meta_monitor_get_outputs (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  return priv->outputs;
}

MetaOutput *
meta_monitor_get_main_output (MetaMonitor *monitor)
{
  return META_MONITOR_GET_CLASS (monitor)->get_main_output (monitor);
}

gboolean
meta_monitor_is_active (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  return !!priv->current_mode;
}

gboolean
meta_monitor_is_primary (MetaMonitor *monitor)
{
  MetaOutput *output;

  output = meta_monitor_get_main_output (monitor);

  return output->is_primary;
}

gboolean
meta_monitor_supports_underscanning (MetaMonitor *monitor)
{
  MetaOutput *output;

  output = meta_monitor_get_main_output (monitor);

  return output->supports_underscanning;
}

gboolean
meta_monitor_is_underscanning (MetaMonitor *monitor)
{
  MetaOutput *output;

  output = meta_monitor_get_main_output (monitor);

  return output->is_underscanning;
}

gboolean
meta_monitor_is_laptop_panel (MetaMonitor *monitor)
{
  MetaOutput *output;

  output = meta_monitor_get_main_output (monitor);

  switch (output->connector_type)
    {
    case META_CONNECTOR_TYPE_eDP:
    case META_CONNECTOR_TYPE_LVDS:
    case META_CONNECTOR_TYPE_DSI:
      return TRUE;
    default:
      return FALSE;
    }
}

gboolean
meta_monitor_is_same_as (MetaMonitor *monitor,
                         MetaMonitor *other_monitor)
{
  MetaMonitorPrivate *priv =
    meta_monitor_get_instance_private (monitor);
  MetaMonitorPrivate *other_priv =
    meta_monitor_get_instance_private (other_monitor);

  return priv->winsys_id == other_priv->winsys_id;
}

void
meta_monitor_get_current_resolution (MetaMonitor *monitor,
                                     int         *width,
                                     int         *height)
{
  MetaMonitorMode *mode = meta_monitor_get_current_mode (monitor);

  *width = mode->spec.width;
  *height = mode->spec.height;
}

void
meta_monitor_derive_layout (MetaMonitor   *monitor,
                            MetaRectangle *layout)
{
  META_MONITOR_GET_CLASS (monitor)->derive_layout (monitor, layout);
}

void
meta_monitor_get_physical_dimensions (MetaMonitor *monitor,
                                      int         *width_mm,
                                      int         *height_mm)
{
  MetaOutput *output;

  output = meta_monitor_get_main_output (monitor);
  *width_mm = output->width_mm;
  *height_mm = output->height_mm;
}

CoglSubpixelOrder
meta_monitor_get_subpixel_order (MetaMonitor *monitor)
{
  MetaOutput *output;

  output = meta_monitor_get_main_output (monitor);
  return output->subpixel_order;
}

const char *
meta_monitor_get_connector (MetaMonitor *monitor)
{
  MetaOutput *output;

  output = meta_monitor_get_main_output (monitor);
  return output->name;
}

const char *
meta_monitor_get_vendor (MetaMonitor *monitor)
{
  MetaOutput *output;

  output = meta_monitor_get_main_output (monitor);
  return output->vendor;
}

const char *
meta_monitor_get_product (MetaMonitor *monitor)
{
  MetaOutput *output;

  output = meta_monitor_get_main_output (monitor);
  return output->product;
}

const char *
meta_monitor_get_serial (MetaMonitor *monitor)
{
  MetaOutput *output;

  output = meta_monitor_get_main_output (monitor);
  return output->serial;
}

MetaConnectorType
meta_monitor_get_connector_type (MetaMonitor *monitor)
{
  MetaOutput *output;

  output = meta_monitor_get_main_output (monitor);
  return output->connector_type;
}

MetaMonitorTransform
meta_monitor_logical_to_crtc_transform (MetaMonitor          *monitor,
                                        MetaMonitorTransform  transform)
{
  MetaOutput *output = meta_monitor_get_main_output (monitor);

  return meta_output_logical_to_crtc_transform (output, transform);
}

MetaMonitorTransform
meta_monitor_crtc_to_logical_transform (MetaMonitor          *monitor,
                                        MetaMonitorTransform  transform)
{
  MetaOutput *output = meta_monitor_get_main_output (monitor);

  return meta_output_crtc_to_logical_transform (output, transform);
}

static void
meta_monitor_dispose (GObject *object)
{
  MetaMonitor *monitor = META_MONITOR (object);
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  if (priv->outputs)
    {
      g_list_free_full (priv->outputs, g_object_unref);
      priv->outputs = NULL;
    }

  G_OBJECT_CLASS (meta_monitor_parent_class)->dispose (object);
}

static void
meta_monitor_finalize (GObject *object)
{
  MetaMonitor *monitor = META_MONITOR (object);
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  g_hash_table_destroy (priv->mode_ids);
  g_list_free_full (priv->modes, (GDestroyNotify) meta_monitor_mode_free);
  meta_monitor_spec_free (priv->spec);
  g_free (priv->display_name);

  G_OBJECT_CLASS (meta_monitor_parent_class)->finalize (object);
}

static void
meta_monitor_init (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  priv->mode_ids = g_hash_table_new (g_str_hash, g_str_equal);
}

static void
meta_monitor_class_init (MetaMonitorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_monitor_dispose;
  object_class->finalize = meta_monitor_finalize;
}

static char *
generate_mode_id (MetaMonitorModeSpec *monitor_mode_spec)
{
  gboolean is_interlaced;
  char refresh_rate_str[G_ASCII_DTOSTR_BUF_SIZE];

  is_interlaced = !!(monitor_mode_spec->flags & META_CRTC_MODE_FLAG_INTERLACE);
  g_ascii_dtostr (refresh_rate_str, G_ASCII_DTOSTR_BUF_SIZE,
                  monitor_mode_spec->refresh_rate);

  return g_strdup_printf ("%dx%d%s@%s",
                          monitor_mode_spec->width,
                          monitor_mode_spec->height,
                          is_interlaced ? "i" : "",
                          refresh_rate_str);
}

static gboolean
meta_monitor_add_mode (MetaMonitor     *monitor,
                       MetaMonitorMode *monitor_mode,
                       gboolean         replace)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);
  MetaMonitorMode *existing_mode;

  existing_mode = g_hash_table_lookup (priv->mode_ids,
                                       meta_monitor_mode_get_id (monitor_mode));
  if (existing_mode && !replace)
    return FALSE;

  if (existing_mode)
    priv->modes = g_list_remove (priv->modes, existing_mode);

  priv->modes = g_list_append (priv->modes, monitor_mode);
  g_hash_table_replace (priv->mode_ids, monitor_mode->id, monitor_mode);

  return TRUE;
}

static MetaMonitorModeSpec
meta_monitor_create_spec (MetaMonitor  *monitor,
                          int           width,
                          int           height,
                          MetaCrtcMode *crtc_mode)
{
  MetaOutput *output = meta_monitor_get_main_output (monitor);

  if (meta_monitor_transform_is_rotated (output->panel_orientation_transform))
    {
      int temp = width;
      width = height;
      height = temp;
    }

  return (MetaMonitorModeSpec) {
    .width = width,
    .height = height,
    .refresh_rate = crtc_mode->refresh_rate,
    .flags = crtc_mode->flags & HANDLED_CRTC_MODE_FLAGS
  };
}

static void
meta_monitor_normal_generate_modes (MetaMonitorNormal *monitor_normal)
{
  MetaMonitor *monitor = META_MONITOR (monitor_normal);
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  MetaOutput *output;
  MetaCrtcModeFlag preferred_mode_flags;
  unsigned int i;

  output = meta_monitor_get_main_output (monitor);
  preferred_mode_flags = output->preferred_mode->flags;

  for (i = 0; i < output->n_modes; i++)
    {
      MetaCrtcMode *crtc_mode = output->modes[i];
      MetaCrtc *crtc;
      MetaMonitorMode *mode;
      gboolean replace;

      mode = g_new0 (MetaMonitorMode, 1);
      mode->monitor = monitor;
      mode->spec = meta_monitor_create_spec (monitor,
                                             crtc_mode->width,
                                             crtc_mode->height,
                                             crtc_mode);
      mode->id = generate_mode_id (&mode->spec);
      mode->crtc_modes = g_new (MetaMonitorCrtcMode, 1);
      mode->crtc_modes[0] = (MetaMonitorCrtcMode) {
        .output = output,
        .crtc_mode = crtc_mode
      };

      /*
       * We don't distinguish between all available mode flags, just the ones
       * that are configurable. We still need to pick some mode though, so
       * prefer ones that has the same set of flags as the preferred mode;
       * otherwise take the first one in the list. This guarantees that the
       * preferred mode is always added.
       */
      replace = crtc_mode->flags == preferred_mode_flags;

      if (!meta_monitor_add_mode (monitor, mode, replace))
        {
          g_assert (crtc_mode != output->preferred_mode);
          meta_monitor_mode_free (mode);
          continue;
        }

      if (crtc_mode == output->preferred_mode)
        monitor_priv->preferred_mode = mode;

      crtc = meta_output_get_assigned_crtc (output);
      if (crtc && crtc->config && crtc_mode == crtc->config->mode)
        monitor_priv->current_mode = mode;
    }
}

MetaMonitorNormal *
meta_monitor_normal_new (MetaGpu            *gpu,
                         MetaMonitorManager *monitor_manager,
                         MetaOutput         *output)
{
  MetaMonitorNormal *monitor_normal;
  MetaMonitor *monitor;
  MetaMonitorPrivate *monitor_priv;

  monitor_normal = g_object_new (META_TYPE_MONITOR_NORMAL, NULL);
  monitor = META_MONITOR (monitor_normal);
  monitor_priv = meta_monitor_get_instance_private (monitor);

  monitor_priv->gpu = gpu;

  monitor_priv->outputs = g_list_append (NULL, g_object_ref (output));
  monitor_priv->winsys_id = output->winsys_id;
  meta_monitor_generate_spec (monitor);

  meta_monitor_normal_generate_modes (monitor_normal);

  monitor_priv->display_name = meta_monitor_make_display_name (monitor,
                                                               monitor_manager);

  return monitor_normal;
}

static MetaOutput *
meta_monitor_normal_get_main_output (MetaMonitor *monitor)
{
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);

  return monitor_priv->outputs->data;
}

static void
meta_monitor_normal_derive_layout (MetaMonitor   *monitor,
                                   MetaRectangle *layout)
{
  MetaOutput *output;
  MetaCrtc *crtc;
  MetaCrtcConfig *crtc_config;

  output = meta_monitor_get_main_output (monitor);
  crtc = meta_output_get_assigned_crtc (output);
  crtc_config = crtc->config;

  g_return_if_fail (crtc_config);

  meta_rectangle_from_graphene_rect (&crtc_config->layout,
                                     META_ROUNDING_STRATEGY_ROUND,
                                     layout);
}

static gboolean
meta_monitor_normal_get_suggested_position (MetaMonitor *monitor,
                                            int         *x,
                                            int         *y)
{
  MetaOutput *output;

  output = meta_monitor_get_main_output (monitor);
  if (output->suggested_x < 0 && output->suggested_y < 0)
    return FALSE;

  if (x)
    *x = output->suggested_x;

  if (y)
    *y = output->suggested_y;

  return TRUE;
}

static void
meta_monitor_normal_calculate_crtc_pos (MetaMonitor          *monitor,
                                        MetaMonitorMode      *monitor_mode,
                                        MetaOutput           *output,
                                        MetaMonitorTransform  crtc_transform,
                                        int                  *out_x,
                                        int                  *out_y)
{
  *out_x = 0;
  *out_y = 0;
}

static void
meta_monitor_normal_init (MetaMonitorNormal *monitor)
{
}

static void
meta_monitor_normal_class_init (MetaMonitorNormalClass *klass)
{
  MetaMonitorClass *monitor_class = META_MONITOR_CLASS (klass);

  monitor_class->get_main_output = meta_monitor_normal_get_main_output;
  monitor_class->derive_layout = meta_monitor_normal_derive_layout;
  monitor_class->calculate_crtc_pos = meta_monitor_normal_calculate_crtc_pos;
  monitor_class->get_suggested_position = meta_monitor_normal_get_suggested_position;
}

uint32_t
meta_monitor_tiled_get_tile_group_id (MetaMonitorTiled *monitor_tiled)
{
  return monitor_tiled->tile_group_id;
}

gboolean
meta_monitor_get_suggested_position (MetaMonitor *monitor,
                                     int         *x,
                                     int         *y)
{
  return META_MONITOR_GET_CLASS (monitor)->get_suggested_position (monitor,
                                                                   x, y);
}

static void
add_tiled_monitor_outputs (MetaGpu          *gpu,
                           MetaMonitorTiled *monitor_tiled)
{
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (META_MONITOR (monitor_tiled));
  GList *outputs;
  GList *l;

  outputs = meta_gpu_get_outputs (gpu);
  for (l = outputs; l; l = l->next)
    {
      MetaOutput *output = l->data;

      if (output->tile_info.group_id != monitor_tiled->tile_group_id)
        continue;

      g_warn_if_fail (output->subpixel_order ==
                      monitor_tiled->origin_output->subpixel_order);

      monitor_priv->outputs = g_list_append (monitor_priv->outputs,
                                             g_object_ref (output));
    }
}

static void
calculate_tile_coordinate (MetaMonitor         *monitor,
                           MetaOutput          *output,
                           MetaMonitorTransform crtc_transform,
                           int                 *out_x,
                           int                 *out_y)
{
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  GList *l;
  int x = 0;
  int y = 0;

  for (l = monitor_priv->outputs; l; l = l->next)
    {
      MetaOutput *other_output = l->data;

      switch (crtc_transform)
        {
        case META_MONITOR_TRANSFORM_NORMAL:
        case META_MONITOR_TRANSFORM_FLIPPED:
          if (other_output->tile_info.loc_v_tile == output->tile_info.loc_v_tile &&
              other_output->tile_info.loc_h_tile < output->tile_info.loc_h_tile)
            x += other_output->tile_info.tile_w;
          if (other_output->tile_info.loc_h_tile == output->tile_info.loc_h_tile &&
              other_output->tile_info.loc_v_tile < output->tile_info.loc_v_tile)
            y += other_output->tile_info.tile_h;
          break;
        case META_MONITOR_TRANSFORM_180:
        case META_MONITOR_TRANSFORM_FLIPPED_180:
          if (other_output->tile_info.loc_v_tile == output->tile_info.loc_v_tile &&
              other_output->tile_info.loc_h_tile > output->tile_info.loc_h_tile)
            x += other_output->tile_info.tile_w;
          if (other_output->tile_info.loc_h_tile == output->tile_info.loc_h_tile &&
              other_output->tile_info.loc_v_tile > output->tile_info.loc_v_tile)
            y += other_output->tile_info.tile_h;
          break;
        case META_MONITOR_TRANSFORM_270:
        case META_MONITOR_TRANSFORM_FLIPPED_270:
          if (other_output->tile_info.loc_v_tile == output->tile_info.loc_v_tile &&
              other_output->tile_info.loc_h_tile > output->tile_info.loc_h_tile)
            y += other_output->tile_info.tile_w;
          if (other_output->tile_info.loc_h_tile == output->tile_info.loc_h_tile &&
              other_output->tile_info.loc_v_tile > output->tile_info.loc_v_tile)
            x += other_output->tile_info.tile_h;
          break;
        case META_MONITOR_TRANSFORM_90:
        case META_MONITOR_TRANSFORM_FLIPPED_90:
          if (other_output->tile_info.loc_v_tile == output->tile_info.loc_v_tile &&
              other_output->tile_info.loc_h_tile < output->tile_info.loc_h_tile)
            y += other_output->tile_info.tile_w;
          if (other_output->tile_info.loc_h_tile == output->tile_info.loc_h_tile &&
              other_output->tile_info.loc_v_tile < output->tile_info.loc_v_tile)
            x += other_output->tile_info.tile_h;
          break;
        }
    }

  *out_x = x;
  *out_y = y;
}

static void
meta_monitor_tiled_calculate_tiled_size (MetaMonitor *monitor,
                                         int         *out_width,
                                         int         *out_height)
{
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  GList *l;
  int width;
  int height;

  width = 0;
  height = 0;
  for (l = monitor_priv->outputs; l; l = l->next)
    {
      MetaOutput *output = l->data;

      if (output->tile_info.loc_v_tile == 0)
        width += output->tile_info.tile_w;

      if (output->tile_info.loc_h_tile == 0)
        height += output->tile_info.tile_h;
    }

  *out_width = width;
  *out_height = height;
}

static gboolean
is_monitor_mode_assigned (MetaMonitor     *monitor,
                          MetaMonitorMode *mode)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);
  GList *l;
  int i;

  for (l = priv->outputs, i = 0; l; l = l->next, i++)
    {
      MetaOutput *output = l->data;
      MetaMonitorCrtcMode *monitor_crtc_mode = &mode->crtc_modes[i];
      MetaCrtc *crtc;

      crtc = meta_output_get_assigned_crtc (output);
      if (monitor_crtc_mode->crtc_mode &&
          (!crtc || !crtc->config ||
           crtc->config->mode != monitor_crtc_mode->crtc_mode))
        return FALSE;
      else if (!monitor_crtc_mode->crtc_mode && crtc)
        return FALSE;
    }

  return TRUE;
}

static gboolean
is_crtc_mode_tiled (MetaOutput   *output,
                    MetaCrtcMode *crtc_mode)
{
  return (crtc_mode->width == (int) output->tile_info.tile_w &&
          crtc_mode->height == (int) output->tile_info.tile_h);
}

static MetaCrtcMode *
find_tiled_crtc_mode (MetaOutput   *output,
                      MetaCrtcMode *reference_crtc_mode)
{
  MetaCrtcMode *crtc_mode;
  unsigned int i;

  crtc_mode = output->preferred_mode;
  if (is_crtc_mode_tiled (output, crtc_mode))
    return crtc_mode;

  for (i = 0; i < output->n_modes; i++)
    {
      crtc_mode = output->modes[i];

      if (!is_crtc_mode_tiled (output, crtc_mode))
        continue;

      if (crtc_mode->refresh_rate != reference_crtc_mode->refresh_rate)
        continue;

      if (crtc_mode->flags != reference_crtc_mode->flags)
        continue;

      return crtc_mode;
    }

  return NULL;
}

static MetaMonitorMode *
create_tiled_monitor_mode (MetaMonitorTiled *monitor_tiled,
                           MetaCrtcMode     *reference_crtc_mode,
                           gboolean         *out_is_preferred)
{
  MetaMonitor *monitor = META_MONITOR (monitor_tiled);
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  MetaMonitorModeTiled *mode;
  int width, height;
  GList *l;
  unsigned int i;
  gboolean is_preferred = TRUE;

  mode = g_new0 (MetaMonitorModeTiled, 1);
  mode->is_tiled = TRUE;
  meta_monitor_tiled_calculate_tiled_size (monitor, &width, &height);
  mode->parent.monitor = monitor;
  mode->parent.spec =
    meta_monitor_create_spec (monitor, width, height, reference_crtc_mode);
  mode->parent.id = generate_mode_id (&mode->parent.spec);

  mode->parent.crtc_modes = g_new0 (MetaMonitorCrtcMode,
                                    g_list_length (monitor_priv->outputs));
  for (l = monitor_priv->outputs, i = 0; l; l = l->next, i++)
    {
      MetaOutput *output = l->data;
      MetaCrtcMode *tiled_crtc_mode;

      tiled_crtc_mode = find_tiled_crtc_mode (output, reference_crtc_mode);
      if (!tiled_crtc_mode)
        {
          g_warning ("No tiled mode found on %s", output->name);
          meta_monitor_mode_free ((MetaMonitorMode *) mode);
          return NULL;
        }

      mode->parent.crtc_modes[i] = (MetaMonitorCrtcMode) {
        .output = output,
        .crtc_mode = tiled_crtc_mode
      };

      is_preferred = is_preferred && tiled_crtc_mode == output->preferred_mode;
    }

  *out_is_preferred = is_preferred;

  return (MetaMonitorMode *) mode;
}

static void
generate_tiled_monitor_modes (MetaMonitorTiled *monitor_tiled)
{
  MetaMonitor *monitor = META_MONITOR (monitor_tiled);
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  MetaOutput *main_output;
  GList *tiled_modes = NULL;
  unsigned int i;
  MetaMonitorMode *best_mode = NULL;
  GList *l;

  main_output = meta_monitor_get_main_output (META_MONITOR (monitor_tiled));

  for (i = 0; i < main_output->n_modes; i++)
    {
      MetaCrtcMode *reference_crtc_mode = main_output->modes[i];
      MetaMonitorMode *mode;
      gboolean is_preferred;

      if (!is_crtc_mode_tiled (main_output, reference_crtc_mode))
        continue;

      mode = create_tiled_monitor_mode (monitor_tiled, reference_crtc_mode,
                                        &is_preferred);
      if (!mode)
        continue;

      tiled_modes = g_list_append (tiled_modes, mode);

      if (is_monitor_mode_assigned (monitor, mode))
        monitor_priv->current_mode = mode;

      if (is_preferred)
        monitor_priv->preferred_mode = mode;
    }

  while ((l = tiled_modes))
    {
      MetaMonitorMode *mode = l->data;

      tiled_modes = g_list_remove_link (tiled_modes, l);

      if (!meta_monitor_add_mode (monitor, mode, FALSE))
        {
          meta_monitor_mode_free (mode);
          continue;
        }

      if (!monitor_priv->preferred_mode)
        {
          if (!best_mode ||
              mode->spec.refresh_rate > best_mode->spec.refresh_rate)
            best_mode = mode;
        }
    }

  if (best_mode)
    monitor_priv->preferred_mode = best_mode;
}

static MetaMonitorMode *
create_untiled_monitor_mode (MetaMonitorTiled *monitor_tiled,
                             MetaOutput       *main_output,
                             MetaCrtcMode     *crtc_mode)
{
  MetaMonitor *monitor = META_MONITOR (monitor_tiled);
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  MetaMonitorModeTiled *mode;
  GList *l;
  int i;

  if (is_crtc_mode_tiled (main_output, crtc_mode))
    return NULL;

  mode = g_new0 (MetaMonitorModeTiled, 1);
  mode->is_tiled = FALSE;
  mode->parent.monitor = monitor;
  mode->parent.spec = meta_monitor_create_spec (monitor,
                                                crtc_mode->width,
                                                crtc_mode->height,
                                                crtc_mode);
  mode->parent.id = generate_mode_id (&mode->parent.spec);
  mode->parent.crtc_modes = g_new0 (MetaMonitorCrtcMode,
                                    g_list_length (monitor_priv->outputs));

  for (l = monitor_priv->outputs, i = 0; l; l = l->next, i++)
    {
      MetaOutput *output = l->data;

      if (output == main_output)
        {
          mode->parent.crtc_modes[i] = (MetaMonitorCrtcMode) {
            .output = output,
            .crtc_mode = crtc_mode
          };
        }
      else
        {
          mode->parent.crtc_modes[i] = (MetaMonitorCrtcMode) {
            .output = output,
            .crtc_mode = NULL
          };
        }
    }

  return &mode->parent;
}

static int
count_untiled_crtc_modes (MetaOutput *output)
{
  int count;
  unsigned int i;

  count = 0;
  for (i = 0; i < output->n_modes; i++)
    {
      MetaCrtcMode *crtc_mode = output->modes[i];

      if (!is_crtc_mode_tiled (output, crtc_mode))
        count++;
    }

  return count;
}

static MetaOutput *
find_untiled_output (MetaMonitorTiled *monitor_tiled)
{
  MetaMonitor *monitor = META_MONITOR (monitor_tiled);
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  MetaOutput *best_output;
  int best_untiled_crtc_mode_count;
  GList *l;

  best_output = monitor_tiled->origin_output;
  best_untiled_crtc_mode_count =
    count_untiled_crtc_modes (monitor_tiled->origin_output);

  for (l = monitor_priv->outputs; l; l = l->next)
    {
      MetaOutput *output = l->data;
      int untiled_crtc_mode_count;

      if (output == monitor_tiled->origin_output)
        continue;

      untiled_crtc_mode_count = count_untiled_crtc_modes (output);
      if (untiled_crtc_mode_count > best_untiled_crtc_mode_count)
        {
          best_untiled_crtc_mode_count = untiled_crtc_mode_count;
          best_output = output;
        }
    }

  return best_output;
}

static void
generate_untiled_monitor_modes (MetaMonitorTiled *monitor_tiled)
{
  MetaMonitor *monitor = META_MONITOR (monitor_tiled);
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  MetaOutput *main_output;
  unsigned int i;

  main_output = meta_monitor_get_main_output (monitor);

  for (i = 0; i < main_output->n_modes; i++)
    {
      MetaCrtcMode *crtc_mode = main_output->modes[i];
      MetaMonitorMode *mode;

      mode = create_untiled_monitor_mode (monitor_tiled,
                                          main_output,
                                          crtc_mode);
      if (!mode)
        continue;

      if (!meta_monitor_add_mode (monitor, mode, FALSE))
        {
          meta_monitor_mode_free (mode);
          continue;
        }

      if (is_monitor_mode_assigned (monitor, mode))
        {
          g_assert (!monitor_priv->current_mode);
          monitor_priv->current_mode = mode;
        }

      if (!monitor_priv->preferred_mode &&
          crtc_mode == main_output->preferred_mode)
        monitor_priv->preferred_mode = mode;
    }
}

static MetaMonitorMode *
find_best_mode (MetaMonitor *monitor)
{
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  MetaMonitorMode *best_mode = NULL;
  GList *l;

  for (l = monitor_priv->modes; l; l = l->next)
    {
      MetaMonitorMode *mode = l->data;
      int area, best_area;

      if (!best_mode)
        {
          best_mode = mode;
          continue;
        }

      area = mode->spec.width * mode->spec.height;
      best_area = best_mode->spec.width * best_mode->spec.height;
      if (area > best_area)
        {
          best_mode = mode;
          continue;
        }

      if (mode->spec.refresh_rate > best_mode->spec.refresh_rate)
        {
          best_mode = mode;
          continue;
        }
    }

  return best_mode;
}

static void
meta_monitor_tiled_generate_modes (MetaMonitorTiled *monitor_tiled)
{
  MetaMonitor *monitor = META_MONITOR (monitor_tiled);
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);

  /*
   * Tiled monitors may look a bit different from each other, depending on the
   * monitor itself, the driver, etc.
   *
   * On some, the tiled modes will be the preferred CRTC modes, and running
   * untiled is done by only enabling (0, 0) tile. In this case, things are
   * pretty straight forward.
   *
   * Other times a monitor may have some bogus mode preferred on the main tile,
   * and an untiled mode preferred on the non-main tile, and there seems to be
   * no guarantee that the (0, 0) tile is the one that should drive the
   * non-tiled mode.
   *
   * To handle both these cases, the following hueristics are implemented:
   *
   *  1) Find all the tiled CRTC modes of the (0, 0) tile, and create tiled
   *     monitor modes for all tiles based on these.
   *  2) If there is any tiled monitor mode combination where all CRTC modes
   *     are the preferred ones, that one is marked as preferred.
   *  3) If there is no preferred mode determined so far, assume the tiled
   *     monitor mode with the highest refresh rate is preferred.
   *  4) Find the tile with highest number of untiled CRTC modes available,
   *     assume this is the one driving the monitor in untiled mode, and
   *     create monitor modes for all untiled CRTC modes of that tile. If
   *     there is still no preferred mode, set any untiled mode as preferred
   *     if the CRTC mode is marked as such.
   *  5) If at this point there is still no preferred mode, just pick the one
   *     with the highest number of pixels and highest refresh rate.
   *
   * Note that this ignores the preference if the preference is a non-tiled
   * mode. This seems to be the case on some systems, where the user tends to
   * manually set up the tiled mode anyway.
   */

  generate_tiled_monitor_modes (monitor_tiled);

  if (!monitor_priv->preferred_mode)
    g_warning ("Tiled monitor on %s didn't have any tiled modes",
               monitor_priv->spec->connector);

  generate_untiled_monitor_modes (monitor_tiled);

  if (!monitor_priv->preferred_mode)
    {
      g_warning ("Tiled monitor on %s didn't have a valid preferred mode",
                 monitor_priv->spec->connector);
      monitor_priv->preferred_mode = find_best_mode (monitor);
    }
}

MetaMonitorTiled *
meta_monitor_tiled_new (MetaGpu            *gpu,
                        MetaMonitorManager *monitor_manager,
                        MetaOutput         *output)
{
  MetaMonitorTiled *monitor_tiled;
  MetaMonitor *monitor;
  MetaMonitorPrivate *monitor_priv;

  monitor_tiled = g_object_new (META_TYPE_MONITOR_TILED, NULL);
  monitor = META_MONITOR (monitor_tiled);
  monitor_priv = meta_monitor_get_instance_private (monitor);

  monitor_priv->gpu = gpu;

  monitor_tiled->tile_group_id = output->tile_info.group_id;
  monitor_priv->winsys_id = output->winsys_id;

  monitor_tiled->origin_output = output;
  add_tiled_monitor_outputs (gpu, monitor_tiled);

  monitor_tiled->main_output = find_untiled_output (monitor_tiled);

  meta_monitor_generate_spec (monitor);

  monitor_tiled->monitor_manager = monitor_manager;
  meta_monitor_manager_tiled_monitor_added (monitor_manager,
                                            META_MONITOR (monitor_tiled));

  meta_monitor_tiled_generate_modes (monitor_tiled);

  monitor_priv->display_name = meta_monitor_make_display_name (monitor,
                                                               monitor_manager);

  return monitor_tiled;
}

static MetaOutput *
meta_monitor_tiled_get_main_output (MetaMonitor *monitor)
{
  MetaMonitorTiled *monitor_tiled = META_MONITOR_TILED (monitor);

  return monitor_tiled->main_output;
}

static void
meta_monitor_tiled_derive_layout (MetaMonitor   *monitor,
                                  MetaRectangle *layout)
{
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  GList *l;
  float min_x, min_y, max_x, max_y;

  min_x = FLT_MAX;
  min_y = FLT_MAX;
  max_x = 0.0;
  max_y = 0.0;
  for (l = monitor_priv->outputs; l; l = l->next)
    {
      MetaOutput *output = l->data;
      MetaCrtc *crtc;
      MetaCrtcConfig *crtc_config;
      graphene_rect_t *crtc_layout;

      crtc = meta_output_get_assigned_crtc (output);
      if (!crtc)
        continue;

      crtc_config = crtc->config;
      g_return_if_fail (crtc_config);

      crtc_layout = &crtc_config->layout;

      min_x = MIN (crtc_layout->origin.x, min_x);
      min_y = MIN (crtc_layout->origin.y, min_y);
      max_x = MAX (crtc_layout->origin.x + crtc_layout->size.width, max_x);
      max_y = MAX (crtc_layout->origin.y + crtc_layout->size.height, max_y);
    }

  *layout = (MetaRectangle) {
    .x = roundf (min_x),
    .y = roundf (min_y),
    .width = roundf (max_x - min_x),
    .height = roundf (max_y - min_y)
  };
}

static gboolean
meta_monitor_tiled_get_suggested_position (MetaMonitor *monitor,
                                           int         *x,
                                           int         *y)
{
  return FALSE;
}

static void
meta_monitor_tiled_calculate_crtc_pos (MetaMonitor          *monitor,
                                       MetaMonitorMode      *monitor_mode,
                                       MetaOutput           *output,
                                       MetaMonitorTransform  crtc_transform,
                                       int                  *out_x,
                                       int                  *out_y)
{
  MetaMonitorModeTiled *mode_tiled = (MetaMonitorModeTiled *) monitor_mode;

  if (mode_tiled->is_tiled)
    {
      calculate_tile_coordinate (monitor, output, crtc_transform,
                                 out_x, out_y);
    }
  else
    {
      *out_x = 0;
      *out_y = 0;
    }
}

static void
meta_monitor_tiled_finalize (GObject *object)
{
  MetaMonitorTiled *monitor_tiled = META_MONITOR_TILED (object);

  meta_monitor_manager_tiled_monitor_removed (monitor_tiled->monitor_manager,
                                              META_MONITOR (monitor_tiled));

  G_OBJECT_CLASS (meta_monitor_tiled_parent_class)->finalize (object);
}

static void
meta_monitor_tiled_init (MetaMonitorTiled *monitor)
{
}

static void
meta_monitor_tiled_class_init (MetaMonitorTiledClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaMonitorClass *monitor_class = META_MONITOR_CLASS (klass);

  object_class->finalize = meta_monitor_tiled_finalize;

  monitor_class->get_main_output = meta_monitor_tiled_get_main_output;
  monitor_class->derive_layout = meta_monitor_tiled_derive_layout;
  monitor_class->calculate_crtc_pos = meta_monitor_tiled_calculate_crtc_pos;
  monitor_class->get_suggested_position = meta_monitor_tiled_get_suggested_position;
}

static void
meta_monitor_mode_free (MetaMonitorMode *monitor_mode)
{
  g_free (monitor_mode->id);
  g_free (monitor_mode->crtc_modes);
  g_free (monitor_mode);
}

MetaMonitorSpec *
meta_monitor_get_spec (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  return priv->spec;
}

MetaLogicalMonitor *
meta_monitor_get_logical_monitor (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  return priv->logical_monitor;
}

MetaMonitorMode *
meta_monitor_get_mode_from_id (MetaMonitor *monitor,
                               const char  *monitor_mode_id)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  return g_hash_table_lookup (priv->mode_ids, monitor_mode_id);
}

static gboolean
meta_monitor_mode_spec_equals (MetaMonitorModeSpec *monitor_mode_spec,
                               MetaMonitorModeSpec *other_monitor_mode_spec)
{
  return (monitor_mode_spec->width == other_monitor_mode_spec->width &&
          monitor_mode_spec->height == other_monitor_mode_spec->height &&
          ABS (monitor_mode_spec->refresh_rate -
               other_monitor_mode_spec->refresh_rate) < MAXIMUM_REFRESH_RATE_DIFF &&
          monitor_mode_spec->flags == other_monitor_mode_spec->flags);
}

MetaMonitorMode *
meta_monitor_get_mode_from_spec (MetaMonitor         *monitor,
                                 MetaMonitorModeSpec *monitor_mode_spec)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);
  GList *l;

  for (l = priv->modes; l; l = l->next)
    {
      MetaMonitorMode *monitor_mode = l->data;

      if (meta_monitor_mode_spec_equals (monitor_mode_spec,
                                         &monitor_mode->spec))
        return monitor_mode;
    }

  return NULL;
}

MetaMonitorMode *
meta_monitor_get_preferred_mode (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  return priv->preferred_mode;
}

MetaMonitorMode *
meta_monitor_get_current_mode (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  return priv->current_mode;
}

static gboolean
is_current_mode_known (MetaMonitor *monitor)
{
  MetaOutput *output;
  MetaCrtc *crtc;

  output = meta_monitor_get_main_output (monitor);
  crtc = meta_output_get_assigned_crtc (output);

  return meta_monitor_is_active (monitor) == (crtc && crtc->config);
}

void
meta_monitor_derive_current_mode (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);
  MetaMonitorMode *current_mode = NULL;
  GList *l;

  for (l = priv->modes; l; l = l->next)
    {
      MetaMonitorMode *mode = l->data;

      if (is_monitor_mode_assigned (monitor, mode))
        {
          current_mode = mode;
          break;
        }
    }

  priv->current_mode = current_mode;

  g_warn_if_fail (is_current_mode_known (monitor));
}

void
meta_monitor_set_current_mode (MetaMonitor     *monitor,
                               MetaMonitorMode *mode)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  priv->current_mode = mode;
}

GList *
meta_monitor_get_modes (MetaMonitor *monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  return priv->modes;
}

void
meta_monitor_calculate_crtc_pos (MetaMonitor          *monitor,
                                 MetaMonitorMode      *monitor_mode,
                                 MetaOutput           *output,
                                 MetaMonitorTransform  crtc_transform,
                                 int                  *out_x,
                                 int                  *out_y)
{
  META_MONITOR_GET_CLASS (monitor)->calculate_crtc_pos (monitor,
                                                        monitor_mode,
                                                        output,
                                                        crtc_transform,
                                                        out_x,
                                                        out_y);
}

/* The minimum resolution at which we turn on a window-scale of 2 */
#define HIDPI_LIMIT 192

/*
 * The minimum screen height at which we turn on a window-scale of 2;
 * below this there just isn't enough vertical real estate for GNOME
 * apps to work, and it's better to just be tiny
 */
#define HIDPI_MIN_HEIGHT 1200

/* From http://en.wikipedia.org/wiki/4K_resolution#Resolutions_of_common_formats */
#define SMALLEST_4K_WIDTH 3656

static float
calculate_scale (MetaMonitor                *monitor,
                 MetaMonitorMode            *monitor_mode,
                 MetaMonitorScalesConstraint constraints)
{
  int resolution_width, resolution_height;
  int width_mm, height_mm;
  int scale;

  scale = 1.0;

  meta_monitor_mode_get_resolution (monitor_mode,
                                    &resolution_width,
                                    &resolution_height);

  if (resolution_height < HIDPI_MIN_HEIGHT)
    goto out;

  /* 4K TV */
  switch (meta_monitor_get_connector_type (monitor))
    {
    case META_CONNECTOR_TYPE_HDMIA:
    case META_CONNECTOR_TYPE_HDMIB:
      if (resolution_width < SMALLEST_4K_WIDTH)
        goto out;
      break;
    default:
      break;
    }

  meta_monitor_get_physical_dimensions (monitor, &width_mm, &height_mm);

  /*
   * Somebody encoded the aspect ratio (16/9 or 16/10) instead of the physical
   * size.
   */
  if (meta_monitor_has_aspect_as_size (monitor))
    goto out;

  if (width_mm > 0 && height_mm > 0)
    {
      double dpi_x, dpi_y;

      dpi_x = (double) resolution_width / (width_mm / 25.4);
      dpi_y = (double) resolution_height / (height_mm / 25.4);

      /*
       * We don't completely trust these values so both must be high, and never
       * pick higher ratio than 2 automatically.
       */
      if (dpi_x > HIDPI_LIMIT && dpi_y > HIDPI_LIMIT)
        scale = 2.0;
    }

out:
  return scale;
}

float
meta_monitor_calculate_mode_scale (MetaMonitor                *monitor,
                                   MetaMonitorMode            *monitor_mode,
                                   MetaMonitorScalesConstraint constraints)
{
  MetaBackend *backend = meta_get_backend ();
  MetaSettings *settings = meta_backend_get_settings (backend);
  int global_scaling_factor;

  if (meta_settings_get_global_scaling_factor (settings,
                                               &global_scaling_factor))
    return global_scaling_factor;

  return calculate_scale (monitor, monitor_mode, constraints);
}

static gboolean
is_logical_size_large_enough (int width,
                              int height)
{
  return width * height >= MINIMUM_LOGICAL_AREA;
}

static gboolean
is_scale_valid_for_size (float width,
                         float height,
                         float scale)
{
  return scale >= MINIMUM_SCALE_FACTOR &&
         scale <= MAXIMUM_SCALE_FACTOR &&
         is_logical_size_large_enough (floorf (width/scale), floorf (width/scale));
}

gboolean
meta_monitor_mode_should_be_advertised (MetaMonitorMode *monitor_mode)
{
  MetaMonitorMode *preferred_mode;

  g_return_val_if_fail (monitor_mode != NULL, FALSE);

  preferred_mode = meta_monitor_get_preferred_mode (monitor_mode->monitor);
  if (monitor_mode->spec.width == preferred_mode->spec.width &&
      monitor_mode->spec.height == preferred_mode->spec.height)
    return TRUE;

  return is_logical_size_large_enough (monitor_mode->spec.width,
                                       monitor_mode->spec.height);
}

static float
get_closest_scale_factor_for_resolution (float width,
                                         float height,
                                         float scale)
{
  unsigned int i, j;
  float scaled_h;
  float scaled_w;
  float best_scale;
  int base_scaled_w;
  gboolean found_one;

  best_scale = 0;

  if (!is_scale_valid_for_size (width, height, scale))
    goto out;

  if (fmodf (width, scale) == 0.0 && fmodf (height, scale) == 0.0)
    return scale;

  i = 0;
  found_one = FALSE;
  base_scaled_w = floorf (width / scale);
  do
    {
      for (j = 0; j < 2; j++)
        {
          float current_scale;
          int offset = i * (j ? 1 : -1);

          scaled_w = base_scaled_w + offset;
          current_scale = width / scaled_w;
          scaled_h = height / current_scale;

          if (current_scale >= scale + SCALE_FACTORS_STEPS ||
              current_scale <= scale - SCALE_FACTORS_STEPS ||
              current_scale < MINIMUM_SCALE_FACTOR ||
              current_scale > MAXIMUM_SCALE_FACTOR)
            {
              goto out;
            }

          if (floorf (scaled_h) == scaled_h)
            {
              found_one = TRUE;

              if (fabsf (current_scale - scale) < fabsf (best_scale - scale))
                best_scale = current_scale;
            }
        }

      i++;
    }
  while (!found_one);

out:
  return best_scale;
}

float *
meta_monitor_calculate_supported_scales (MetaMonitor                 *monitor,
                                         MetaMonitorMode             *monitor_mode,
                                         MetaMonitorScalesConstraint  constraints,
                                         int                         *n_supported_scales)
{
  unsigned int i, j;
  int width, height;
  GArray *supported_scales;

  supported_scales = g_array_new (FALSE, FALSE, sizeof (float));

  meta_monitor_mode_get_resolution (monitor_mode, &width, &height);

  for (i = floorf (MINIMUM_SCALE_FACTOR);
       i <= ceilf (MAXIMUM_SCALE_FACTOR);
       i++)
    {
      for (j = 0; j < SCALE_FACTORS_PER_INTEGER; j++)
        {
          float scale;
          float scale_value = i + j * SCALE_FACTORS_STEPS;

          if (constraints & META_MONITOR_SCALES_CONSTRAINT_NO_FRAC)
            {
              if (fmodf (scale_value, 1.0) != 0.0)
                continue;
            }

          if ((constraints & META_MONITOR_SCALES_CONSTRAINT_NO_FRAC) ||
              (constraints & META_MONITOR_SCALES_CONSTRAINT_NO_LOGICAL))
            {
              if (!is_scale_valid_for_size (width, height, scale_value))
                continue;

              scale = scale_value;
            }
          else
            scale = get_closest_scale_factor_for_resolution (width,
                                                             height,
                                                             scale_value);
          if (scale > 0.0f)
            g_array_append_val (supported_scales, scale);
        }
    }

  if (supported_scales->len == 0)
    {
      float fallback_scale;

      fallback_scale = 1.0;
      g_array_append_val (supported_scales, fallback_scale);
    }

  *n_supported_scales = supported_scales->len;
  return (float *) g_array_free (supported_scales, FALSE);
}

MetaMonitorModeSpec *
meta_monitor_mode_get_spec (MetaMonitorMode *monitor_mode)
{
  return &monitor_mode->spec;
}

const char *
meta_monitor_mode_get_id (MetaMonitorMode *monitor_mode)
{
  return monitor_mode->id;
}

void
meta_monitor_mode_get_resolution (MetaMonitorMode *monitor_mode,
                                  int             *width,
                                  int             *height)
{
  *width = monitor_mode->spec.width;
  *height = monitor_mode->spec.height;
}

float
meta_monitor_mode_get_refresh_rate (MetaMonitorMode *monitor_mode)
{
  return monitor_mode->spec.refresh_rate;
}

MetaCrtcModeFlag
meta_monitor_mode_get_flags (MetaMonitorMode *monitor_mode)
{
  return monitor_mode->spec.flags;
}

gboolean
meta_monitor_mode_foreach_crtc (MetaMonitor          *monitor,
                                MetaMonitorMode      *mode,
                                MetaMonitorModeFunc   func,
                                gpointer              user_data,
                                GError              **error)
{
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  GList *l;
  int i;

  for (l = monitor_priv->outputs, i = 0; l; l = l->next, i++)
    {
      MetaMonitorCrtcMode *monitor_crtc_mode = &mode->crtc_modes[i];

      if (!monitor_crtc_mode->crtc_mode)
        continue;

      if (!func (monitor, mode, monitor_crtc_mode, user_data, error))
        return FALSE;
    }

  return TRUE;
}

gboolean
meta_monitor_mode_foreach_output (MetaMonitor          *monitor,
                                  MetaMonitorMode      *mode,
                                  MetaMonitorModeFunc   func,
                                  gpointer              user_data,
                                  GError              **error)
{
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);
  GList *l;
  int i;

  for (l = monitor_priv->outputs, i = 0; l; l = l->next, i++)
    {
      MetaMonitorCrtcMode *monitor_crtc_mode = &mode->crtc_modes[i];

      if (!func (monitor, mode, monitor_crtc_mode, user_data, error))
        return FALSE;
    }

  return TRUE;
}

const char *
meta_monitor_get_display_name (MetaMonitor *monitor)
{
  MetaMonitorPrivate *monitor_priv =
    meta_monitor_get_instance_private (monitor);

  return monitor_priv->display_name;
}

void
meta_monitor_set_logical_monitor (MetaMonitor        *monitor,
                                  MetaLogicalMonitor *logical_monitor)
{
  MetaMonitorPrivate *priv = meta_monitor_get_instance_private (monitor);

  priv->logical_monitor = logical_monitor;
}
