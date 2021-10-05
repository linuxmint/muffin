/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "wayland/meta-window-wayland.h"

#include <errno.h>
#include <string.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-backend-private.h"
#include "backends/meta-logical-monitor.h"
#include "compositor/meta-surface-actor-wayland.h"
#include "compositor/meta-window-actor-private.h"
#include "core/boxes-private.h"
#include "core/stack-tracker.h"
#include "core/window-private.h"
#include "meta/meta-x11-errors.h"
#include "wayland/meta-wayland-actor-surface.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-surface.h"
#include "wayland/meta-wayland-window-configuration.h"
#include "wayland/meta-wayland-xdg-shell.h"

struct _MetaWindowWayland
{
  MetaWindow parent;

  int geometry_scale;

  GList *pending_configurations;
  gboolean has_pending_state_change;

  int last_sent_x;
  int last_sent_y;
  int last_sent_width;
  int last_sent_height;
  int last_sent_rel_x;
  int last_sent_rel_y;
  int last_sent_geometry_scale;
  MetaGravity last_sent_gravity;

  gboolean has_been_shown;
};

struct _MetaWindowWaylandClass
{
  MetaWindowClass parent_class;
};

G_DEFINE_TYPE (MetaWindowWayland, meta_window_wayland, META_TYPE_WINDOW)

static void
set_geometry_scale_for_window (MetaWindowWayland *wl_window,
                               int                geometry_scale)
{
  MetaWindowActor *window_actor;

  wl_window->geometry_scale = geometry_scale;

  window_actor = meta_window_actor_from_window (META_WINDOW (wl_window));
  if (window_actor)
    meta_window_actor_set_geometry_scale (window_actor, geometry_scale);
}

static int
get_window_geometry_scale_for_logical_monitor (MetaLogicalMonitor *logical_monitor)
{
  g_assert (logical_monitor);

  if (meta_is_stage_views_scaled ())
    return 1;
  else
    return meta_logical_monitor_get_scale (logical_monitor);
}

static void
meta_window_wayland_manage (MetaWindow *window)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
  MetaDisplay *display = window->display;

  wl_window->geometry_scale = meta_window_wayland_get_geometry_scale (window);

  meta_display_register_wayland_window (display, window);

  {
    meta_stack_tracker_record_add (window->display->stack_tracker,
                                   window->stamp,
                                   0);
  }

  meta_wayland_surface_window_managed (window->surface, window);
}

static void
meta_window_wayland_unmanage (MetaWindow *window)
{
  {
    meta_stack_tracker_record_remove (window->display->stack_tracker,
                                      window->stamp,
                                      0);
  }

  meta_display_unregister_wayland_window (window->display, window);
}

static void
meta_window_wayland_ping (MetaWindow *window,
                          guint32     serial)
{
  meta_wayland_surface_ping (window->surface, serial);
}

static void
meta_window_wayland_delete (MetaWindow *window,
                            guint32     timestamp)
{
  meta_wayland_surface_delete (window->surface);
}

static void
meta_window_wayland_kill (MetaWindow *window)
{
  MetaWaylandSurface *surface = window->surface;
  struct wl_resource *resource = surface->resource;

  /* Send the client an unrecoverable error to kill the client. */
  wl_resource_post_error (resource,
                          WL_DISPLAY_ERROR_NO_MEMORY,
                          "User requested that we kill you. Sorry. Don't take it too personally.");
}

static void
meta_window_wayland_focus (MetaWindow *window,
                           guint32     timestamp)
{
  if (meta_window_is_focusable (window))
    {
      meta_display_set_input_focus (window->display,
                                    window,
                                    FALSE,
                                    timestamp);
    }
}

static void
meta_window_wayland_configure (MetaWindowWayland              *wl_window,
                               MetaWaylandWindowConfiguration *configuration)
{
  MetaWindow *window = META_WINDOW (wl_window);

  meta_wayland_surface_configure_notify (window->surface, configuration);

  wl_window->pending_configurations =
    g_list_prepend (wl_window->pending_configurations, configuration);
}

static void
surface_state_changed (MetaWindow *window)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
  MetaWaylandWindowConfiguration *configuration;

  /* don't send notify when the window is being unmanaged */
  if (window->unmanaging)
    return;

  configuration =
    meta_wayland_window_configuration_new (wl_window->last_sent_x,
                                           wl_window->last_sent_y,
                                           wl_window->last_sent_width,
                                           wl_window->last_sent_height,
                                           wl_window->last_sent_geometry_scale,
                                           META_MOVE_RESIZE_STATE_CHANGED,
                                           wl_window->last_sent_gravity);

  meta_window_wayland_configure (wl_window, configuration);
}

