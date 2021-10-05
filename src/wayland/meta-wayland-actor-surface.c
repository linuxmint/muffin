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
 *
 */

#include "config.h"

#include "wayland/meta-wayland-actor-surface.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-logical-monitor.h"
#include "compositor/meta-surface-actor-wayland.h"
#include "compositor/meta-window-actor-wayland.h"
#include "compositor/region-utils.h"
#include "wayland/meta-wayland-buffer.h"
#include "wayland/meta-wayland-surface.h"
#include "wayland/meta-window-wayland.h"
#include "wayland/meta-xwayland-surface.h"

typedef struct _MetaWaylandActorSurfacePrivate MetaWaylandActorSurfacePrivate;

struct _MetaWaylandActorSurfacePrivate
{
  MetaSurfaceActor *actor;

  gulong actor_destroyed_handler_id;

  struct wl_list frame_callback_list;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (MetaWaylandActorSurface,
                                     meta_wayland_actor_surface,
                                     META_TYPE_WAYLAND_SURFACE_ROLE)

static void
meta_wayland_actor_surface_constructed (GObject *object)
{
  G_OBJECT_CLASS (meta_wayland_actor_surface_parent_class)->constructed (object);

  meta_wayland_actor_surface_reset_actor (META_WAYLAND_ACTOR_SURFACE (object));
}

static void
clear_surface_actor (MetaWaylandActorSurface *actor_surface)
{
  MetaWaylandActorSurfacePrivate *priv =
    meta_wayland_actor_surface_get_instance_private (actor_surface);
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (actor_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);

  if (!priv->actor)
    return;

  g_clear_signal_handler (&priv->actor_destroyed_handler_id, priv->actor);
  g_signal_handlers_disconnect_by_func (priv->actor,
                                        meta_wayland_surface_notify_geometry_changed,
                                        surface);
  g_clear_object (&priv->actor);
}

static void
meta_wayland_actor_surface_dispose (GObject *object)
{
  MetaWaylandActorSurface *actor_surface = META_WAYLAND_ACTOR_SURFACE (object);
  MetaWaylandActorSurfacePrivate *priv =
    meta_wayland_actor_surface_get_instance_private (actor_surface);
  MetaWaylandFrameCallback *cb, *next;

  if (priv->actor)
    {
      clutter_actor_set_reactive (CLUTTER_ACTOR (priv->actor), FALSE);
      clear_surface_actor (actor_surface);
    }

  wl_list_for_each_safe (cb, next, &priv->frame_callback_list, link)
    wl_resource_destroy (cb->resource);

  G_OBJECT_CLASS (meta_wayland_actor_surface_parent_class)->dispose (object);
}

static void
meta_wayland_actor_surface_assigned (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandActorSurfacePrivate *priv =
    meta_wayland_actor_surface_get_instance_private (META_WAYLAND_ACTOR_SURFACE (surface_role));
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);

  if (wl_list_empty (&surface->unassigned.pending_frame_callback_list))
    return;

  wl_list_insert_list (priv->frame_callback_list.prev,
                       &surface->unassigned.pending_frame_callback_list);
  wl_list_init (&surface->unassigned.pending_frame_callback_list);

  meta_wayland_compositor_add_frame_callback_surface (surface->compositor,
                                                      surface);
}

void
meta_wayland_actor_surface_queue_frame_callbacks (MetaWaylandActorSurface *actor_surface,
                                                  MetaWaylandSurfaceState *pending)
{
  MetaWaylandActorSurfacePrivate *priv =
    meta_wayland_actor_surface_get_instance_private (actor_surface);
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (actor_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);

  if (wl_list_empty (&pending->frame_callback_list))
    return;

  wl_list_insert_list (priv->frame_callback_list.prev,
                       &pending->frame_callback_list);
  wl_list_init (&pending->frame_callback_list);

  meta_wayland_compositor_add_frame_callback_surface (surface->compositor,
                                                      surface);
}

void
meta_wayland_actor_surface_emit_frame_callbacks (MetaWaylandActorSurface *actor_surface,
                                                 uint32_t                 timestamp_ms)
{
  MetaWaylandActorSurfacePrivate *priv =
    meta_wayland_actor_surface_get_instance_private (actor_surface);

  while (!wl_list_empty (&priv->frame_callback_list))
    {
      MetaWaylandFrameCallback *callback =
        wl_container_of (priv->frame_callback_list.next, callback, link);

      wl_callback_send_done (callback->resource, timestamp_ms);
      wl_resource_destroy (callback->resource);
    }
}

double
meta_wayland_actor_surface_get_geometry_scale (MetaWaylandActorSurface *actor_surface)
{
  MetaWaylandActorSurfaceClass *actor_surface_class =
    META_WAYLAND_ACTOR_SURFACE_GET_CLASS (actor_surface);

  return actor_surface_class->get_geometry_scale (actor_surface);
}

