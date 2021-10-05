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

/**
 * SECTION:meta-logical-monitor
 * @title: MetaLogicalMonitor
 * @short_description: An abstraction for a monitor(set) and its configuration.
 *
 * A logical monitor is a group of one or more physical monitors that
 * must behave and be treated as single one. This happens, for example,
 * when 2 monitors are mirrored. Each physical monitor is represented
 * by a #MetaMonitor.
 *
 * #MetaLogicalMonitor has a single viewport, with its owns transformations
 * (such as scaling), that are applied to all the #MetaMonitor<!-- -->s that
 * are grouped by it.
 *
 * #MetaLogicalMonitor provides an abstraction that makes it easy to handle
 * the specifics of setting up different #MetaMonitor<!-- -->s. It then can
 * be used more easily by #MetaRendererView.
 */

#include "config.h"

#include "backends/meta-logical-monitor.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-crtc.h"
#include "backends/meta-output.h"

G_DEFINE_TYPE (MetaLogicalMonitor, meta_logical_monitor, G_TYPE_OBJECT)

static MetaMonitor *
get_first_monitor (MetaMonitorManager *monitor_manager,
                   GList              *monitor_configs)
{
  MetaMonitorConfig *first_monitor_config;
  MetaMonitorSpec *first_monitor_spec;

  first_monitor_config = g_list_first (monitor_configs)->data;
  first_monitor_spec = first_monitor_config->monitor_spec;

  return meta_monitor_manager_get_monitor_from_spec (monitor_manager,
                                                     first_monitor_spec);
}

typedef struct
{
  MetaMonitorManager *monitor_manager;
  MetaLogicalMonitor *logical_monitor;
} AddMonitorFromConfigData;

static void
add_monitor_from_config (MetaMonitorConfig        *monitor_config,
                         AddMonitorFromConfigData *data)
{
  MetaMonitorSpec *monitor_spec;
  MetaMonitor *monitor;

  monitor_spec = monitor_config->monitor_spec;
  monitor = meta_monitor_manager_get_monitor_from_spec (data->monitor_manager,
                                                        monitor_spec);

  meta_logical_monitor_add_monitor (data->logical_monitor, monitor);
}

MetaLogicalMonitor *
meta_logical_monitor_new (MetaMonitorManager       *monitor_manager,
                          MetaLogicalMonitorConfig *logical_monitor_config,
                          int                       monitor_number)
{
  MetaLogicalMonitor *logical_monitor;
  GList *monitor_configs;
  MetaMonitor *first_monitor;
  MetaOutput *main_output;

  logical_monitor = g_object_new (META_TYPE_LOGICAL_MONITOR, NULL);

  monitor_configs = logical_monitor_config->monitor_configs;
  first_monitor = get_first_monitor (monitor_manager, monitor_configs);
  main_output = meta_monitor_get_main_output (first_monitor);

  logical_monitor->number = monitor_number;
  logical_monitor->winsys_id = main_output->winsys_id;
  logical_monitor->scale = logical_monitor_config->scale;
  logical_monitor->transform = logical_monitor_config->transform;
  logical_monitor->in_fullscreen = -1;
  logical_monitor->rect = logical_monitor_config->layout;

  logical_monitor->is_presentation = TRUE;
  g_list_foreach (monitor_configs, (GFunc) add_monitor_from_config,
                  &(AddMonitorFromConfigData) {
                    .monitor_manager = monitor_manager,
                    .logical_monitor = logical_monitor
                  });

  return logical_monitor;
}

static MetaMonitorTransform
derive_monitor_transform (MetaMonitor *monitor)
{
  MetaOutput *main_output;
  MetaMonitorTransform transform;

  main_output = meta_monitor_get_main_output (monitor);
  transform = meta_output_get_assigned_crtc (main_output)->config->transform;

  return meta_monitor_crtc_to_logical_transform (monitor, transform);
}

MetaLogicalMonitor *
meta_logical_monitor_new_derived (MetaMonitorManager *monitor_manager,
                                  MetaMonitor        *monitor,
                                  MetaRectangle      *layout,
                                  float               scale,
                                  int                 monitor_number)
{
  MetaLogicalMonitor *logical_monitor;
  MetaOutput *main_output;
  MetaMonitorTransform transform;

  logical_monitor = g_object_new (META_TYPE_LOGICAL_MONITOR, NULL);

  transform = derive_monitor_transform (monitor);

  main_output = meta_monitor_get_main_output (monitor);
  logical_monitor->number = monitor_number;
  logical_monitor->winsys_id = main_output->winsys_id;
  logical_monitor->scale = scale;
  logical_monitor->transform = transform;
  logical_monitor->in_fullscreen = -1;
  logical_monitor->rect = *layout;

  logical_monitor->is_presentation = TRUE;
  meta_logical_monitor_add_monitor (logical_monitor, monitor);

  return logical_monitor;
}

