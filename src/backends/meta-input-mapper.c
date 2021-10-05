/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright 2018 Red Hat, Inc.
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
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#ifdef HAVE_LIBGUDEV
#include <gudev/gudev.h>
#endif

#include "meta-input-mapper-private.h"
#include "meta-monitor-manager-private.h"
#include "meta-logical-monitor.h"
#include "meta-backend-private.h"

#define MAX_SIZE_MATCH_DIFF 0.05

typedef struct _MetaMapperInputInfo MetaMapperInputInfo;
typedef struct _MetaMapperOutputInfo MetaMapperOutputInfo;
typedef struct _MappingHelper MappingHelper;
typedef struct _DeviceCandidates DeviceCandidates;

struct _MetaInputMapper
{
  GObject parent_instance;
  MetaMonitorManager *monitor_manager;
  ClutterSeat *seat;
  GHashTable *input_devices; /* ClutterInputDevice -> MetaMapperInputInfo */
  GHashTable *output_devices; /* MetaLogicalMonitor -> MetaMapperOutputInfo */
#ifdef HAVE_LIBGUDEV
  GUdevClient *udev_client;
#endif
};

typedef enum
{
  META_INPUT_CAP_TOUCH  = 1 << 0, /* touch device, either touchscreen or tablet */
  META_INPUT_CAP_STYLUS = 1 << 1, /* tablet pen */
  META_INPUT_CAP_ERASER = 1 << 2, /* tablet eraser */
  META_INPUT_CAP_PAD    = 1 << 3, /* pad device, most usually in tablets */
  META_INPUT_CAP_CURSOR = 1 << 4  /* pointer-like device in tablets */
} MetaInputCapabilityFlags;

typedef enum
{
  META_MATCH_IS_BUILTIN,   /* Output is builtin, applies mainly to system-integrated devices */
  META_MATCH_SIZE,         /* Size from input device and output match */
  META_MATCH_EDID_FULL,    /* Full EDID model match, eg. "Cintiq 12WX" */
  META_MATCH_EDID_PARTIAL, /* Partial EDID model match, eg. "Cintiq" */
  META_MATCH_EDID_VENDOR,  /* EDID vendor match, eg. "WAC" for Wacom */
  N_OUTPUT_MATCHES
} MetaOutputMatchType;

struct _MetaMapperInputInfo
{
  ClutterInputDevice *device;
  MetaInputMapper *mapper;
  MetaMapperOutputInfo *output;
  guint builtin : 1;
};

struct _MetaMapperOutputInfo
{
  MetaLogicalMonitor *logical_monitor;
  GList *input_devices;
  MetaInputCapabilityFlags attached_caps;
};

struct _MappingHelper
{
  GArray *device_maps;
};

struct _DeviceCandidates
{
  MetaMapperInputInfo *input;

  MetaMonitor *candidates[N_OUTPUT_MATCHES];

  MetaOutputMatchType best;
};

enum
{
  DEVICE_MAPPED,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0, };

G_DEFINE_TYPE (MetaInputMapper, meta_input_mapper, G_TYPE_OBJECT)

static MetaMapperInputInfo *
mapper_input_info_new (ClutterInputDevice *device,
                       MetaInputMapper    *mapper,
                       gboolean            builtin)
{
  MetaMapperInputInfo *info;

  info = g_new0 (MetaMapperInputInfo, 1);
  info->mapper = mapper;
  info->device = device;
  info->builtin = builtin;

  return info;
}

static void
mapper_input_info_free (MetaMapperInputInfo *info)
{
  g_free (info);
}

static MetaMapperOutputInfo *
mapper_output_info_new (MetaLogicalMonitor *logical_monitor)
{
  MetaMapperOutputInfo *info;

  info = g_new0 (MetaMapperOutputInfo, 1);
  info->logical_monitor = logical_monitor;

  return info;
}

static void
mapper_output_info_free (MetaMapperOutputInfo *info)
{
  g_free (info);
}