static void
meta_window_wayland_grab_op_began (MetaWindow *window,
                                   MetaGrabOp  op)
{
  if (meta_grab_op_is_resizing (op))
    surface_state_changed (window);

  META_WINDOW_CLASS (meta_window_wayland_parent_class)->grab_op_began (window, op);
}

static void
meta_window_wayland_grab_op_ended (MetaWindow *window,
                                   MetaGrabOp  op)
{
  if (meta_grab_op_is_resizing (op))
    surface_state_changed (window);

  META_WINDOW_CLASS (meta_window_wayland_parent_class)->grab_op_ended (window, op);
}

static void
meta_window_wayland_move_resize_internal (MetaWindow                *window,
                                          MetaGravity                gravity,
                                          MetaRectangle              unconstrained_rect,
                                          MetaRectangle              constrained_rect,
                                          MetaRectangle              temporary_rect,
                                          int                        rel_x,
                                          int                        rel_y,
                                          MetaMoveResizeFlags        flags,
                                          MetaMoveResizeResultFlags *result)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
  gboolean can_move_now = FALSE;
  int configured_x;
  int configured_y;
  int configured_width;
  int configured_height;
  int geometry_scale;
  int new_x;
  int new_y;
  int new_buffer_x;
  int new_buffer_y;

  g_assert (window->frame == NULL);

  /* don't do anything if we're dropping the window, see #751847 */
  if (window->unmanaging)
    return;

  configured_x = constrained_rect.x;
  configured_y = constrained_rect.y;

  /* The scale the window is drawn in might change depending on what monitor it
   * is mainly on. Scale the configured rectangle to be in logical pixel
   * coordinate space so that we can have a scale independent size to pass
   * to the Wayland surface. */
  geometry_scale = meta_window_wayland_get_geometry_scale (window);

  if (flags & META_MOVE_RESIZE_UNMAXIMIZE &&
      !meta_window_is_fullscreen (window))
    {
      configured_width = 0;
      configured_height = 0;
    }
  else if (flags & META_MOVE_RESIZE_UNFULLSCREEN &&
           !meta_window_get_maximized (window) &&
           meta_window_get_tile_mode (window) == META_TILE_NONE)
    {
      configured_width = 0;
      configured_height = 0;
    }
  else
    {
      configured_width = constrained_rect.width;
      configured_height = constrained_rect.height;
    }

  /* For wayland clients, the size is completely determined by the client,
   * and while this allows to avoid some trickery with frames and the resulting
   * lagging, we also need to insist a bit when the constraints would apply
   * a different size than the client decides.
   *
   * Note that this is not generally a problem for normal toplevel windows (the
   * constraints don't see the size hints, or just change the position), but
   * it can be for maximized or fullscreen.
   */

  if (flags & META_MOVE_RESIZE_FORCE_MOVE)
    {
      can_move_now = TRUE;
    }
  else if (flags & META_MOVE_RESIZE_WAYLAND_FINISH_MOVE_RESIZE)
    {
      /* This is a call to wl_surface_commit(), ignore the constrained_rect and
       * update the real client size to match the buffer size.
       */

      if (window->rect.width != unconstrained_rect.width ||
          window->rect.height != unconstrained_rect.height)
        {
          *result |= META_MOVE_RESIZE_RESULT_RESIZED;
          window->rect.width = unconstrained_rect.width;
          window->rect.height = unconstrained_rect.height;
        }

      /* This is a commit of an attach. We should move the window to match the
       * new position the client wants. */
      can_move_now = TRUE;
      if (window->placement.state == META_PLACEMENT_STATE_CONSTRAINED_CONFIGURED)
        window->placement.state = META_PLACEMENT_STATE_CONSTRAINED_FINISHED;
    }
  else
    {
      if (window->placement.rule)
        {
          switch (window->placement.state)
            {
            case META_PLACEMENT_STATE_UNCONSTRAINED:
            case META_PLACEMENT_STATE_CONSTRAINED_CONFIGURED:
            case META_PLACEMENT_STATE_INVALIDATED:
              can_move_now = FALSE;
              break;
            case META_PLACEMENT_STATE_CONSTRAINED_PENDING:
              {
                if (flags & META_MOVE_RESIZE_PLACEMENT_CHANGED ||
                    rel_x != wl_window->last_sent_rel_x ||
                    rel_y != wl_window->last_sent_rel_y ||
                    constrained_rect.width != window->rect.width ||
                    constrained_rect.height != window->rect.height)
                  {
                    MetaWaylandWindowConfiguration *configuration;

                    configuration =
                      meta_wayland_window_configuration_new_relative (rel_x,
                                                                      rel_y,
                                                                      configured_width,
                                                                      configured_height,
                                                                      geometry_scale);
                    meta_window_wayland_configure (wl_window, configuration);

                    wl_window->last_sent_rel_x = rel_x;
                    wl_window->last_sent_rel_y = rel_y;

                    window->placement.state = META_PLACEMENT_STATE_CONSTRAINED_CONFIGURED;

                    can_move_now = FALSE;
                  }
                else
                  {
                    window->placement.state =
                      META_PLACEMENT_STATE_CONSTRAINED_FINISHED;

                    can_move_now = TRUE;
                  }
                break;
              }
            case META_PLACEMENT_STATE_CONSTRAINED_FINISHED:
              can_move_now = TRUE;
              break;
            }
        }
      else if (constrained_rect.width != window->rect.width ||
               constrained_rect.height != window->rect.height ||
               flags & META_MOVE_RESIZE_STATE_CHANGED)
        {
          MetaWaylandWindowConfiguration *configuration;

          /* If the constrained size is 1x1 and the unconstrained size is 0x0
           * it means that we are trying to resize a window where the client has
           * not yet committed a buffer. The 1x1 constrained size is a result of
           * how the constraints code works. Lets avoid trying to have the
           * client configure itself to draw on a 1x1 surface.
           *
           * We cannot guard against only an empty unconstrained_rect here,
           * because the client may have created a xdg surface without a buffer
           * attached and asked it to be maximized. In such case we should let
           * it know about the expected window geometry of a maximized window,
           * even though there is currently no buffer attached. */
          if (unconstrained_rect.width == 0 &&
              unconstrained_rect.height == 0 &&
              constrained_rect.width == 1 &&
              constrained_rect.height == 1)
            return;

          configuration =
            meta_wayland_window_configuration_new (configured_x,
                                                   configured_y,
                                                   configured_width,
                                                   configured_height,
                                                   geometry_scale,
                                                   flags,
                                                   gravity);
          meta_window_wayland_configure (wl_window, configuration);
          can_move_now = FALSE;
        }
      else
        {
          can_move_now = TRUE;
        }
    }

  wl_window->last_sent_x = configured_x;
  wl_window->last_sent_y = configured_y;
  wl_window->last_sent_width = configured_width;
  wl_window->last_sent_height = configured_height;
  wl_window->last_sent_geometry_scale = geometry_scale;
  wl_window->last_sent_gravity = gravity;

  if (can_move_now)
    {
      new_x = constrained_rect.x;
      new_y = constrained_rect.y;
    }
  else
    {
      new_x = temporary_rect.x;
      new_y = temporary_rect.y;

      wl_window->has_pending_state_change |=
        !!(flags & META_MOVE_RESIZE_STATE_CHANGED);
    }

  if (new_x != window->rect.x || new_y != window->rect.y)
    {
      *result |= META_MOVE_RESIZE_RESULT_MOVED;
      window->rect.x = new_x;
      window->rect.y = new_y;
    }

  if (window->placement.rule &&
      window->placement.state == META_PLACEMENT_STATE_CONSTRAINED_FINISHED)
    {
      window->placement.current.rel_x = rel_x;
      window->placement.current.rel_y = rel_y;
    }

  new_buffer_x = new_x - window->custom_frame_extents.left;
  new_buffer_y = new_y - window->custom_frame_extents.top;

  if (new_buffer_x != window->buffer_rect.x ||
      new_buffer_y != window->buffer_rect.y)
    {
      *result |= META_MOVE_RESIZE_RESULT_MOVED;
      window->buffer_rect.x = new_buffer_x;
      window->buffer_rect.y = new_buffer_y;
    }

  if (can_move_now &&
      flags & META_MOVE_RESIZE_WAYLAND_STATE_CHANGED)
    *result |= META_MOVE_RESIZE_RESULT_STATE_CHANGED;
}