static void
meta_wayland_actor_surface_real_sync_actor_state (MetaWaylandActorSurface *actor_surface)
{
  MetaWaylandActorSurfacePrivate *priv =
    meta_wayland_actor_surface_get_instance_private (actor_surface);
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (actor_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaSurfaceActor *surface_actor;
  MetaShapedTexture *stex;
  MetaWaylandBuffer *buffer;
  cairo_rectangle_int_t surface_rect;
  MetaWaylandSurface *subsurface_surface;

  surface_actor = priv->actor;
  stex = meta_surface_actor_get_texture (surface_actor);

  buffer = surface->buffer_ref.buffer;
  if (buffer)
    {
      CoglSnippet *snippet;
      gboolean is_y_inverted;

      snippet = meta_wayland_buffer_create_snippet (buffer);
      is_y_inverted = meta_wayland_buffer_is_y_inverted (buffer);

      meta_shaped_texture_set_texture (stex, surface->texture);
      meta_shaped_texture_set_snippet (stex, snippet);
      meta_shaped_texture_set_is_y_inverted (stex, is_y_inverted);
      meta_shaped_texture_set_buffer_scale (stex, surface->scale);
      cogl_clear_object (&snippet);
    }
  else
    {
      meta_shaped_texture_set_texture (stex, NULL);
    }

  surface_rect = (cairo_rectangle_int_t) {
    .width = meta_wayland_surface_get_width (surface),
    .height = meta_wayland_surface_get_height (surface),
  };

  if (surface->input_region)
    {
      cairo_region_t *input_region;

      input_region = cairo_region_copy (surface->input_region);
      cairo_region_intersect_rectangle (input_region, &surface_rect);
      meta_surface_actor_set_input_region (surface_actor, input_region);
      cairo_region_destroy (input_region);
    }
  else
    {
      meta_surface_actor_set_input_region (surface_actor, NULL);
    }

  if (!META_IS_XWAYLAND_SURFACE (surface_role))
    {
      if (!meta_shaped_texture_has_alpha (stex))
        {
          cairo_region_t *opaque_region;

          opaque_region = cairo_region_create_rectangle (&surface_rect);
          meta_surface_actor_set_opaque_region (surface_actor, opaque_region);
          cairo_region_destroy (opaque_region);
        }
      else if (surface->opaque_region)
        {
          cairo_region_t *opaque_region;

          opaque_region = cairo_region_copy (surface->opaque_region);
          cairo_region_intersect_rectangle (opaque_region, &surface_rect);
          meta_surface_actor_set_opaque_region (surface_actor, opaque_region);
          cairo_region_destroy (opaque_region);
        }
      else
        {
          meta_surface_actor_set_opaque_region (surface_actor, NULL);
        }
    }

  meta_surface_actor_set_transform (surface_actor, surface->buffer_transform);

  if (surface->viewport.has_src_rect)
    {
      meta_surface_actor_set_viewport_src_rect (surface_actor,
                                                &surface->viewport.src_rect);
    }
  else
    {
      meta_surface_actor_reset_viewport_src_rect (surface_actor);
    }

  if (surface->viewport.has_dst_size)
    {
      meta_surface_actor_set_viewport_dst_size (surface_actor,
                                                surface->viewport.dst_width,
                                                surface->viewport.dst_height);
    }
  else
    {
      meta_surface_actor_reset_viewport_dst_size (surface_actor);
    }

  META_WAYLAND_SURFACE_FOREACH_SUBSURFACE (surface, subsurface_surface)
    {
      MetaWaylandActorSurface *actor_surface;

      actor_surface = META_WAYLAND_ACTOR_SURFACE (subsurface_surface->role);
      meta_wayland_actor_surface_sync_actor_state (actor_surface);
    }
}

void
meta_wayland_actor_surface_sync_actor_state (MetaWaylandActorSurface *actor_surface)
{
  MetaWaylandActorSurfaceClass *actor_surface_class =
    META_WAYLAND_ACTOR_SURFACE_GET_CLASS (actor_surface);

  actor_surface_class->sync_actor_state (actor_surface);
}

static void
meta_wayland_actor_surface_apply_state (MetaWaylandSurfaceRole  *surface_role,
                                        MetaWaylandSurfaceState *pending)
{
  MetaWaylandActorSurface *actor_surface =
    META_WAYLAND_ACTOR_SURFACE (surface_role);
  MetaWaylandActorSurfacePrivate *priv =
    meta_wayland_actor_surface_get_instance_private (actor_surface);

  if (!wl_list_empty (&pending->frame_callback_list) &&
      priv->actor &&
      !meta_surface_actor_is_obscured (priv->actor))
    {
      MetaWaylandSurface *surface =
        meta_wayland_surface_role_get_surface (surface_role);
      MetaBackend *backend = surface->compositor->backend;
      ClutterActor *stage = meta_backend_get_stage (backend);

      clutter_stage_schedule_update (CLUTTER_STAGE (stage));
    }

  meta_wayland_actor_surface_queue_frame_callbacks (actor_surface, pending);

  meta_wayland_actor_surface_sync_actor_state (actor_surface);
}

static gboolean
meta_wayland_actor_surface_is_on_logical_monitor (MetaWaylandSurfaceRole *surface_role,
                                                  MetaLogicalMonitor     *logical_monitor)
{
  MetaWaylandActorSurfacePrivate *priv =
    meta_wayland_actor_surface_get_instance_private (META_WAYLAND_ACTOR_SURFACE (surface_role));
  ClutterActor *actor = CLUTTER_ACTOR (priv->actor);
  float x, y, width, height;
  cairo_rectangle_int_t actor_rect;
  cairo_region_t *region;
  MetaRectangle logical_monitor_layout;
  gboolean is_on_monitor;

  if (!clutter_actor_is_mapped (actor))
    return FALSE;

  clutter_actor_get_transformed_position (actor, &x, &y);
  clutter_actor_get_transformed_size (actor, &width, &height);

  actor_rect.x = (int) roundf (x);
  actor_rect.y = (int) roundf (y);
  actor_rect.width = (int) roundf (x + width) - actor_rect.x;
  actor_rect.height = (int) roundf (y + height) - actor_rect.y;

  /* Calculate the scaled surface actor region. */
  region = cairo_region_create_rectangle (&actor_rect);

  logical_monitor_layout = meta_logical_monitor_get_layout (logical_monitor);

  cairo_region_intersect_rectangle (region,
				    &((cairo_rectangle_int_t) {
				      .x = logical_monitor_layout.x,
				      .y = logical_monitor_layout.y,
				      .width = logical_monitor_layout.width,
				      .height = logical_monitor_layout.height,
				    }));

  is_on_monitor = !cairo_region_is_empty (region);
  cairo_region_destroy (region);

  return is_on_monitor;
}

static void
meta_wayland_actor_surface_get_relative_coordinates (MetaWaylandSurfaceRole *surface_role,
                                                     float                   abs_x,
                                                     float                   abs_y,
                                                     float                  *out_sx,
                                                     float                  *out_sy)
{
  MetaWaylandActorSurface *actor_surface =
    META_WAYLAND_ACTOR_SURFACE (surface_role);
  MetaWaylandActorSurfacePrivate *priv =
    meta_wayland_actor_surface_get_instance_private (actor_surface);

  clutter_actor_transform_stage_point (CLUTTER_ACTOR (priv->actor),
                                       abs_x, abs_y,
                                       out_sx, out_sy);
}

static void
meta_wayland_actor_surface_init (MetaWaylandActorSurface *actor_surface)
{
  MetaWaylandActorSurfacePrivate *priv =
    meta_wayland_actor_surface_get_instance_private (actor_surface);

  wl_list_init (&priv->frame_callback_list);
}

static void
meta_wayland_actor_surface_class_init (MetaWaylandActorSurfaceClass *klass)
{
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_wayland_actor_surface_constructed;
  object_class->dispose = meta_wayland_actor_surface_dispose;

  surface_role_class->assigned = meta_wayland_actor_surface_assigned;
  surface_role_class->apply_state = meta_wayland_actor_surface_apply_state;
  surface_role_class->is_on_logical_monitor =
    meta_wayland_actor_surface_is_on_logical_monitor;
  surface_role_class->get_relative_coordinates =
    meta_wayland_actor_surface_get_relative_coordinates;

  klass->sync_actor_state = meta_wayland_actor_surface_real_sync_actor_state;
}

MetaSurfaceActor *
meta_wayland_actor_surface_get_actor (MetaWaylandActorSurface *actor_surface)
{
  MetaWaylandActorSurfacePrivate *priv =
    meta_wayland_actor_surface_get_instance_private (actor_surface);

  return priv->actor;
}

static void
on_actor_destroyed (ClutterActor            *actor,
                    MetaWaylandActorSurface *actor_surface)
{
  clear_surface_actor (actor_surface);
}

void
meta_wayland_actor_surface_reset_actor (MetaWaylandActorSurface *actor_surface)
{
  MetaWaylandActorSurfacePrivate *priv =
    meta_wayland_actor_surface_get_instance_private (actor_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (META_WAYLAND_SURFACE_ROLE (actor_surface));
  MetaWaylandSurface *subsurface_surface;

  META_WAYLAND_SURFACE_FOREACH_SUBSURFACE (surface, subsurface_surface)
    {
      MetaWaylandActorSurface *actor_surface;

      actor_surface = META_WAYLAND_ACTOR_SURFACE (subsurface_surface->role);
      meta_wayland_actor_surface_reset_actor (actor_surface);
      meta_wayland_actor_surface_sync_actor_state (actor_surface);
    }

  clear_surface_actor (actor_surface);

  priv->actor = g_object_ref_sink (meta_surface_actor_wayland_new (surface));
  priv->actor_destroyed_handler_id =
    g_signal_connect (priv->actor, "destroy",
                      G_CALLBACK (on_actor_destroyed),
                      actor_surface);

  g_signal_connect_swapped (priv->actor, "notify::allocation",
                            G_CALLBACK (meta_wayland_surface_notify_geometry_changed),
                            surface);
  g_signal_connect_swapped (priv->actor, "notify::position",
                            G_CALLBACK (meta_wayland_surface_notify_geometry_changed),
                            surface);
  g_signal_connect_swapped (priv->actor, "notify::mapped",
                            G_CALLBACK (meta_wayland_surface_notify_geometry_changed),
                            surface);
}