static MetaInputCapabilityFlags
mapper_input_info_get_caps (MetaMapperInputInfo *info)
{
  ClutterInputDeviceType type;

  type = clutter_input_device_get_device_type (info->device);

  switch (type)
    {
    case CLUTTER_TOUCHSCREEN_DEVICE:
      return META_INPUT_CAP_TOUCH;
    case CLUTTER_TABLET_DEVICE:
    case CLUTTER_PEN_DEVICE:
      return META_INPUT_CAP_STYLUS;
    case CLUTTER_ERASER_DEVICE:
      return META_INPUT_CAP_ERASER;
    case CLUTTER_CURSOR_DEVICE:
      return META_INPUT_CAP_CURSOR;
    case CLUTTER_PAD_DEVICE:
      return META_INPUT_CAP_PAD;
    default:
      return 0;
    }
}

static void
mapper_input_info_set_output (MetaMapperInputInfo  *input,
                              MetaMapperOutputInfo *output,
                              MetaMonitor          *monitor)
{
  if (input->output == output)
    return;

  input->output = output;
  g_signal_emit (input->mapper, signals[DEVICE_MAPPED], 0,
                 input->device,
                 output ? output->logical_monitor : NULL, monitor);
}

static void
mapper_output_info_add_input (MetaMapperOutputInfo *output,
                              MetaMapperInputInfo  *input,
                              MetaMonitor          *monitor)
{
  g_assert (input->output == NULL);

  output->input_devices = g_list_prepend (output->input_devices, input);
  output->attached_caps |= mapper_input_info_get_caps (input);

  mapper_input_info_set_output (input, output, monitor);
}

static void
mapper_output_info_remove_input (MetaMapperOutputInfo *output,
                                 MetaMapperInputInfo  *input)
{
  GList *l;

  g_assert (input->output == output);

  output->input_devices = g_list_remove (output->input_devices, input);
  output->attached_caps = 0;

  for (l = output->input_devices; l; l = l->next)
    output->attached_caps |= mapper_input_info_get_caps (l->data);

  mapper_input_info_set_output (input, NULL, NULL);
}

static void
mapper_output_info_clear_inputs (MetaMapperOutputInfo *output)
{
  while (output->input_devices)
    {
      MetaMapperInputInfo *input = output->input_devices->data;

      mapper_input_info_set_output (input, NULL, NULL);
      output->input_devices = g_list_remove (output->input_devices, input);
    }

  output->attached_caps = 0;
}

static void
mapping_helper_init (MappingHelper *helper)
{
  helper->device_maps = g_array_new (FALSE, FALSE, sizeof (DeviceCandidates));
}

static void
mapping_helper_release (MappingHelper *helper)
{
  g_array_unref (helper->device_maps);
}

static gboolean
match_edid (MetaMapperInputInfo  *input,
            MetaMonitor          *monitor,
            MetaOutputMatchType  *match_type)
{
  const gchar *dev_name;

  dev_name = clutter_input_device_get_device_name (input->device);

  if (strcasestr (dev_name, meta_monitor_get_vendor (monitor)) == NULL)
    return FALSE;

  *match_type = META_MATCH_EDID_VENDOR;

  if (strcasestr (dev_name, meta_monitor_get_product (monitor)) != NULL)
    {
      *match_type = META_MATCH_EDID_FULL;
    }
  else
    {
      int i;
      g_auto (GStrv) split = NULL;

      split = g_strsplit (meta_monitor_get_product (monitor), " ", -1);

      for (i = 0; split[i]; i++)
        {
          if (strcasestr (dev_name, split[i]) != NULL)
            {
              *match_type = META_MATCH_EDID_PARTIAL;
              break;
            }
        }
    }

  return TRUE;
}