static void
scale_size (int  *width,
            int  *height,
            float scale)
{
  if (*width < G_MAXINT)
    {
      float new_width = (*width * scale);
      *width = (int) MIN (new_width, G_MAXINT);
    }

  if (*height < G_MAXINT)
    {
      float new_height = (*height * scale);
      *height = (int) MIN (new_height, G_MAXINT);
    }
}

static void
scale_rect_size (MetaRectangle *rect,
                 float          scale)
{
  scale_size (&rect->width, &rect->height, scale);
}

static void
meta_window_wayland_update_main_monitor (MetaWindow                   *window,
                                         MetaWindowUpdateMonitorFlags  flags)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaWindow *toplevel_window;
  MetaLogicalMonitor *from;
  MetaLogicalMonitor *to;
  MetaLogicalMonitor *scaled_new;
  float from_scale, to_scale;
  float scale;
  MetaRectangle rect;

  from = window->monitor;

  /* If the window is not a toplevel window (i.e. it's a popup window) just use
   * the monitor of the toplevel. */
  toplevel_window = meta_wayland_surface_get_toplevel_window (window->surface);
  if (toplevel_window != window)
    {
      meta_window_update_monitor (toplevel_window, flags);
      window->monitor = toplevel_window->monitor;
      return;
    }

  /* Require both the current and the new monitor would be the new main monitor,
   * even given the resulting scale the window would end up having. This is
   * needed to avoid jumping back and forth between the new and the old, since
   * changing main monitor may cause the window to be resized so that it no
   * longer have that same new main monitor. */
  to = meta_window_calculate_main_logical_monitor (window);

  if (from == to)
    return;

  if (from == NULL || to == NULL)
    {
      window->monitor = to;
      return;
    }

  if (flags & META_WINDOW_UPDATE_MONITOR_FLAGS_FORCE)
    {
      window->monitor = to;
      return;
    }

  from_scale = meta_logical_monitor_get_scale (from);
  to_scale = meta_logical_monitor_get_scale (to);

  if (from_scale == to_scale)
    {
      window->monitor = to;
      return;
    }

  if (meta_is_stage_views_scaled ())
    {
      window->monitor = to;
      return;
    }

  /* To avoid a window alternating between two main monitors because scaling
   * changes the main monitor, wait until both the current and the new scale
   * will result in the same main monitor. */
  scale = to_scale / from_scale;
  rect = window->rect;
  scale_rect_size (&rect, scale);
  scaled_new =
    meta_monitor_manager_get_logical_monitor_from_rect (monitor_manager, &rect);
  if (to != scaled_new)
    return;

  window->monitor = to;
}

