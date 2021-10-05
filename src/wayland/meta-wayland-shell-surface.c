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

#include "wayland/meta-wayland-shell-surface.h"

#include "compositor/meta-surface-actor-wayland.h"
#include "compositor/meta-window-actor-private.h"
#include "compositor/meta-window-actor-wayland.h"
#include "wayland/meta-wayland-actor-surface.h"
#include "wayland/meta-wayland-buffer.h"
#include "wayland/meta-wayland-subsurface.h"
#include "wayland/meta-wayland-surface.h"
#include "wayland/meta-window-wayland.h"

typedef struct _MetaWaylandShellSurfacePrivate
{
  MetaWindow *window;

  gulong unmanaging_handler_id;
  gulong position_changed_handler_id;
  gulong effects_completed_handler_id;
} MetaWaylandShellSurfacePrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (MetaWaylandShellSurface,
                                     meta_wayland_shell_surface,
                                     META_TYPE_WAYLAND_ACTOR_SURFACE)

void
meta_wayland_shell_surface_calculate_geometry (MetaWaylandShellSurface *shell_surface,
                                               MetaRectangle           *out_geometry)
{
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (shell_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaRectangle geometry;
  MetaWaylandSurface *subsurface_surface;

  geometry = (MetaRectangle) {
    .width = meta_wayland_surface_get_width (surface),
    .height = meta_wayland_surface_get_height (surface),
  };

  META_WAYLAND_SURFACE_FOREACH_SUBSURFACE (surface, subsurface_surface)
    {
      MetaWaylandSubsurface *subsurface;

      subsurface = META_WAYLAND_SUBSURFACE (subsurface_surface->role);
      meta_wayland_subsurface_union_geometry (subsurface,
                                              0, 0,
                                              &geometry);
    }

  *out_geometry = geometry;
}

void
meta_wayland_shell_surface_determine_geometry (MetaWaylandShellSurface *shell_surface,
                                               MetaRectangle           *set_geometry,
                                               MetaRectangle           *out_geometry)
{
  MetaRectangle bounding_geometry = { 0 };
  MetaRectangle intersected_geometry = { 0 };

  meta_wayland_shell_surface_calculate_geometry (shell_surface,
                                                 &bounding_geometry);

  meta_rectangle_intersect (set_geometry, &bounding_geometry,
                            &intersected_geometry);

  *out_geometry = intersected_geometry;
}

static void
clear_window (MetaWaylandShellSurface *shell_surface)
{
  MetaWaylandShellSurfacePrivate *priv =
    meta_wayland_shell_surface_get_instance_private (shell_surface);
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (shell_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaSurfaceActor *surface_actor;

  if (!priv->window)
    return;

  g_clear_signal_handler (&priv->unmanaging_handler_id,
                          priv->window);
  g_clear_signal_handler (&priv->position_changed_handler_id,
                          priv->window);
  g_clear_signal_handler (&priv->effects_completed_handler_id,
                          meta_window_actor_from_window (priv->window));
  priv->window = NULL;

  surface_actor = meta_wayland_surface_get_actor (surface);
  if (surface_actor)
    clutter_actor_set_reactive (CLUTTER_ACTOR (surface_actor), FALSE);

  meta_wayland_surface_notify_unmapped (surface);
}

static void
window_unmanaging (MetaWindow              *window,
                   MetaWaylandShellSurface *shell_surface)
{
  clear_window (shell_surface);
}

static void
window_position_changed (MetaWindow         *window,
                         MetaWaylandSurface *surface)
{
  meta_wayland_surface_update_outputs_recursively (surface);
}

static void
window_actor_effects_completed (MetaWindowActor    *window_actor,
                                MetaWaylandSurface *surface)
{
  meta_wayland_surface_update_outputs_recursively (surface);
  meta_wayland_compositor_repick (surface->compositor);
}

void
meta_wayland_shell_surface_set_window (MetaWaylandShellSurface *shell_surface,
                                       MetaWindow              *window)
{
  MetaWaylandShellSurfacePrivate *priv =
    meta_wayland_shell_surface_get_instance_private (shell_surface);
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (shell_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaSurfaceActor *surface_actor;

  g_assert (!priv->window);

  priv->window = window;

  surface_actor = meta_wayland_surface_get_actor (surface);
  if (surface_actor)
    clutter_actor_set_reactive (CLUTTER_ACTOR (surface_actor), TRUE);

  priv->unmanaging_handler_id =
    g_signal_connect (window,
                      "unmanaging",
                      G_CALLBACK (window_unmanaging),
                      shell_surface);
  priv->position_changed_handler_id =
    g_signal_connect (window,
                      "position-changed",
                      G_CALLBACK (window_position_changed),
                      surface);
  priv->effects_completed_handler_id =
    g_signal_connect (meta_window_actor_from_window (window),
                      "effects-completed",
                      G_CALLBACK (window_actor_effects_completed),
                      surface);

  meta_window_update_monitor (window, META_WINDOW_UPDATE_MONITOR_FLAGS_NONE);
}

void
meta_wayland_shell_surface_configure (MetaWaylandShellSurface        *shell_surface,
                                      MetaWaylandWindowConfiguration *configuration)
{
  MetaWaylandShellSurfaceClass *shell_surface_class =
    META_WAYLAND_SHELL_SURFACE_GET_CLASS (shell_surface);

  shell_surface_class->configure (shell_surface, configuration);
}

void
meta_wayland_shell_surface_ping (MetaWaylandShellSurface *shell_surface,
                                 uint32_t                 serial)
{
  MetaWaylandShellSurfaceClass *shell_surface_class =
    META_WAYLAND_SHELL_SURFACE_GET_CLASS (shell_surface);

  shell_surface_class->ping (shell_surface, serial);
}

void
meta_wayland_shell_surface_close (MetaWaylandShellSurface *shell_surface)
{
  MetaWaylandShellSurfaceClass *shell_surface_class =
    META_WAYLAND_SHELL_SURFACE_GET_CLASS (shell_surface);

  shell_surface_class->close (shell_surface);
}

void
meta_wayland_shell_surface_managed (MetaWaylandShellSurface *shell_surface,
                                    MetaWindow              *window)
{
  MetaWaylandShellSurfaceClass *shell_surface_class =
    META_WAYLAND_SHELL_SURFACE_GET_CLASS (shell_surface);

  shell_surface_class->managed (shell_surface, window);
}

static void
meta_wayland_shell_surface_assigned (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (meta_wayland_shell_surface_parent_class);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);

  surface->dnd.funcs = meta_wayland_data_device_get_drag_dest_funcs ();

  surface_role_class->assigned (surface_role);
}

static void
meta_wayland_shell_surface_surface_pre_apply_state (MetaWaylandSurfaceRole  *surface_role,
                                                    MetaWaylandSurfaceState *pending)
{
  MetaWaylandShellSurface *shell_surface =
    META_WAYLAND_SHELL_SURFACE (surface_role);
  MetaWaylandShellSurfacePrivate *priv =
    meta_wayland_shell_surface_get_instance_private (shell_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);

  if (pending->newly_attached &&
      !surface->buffer_ref.buffer &&
      priv->window)
    meta_window_queue (priv->window, META_QUEUE_CALC_SHOWING);
}

static void
meta_wayland_shell_surface_surface_apply_state (MetaWaylandSurfaceRole  *surface_role,
                                                MetaWaylandSurfaceState *pending)
{
  MetaWaylandShellSurface *shell_surface =
    META_WAYLAND_SHELL_SURFACE (surface_role);
  MetaWaylandShellSurfacePrivate *priv =
    meta_wayland_shell_surface_get_instance_private (shell_surface);
  MetaWaylandActorSurface *actor_surface =
    META_WAYLAND_ACTOR_SURFACE (surface_role);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandSurfaceRoleClass *surface_role_class;
  MetaWindow *window;
  MetaWaylandBuffer *buffer;
  double geometry_scale;

  surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (meta_wayland_shell_surface_parent_class);
  surface_role_class->apply_state (surface_role, pending);

  buffer = surface->buffer_ref.buffer;
  if (!buffer)
    return;

  window = priv->window;
  if (!window)
    return;

  geometry_scale = meta_wayland_actor_surface_get_geometry_scale (actor_surface);

  window->buffer_rect.width =
    meta_wayland_surface_get_width (surface) * geometry_scale;
  window->buffer_rect.height =
    meta_wayland_surface_get_height (surface) * geometry_scale;
}

static MetaWindow *
meta_wayland_shell_surface_get_window (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandShellSurface *shell_surface =
    META_WAYLAND_SHELL_SURFACE (surface_role);
  MetaWaylandShellSurfacePrivate *priv =
    meta_wayland_shell_surface_get_instance_private (shell_surface);

  return priv->window;
}

static void
meta_wayland_shell_surface_notify_subsurface_state_changed (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandShellSurface *shell_surface =
    META_WAYLAND_SHELL_SURFACE (surface_role);
  MetaWaylandShellSurfacePrivate *priv =
    meta_wayland_shell_surface_get_instance_private (shell_surface);
  MetaWindow *window;
  MetaWindowActor *window_actor;

  window = priv->window;
  if (!window)
    return;

  window_actor = meta_window_actor_from_window (window);
  meta_window_actor_wayland_rebuild_surface_tree (window_actor);
}

static double
meta_wayland_shell_surface_get_geometry_scale (MetaWaylandActorSurface *actor_surface)
{
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (actor_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWindow *toplevel_window;

  toplevel_window = meta_wayland_surface_get_toplevel_window (surface);
  if (meta_is_stage_views_scaled () || !toplevel_window)
    return 1;
  else
    return meta_window_wayland_get_geometry_scale (toplevel_window);
}

static void
meta_wayland_shell_surface_sync_actor_state (MetaWaylandActorSurface *actor_surface)
{
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (actor_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandActorSurfaceClass *actor_surface_class =
    META_WAYLAND_ACTOR_SURFACE_CLASS (meta_wayland_shell_surface_parent_class);
  MetaWindow *toplevel_window;

  toplevel_window = meta_wayland_surface_get_toplevel_window (surface);
  if (!toplevel_window)
    return;

  actor_surface_class->sync_actor_state (actor_surface);
}

void
meta_wayland_shell_surface_destroy_window (MetaWaylandShellSurface *shell_surface)
{
  MetaWaylandShellSurfacePrivate *priv =
    meta_wayland_shell_surface_get_instance_private (shell_surface);
  MetaWindow *window;
  MetaDisplay *display;
  uint32_t timestamp;

  window = priv->window;
  if (!window)
    return;

  display = meta_window_get_display (window);
  timestamp = meta_display_get_current_time_roundtrip (display);
  meta_window_unmanage (window, timestamp);
  g_assert (!priv->window);
}

static void
meta_wayland_shell_surface_finalize (GObject *object)
{
  MetaWaylandShellSurface *shell_surface = META_WAYLAND_SHELL_SURFACE (object);

  meta_wayland_shell_surface_destroy_window (shell_surface);

  G_OBJECT_CLASS (meta_wayland_shell_surface_parent_class)->finalize (object);
}

static void
meta_wayland_shell_surface_init (MetaWaylandShellSurface *role)
{
}

static void
meta_wayland_shell_surface_class_init (MetaWaylandShellSurfaceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (klass);
  MetaWaylandActorSurfaceClass *actor_surface_class =
    META_WAYLAND_ACTOR_SURFACE_CLASS (klass);

  object_class->finalize = meta_wayland_shell_surface_finalize;

  surface_role_class->assigned = meta_wayland_shell_surface_assigned;
  surface_role_class->pre_apply_state =
    meta_wayland_shell_surface_surface_pre_apply_state;
  surface_role_class->apply_state =
    meta_wayland_shell_surface_surface_apply_state;
  surface_role_class->notify_subsurface_state_changed =
    meta_wayland_shell_surface_notify_subsurface_state_changed;
  surface_role_class->get_window = meta_wayland_shell_surface_get_window;

  actor_surface_class->get_geometry_scale =
    meta_wayland_shell_surface_get_geometry_scale;
  actor_surface_class->sync_actor_state =
    meta_wayland_shell_surface_sync_actor_state;
}