static gboolean
input_device_get_physical_size (MetaInputMapper    *mapper,
                                ClutterInputDevice *device,
                                double             *width,
                                double             *height)
{
#ifdef HAVE_LIBGUDEV
  g_autoptr (GUdevDevice) udev_device = NULL;
  const char *node;

  node = clutter_input_device_get_device_node (device);
  udev_device = g_udev_client_query_by_device_file (mapper->udev_client, node);

  if (udev_device &&
      g_udev_device_has_property (udev_device, "ID_INPUT_WIDTH_MM"))
    {
      *width = g_udev_device_get_property_as_double (udev_device,
                                                     "ID_INPUT_WIDTH_MM");
      *height = g_udev_device_get_property_as_double (udev_device,
                                                      "ID_INPUT_HEIGHT_MM");
      return TRUE;
    }
#endif

  return FALSE;
}

static gboolean
find_size_match (MetaMapperInputInfo  *input,
                 GList                *monitors,
                 MetaMonitor         **matched_monitor)
{
  double min_w_diff, min_h_diff;
  double i_width, i_height;
  gboolean found = FALSE;
  GList *l;

  min_w_diff = min_h_diff = MAX_SIZE_MATCH_DIFF;

  if (!input_device_get_physical_size (input->mapper, input->device,
                                       &i_width, &i_height))
    return FALSE;

  for (l = monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;
      double w_diff, h_diff;
      int o_width, o_height;

      meta_monitor_get_physical_dimensions (monitor, &o_width, &o_height);
      w_diff = ABS (1 - ((double) o_width / i_width));
      h_diff = ABS (1 - ((double) o_height / i_height));

      if (w_diff >= min_w_diff || h_diff >= min_h_diff)
        continue;

      *matched_monitor = monitor;
      min_w_diff = w_diff;
      min_h_diff = h_diff;
      found = TRUE;
    }

  return found;
}

static gboolean
find_builtin_output (MetaInputMapper  *mapper,
                     MetaMonitor     **matched_monitor)
{
  MetaMonitor *panel;

  panel = meta_monitor_manager_get_laptop_panel (mapper->monitor_manager);
  *matched_monitor = panel;
  return panel != NULL;
}

static void
guess_candidates (MetaInputMapper     *mapper,
                  MetaMapperInputInfo *input,
                  DeviceCandidates    *info)
{
  MetaOutputMatchType best = N_OUTPUT_MATCHES;
  GList *monitors, *l;
  MetaMonitor *matched_monitor = NULL;

  monitors = meta_monitor_manager_get_monitors (mapper->monitor_manager);

  for (l = monitors; l; l = l->next)
    {
      MetaOutputMatchType edid_match;

      if (match_edid (input, l->data, &edid_match))
        {
          best = MIN (best, edid_match);
          info->candidates[edid_match] = l->data;
        }
    }

  if (find_size_match (input, monitors, &matched_monitor))
    {
      best = MIN (best, META_MATCH_SIZE);
      info->candidates[META_MATCH_SIZE] = matched_monitor;
    }

  if (input->builtin || best == N_OUTPUT_MATCHES)
    {
      best = MIN (best, META_MATCH_IS_BUILTIN);
      find_builtin_output (mapper, &info->candidates[META_MATCH_IS_BUILTIN]);
    }

  info->best = best;
}

static void
mapping_helper_add (MappingHelper       *helper,
		    MetaMapperInputInfo *input,
                    MetaInputMapper     *mapper)
{
  DeviceCandidates info = { 0, };
  guint i, pos = 0;

  info.input = input;

  guess_candidates (mapper, input, &info);

  for (i = 0; i < helper->device_maps->len; i++)
    {
      DeviceCandidates *elem;

      elem = &g_array_index (helper->device_maps, DeviceCandidates, i);

      if (elem->best < info.best)
        pos = i;
    }

  if (pos >= helper->device_maps->len)
    g_array_append_val (helper->device_maps, info);
  else
    g_array_insert_val (helper->device_maps, pos, info);
}