static void
meta_window_wayland_main_monitor_changed (MetaWindow               *window,
                                          const MetaLogicalMonitor *old)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
  int old_geometry_scale = wl_window->geometry_scale;
  int geometry_scale;
  float scale_factor;
  MetaWaylandSurface *surface;

  if (!window->monitor)
    return;

  geometry_scale = meta_window_wayland_get_geometry_scale (window);

  /* This function makes sure that window geometry, window actor geometry and
   * surface actor geometry gets set according the old and current main monitor
   * scale. If there either is no past or current main monitor, or if the scale
   * didn't change, there is nothing to do. */
  if (old == NULL ||
      window->monitor == NULL ||
      old_geometry_scale == geometry_scale)
    return;

  /* MetaWindow keeps its rectangles in the physical pixel coordinate space.
   * When the main monitor of a window changes, it can cause the corresponding
   * window surfaces to be scaled given the monitor scale, so we need to scale
   * the rectangles in MetaWindow accordingly. */

  scale_factor = (float) geometry_scale / old_geometry_scale;

  /* Window size. */
  scale_rect_size (&window->rect, scale_factor);
  scale_rect_size (&window->unconstrained_rect, scale_factor);
  scale_rect_size (&window->saved_rect, scale_factor);
  scale_size (&window->size_hints.min_width, &window->size_hints.min_height, scale_factor);
  scale_size (&window->size_hints.max_width, &window->size_hints.max_height, scale_factor);

  /* Window geometry offset (XXX: Need a better place, see
   * meta_window_wayland_finish_move_resize). */
  window->custom_frame_extents.left =
    (int)(scale_factor * window->custom_frame_extents.left);
  window->custom_frame_extents.top =
    (int)(scale_factor * window->custom_frame_extents.top);

  /* Buffer rect. */
  scale_rect_size (&window->buffer_rect, scale_factor);
  window->buffer_rect.x =
    window->rect.x - window->custom_frame_extents.left;
  window->buffer_rect.y =
    window->rect.y - window->custom_frame_extents.top;

  meta_compositor_sync_window_geometry (window->display->compositor,
                                        window,
                                        TRUE);

  surface = window->surface;
  if (surface)
    {
      MetaWaylandActorSurface *actor_surface =
        META_WAYLAND_ACTOR_SURFACE (surface->role);

      meta_wayland_actor_surface_sync_actor_state (actor_surface);
    }

  set_geometry_scale_for_window (wl_window, geometry_scale);
  meta_window_emit_size_changed (window);
}

