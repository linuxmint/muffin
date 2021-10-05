/*
 * Copyright (C) 2017 Red Hat
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

#include "backends/meta-output.h"

typedef struct _MetaOutputPrivate
{
  /* The CRTC driving this output, NULL if the output is not enabled */
  MetaCrtc *crtc;
} MetaOutputPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaOutput, meta_output, G_TYPE_OBJECT)

MetaGpu *
meta_output_get_gpu (MetaOutput *output)
{
  return output->gpu;
}

void
meta_output_assign_crtc (MetaOutput *output,
                         MetaCrtc   *crtc)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  g_assert (crtc);

  g_set_object (&priv->crtc, crtc);
}

void
meta_output_unassign_crtc (MetaOutput *output)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  g_clear_object (&priv->crtc);
}

MetaCrtc *
meta_output_get_assigned_crtc (MetaOutput *output)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  return priv->crtc;
}

MetaMonitorTransform
meta_output_logical_to_crtc_transform (MetaOutput           *output,
                                       MetaMonitorTransform  transform)
{
  MetaMonitorTransform panel_orientation_transform;

  panel_orientation_transform = output->panel_orientation_transform;
  return meta_monitor_transform_transform (transform,
                                           panel_orientation_transform);
}

MetaMonitorTransform
meta_output_crtc_to_logical_transform (MetaOutput           *output,
                                       MetaMonitorTransform  transform)
{
  MetaMonitorTransform inverted_panel_orientation_transform;

  inverted_panel_orientation_transform =
    meta_monitor_transform_invert (output->panel_orientation_transform);
  return meta_monitor_transform_transform (transform,
                                           inverted_panel_orientation_transform);
}

static void
meta_output_dispose (GObject *object)
{
  MetaOutput *output = META_OUTPUT (object);
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  g_clear_object (&priv->crtc);

  G_OBJECT_CLASS (meta_output_parent_class)->dispose (object);
}

static void
meta_output_finalize (GObject *object)
{
  MetaOutput *output = META_OUTPUT (object);

  g_free (output->name);
  g_free (output->vendor);
  g_free (output->product);
  g_free (output->serial);
  g_free (output->modes);
  g_free (output->possible_crtcs);
  g_free (output->possible_clones);

  if (output->driver_notify)
    output->driver_notify (output);

  G_OBJECT_CLASS (meta_output_parent_class)->finalize (object);
}

static void
meta_output_init (MetaOutput *output)
{
}

static void
meta_output_class_init (MetaOutputClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_output_dispose;
  object_class->finalize = meta_output_finalize;
}