static void
mapping_helper_apply (MappingHelper   *helper,
                      MetaInputMapper *mapper)
{
  guint i;

  /* Now, decide which input claims which output */
  for (i = 0; i < helper->device_maps->len; i++)
    {
      MetaMapperOutputInfo *output;
      DeviceCandidates *info;
      MetaOutputMatchType j;

      info = &g_array_index (helper->device_maps, DeviceCandidates, i);

      for (j = 0; j < N_OUTPUT_MATCHES; j++)
        {
          MetaLogicalMonitor *logical_monitor;

          if (!info->candidates[j])
            continue;

          logical_monitor =
            meta_monitor_get_logical_monitor (info->candidates[j]);
          output = g_hash_table_lookup (mapper->output_devices,
                                        logical_monitor);

          if (!output)
            continue;

          if (output->attached_caps & mapper_input_info_get_caps (info->input))
            continue;

          mapper_output_info_add_input (output, info->input,
                                        info->candidates[j]);
          break;
        }
    }
}

static void
mapper_recalculate_candidates (MetaInputMapper *mapper)
{
  MetaMapperInputInfo *input;
  MappingHelper helper;
  GHashTableIter iter;

  mapping_helper_init (&helper);
  g_hash_table_iter_init (&iter, mapper->input_devices);

  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &input))
    mapping_helper_add (&helper, input, mapper);

  mapping_helper_apply (&helper, mapper);
  mapping_helper_release (&helper);
}

static void
mapper_recalculate_input (MetaInputMapper     *mapper,
                          MetaMapperInputInfo *input)
{
  MappingHelper helper;

  mapping_helper_init (&helper);
  mapping_helper_add (&helper, input, mapper);
  mapping_helper_apply (&helper, mapper);
  mapping_helper_release (&helper);
}

static void
mapper_update_outputs (MetaInputMapper *mapper)
{
  MetaMapperOutputInfo *output;
  GList *logical_monitors, *l;
  GHashTableIter iter;

  g_hash_table_iter_init (&iter, mapper->output_devices);

  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &output))
    {
      mapper_output_info_clear_inputs (output);
      g_hash_table_iter_remove (&iter);
    }

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (mapper->monitor_manager);

  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      MetaMapperOutputInfo *info;

      info = mapper_output_info_new (logical_monitor);
      g_hash_table_insert (mapper->output_devices, logical_monitor, info);
    }

  mapper_recalculate_candidates (mapper);
}

static void
input_mapper_monitors_changed_cb (MetaMonitorManager *monitor_manager,
                                  MetaInputMapper    *mapper)
{
  mapper_update_outputs (mapper);
}

static void
input_mapper_device_removed_cb (ClutterSeat        *seat,
                                ClutterInputDevice *device,
                                MetaInputMapper    *mapper)
{
  meta_input_mapper_remove_device (mapper, device);
}

static void
meta_input_mapper_finalize (GObject *object)
{
  MetaInputMapper *mapper = META_INPUT_MAPPER (object);

  g_signal_handlers_disconnect_by_func (mapper->monitor_manager,
                                        input_mapper_monitors_changed_cb,
                                        mapper);
  g_signal_handlers_disconnect_by_func (mapper->seat,
                                        input_mapper_device_removed_cb,
                                        mapper);

  g_hash_table_unref (mapper->input_devices);
  g_hash_table_unref (mapper->output_devices);
#ifdef HAVE_LIBGUDEV
  g_clear_object (&mapper->udev_client);
#endif

  G_OBJECT_CLASS (meta_input_mapper_parent_class)->finalize (object);
}