static uint32_t
meta_window_wayland_get_client_pid (MetaWindow *window)
{
  MetaWaylandSurface *surface = window->surface;
  struct wl_resource *resource = surface->resource;
  pid_t pid;

  wl_client_get_credentials (wl_resource_get_client (resource), &pid, NULL, NULL);
  return (uint32_t)pid;
}

static void
appears_focused_changed (GObject    *object,
                         GParamSpec *pspec,
                         gpointer    user_data)
{
  MetaWindow *window = META_WINDOW (object);

  if (window->placement.rule)
    return;

  surface_state_changed (window);
}

static void
on_window_shown (MetaWindow *window)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
  gboolean has_been_shown;

  has_been_shown = wl_window->has_been_shown;
  wl_window->has_been_shown = TRUE;

  if (!has_been_shown)
    meta_compositor_sync_updates_frozen (window->display->compositor, window);
}

static void
meta_window_wayland_init (MetaWindowWayland *wl_window)
{
  MetaWindow *window = META_WINDOW (wl_window);

  wl_window->geometry_scale = 1;

  g_signal_connect (window, "notify::appears-focused",
                    G_CALLBACK (appears_focused_changed), NULL);
  g_signal_connect (window, "shown",
                    G_CALLBACK (on_window_shown), NULL);
}

static void
meta_window_wayland_force_restore_shortcuts (MetaWindow         *window,
                                             ClutterInputDevice *source)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();

  meta_wayland_compositor_restore_shortcuts (compositor, source);
}

static gboolean
meta_window_wayland_shortcuts_inhibited (MetaWindow         *window,
                                         ClutterInputDevice *source)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();

  return meta_wayland_compositor_is_shortcuts_inhibited (compositor, source);
}

static gboolean
meta_window_wayland_is_focusable (MetaWindow *window)
{
  return window->input;
}

static gboolean
meta_window_wayland_can_ping (MetaWindow *window)
{
  return TRUE;
}

static gboolean
meta_window_wayland_is_stackable (MetaWindow *window)
{
  return meta_wayland_surface_get_buffer (window->surface) != NULL;
}

static gboolean
meta_window_wayland_are_updates_frozen (MetaWindow *window)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);

  return !wl_window->has_been_shown;
}

static gboolean
meta_window_wayland_is_focus_async (MetaWindow *window)
{
  return FALSE;
}

static MetaStackLayer
meta_window_wayland_calculate_layer (MetaWindow *window)
{
  return meta_window_get_default_layer (window);
}

static void
meta_window_wayland_map (MetaWindow *window)
{
}

static void
meta_window_wayland_unmap (MetaWindow *window)
{
}

static void
meta_window_wayland_finalize (GObject *object)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (object);

  g_list_free_full (wl_window->pending_configurations,
                    (GDestroyNotify) meta_wayland_window_configuration_free);

  G_OBJECT_CLASS (meta_window_wayland_parent_class)->finalize (object);
}

static void
meta_window_wayland_class_init (MetaWindowWaylandClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaWindowClass *window_class = META_WINDOW_CLASS (klass);

  object_class->finalize = meta_window_wayland_finalize;

  window_class->manage = meta_window_wayland_manage;
  window_class->unmanage = meta_window_wayland_unmanage;
  window_class->ping = meta_window_wayland_ping;
  window_class->delete = meta_window_wayland_delete;
  window_class->kill = meta_window_wayland_kill;
  window_class->focus = meta_window_wayland_focus;
  window_class->grab_op_began = meta_window_wayland_grab_op_began;
  window_class->grab_op_ended = meta_window_wayland_grab_op_ended;
  window_class->move_resize_internal = meta_window_wayland_move_resize_internal;
  window_class->update_main_monitor = meta_window_wayland_update_main_monitor;
  window_class->main_monitor_changed = meta_window_wayland_main_monitor_changed;
  window_class->get_client_pid = meta_window_wayland_get_client_pid;
  window_class->force_restore_shortcuts = meta_window_wayland_force_restore_shortcuts;
  window_class->shortcuts_inhibited = meta_window_wayland_shortcuts_inhibited;
  window_class->is_focusable = meta_window_wayland_is_focusable;
  window_class->is_stackable = meta_window_wayland_is_stackable;
  window_class->can_ping = meta_window_wayland_can_ping;
  window_class->are_updates_frozen = meta_window_wayland_are_updates_frozen;
  window_class->calculate_layer = meta_window_wayland_calculate_layer;
  window_class->map = meta_window_wayland_map;
  window_class->unmap = meta_window_wayland_unmap;
  window_class->is_focus_async = meta_window_wayland_is_focus_async;
}

