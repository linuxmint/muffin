/*
 * Copyright (C) 2018 Endless, Inc.
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
 *     Georges Basile Stavracas Neto <gbsneto@gnome.org>
 */

#include "compositor/meta-surface-actor-wayland.h"
#include "compositor/meta-window-actor-wayland.h"
#include "meta/meta-window-actor.h"
#include "wayland/meta-wayland-surface.h"

struct _MetaWindowActorWayland
{
  MetaWindowActor parent;
};

G_DEFINE_TYPE (MetaWindowActorWayland, meta_window_actor_wayland, META_TYPE_WINDOW_ACTOR)

typedef struct _SurfaceTreeTraverseData
{
  MetaWindowActor *window_actor;
  int index;
} SurfaceTreeTraverseData;

static gboolean
get_surface_actor_list (GNode    *node,
                        gpointer  data)
{
  MetaWaylandSurface *surface = node->data;
  MetaSurfaceActor *surface_actor = meta_wayland_surface_get_actor (surface);
  GList **surface_actors = data;

  *surface_actors = g_list_prepend (*surface_actors, surface_actor);
  return FALSE;
}

static gboolean
set_surface_actor_index (GNode    *node,
                         gpointer  data)
{
  MetaWaylandSurface *surface = node->data;
  SurfaceTreeTraverseData *traverse_data = data;

  ClutterActor *window_actor = CLUTTER_ACTOR (traverse_data->window_actor);
  ClutterActor *surface_actor =
    CLUTTER_ACTOR (meta_wayland_surface_get_actor (surface));

  if (clutter_actor_contains (window_actor, surface_actor))
    {
      if (clutter_actor_get_child_at_index (window_actor, traverse_data->index) !=
          surface_actor)
        {
          clutter_actor_set_child_at_index (window_actor,
                                            surface_actor,
                                            traverse_data->index);
        }
    }
  else
    {
      clutter_actor_insert_child_at_index (window_actor,
                                           surface_actor,
                                           traverse_data->index);
    }
  traverse_data->index++;

  return FALSE;
}

void
meta_window_actor_wayland_rebuild_surface_tree (MetaWindowActor *actor)
{
  MetaSurfaceActor *surface_actor =
    meta_window_actor_get_surface (actor);
  MetaWaylandSurface *surface = meta_surface_actor_wayland_get_surface (
    META_SURFACE_ACTOR_WAYLAND (surface_actor));
  GNode *root_node = surface->subsurface_branch_node;
  g_autoptr (GList) surface_actors = NULL;
  g_autoptr (GList) children = NULL;
  GList *l;
  SurfaceTreeTraverseData traverse_data;

  g_node_traverse (root_node,
                   G_IN_ORDER,
                   G_TRAVERSE_LEAVES,
                   -1,
                   get_surface_actor_list,
                   &surface_actors);

  children = clutter_actor_get_children (CLUTTER_ACTOR (actor));
  for (l = children; l; l = l->next)
    {
      ClutterActor *child_actor = l->data;

      if (META_IS_SURFACE_ACTOR_WAYLAND (child_actor) &&
          !g_list_find (surface_actors, child_actor))
        clutter_actor_remove_child (CLUTTER_ACTOR (actor), child_actor);
    }

  traverse_data = (SurfaceTreeTraverseData) {
    .window_actor = actor,
    .index = 0,
  };

  g_node_traverse (root_node,
                   G_IN_ORDER,
                   G_TRAVERSE_LEAVES,
                   -1,
                   set_surface_actor_index,
                   &traverse_data);
}

static void
meta_window_actor_wayland_assign_surface_actor (MetaWindowActor  *actor,
                                                MetaSurfaceActor *surface_actor)
{
  MetaWindowActorClass *parent_class =
    META_WINDOW_ACTOR_CLASS (meta_window_actor_wayland_parent_class);

  g_warn_if_fail (!meta_window_actor_get_surface (actor));

  parent_class->assign_surface_actor (actor, surface_actor);

  meta_window_actor_wayland_rebuild_surface_tree (actor);
}

static void
meta_window_actor_wayland_frame_complete (MetaWindowActor  *actor,
                                          ClutterFrameInfo *frame_info,
                                          int64_t           presentation_time)
{
}

static void
meta_window_actor_wayland_queue_frame_drawn (MetaWindowActor *actor,
                                             gboolean         skip_sync_delay)
{
}

static void
meta_window_actor_wayland_pre_paint (MetaWindowActor *actor)
{
}

static void
meta_window_actor_wayland_post_paint (MetaWindowActor *actor)
{
}

static void
meta_window_actor_wayland_queue_destroy (MetaWindowActor *actor)
{
}

static void
meta_window_actor_wayland_set_frozen (MetaWindowActor *actor,
                                      gboolean         frozen)
{
}

static void
meta_window_actor_wayland_update_regions (MetaWindowActor *actor)
{
}

static void
meta_window_actor_wayland_dispose (GObject *object)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (object);
  MetaSurfaceActor *surface_actor =
  meta_window_actor_get_surface (window_actor);
  GList *children;
  GList *l;

  children = clutter_actor_get_children (CLUTTER_ACTOR (window_actor));
  for (l = children; l; l = l->next)
    {
      ClutterActor *child_actor = l->data;

      if (META_IS_SURFACE_ACTOR_WAYLAND (child_actor) &&
        child_actor != CLUTTER_ACTOR (surface_actor))
        clutter_actor_remove_child (CLUTTER_ACTOR (window_actor), child_actor);
    }

  G_OBJECT_CLASS (meta_window_actor_wayland_parent_class)->dispose (object);
}

static void
meta_window_actor_wayland_class_init (MetaWindowActorWaylandClass *klass)
{
  MetaWindowActorClass *window_actor_class = META_WINDOW_ACTOR_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  window_actor_class->assign_surface_actor = meta_window_actor_wayland_assign_surface_actor;
  window_actor_class->frame_complete = meta_window_actor_wayland_frame_complete;
  window_actor_class->queue_frame_drawn = meta_window_actor_wayland_queue_frame_drawn;
  window_actor_class->pre_paint = meta_window_actor_wayland_pre_paint;
  window_actor_class->post_paint = meta_window_actor_wayland_post_paint;
  window_actor_class->queue_destroy = meta_window_actor_wayland_queue_destroy;
  window_actor_class->set_frozen = meta_window_actor_wayland_set_frozen;
  window_actor_class->update_regions = meta_window_actor_wayland_update_regions;

  object_class->dispose = meta_window_actor_wayland_dispose;
}

static void
meta_window_actor_wayland_init (MetaWindowActorWayland *self)
{
}