static void
meta_input_mapper_constructed (GObject *object)
{
#ifdef HAVE_LIBGUDEV
  const char *udev_subsystems[] = { "input", NULL };
#endif
  MetaInputMapper *mapper = META_INPUT_MAPPER (object);
  MetaBackend *backend;

  G_OBJECT_CLASS (meta_input_mapper_parent_class)->constructed (object);

#ifdef HAVE_LIBGUDEV
  mapper->udev_client = g_udev_client_new (udev_subsystems);
#endif

  mapper->seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  g_signal_connect (mapper->seat, "device-removed",
                    G_CALLBACK (input_mapper_device_removed_cb), mapper);

  backend = meta_get_backend ();
  mapper->monitor_manager = meta_backend_get_monitor_manager (backend);
  g_signal_connect (mapper->monitor_manager, "monitors-changed-internal",
                    G_CALLBACK (input_mapper_monitors_changed_cb), mapper);

  mapper_update_outputs (mapper);
}

static void
meta_input_mapper_class_init (MetaInputMapperClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_input_mapper_constructed;
  object_class->finalize = meta_input_mapper_finalize;

  signals[DEVICE_MAPPED] =
    g_signal_new ("device-mapped",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 3,
                  CLUTTER_TYPE_INPUT_DEVICE,
                  G_TYPE_POINTER, G_TYPE_POINTER);
}

static void
meta_input_mapper_init (MetaInputMapper *mapper)
{
  mapper->input_devices =
    g_hash_table_new_full (NULL, NULL, NULL,
                           (GDestroyNotify) mapper_input_info_free);
  mapper->output_devices =
    g_hash_table_new_full (NULL, NULL, NULL,
                           (GDestroyNotify) mapper_output_info_free);
}

MetaInputMapper *
meta_input_mapper_new (void)
{
  return g_object_new (META_TYPE_INPUT_MAPPER, NULL);
}

void
meta_input_mapper_add_device (MetaInputMapper    *mapper,
                              ClutterInputDevice *device,
                              gboolean            builtin)
{
  MetaMapperInputInfo *info;

  g_return_if_fail (mapper != NULL);
  g_return_if_fail (device != NULL);

  if (g_hash_table_contains (mapper->input_devices, device))
    return;

  info = mapper_input_info_new (device, mapper, builtin);
  g_hash_table_insert (mapper->input_devices, device, info);
  mapper_recalculate_input (mapper, info);
}

void
meta_input_mapper_remove_device (MetaInputMapper    *mapper,
                                 ClutterInputDevice *device)
{
  MetaMapperInputInfo *input;

  g_return_if_fail (mapper != NULL);
  g_return_if_fail (device != NULL);

  input = g_hash_table_lookup (mapper->input_devices, device);

  if (input)
    {
      if (input->output)
        mapper_output_info_remove_input (input->output, input);
      g_hash_table_remove (mapper->input_devices, device);
    }
}

ClutterInputDevice *
meta_input_mapper_get_logical_monitor_device (MetaInputMapper        *mapper,
                                              MetaLogicalMonitor     *logical_monitor,
                                              ClutterInputDeviceType  device_type)
{
  MetaMapperOutputInfo *output;
  GList *l;

  output = g_hash_table_lookup (mapper->output_devices, logical_monitor);
  if (!output)
    return NULL;

  for (l = output->input_devices; l; l = l->next)
    {
      MetaMapperInputInfo *input = l->data;

      if (clutter_input_device_get_device_type (input->device) == device_type)
        return input->device;
    }

  return NULL;
}

MetaLogicalMonitor *
meta_input_mapper_get_device_logical_monitor (MetaInputMapper    *mapper,
                                              ClutterInputDevice *device)
{
  MetaMapperOutputInfo *output;
  MetaLogicalMonitor *logical_monitor;
  GHashTableIter iter;
  GList *l;

  g_hash_table_iter_init (&iter, mapper->output_devices);

  while (g_hash_table_iter_next (&iter, (gpointer *) &logical_monitor,
                                 (gpointer *) &output))
    {
      for (l = output->input_devices; l; l = l->next)
        {
          MetaMapperInputInfo *input = l->data;

          if (input->device == device)
            return logical_monitor;
        }
    }

  return NULL;
}