MetaWindow *
meta_window_wayland_new (MetaDisplay        *display,
                         MetaWaylandSurface *surface)
{
  XWindowAttributes attrs = { 0 };
  MetaWindowWayland *wl_window;
  MetaWindow *window;

  /*
   * Set attributes used by _meta_window_shared_new, don't bother trying to fake
   * X11 window attributes with the rest, since they'll be ignored anyway.
   */
  attrs.x = 0;
  attrs.y = 0;
  attrs.width = 0;
  attrs.height = 0;
  attrs.depth = 24;
  attrs.visual = NULL;
  attrs.map_state = IsUnmapped;
  attrs.override_redirect = False;

  window = _meta_window_shared_new (display,
                                    META_WINDOW_CLIENT_TYPE_WAYLAND,
                                    surface,
                                    None,
                                    WithdrawnState,
                                    META_COMP_EFFECT_CREATE,
                                    &attrs);

  wl_window = META_WINDOW_WAYLAND (window);
  set_geometry_scale_for_window (wl_window, wl_window->geometry_scale);

  return window;
}

MetaWaylandWindowConfiguration *
meta_window_wayland_peek_configuration (MetaWindowWayland *wl_window,
                                        uint32_t           serial)
{
  GList *l;

  for (l = wl_window->pending_configurations; l; l = l->next)
    {
      MetaWaylandWindowConfiguration *configuration = l->data;

      if (configuration->serial == serial)
        return configuration;
    }

  return NULL;
}

static MetaWaylandWindowConfiguration *
acquire_acked_configuration (MetaWindowWayland       *wl_window,
                             MetaWaylandSurfaceState *pending)
{
  GList *l;

  if (!pending->has_acked_configure_serial)
    return NULL;

  for (l = wl_window->pending_configurations; l; l = l->next)
    {
      MetaWaylandWindowConfiguration *configuration = l->data;
      GList *tail;
      gboolean is_matching_configuration;

      if (configuration->serial > pending->acked_configure_serial)
        continue;

      tail = l;

      if (tail->prev)
        {
          tail->prev->next = NULL;
          tail->prev = NULL;
        }
      else
        {
          wl_window->pending_configurations = NULL;
        }

      is_matching_configuration =
        configuration->serial == pending->acked_configure_serial;

      if (is_matching_configuration)
        tail = g_list_delete_link (tail, l);
      g_list_free_full (tail,
                        (GDestroyNotify) meta_wayland_window_configuration_free);

      if (is_matching_configuration)
        return configuration;
      else
        return NULL;
    }

  return NULL;
}

int
meta_window_wayland_get_geometry_scale (MetaWindow *window)
{
  if (!window->monitor)
    return 1;

  return get_window_geometry_scale_for_logical_monitor (window->monitor);
}

static void
calculate_offset (MetaWaylandWindowConfiguration *configuration,
                  MetaRectangle                  *geometry,
                  MetaRectangle                  *rect)
{
  int offset_x;
  int offset_y;

  rect->x = configuration->x;
  rect->y = configuration->y;

  offset_x = configuration->width - geometry->width;
  offset_y = configuration->height - geometry->height;
  switch (configuration->gravity)
    {
    case META_GRAVITY_SOUTH:
    case META_GRAVITY_SOUTH_WEST:
      rect->y += offset_y;
      break;
    case META_GRAVITY_EAST:
    case META_GRAVITY_NORTH_EAST:
      rect->x += offset_x;
      break;
    case META_GRAVITY_SOUTH_EAST:
      rect->x += offset_x;
      rect->y += offset_y;
      break;
    default:
      break;
    }
}

/**
 * meta_window_move_resize_wayland:
 *
 * Complete a resize operation from a wayland client.
 */
