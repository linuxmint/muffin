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

#include <float.h>

#include "compositor/meta-surface-actor-wayland.h"
#include "compositor/meta-window-actor-wayland.h"
#include "meta/meta-window-actor.h"
#include "wayland/meta-wayland-buffer.h"
#include "wayland/meta-wayland-single-pixel-buffer.h"
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
set_surface_actor_index (GNode    *node,
                         gpointer  data)
{
  MetaWaylandSurface *surface = node->data;
  MetaSurfaceActor *surface_actor = meta_wayland_surface_get_actor (surface);

  SurfaceTreeTraverseData *traverse_data = data;

  if (clutter_actor_contains (CLUTTER_ACTOR (traverse_data->window_actor),
                              CLUTTER_ACTOR (surface_actor)))
    {
      clutter_actor_set_child_at_index (
        CLUTTER_ACTOR (traverse_data->window_actor),
        CLUTTER_ACTOR (surface_actor),
        traverse_data->index);
    }
  else
    {
      clutter_actor_insert_child_at_index (
        CLUTTER_ACTOR (traverse_data->window_actor),
        CLUTTER_ACTOR (surface_actor),
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
  SurfaceTreeTraverseData traverse_data;

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

  g_list_free (children);

  G_OBJECT_CLASS (meta_window_actor_wayland_parent_class)->dispose (object);
}

static MetaSurfaceActor *
meta_window_actor_wayland_get_scanout_candidate (MetaWindowActor  *actor,
                                                 const char      **reason)
{
  ClutterActor *self = CLUTTER_ACTOR (actor);
  ClutterActorIter iter;
  ClutterActor *child;
  MetaSurfaceActor *topmost = NULL;
  MetaSurfaceActor *bottommost = NULL;
  int n_visible = 0;
  MetaWindow *window;
  ClutterActorBox surface_box;
  float window_width, window_height;

  if (clutter_actor_get_n_children (self) == 1)
    return meta_window_actor_get_surface (actor);

  clutter_actor_iter_init (&iter, self);
  while (clutter_actor_iter_next (&iter, &child))
    {
      MetaSurfaceActor *surface_actor = META_SURFACE_ACTOR (child);

      if (!clutter_actor_is_mapped (child))
        continue;

      if (meta_surface_actor_is_obscured (surface_actor))
        continue;

      if (!bottommost)
        bottommost = surface_actor;

      topmost = surface_actor;
      n_visible++;
    }

  if (!topmost)
    {
      *reason = "no visible surface";
      return NULL;
    }

  window = meta_window_actor_get_meta_window (actor);

  if (meta_window_is_fullscreen (window) && n_visible == 1)
    return topmost;

  if (meta_window_is_fullscreen (window) && n_visible == 2)
    {
      MetaWaylandSurface *bg_surface;
      MetaWaylandBuffer *buffer;
      MetaWaylandSinglePixelBuffer *sp_buffer;

      bg_surface =
        meta_surface_actor_wayland_get_surface (META_SURFACE_ACTOR_WAYLAND (bottommost));
      buffer = bg_surface ? meta_wayland_surface_get_buffer (bg_surface) : NULL;
      sp_buffer = buffer ? buffer->single_pixel.single_pixel_buffer : NULL;

      if (sp_buffer &&
          meta_wayland_single_pixel_buffer_is_opaque_black (sp_buffer))
        return topmost;
    }

  if (!meta_surface_actor_is_opaque (topmost))
    {
      if (!meta_surface_actor_get_texture (topmost))
        *reason = "top surface has no texture";
      else if (!meta_surface_actor_get_opaque_region (topmost))
        *reason = "top surface has alpha, no opaque region declared";
      else
        *reason = "top surface opaque region does not cover it";

      return NULL;
    }

  /* Allocation boxes are parent-relative, so the surface's box is already in
   * window-actor coordinates and compares against the window actor's size with
   * the origin at zero. */
  clutter_actor_get_size (self, &window_width, &window_height);
  clutter_actor_get_allocation_box (CLUTTER_ACTOR (topmost), &surface_box);

  if (!G_APPROX_VALUE (surface_box.x1, 0.0f, FLT_EPSILON) ||
      !G_APPROX_VALUE (surface_box.y1, 0.0f, FLT_EPSILON) ||
      !G_APPROX_VALUE (surface_box.x2 - surface_box.x1, window_width, FLT_EPSILON) ||
      !G_APPROX_VALUE (surface_box.y2 - surface_box.y1, window_height, FLT_EPSILON))
    {
      *reason = "top surface does not cover window";
      return NULL;
    }

  return topmost;
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
  window_actor_class->get_scanout_candidate = meta_window_actor_wayland_get_scanout_candidate;

  object_class->dispose = meta_window_actor_wayland_dispose;
}

static void
meta_window_actor_wayland_init (MetaWindowActorWayland *self)
{
}
