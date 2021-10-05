/*
 * Copyright (C) 2012,2013 Intel Corporation
 * Copyright (C) 2013-2017 Red Hat, Inc.
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

#include "wayland/meta-wayland-subsurface.h"

#include "compositor/meta-surface-actor-wayland.h"
#include "compositor/meta-window-actor-wayland.h"
#include "wayland/meta-wayland.h"
#include "wayland/meta-wayland-actor-surface.h"
#include "wayland/meta-wayland-buffer.h"
#include "wayland/meta-wayland-surface.h"
#include "wayland/meta-window-wayland.h"

struct _MetaWaylandSubsurface
{
  MetaWaylandActorSurface parent;
};

G_DEFINE_TYPE (MetaWaylandSubsurface,
               meta_wayland_subsurface,
               META_TYPE_WAYLAND_ACTOR_SURFACE)

static void
transform_subsurface_position (MetaWaylandSurface *surface,
                               int                *x,
                               int                *y)
{
  do
    {
      *x += surface->sub.x;
      *y += surface->sub.y;

      surface = surface->sub.parent;
    }
  while (surface);
}

static void
sync_actor_subsurface_state (MetaWaylandSurface *surface)
{
  ClutterActor *actor = CLUTTER_ACTOR (meta_wayland_surface_get_actor (surface));
  MetaWindow *toplevel_window;
  int x, y;

  toplevel_window = meta_wayland_surface_get_toplevel_window (surface);
  if (!toplevel_window)
    return;

  if (toplevel_window->client_type == META_WINDOW_CLIENT_TYPE_X11)
    return;

  x = y = 0;
  transform_subsurface_position (surface, &x, &y);

  clutter_actor_set_position (actor, x, y);
  clutter_actor_set_reactive (actor, TRUE);

  if (surface->buffer_ref.buffer)
    clutter_actor_show (actor);
  else
    clutter_actor_hide (actor);
}

static gboolean
is_child (MetaWaylandSurface *surface,
          MetaWaylandSurface *sibling)
{
  if (surface->sub.parent == sibling)
    return TRUE;
  else
    return FALSE;
}

static gboolean
is_sibling (MetaWaylandSurface *surface,
            MetaWaylandSurface *sibling)
{
  if (surface->sub.parent == sibling->sub.parent)
    return TRUE;
  else
    return FALSE;
}

static gboolean
is_surface_effectively_synchronized (MetaWaylandSurface *surface)
{
  return meta_wayland_surface_should_cache_state (surface);
}

void
meta_wayland_subsurface_parent_state_applied (MetaWaylandSubsurface *subsurface)
{
  MetaWaylandSurfaceRole *surface_role = META_WAYLAND_SURFACE_ROLE (subsurface);
  MetaWaylandActorSurface *actor_surface =
    META_WAYLAND_ACTOR_SURFACE (subsurface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);

  if (surface->sub.pending_pos)
    {
      surface->sub.x = surface->sub.pending_x;
      surface->sub.y = surface->sub.pending_y;
      surface->sub.pending_pos = FALSE;
    }

  if (is_surface_effectively_synchronized (surface))
    meta_wayland_surface_apply_cached_state (surface);

  meta_wayland_actor_surface_sync_actor_state (actor_surface);
}

void
meta_wayland_subsurface_union_geometry (MetaWaylandSubsurface *subsurface,
                                        int                    parent_x,
                                        int                    parent_y,
                                        MetaRectangle         *out_geometry)
{
  MetaWaylandSurfaceRole *surface_role = META_WAYLAND_SURFACE_ROLE (subsurface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaRectangle geometry;
  MetaWaylandSurface *subsurface_surface;

  geometry = (MetaRectangle) {
    .x = surface->offset_x + surface->sub.x,
    .y = surface->offset_y + surface->sub.y,
    .width = meta_wayland_surface_get_width (surface),
    .height = meta_wayland_surface_get_height (surface),
  };

  meta_rectangle_union (out_geometry, &geometry, out_geometry);

  META_WAYLAND_SURFACE_FOREACH_SUBSURFACE (surface, subsurface_surface)
    {
      MetaWaylandSubsurface *subsurface;

      subsurface = META_WAYLAND_SUBSURFACE (subsurface_surface->role);
      meta_wayland_subsurface_union_geometry (subsurface,
                                              parent_x + geometry.x,
                                              parent_y + geometry.y,
                                              out_geometry);
    }
}

static void
meta_wayland_subsurface_assigned (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (meta_wayland_subsurface_parent_class);

  surface->dnd.funcs = meta_wayland_data_device_get_drag_dest_funcs ();

  surface_role_class->assigned (surface_role);
}

static MetaWaylandSurface *
meta_wayland_subsurface_get_toplevel (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandSurface *parent = surface->sub.parent;

  if (parent)
    return meta_wayland_surface_get_toplevel (parent);
  else
    return NULL;
}

static gboolean
meta_wayland_subsurface_should_cache_state (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandSurface *parent;

  if (surface->sub.synchronous)
    return TRUE;

  parent = surface->sub.parent;
  if (parent)
    return meta_wayland_surface_should_cache_state (parent);

  return TRUE;
}

static void
meta_wayland_subsurface_notify_subsurface_state_changed (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandSurface *parent = surface->sub.parent;

  if (parent)
    return meta_wayland_surface_notify_subsurface_state_changed (parent);
}

static double
meta_wayland_subsurface_get_geometry_scale (MetaWaylandActorSurface *actor_surface)
{
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (actor_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandSurface *parent = surface->sub.parent;

  if (parent)
    {
      MetaWaylandActorSurface *parent_actor;

      parent_actor = META_WAYLAND_ACTOR_SURFACE (surface->sub.parent->role);
      return meta_wayland_actor_surface_get_geometry_scale (parent_actor);
    }
  else
    {
      return 1;
    }
}

static void
meta_wayland_subsurface_sync_actor_state (MetaWaylandActorSurface *actor_surface)
{
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (actor_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandActorSurfaceClass *actor_surface_class =
    META_WAYLAND_ACTOR_SURFACE_CLASS (meta_wayland_subsurface_parent_class);
  MetaWaylandSurface *toplevel_surface;

  toplevel_surface = meta_wayland_surface_get_toplevel (surface);
  if (toplevel_surface && meta_wayland_surface_get_window (toplevel_surface))
    actor_surface_class->sync_actor_state (actor_surface);

  sync_actor_subsurface_state (surface);
}

static void
meta_wayland_subsurface_init (MetaWaylandSubsurface *role)
{
}

static void
meta_wayland_subsurface_class_init (MetaWaylandSubsurfaceClass *klass)
{
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (klass);
  MetaWaylandActorSurfaceClass *actor_surface_class =
    META_WAYLAND_ACTOR_SURFACE_CLASS (klass);

  surface_role_class->assigned = meta_wayland_subsurface_assigned;
  surface_role_class->get_toplevel = meta_wayland_subsurface_get_toplevel;
  surface_role_class->should_cache_state = meta_wayland_subsurface_should_cache_state;
  surface_role_class->notify_subsurface_state_changed =
    meta_wayland_subsurface_notify_subsurface_state_changed;

  actor_surface_class->get_geometry_scale =
    meta_wayland_subsurface_get_geometry_scale;
  actor_surface_class->sync_actor_state =
    meta_wayland_subsurface_sync_actor_state;
}

static void
unparent_actor (MetaWaylandSurface *surface)
{
  ClutterActor *actor;
  ClutterActor *parent_actor;

  actor = CLUTTER_ACTOR (meta_wayland_surface_get_actor (surface));
  if (!actor)
    return;

  parent_actor = clutter_actor_get_parent (actor);
  clutter_actor_remove_child (parent_actor, actor);
}

static void
wl_subsurface_destructor (struct wl_resource *resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (resource);

  g_node_unlink (surface->subsurface_branch_node);
  unparent_actor (surface);

  if (surface->sub.parent)
    {
      wl_list_remove (&surface->sub.parent_destroy_listener.link);
      surface->sub.parent = NULL;
    }

  surface->wl_subsurface = NULL;
}

static void
wl_subsurface_destroy (struct wl_client   *client,
                       struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
wl_subsurface_set_position (struct wl_client   *client,
                            struct wl_resource *resource,
                            int32_t             x,
                            int32_t             y)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (resource);

  surface->sub.pending_x = x;
  surface->sub.pending_y = y;
  surface->sub.pending_pos = TRUE;
}

static gboolean
is_valid_sibling (MetaWaylandSurface *surface,
                  MetaWaylandSurface *sibling)
{
  if (is_child (surface, sibling))
    return TRUE;
  if (is_sibling (surface, sibling))
    return TRUE;
  return FALSE;
}

static void
subsurface_handle_pending_surface_destroyed (struct wl_listener *listener,
                                             void               *data)
{
  MetaWaylandSubsurfacePlacementOp *op =
    wl_container_of (listener, op, surface_destroy_listener);

  op->surface = NULL;
}

static void
subsurface_handle_pending_sibling_destroyed (struct wl_listener *listener,
                                             void               *data)
{
  MetaWaylandSubsurfacePlacementOp *op =
    wl_container_of (listener, op, sibling_destroy_listener);

  op->sibling = NULL;
}

void
meta_wayland_subsurface_placement_op_free (MetaWaylandSubsurfacePlacementOp *op)
{
  if (op->surface)
    wl_list_remove (&op->surface_destroy_listener.link);
  if (op->sibling)
    wl_list_remove (&op->sibling_destroy_listener.link);
  g_free (op);
}

static void
queue_subsurface_placement (MetaWaylandSurface             *surface,
                            MetaWaylandSurface             *sibling,
                            MetaWaylandSubsurfacePlacement  placement)
{
  MetaWaylandSurface *parent = surface->sub.parent;
  MetaWaylandSubsurfacePlacementOp *op =
    g_new0 (MetaWaylandSubsurfacePlacementOp, 1);

  op->placement = placement;
  op->surface = surface;
  op->sibling = sibling;
  op->surface_destroy_listener.notify =
    subsurface_handle_pending_surface_destroyed;
  op->sibling_destroy_listener.notify =
    subsurface_handle_pending_sibling_destroyed;
  wl_resource_add_destroy_listener (surface->resource,
                                    &op->surface_destroy_listener);
  wl_resource_add_destroy_listener (sibling->resource,
                                    &op->sibling_destroy_listener);

  parent->pending_state->subsurface_placement_ops =
    g_slist_append (parent->pending_state->subsurface_placement_ops, op);
}

static void
wl_subsurface_place_above (struct wl_client   *client,
                           struct wl_resource *resource,
                           struct wl_resource *sibling_resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (resource);
  MetaWaylandSurface *sibling = wl_resource_get_user_data (sibling_resource);

  if (!is_valid_sibling (surface, sibling))
    {
      wl_resource_post_error (resource, WL_SUBSURFACE_ERROR_BAD_SURFACE,
                              "wl_subsurface::place_above: wl_surface@%d is "
                              "not a valid parent or sibling",
                              wl_resource_get_id (sibling->resource));
      return;
    }

  queue_subsurface_placement (surface,
                              sibling,
                              META_WAYLAND_SUBSURFACE_PLACEMENT_ABOVE);
}

static void
wl_subsurface_place_below (struct wl_client   *client,
                           struct wl_resource *resource,
                           struct wl_resource *sibling_resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (resource);
  MetaWaylandSurface *sibling = wl_resource_get_user_data (sibling_resource);

  if (!is_valid_sibling (surface, sibling))
    {
      wl_resource_post_error (resource, WL_SUBSURFACE_ERROR_BAD_SURFACE,
                              "wl_subsurface::place_below: wl_surface@%d is "
                              "not a valid parent or sibling",
                              wl_resource_get_id (sibling->resource));
      return;
    }

  queue_subsurface_placement (surface,
                              sibling,
                              META_WAYLAND_SUBSURFACE_PLACEMENT_BELOW);
}

static void
wl_subsurface_set_sync (struct wl_client   *client,
                        struct wl_resource *resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (resource);

  surface->sub.synchronous = TRUE;
}

static void
wl_subsurface_set_desync (struct wl_client   *client,
                          struct wl_resource *resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (resource);
  gboolean was_effectively_synchronized;

  was_effectively_synchronized = is_surface_effectively_synchronized (surface);
  surface->sub.synchronous = FALSE;

  if (was_effectively_synchronized &&
      !is_surface_effectively_synchronized (surface))
    meta_wayland_surface_apply_cached_state (surface);
}

static const struct wl_subsurface_interface meta_wayland_wl_subsurface_interface = {
  wl_subsurface_destroy,
  wl_subsurface_set_position,
  wl_subsurface_place_above,
  wl_subsurface_place_below,
  wl_subsurface_set_sync,
  wl_subsurface_set_desync,
};

static void
wl_subcompositor_destroy (struct wl_client   *client,
                          struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
surface_handle_parent_surface_destroyed (struct wl_listener *listener,
                                         void               *data)
{
  MetaWaylandSurface *surface = wl_container_of (listener,
                                                 surface,
                                                 sub.parent_destroy_listener);

  surface->sub.parent = NULL;
}

static gboolean
is_same_or_ancestor (MetaWaylandSurface *surface,
                     MetaWaylandSurface *other_surface)
{
  if (surface == other_surface)
    return TRUE;
  if (other_surface->sub.parent)
    return is_same_or_ancestor (surface, other_surface->sub.parent);
  return FALSE;
}

static void
wl_subcompositor_get_subsurface (struct wl_client   *client,
                                 struct wl_resource *resource,
                                 uint32_t            id,
                                 struct wl_resource *surface_resource,
                                 struct wl_resource *parent_resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandSurface *parent = wl_resource_get_user_data (parent_resource);
  MetaWindow *toplevel_window;

  if (surface->wl_subsurface)
    {
      wl_resource_post_error (surface_resource,
                              WL_DISPLAY_ERROR_INVALID_OBJECT,
                              "wl_subcompositor::get_subsurface already requested");
      return;
    }

  if (is_same_or_ancestor (surface, parent))
    {
      wl_resource_post_error (resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                              "Circular relationship between wl_surface@%d "
                              "and parent surface wl_surface@%d",
                              wl_resource_get_id (surface->resource),
                              wl_resource_get_id (parent->resource));
      return;
    }

  if (!meta_wayland_surface_assign_role (surface,
                                         META_TYPE_WAYLAND_SUBSURFACE,
                                         NULL))
    {
      wl_resource_post_error (resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                              "wl_surface@%d already has a different role",
                              wl_resource_get_id (surface->resource));
      return;
    }

  toplevel_window = meta_wayland_surface_get_toplevel_window (parent);
  if (toplevel_window &&
      toplevel_window->client_type == META_WINDOW_CLIENT_TYPE_X11)
    g_warning ("XWayland subsurfaces not currently supported");

  surface->wl_subsurface =
    wl_resource_create (client,
                        &wl_subsurface_interface,
                        wl_resource_get_version (resource),
                        id);
  wl_resource_set_implementation (surface->wl_subsurface,
                                  &meta_wayland_wl_subsurface_interface,
                                  surface,
                                  wl_subsurface_destructor);

  surface->sub.synchronous = TRUE;
  surface->sub.parent = parent;
  surface->sub.parent_destroy_listener.notify =
    surface_handle_parent_surface_destroyed;
  wl_resource_add_destroy_listener (parent->resource,
                                    &surface->sub.parent_destroy_listener);

  g_node_append (parent->subsurface_branch_node,
                 surface->subsurface_branch_node);

  meta_wayland_surface_notify_subsurface_state_changed (parent);
}

static const struct wl_subcompositor_interface meta_wayland_subcompositor_interface = {
  wl_subcompositor_destroy,
  wl_subcompositor_get_subsurface,
};

static void
bind_subcompositor (struct wl_client *client,
                    void             *data,
                    uint32_t          version,
                    uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client, &wl_subcompositor_interface,
                                 version, id);
  wl_resource_set_implementation (resource, &meta_wayland_subcompositor_interface,
                                  data, NULL);
}

void
meta_wayland_subsurfaces_init (MetaWaylandCompositor *compositor)
{
  if (wl_global_create (compositor->wayland_display,
                        &wl_subcompositor_interface,
                        META_WL_SUBCOMPOSITOR_VERSION,
                        compositor, bind_subcompositor) == NULL)
    g_error ("Failed to register a global wl-subcompositor object");
}