void
meta_window_wayland_finish_move_resize (MetaWindow              *window,
                                        MetaRectangle            new_geom,
                                        MetaWaylandSurfaceState *pending)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (window);
  MetaDisplay *display = window->display;
  int dx, dy;
  int geometry_scale;
  MetaGravity gravity;
  MetaRectangle rect;
  MetaMoveResizeFlags flags;
  MetaWaylandWindowConfiguration *acked_configuration;
  gboolean is_window_being_resized;

  /* new_geom is in the logical pixel coordinate space, but MetaWindow wants its
   * rects to represent what in turn will end up on the stage, i.e. we need to
   * scale new_geom to physical pixels given what buffer scale and texture scale
   * is in use. */

  geometry_scale = meta_window_wayland_get_geometry_scale (window);
  new_geom.x *= geometry_scale;
  new_geom.y *= geometry_scale;
  new_geom.width *= geometry_scale;
  new_geom.height *= geometry_scale;

  /* The (dx, dy) offset is also in logical pixel coordinate space and needs
   * to be scaled in the same way as new_geom. */
  dx = pending->dx * geometry_scale;
  dy = pending->dy * geometry_scale;

  /* XXX: Find a better place to store the window geometry offsets. */
  window->custom_frame_extents.left = new_geom.x;
  window->custom_frame_extents.top = new_geom.y;

  flags = META_MOVE_RESIZE_WAYLAND_FINISH_MOVE_RESIZE;

  acked_configuration = acquire_acked_configuration (wl_window, pending);

  /* x/y are ignored when we're doing interactive resizing */
  is_window_being_resized = (meta_grab_op_is_resizing (display->grab_op) &&
                             display->grab_window == window);

  if (!is_window_being_resized)
    {
      if (acked_configuration)
        {
          if (window->placement.rule)
            {
              MetaWindow *parent;

              parent = meta_window_get_transient_for (window);
              rect.x = parent->rect.x + acked_configuration->rel_x;
              rect.y = parent->rect.y + acked_configuration->rel_y;
            }
          else
            {
              calculate_offset (acked_configuration, &new_geom, &rect);
            }
        }
      else
        {
          rect.x = window->rect.x;
          rect.y = window->rect.y;
        }

      rect.x += dx;
      rect.y += dy;
    }
  else
    {
      if (acked_configuration)
        calculate_offset (acked_configuration, &new_geom, &rect);
    }

  if (rect.x != window->rect.x || rect.y != window->rect.y)
    flags |= META_MOVE_RESIZE_MOVE_ACTION;

  if (wl_window->has_pending_state_change && acked_configuration)
    {
      flags |= META_MOVE_RESIZE_WAYLAND_STATE_CHANGED;
      wl_window->has_pending_state_change = FALSE;
    }

  rect.width = new_geom.width;
  rect.height = new_geom.height;

  if (rect.width != window->rect.width || rect.height != window->rect.height)
    flags |= META_MOVE_RESIZE_RESIZE_ACTION;

  if (window->display->grab_window == window)
    gravity = meta_resize_gravity_from_grab_op (window->display->grab_op);
  else
    gravity = META_GRAVITY_STATIC;
  meta_window_move_resize_internal (window, flags, gravity, rect);

  g_clear_pointer (&acked_configuration, meta_wayland_window_configuration_free);
}

void
meta_window_wayland_place_relative_to (MetaWindow *window,
                                       MetaWindow *other,
                                       int         x,
                                       int         y)
{
  int geometry_scale;

  /* If there is no monitor, we can't position the window reliably. */
  if (!other->monitor)
    return;

  geometry_scale = meta_window_wayland_get_geometry_scale (other);
  meta_window_move_frame (window, FALSE,
                          other->buffer_rect.x + (x * geometry_scale),
                          other->buffer_rect.y + (y * geometry_scale));
  window->placed = TRUE;
}

void
meta_window_place_with_placement_rule (MetaWindow        *window,
                                       MetaPlacementRule *placement_rule)
{
  gboolean first_placement;

  first_placement = !window->placement.rule;

  g_clear_pointer (&window->placement.rule, g_free);
  window->placement.rule = g_new0 (MetaPlacementRule, 1);
  *window->placement.rule = *placement_rule;

  window->unconstrained_rect.x = window->rect.x;
  window->unconstrained_rect.y = window->rect.y;
  window->unconstrained_rect.width = placement_rule->width;
  window->unconstrained_rect.height = placement_rule->height;

  window->calc_placement = first_placement;
  meta_window_move_resize_internal (window,
                                    (META_MOVE_RESIZE_MOVE_ACTION |
                                     META_MOVE_RESIZE_RESIZE_ACTION |
                                     META_MOVE_RESIZE_PLACEMENT_CHANGED),
                                    META_GRAVITY_NORTH_WEST,
                                    window->unconstrained_rect);
  window->calc_placement = FALSE;
}