void
meta_logical_monitor_add_monitor (MetaLogicalMonitor *logical_monitor,
                                  MetaMonitor        *monitor)
{
  GList *l;
  gboolean is_presentation;

  is_presentation = logical_monitor->is_presentation;
  logical_monitor->monitors = g_list_append (logical_monitor->monitors,
                                             g_object_ref (monitor));

  for (l = logical_monitor->monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;
      GList *outputs;
      GList *l_output;

      outputs = meta_monitor_get_outputs (monitor);
      for (l_output = outputs; l_output; l_output = l_output->next)
        {
          MetaOutput *output = l_output->data;

          is_presentation = is_presentation && output->is_presentation;
        }
    }

  logical_monitor->is_presentation = is_presentation;

  meta_monitor_set_logical_monitor (monitor, logical_monitor);
}

gboolean
meta_logical_monitor_is_primary (MetaLogicalMonitor *logical_monitor)
{
  return logical_monitor->is_primary;
}

void
meta_logical_monitor_make_primary (MetaLogicalMonitor *logical_monitor)
{
  logical_monitor->is_primary = TRUE;
}

float
meta_logical_monitor_get_scale (MetaLogicalMonitor *logical_monitor)
{
  return logical_monitor->scale;
}

MetaMonitorTransform
meta_logical_monitor_get_transform (MetaLogicalMonitor *logical_monitor)
{
  return logical_monitor->transform;
}

MetaRectangle
meta_logical_monitor_get_layout (MetaLogicalMonitor *logical_monitor)
{
  return logical_monitor->rect;
}

GList *
meta_logical_monitor_get_monitors (MetaLogicalMonitor *logical_monitor)
{
  return logical_monitor->monitors;
}

typedef struct _ForeachCrtcData
{
  MetaLogicalMonitor *logical_monitor;
  MetaLogicalMonitorCrtcFunc func;
  gpointer user_data;
} ForeachCrtcData;

static gboolean
foreach_crtc (MetaMonitor         *monitor,
              MetaMonitorMode     *mode,
              MetaMonitorCrtcMode *monitor_crtc_mode,
              gpointer             user_data,
              GError             **error)
{
  ForeachCrtcData *data = user_data;

  data->func (data->logical_monitor,
              monitor,
              monitor_crtc_mode->output,
              meta_output_get_assigned_crtc (monitor_crtc_mode->output),
              data->user_data);

  return TRUE;
}

void
meta_logical_monitor_foreach_crtc (MetaLogicalMonitor        *logical_monitor,
                                   MetaLogicalMonitorCrtcFunc func,
                                   gpointer                   user_data)
{
  GList *l;

  for (l = logical_monitor->monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;
      MetaMonitorMode *mode;
      ForeachCrtcData data = {
        .logical_monitor = logical_monitor,
        .func = func,
        .user_data = user_data
      };

      mode = meta_monitor_get_current_mode (monitor);
      meta_monitor_mode_foreach_crtc (monitor, mode, foreach_crtc, &data, NULL);
    }
}

static void
meta_logical_monitor_init (MetaLogicalMonitor *logical_monitor)
{
}

static void
meta_logical_monitor_dispose (GObject *object)
{
  MetaLogicalMonitor *logical_monitor = META_LOGICAL_MONITOR (object);

  if (logical_monitor->monitors)
    {
      g_list_free_full (logical_monitor->monitors, g_object_unref);
      logical_monitor->monitors = NULL;
    }

  G_OBJECT_CLASS (meta_logical_monitor_parent_class)->dispose (object);
}

static void
meta_logical_monitor_class_init (MetaLogicalMonitorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_logical_monitor_dispose;
}

gboolean
meta_logical_monitor_has_neighbor (MetaLogicalMonitor   *logical_monitor,
                                   MetaLogicalMonitor   *neighbor,
                                   MetaDisplayDirection  neighbor_direction)
{
  switch (neighbor_direction)
    {
    case META_DISPLAY_RIGHT:
      if (neighbor->rect.x == (logical_monitor->rect.x +
                               logical_monitor->rect.width) &&
          meta_rectangle_vert_overlap (&neighbor->rect,
                                       &logical_monitor->rect))
        return TRUE;
      break;
    case META_DISPLAY_LEFT:
      if (logical_monitor->rect.x == (neighbor->rect.x +
                                      neighbor->rect.width) &&
          meta_rectangle_vert_overlap (&neighbor->rect,
                                       &logical_monitor->rect))
        return TRUE;
      break;
    case META_DISPLAY_UP:
      if (logical_monitor->rect.y == (neighbor->rect.y +
                                      neighbor->rect.height) &&
          meta_rectangle_horiz_overlap (&neighbor->rect,
                                        &logical_monitor->rect))
        return TRUE;
      break;
    case META_DISPLAY_DOWN:
      if (neighbor->rect.y == (logical_monitor->rect.y +
                               logical_monitor->rect.height) &&
          meta_rectangle_horiz_overlap (&neighbor->rect,
                                        &logical_monitor->rect))
        return TRUE;
      break;
    }

  return FALSE;
}
