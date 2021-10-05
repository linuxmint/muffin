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
 *
 * Written by:
 *     Jonas Ådahl <jadahl@gmail.com>
 */

/**
 * SECTION:meta-renderer
 * @title: MetaRenderer
 * @short_description: Keeps track of the different renderer views.
 *
 * A MetaRenderer object has 2 functions:
 *
 * 1) Keeping a list of #MetaRendererView<!-- -->s, each responsible for
 * rendering a part of the stage, corresponding to each #MetaLogicalMonitor. It
 * keeps track of this list by querying the list of logical monitors in the
 * #MetaBackend's #MetaMonitorManager, and creating a renderer view for each
 * logical monitor it encounters.
 *
 * 2) Creating and setting up an appropriate #CoglRenderer. For example, a
 * #MetaRenderer might call cogl_renderer_set_custom_winsys() to tie the
 * backend-specific mechanisms into Cogl.
 */

#include "config.h"

#include "backends/meta-renderer.h"

#include <glib-object.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-logical-monitor.h"

enum
{
  PROP_0,

  PROP_BACKEND,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaRendererPrivate
{
  MetaBackend *backend;
  GList *views;
} MetaRendererPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaRenderer, meta_renderer, G_TYPE_OBJECT)

MetaBackend *
meta_renderer_get_backend (MetaRenderer *renderer)
{
  MetaRendererPrivate *priv = meta_renderer_get_instance_private (renderer);

  return priv->backend;
}

/**
 * meta_renderer_create_cogl_renderer:
 * @renderer: a #MetaRenderer object
 *
 * Creates a #CoglRenderer that is appropriate for a certain backend. For
 * example, a #MetaRenderer might call cogl_renderer_set_custom_winsys() to tie
 * the backend-specific mechanisms (such as swapBuffers and vsync) into Cogl.
 *
 * Returns: (transfer full): a newly made #CoglRenderer.
 */
CoglRenderer *
meta_renderer_create_cogl_renderer (MetaRenderer *renderer)
{
  return META_RENDERER_GET_CLASS (renderer)->create_cogl_renderer (renderer);
}

static MetaRendererView *
meta_renderer_create_view (MetaRenderer       *renderer,
                           MetaLogicalMonitor *logical_monitor,
                           MetaOutput         *output,
                           MetaCrtc           *crtc)
{
  return META_RENDERER_GET_CLASS (renderer)->create_view (renderer,
                                                          logical_monitor,
                                                          output,
                                                          crtc);
}

/**
 * meta_renderer_rebuild_views:
 * @renderer: a #MetaRenderer object
 *
 * Rebuilds the internal list of #MetaRendererView objects by querying the
 * current #MetaBackend's #MetaMonitorManager.
 *
 * This also leads to the original list of monitors being unconditionally freed.
 */
void
meta_renderer_rebuild_views (MetaRenderer *renderer)
{
  return META_RENDERER_GET_CLASS (renderer)->rebuild_views (renderer);
}

static void
create_crtc_view (MetaLogicalMonitor *logical_monitor,
                  MetaMonitor        *monitor,
                  MetaOutput         *output,
                  MetaCrtc           *crtc,
                  gpointer            user_data)
{
  MetaRenderer *renderer = user_data;
  MetaRendererPrivate *priv = meta_renderer_get_instance_private (renderer);
  MetaRendererView *view;

  view = meta_renderer_create_view (renderer, logical_monitor, output, crtc);
  priv->views = g_list_append (priv->views, view);
}

static void
meta_renderer_real_rebuild_views (MetaRenderer *renderer)
{
  MetaRendererPrivate *priv = meta_renderer_get_instance_private (renderer);
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GList *logical_monitors, *l;

  g_list_free_full (priv->views, g_object_unref);
  priv->views = NULL;

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);

  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;

      meta_logical_monitor_foreach_crtc (logical_monitor,
                                         create_crtc_view,
                                         renderer);
    }
}

void
meta_renderer_add_view (MetaRenderer     *renderer,
                        MetaRendererView *view)
{
  MetaRendererPrivate *priv = meta_renderer_get_instance_private (renderer);

  priv->views = g_list_append (priv->views, view);
}

/**
 * meta_renderer_get_views:
 * @renderer: a #MetaRenderer object
 *
 * Returns a list of #MetaRendererView objects, each dealing with a part of the
 * stage.
 *
 * Returns: (transfer none) (element-type MetaRendererView): a list of
 * #MetaRendererView objects.
 */
GList *
meta_renderer_get_views (MetaRenderer *renderer)
{
  MetaRendererPrivate *priv = meta_renderer_get_instance_private (renderer);

  return priv->views;
}

gboolean
meta_renderer_is_hardware_accelerated (MetaRenderer *renderer)
{
  MetaRendererPrivate *priv = meta_renderer_get_instance_private (renderer);
  MetaBackend *backend = priv->backend;
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  CoglGpuInfo *info = &cogl_context->gpu;

  switch (info->architecture)
    {
    case COGL_GPU_INFO_ARCHITECTURE_UNKNOWN:
    case COGL_GPU_INFO_ARCHITECTURE_SANDYBRIDGE:
    case COGL_GPU_INFO_ARCHITECTURE_SGX:
    case COGL_GPU_INFO_ARCHITECTURE_MALI:
      return TRUE;
    case COGL_GPU_INFO_ARCHITECTURE_LLVMPIPE:
    case COGL_GPU_INFO_ARCHITECTURE_SOFTPIPE:
    case COGL_GPU_INFO_ARCHITECTURE_SWRAST:
      return FALSE;
    }

  g_assert_not_reached ();
  return FALSE;
}

static void
meta_renderer_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  MetaRenderer *renderer = META_RENDERER (object);
  MetaRendererPrivate *priv = meta_renderer_get_instance_private (renderer);

  switch (prop_id)
    {
    case PROP_BACKEND:
      g_value_set_object (value, priv->backend);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_renderer_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  MetaRenderer *renderer = META_RENDERER (object);
  MetaRendererPrivate *priv = meta_renderer_get_instance_private (renderer);

  switch (prop_id)
    {
    case PROP_BACKEND:
      priv->backend = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_renderer_finalize (GObject *object)
{
  MetaRenderer *renderer = META_RENDERER (object);
  MetaRendererPrivate *priv = meta_renderer_get_instance_private (renderer);

  g_list_free_full (priv->views, g_object_unref);
  priv->views = NULL;

  G_OBJECT_CLASS (meta_renderer_parent_class)->finalize (object);
}

static void
meta_renderer_init (MetaRenderer *renderer)
{
}

static void
meta_renderer_class_init (MetaRendererClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_renderer_get_property;
  object_class->set_property = meta_renderer_set_property;
  object_class->finalize = meta_renderer_finalize;

  klass->rebuild_views = meta_renderer_real_rebuild_views;

  obj_props[PROP_BACKEND] =
    g_param_spec_object ("backend",
                         "backend",
                         "MetaBackend",
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}