void
meta_window_update_placement_rule (MetaWindow        *window,
                                   MetaPlacementRule *placement_rule)
{
  window->placement.state = META_PLACEMENT_STATE_INVALIDATED;
  meta_window_place_with_placement_rule (window, placement_rule);
}

void
meta_window_wayland_set_min_size (MetaWindow *window,
                                  int         width,
                                  int         height)
{
  gint64 new_width, new_height;
  float scale;

  meta_topic (META_DEBUG_GEOMETRY, "Window %s sets min size %d x %d\n",
              window->desc, width, height);

  if (width == 0 && height == 0)
    {
      window->size_hints.min_width = 0;
      window->size_hints.min_height = 0;
      window->size_hints.flags &= ~PMinSize;

      return;
    }

  scale = (float) meta_window_wayland_get_geometry_scale (window);
  scale_size (&width, &height, scale);

  new_width = width + (window->custom_frame_extents.left +
                       window->custom_frame_extents.right);
  new_height = height + (window->custom_frame_extents.top +
                         window->custom_frame_extents.bottom);

  window->size_hints.min_width = (int) MIN (new_width, G_MAXINT);
  window->size_hints.min_height = (int) MIN (new_height, G_MAXINT);
  window->size_hints.flags |= PMinSize;
}

void
meta_window_wayland_set_max_size (MetaWindow *window,
                                  int         width,
                                  int         height)

{
  gint64 new_width, new_height;
  float scale;

  meta_topic (META_DEBUG_GEOMETRY, "Window %s sets max size %d x %d\n",
              window->desc, width, height);

  if (width == 0 && height == 0)
    {
      window->size_hints.max_width = G_MAXINT;
      window->size_hints.max_height = G_MAXINT;
      window->size_hints.flags &= ~PMaxSize;

      return;
    }

  scale = (float) meta_window_wayland_get_geometry_scale (window);
  scale_size (&width, &height, scale);

  new_width = width + (window->custom_frame_extents.left +
                       window->custom_frame_extents.right);
  new_height = height + (window->custom_frame_extents.top +
                         window->custom_frame_extents.bottom);

  window->size_hints.max_width = (int) ((new_width > 0 && new_width < G_MAXINT) ?
                                        new_width : G_MAXINT);
  window->size_hints.max_height = (int)  ((new_height > 0 && new_height < G_MAXINT) ?
                                          new_height : G_MAXINT);
  window->size_hints.flags |= PMaxSize;
}

void
meta_window_wayland_get_min_size (MetaWindow *window,
                                  int        *width,
                                  int        *height)
{
  gint64 current_width, current_height;
  float scale;

  if (!(window->size_hints.flags & PMinSize))
    {
      /* Zero means unlimited */
      *width = 0;
      *height = 0;

      return;
    }

  current_width = window->size_hints.min_width -
                  (window->custom_frame_extents.left +
                   window->custom_frame_extents.right);
  current_height = window->size_hints.min_height -
                   (window->custom_frame_extents.top +
                    window->custom_frame_extents.bottom);

  *width = MAX (current_width, 0);
  *height = MAX (current_height, 0);

  scale = 1.0 / (float) meta_window_wayland_get_geometry_scale (window);
  scale_size (width, height, scale);
}

void
meta_window_wayland_get_max_size (MetaWindow *window,
                                  int        *width,
                                  int        *height)
{
  gint64 current_width = 0;
  gint64 current_height = 0;
  float scale;

  if (!(window->size_hints.flags & PMaxSize))
    {
      /* Zero means unlimited */
      *width = 0;
      *height = 0;

      return;
    }

  if (window->size_hints.max_width < G_MAXINT)
    current_width = window->size_hints.max_width -
                    (window->custom_frame_extents.left +
                     window->custom_frame_extents.right);

  if (window->size_hints.max_height < G_MAXINT)
    current_height = window->size_hints.max_height -
                     (window->custom_frame_extents.top +
                      window->custom_frame_extents.bottom);

  *width = CLAMP (current_width, 0, G_MAXINT);
  *height = CLAMP (current_height, 0, G_MAXINT);

  scale = 1.0 / (float) meta_window_wayland_get_geometry_scale (window);
  scale_size (width, height, scale);
}
