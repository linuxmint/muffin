/*
 * Copyright (C) 2024 Linux Mint
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

#include "wayland/meta-wayland-layer-shell.h"

#include "backends/meta-logical-monitor.h"
#include "compositor/compositor-private.h"
#include "compositor/meta-surface-actor-wayland.h"
#include "core/meta-workspace-manager-private.h"
#include "core/workspace-private.h"
#include "meta/boxes.h"
#include "wayland/meta-wayland-data-device.h"
#include "wayland/meta-wayland-outputs.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-surface.h"
#include "wayland/meta-wayland-versions.h"
#include "wayland/meta-wayland-xdg-shell.h"

#include "wlr-layer-shell-unstable-v1-server-protocol.h"

/* Layer shell global */
struct _MetaWaylandLayerShell
{
  GObject parent;
  GList *shell_resources;
  GList *layer_surfaces;
  MetaWaylandCompositor *compositor;
  gulong workareas_changed_handler_id;
};

G_DEFINE_TYPE (MetaWaylandLayerShell, meta_wayland_layer_shell, G_TYPE_OBJECT)

/* Layer surface state (pending/current) */
typedef struct
{
  uint32_t anchor;
  int32_t exclusive_zone;
  struct {
    int32_t top;
    int32_t right;
    int32_t bottom;
    int32_t left;
  } margin;
  uint32_t desired_width;
  uint32_t desired_height;
  MetaLayerShellLayer layer;
  uint32_t keyboard_interactivity;
} MetaWaylandLayerSurfaceState;

/* Layer surface */
struct _MetaWaylandLayerSurface
{
  MetaWaylandActorSurface parent;

  struct wl_resource *resource;
  MetaWaylandOutput *output;
  char *namespace;
  MetaLayerShellLayer initial_layer;

  MetaWaylandLayerSurfaceState current;
  MetaWaylandLayerSurfaceState pending;

  uint32_t configure_serial;
  gboolean configured;
  gboolean mapped;
};

G_DEFINE_TYPE (MetaWaylandLayerSurface, meta_wayland_layer_surface,
               META_TYPE_WAYLAND_ACTOR_SURFACE)

enum
{
  PROP_0,
  PROP_OUTPUT,
  PROP_NAMESPACE,
  PROP_INITIAL_LAYER,
  N_PROPS
};

static GParamSpec *layer_surface_props[N_PROPS] = { NULL, };

static void meta_wayland_layer_surface_send_configure (MetaWaylandLayerSurface *layer_surface);

static MetaWaylandLayerShell *
meta_wayland_layer_shell_from_compositor (MetaWaylandCompositor *compositor)
{
  return g_object_get_data (G_OBJECT (compositor), "-meta-wayland-layer-shell");
}

static void
on_workareas_changed (MetaDisplay           *display,
                      MetaWaylandLayerShell *layer_shell)
{
  meta_wayland_layer_shell_on_workarea_changed (layer_shell->compositor);
}

static void
meta_wayland_layer_shell_ensure_signal_connected (MetaWaylandLayerShell *layer_shell)
{
  MetaDisplay *display;

  if (layer_shell->workareas_changed_handler_id != 0)
    return;

  display = meta_get_display ();
  if (!display)
    return;

  layer_shell->workareas_changed_handler_id =
    g_signal_connect (display, "workareas-changed",
                      G_CALLBACK (on_workareas_changed),
                      layer_shell);
}

static MetaSide
get_strut_side_from_anchor (uint32_t anchor)
{
  gboolean anchored_top = (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP);
  gboolean anchored_bottom = (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM);
  gboolean anchored_left = (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
  gboolean anchored_right = (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);

  if (anchored_top && !anchored_bottom)
    return META_SIDE_TOP;
  else if (anchored_bottom && !anchored_top)
    return META_SIDE_BOTTOM;
  else if (anchored_left && !anchored_right)
    return META_SIDE_LEFT;
  else if (anchored_right && !anchored_left)
    return META_SIDE_RIGHT;
  else
    return -1;
}

/**
 * Calculate the total exclusive zone offset from OTHER layer surfaces
 * on the same edge that were created before this surface.
 *
 * Surfaces are stored in reverse creation order (newest first), so
 * surfaces that appear AFTER this one in the list were created earlier
 * and should be positioned closer to the edge.
 */
static int
get_other_layer_surfaces_exclusive_offset (MetaWaylandLayerSurface *layer_surface,
                                           MetaWaylandCompositor   *compositor,
                                           MetaSide                 side)
{
  MetaWaylandLayerShell *layer_shell;
  GList *l;
  int offset = 0;
  gboolean found_self = FALSE;

  layer_shell = meta_wayland_layer_shell_from_compositor (compositor);
  if (!layer_shell)
    return 0;

  /* Iterate through all layer surfaces. Surfaces after this one in the list
   * were created earlier and are "before" us for stacking purposes. */
  for (l = layer_shell->layer_surfaces; l; l = l->next)
    {
      MetaWaylandLayerSurface *other = l->data;

      if (other == layer_surface)
        {
          found_self = TRUE;
          continue;
        }

      /* Only count surfaces that come after us (created before us) */
      if (!found_self)
        continue;

      /* Only count mapped surfaces with exclusive_zone > 0 on the same edge */
      if (!other->mapped || other->current.exclusive_zone <= 0)
        continue;

      MetaSide other_side = get_strut_side_from_anchor (other->current.anchor);
      if (other_side != side)
        continue;

      /* Add this surface's exclusive zone (plus its margin on this edge) */
      switch (side)
        {
        case META_SIDE_TOP:
          offset += other->current.exclusive_zone + other->current.margin.top;
          break;
        case META_SIDE_BOTTOM:
          offset += other->current.exclusive_zone + other->current.margin.bottom;
          break;
        case META_SIDE_LEFT:
          offset += other->current.exclusive_zone + other->current.margin.left;
          break;
        case META_SIDE_RIGHT:
          offset += other->current.exclusive_zone + other->current.margin.right;
          break;
        default:
          break;
        }
    }

  return offset;
}

static gboolean
get_layer_surface_bounds (MetaWaylandLayerSurface *layer_surface,
                          MetaRectangle           *output_rect,
                          MetaRectangle           *usable_area)
{
  MetaWaylandLayerSurfaceState *state = &layer_surface->current;
  MetaWaylandSurface *surface;
  MetaLogicalMonitor *logical_monitor = NULL;
  MetaRectangle monitor_rect = { 0, 0, 0, 0 };

  surface = meta_wayland_surface_role_get_surface (META_WAYLAND_SURFACE_ROLE (layer_surface));

  if (layer_surface->output && layer_surface->output->logical_monitor)
    {
      logical_monitor = layer_surface->output->logical_monitor;
      monitor_rect = logical_monitor->rect;
    }
  else
    {
      MetaBackend *backend = meta_get_backend ();
      MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
      MetaLogicalMonitor *primary = meta_monitor_manager_get_primary_logical_monitor (monitor_manager);

      if (primary)
        {
          logical_monitor = primary;
          monitor_rect = primary->rect;
        }
      else
        {
          monitor_rect.width = 1920;
          monitor_rect.height = 1080;
        }
    }

  *output_rect = monitor_rect;

  if (state->exclusive_zone == -1)
    {
      /* Full output, ignore all panels */
      *usable_area = monitor_rect;
    }
  else if (logical_monitor)
    {
      MetaDisplay *display = meta_get_display ();
      MetaWorkspaceManager *workspace_manager;
      MetaWorkspace *workspace;

      *usable_area = monitor_rect;
      if (display)
        {
          workspace_manager = meta_display_get_workspace_manager (display);
          if (workspace_manager)
            {
              workspace = meta_workspace_manager_get_active_workspace (workspace_manager);
              if (workspace)
                {
                  if (state->exclusive_zone > 0)
                    {
                      MetaSide side;
                      int other_surfaces_offset;

                      /* For surfaces that claim exclusive space, use workarea
                       * excluding layer-shell struts to avoid circular dependency
                       * (surface's own strut affecting its position). */
                      meta_workspace_get_work_area_for_logical_monitor_excluding_layer_shell (
                          workspace, logical_monitor, usable_area);

                      /* Also account for other layer surfaces on the same edge
                       * that were created before this one. */
                      side = get_strut_side_from_anchor (state->anchor);
                      if (side != (MetaSide) -1 && surface && surface->compositor)
                        {
                          other_surfaces_offset =
                            get_other_layer_surfaces_exclusive_offset (layer_surface,
                                                                       surface->compositor,
                                                                       side);
                          switch (side)
                            {
                            case META_SIDE_TOP:
                              usable_area->y += other_surfaces_offset;
                              usable_area->height -= other_surfaces_offset;
                              break;
                            case META_SIDE_BOTTOM:
                              usable_area->height -= other_surfaces_offset;
                              break;
                            case META_SIDE_LEFT:
                              usable_area->x += other_surfaces_offset;
                              usable_area->width -= other_surfaces_offset;
                              break;
                            case META_SIDE_RIGHT:
                              usable_area->width -= other_surfaces_offset;
                              break;
                            default:
                              break;
                            }
                        }
                    }
                  else
                    {
                      /* For surfaces with exclusive_zone == 0, use full workarea
                       * (they respect all panels including other layer surfaces). */
                      meta_workspace_get_work_area_for_logical_monitor (workspace,
                                                                         logical_monitor,
                                                                         usable_area);
                    }
                  return TRUE;
                }
            }
        }
    }
  else
    {
      *usable_area = monitor_rect;
    }

  return TRUE;
}

static MetaStrut *
meta_wayland_layer_surface_create_strut (MetaWaylandLayerSurface *layer_surface)
{
  MetaWaylandLayerSurfaceState *state = &layer_surface->current;
  MetaRectangle output_rect = { 0, 0, 0, 0 };
  MetaRectangle usable_area = { 0, 0, 0, 0 };
  MetaStrut *strut;
  MetaSide side;
  int offset_top, offset_bottom, offset_left, offset_right;

  if (state->exclusive_zone <= 0 || !layer_surface->mapped)
    return NULL;

  side = get_strut_side_from_anchor (state->anchor);
  if (side == (MetaSide) -1)
    return NULL;

  get_layer_surface_bounds (layer_surface, &output_rect, &usable_area);

  /* Calculate how much the workarea is offset from output on each edge
   * (this accounts for Cinnamon panels via builtin_struts). */
  offset_top = usable_area.y - output_rect.y;
  offset_bottom = (output_rect.y + output_rect.height) -
                  (usable_area.y + usable_area.height);
  offset_left = usable_area.x - output_rect.x;
  offset_right = (output_rect.x + output_rect.width) -
                 (usable_area.x + usable_area.width);

  strut = g_new0 (MetaStrut, 1);
  strut->side = side;

  /* Create strut from OUTPUT edge, extending to cover both the existing
   * workarea offset (Cinnamon panels) AND this surface's exclusive zone.
   * This matches how builtin_struts are processed. */
  switch (side)
    {
    case META_SIDE_TOP:
      strut->rect.x = output_rect.x;
      strut->rect.y = output_rect.y;
      strut->rect.width = output_rect.width;
      strut->rect.height = offset_top + state->exclusive_zone + state->margin.top;
      break;
    case META_SIDE_BOTTOM:
      strut->rect.x = output_rect.x;
      strut->rect.height = offset_bottom + state->exclusive_zone + state->margin.bottom;
      strut->rect.y = output_rect.y + output_rect.height - strut->rect.height;
      strut->rect.width = output_rect.width;
      break;
    case META_SIDE_LEFT:
      strut->rect.x = output_rect.x;
      strut->rect.y = output_rect.y;
      strut->rect.width = offset_left + state->exclusive_zone + state->margin.left;
      strut->rect.height = output_rect.height;
      break;
    case META_SIDE_RIGHT:
      strut->rect.y = output_rect.y;
      strut->rect.width = offset_right + state->exclusive_zone + state->margin.right;
      strut->rect.x = output_rect.x + output_rect.width - strut->rect.width;
      strut->rect.height = output_rect.height;
      break;
    default:
      g_free (strut);
      return NULL;
    }

  return strut;
}

static void
layer_surface_set_size (struct wl_client   *client,
                        struct wl_resource *resource,
                        uint32_t            width,
                        uint32_t            height)
{
  MetaWaylandLayerSurface *layer_surface = wl_resource_get_user_data (resource);

  layer_surface->pending.desired_width = width;
  layer_surface->pending.desired_height = height;
}

static void
layer_surface_set_anchor (struct wl_client   *client,
                          struct wl_resource *resource,
                          uint32_t            anchor)
{
  MetaWaylandLayerSurface *layer_surface = wl_resource_get_user_data (resource);

  if (anchor > (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT))
    {
      wl_resource_post_error (resource,
                              ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_ANCHOR,
                              "Invalid anchor value");
      return;
    }

  layer_surface->pending.anchor = anchor;
}

static void
layer_surface_set_exclusive_zone (struct wl_client   *client,
                                  struct wl_resource *resource,
                                  int32_t             zone)
{
  MetaWaylandLayerSurface *layer_surface = wl_resource_get_user_data (resource);

  layer_surface->pending.exclusive_zone = zone;
}

static void
layer_surface_set_margin (struct wl_client   *client,
                          struct wl_resource *resource,
                          int32_t             top,
                          int32_t             right,
                          int32_t             bottom,
                          int32_t             left)
{
  MetaWaylandLayerSurface *layer_surface = wl_resource_get_user_data (resource);

  layer_surface->pending.margin.top = top;
  layer_surface->pending.margin.right = right;
  layer_surface->pending.margin.bottom = bottom;
  layer_surface->pending.margin.left = left;
}

static void
layer_surface_set_keyboard_interactivity (struct wl_client   *client,
                                          struct wl_resource *resource,
                                          uint32_t            keyboard_interactivity)
{
  MetaWaylandLayerSurface *layer_surface = wl_resource_get_user_data (resource);

  if (keyboard_interactivity > ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND)
    {
      wl_resource_post_error (resource,
                              ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_KEYBOARD_INTERACTIVITY,
                              "Invalid keyboard interactivity value");
      return;
    }

  layer_surface->pending.keyboard_interactivity = keyboard_interactivity;
}

static void
layer_surface_get_popup (struct wl_client   *client,
                         struct wl_resource *resource,
                         struct wl_resource *popup_resource)
{
  MetaWaylandLayerSurface *layer_surface = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (META_WAYLAND_SURFACE_ROLE (layer_surface));
  MetaWaylandXdgPopup *xdg_popup;

  if (!popup_resource)
    {
      wl_resource_post_error (resource,
                              ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SURFACE_STATE,
                              "popup resource is NULL");
      return;
    }

  xdg_popup = wl_resource_get_user_data (popup_resource);
  if (!xdg_popup || !META_IS_WAYLAND_XDG_POPUP (xdg_popup))
    {
      wl_resource_post_error (resource,
                              ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SURFACE_STATE,
                              "popup is not a valid xdg_popup");
      return;
    }

  meta_wayland_xdg_popup_set_parent_surface (xdg_popup, surface);
}

static void
layer_surface_ack_configure (struct wl_client   *client,
                             struct wl_resource *resource,
                             uint32_t            serial)
{
  MetaWaylandLayerSurface *layer_surface = wl_resource_get_user_data (resource);

  layer_surface->configure_serial = serial;
  layer_surface->configured = TRUE;
}

static void
layer_surface_destroy (struct wl_client   *client,
                       struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
layer_surface_set_layer (struct wl_client   *client,
                         struct wl_resource *resource,
                         uint32_t            layer)
{
  MetaWaylandLayerSurface *layer_surface = wl_resource_get_user_data (resource);

  if (layer > ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY)
    {
      wl_resource_post_error (resource,
                              ZWLR_LAYER_SHELL_V1_ERROR_INVALID_LAYER,
                              "Invalid layer value");
      return;
    }

  layer_surface->pending.layer = layer;
}

static void
layer_surface_set_exclusive_edge (struct wl_client   *client,
                                  struct wl_resource *resource,
                                  uint32_t            edge)
{
  /* TODO: Implement exclusive edge */
}

static const struct zwlr_layer_surface_v1_interface layer_surface_interface = {
  layer_surface_set_size,
  layer_surface_set_anchor,
  layer_surface_set_exclusive_zone,
  layer_surface_set_margin,
  layer_surface_set_keyboard_interactivity,
  layer_surface_get_popup,
  layer_surface_ack_configure,
  layer_surface_destroy,
  layer_surface_set_layer,
  layer_surface_set_exclusive_edge,
};

static void
layer_surface_resource_destroyed (struct wl_resource *resource)
{
  MetaWaylandLayerSurface *layer_surface = wl_resource_get_user_data (resource);

  if (layer_surface)
    {
      layer_surface->resource = NULL;
      g_free (layer_surface->namespace);
      layer_surface->namespace = NULL;
    }
}

/* Calculate surface position based on anchor, margin, and output/workarea geometry */
static void
calculate_surface_position (MetaWaylandLayerSurface *layer_surface,
                            int                     *out_x,
                            int                     *out_y)
{
  MetaRectangle output_rect = { 0, 0, 0, 0 };
  MetaRectangle usable_area = { 0, 0, 0, 0 };
  MetaRectangle *bounds;
  MetaWaylandSurface *surface;
  int width, height;
  int x = 0, y = 0;
  uint32_t anchor;

  surface = meta_wayland_surface_role_get_surface (META_WAYLAND_SURFACE_ROLE (layer_surface));
  if (!surface || !surface->buffer_ref.buffer)
    {
      *out_x = 0;
      *out_y = 0;
      return;
    }

  get_layer_surface_bounds (layer_surface, &output_rect, &usable_area);

  /* Use appropriate bounds based on exclusive_zone:
   * -1: use full output (extend under panels)
   * 0 or >0: use workarea (respect builtin panels like Cinnamon's) */
  if (layer_surface->current.exclusive_zone == -1)
    bounds = &output_rect;
  else
    bounds = &usable_area;

  width = meta_wayland_surface_get_width (surface);
  height = meta_wayland_surface_get_height (surface);
  anchor = layer_surface->current.anchor;

  /* Calculate X position */
  if ((anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) &&
      (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT))
    {
      /* Horizontally centered, stretched */
      x = bounds->x + layer_surface->current.margin.left;
    }
  else if (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)
    {
      x = bounds->x + layer_surface->current.margin.left;
    }
  else if (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)
    {
      x = bounds->x + bounds->width - width - layer_surface->current.margin.right;
    }
  else
    {
      /* Horizontally centered */
      x = bounds->x + (bounds->width - width) / 2;
    }

  /* Calculate Y position */
  if ((anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) &&
      (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM))
    {
      /* Vertically centered, stretched */
      y = bounds->y + layer_surface->current.margin.top;
    }
  else if (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)
    {
      y = bounds->y + layer_surface->current.margin.top;
    }
  else if (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)
    {
      y = bounds->y + bounds->height - height - layer_surface->current.margin.bottom;
    }
  else
    {
      /* Vertically centered */
      y = bounds->y + (bounds->height - height) / 2;
    }

  *out_x = x;
  *out_y = y;
}

static ClutterActor *
get_layer_container_for_layer (MetaWaylandLayerSurface *layer_surface)
{
  MetaDisplay *display;
  ClutterActor *layer_container = NULL;

  display = meta_get_display ();

  switch (layer_surface->current.layer)
    {
    case META_LAYER_SHELL_LAYER_BACKGROUND:
    case META_LAYER_SHELL_LAYER_BOTTOM:
      /* Use bottom_window_group for background and bottom layers */
      layer_container = meta_get_bottom_window_group_for_display (display);
      break;
    case META_LAYER_SHELL_LAYER_TOP:
      /* Use top_window_group for top layer */
      layer_container = meta_get_top_window_group_for_display (display);
      break;
    case META_LAYER_SHELL_LAYER_OVERLAY:
      /* Use feedback_group for overlay (topmost) */
      layer_container = meta_get_feedback_group_for_display (display);
      break;
    }

  return layer_container;
}

static void
meta_wayland_layer_surface_apply_state (MetaWaylandSurfaceRole  *surface_role,
                                        MetaWaylandSurfaceState *pending)
{
  MetaWaylandLayerSurface *layer_surface = META_WAYLAND_LAYER_SURFACE (surface_role);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandActorSurface *actor_surface = META_WAYLAND_ACTOR_SURFACE (surface_role);
  MetaSurfaceActor *surface_actor;
  ClutterActor *layer_container;
  gboolean had_buffer;
  gboolean has_buffer;
  gboolean struts_changed = FALSE;
  MetaWaylandLayerSurfaceState old_state;
  int x, y;

  had_buffer = layer_surface->mapped;
  has_buffer = surface->buffer_ref.buffer != NULL;

  /* Save old state for strut change detection */
  old_state = layer_surface->current;

  /* Copy pending state to current */
  layer_surface->current = layer_surface->pending;

  /* Chain up to handle frame callbacks */
  meta_wayland_actor_surface_queue_frame_callbacks (actor_surface, pending);

  /* If client committed without a buffer and hasn't been properly configured,
   * send a configure with the calculated size based on their anchors. */
  if (!has_buffer && !layer_surface->configured)
    {
      meta_wayland_layer_surface_send_configure (layer_surface);
    }

  surface_actor = meta_wayland_actor_surface_get_actor (actor_surface);
  if (!surface_actor)
    return;

  layer_container = get_layer_container_for_layer (layer_surface);
  if (!layer_container)
    return;

  if (has_buffer)
    {
      if (!had_buffer)
        {
          /* Surface is being mapped */
          ClutterActor *actor = CLUTTER_ACTOR (surface_actor);

          clutter_actor_set_reactive (actor, TRUE);
          clutter_actor_add_child (layer_container, actor);
          layer_surface->mapped = TRUE;

          /* Mapping may affect struts */
          if (layer_surface->current.exclusive_zone > 0)
            struts_changed = TRUE;

          g_debug ("Layer surface mapped: namespace=%s layer=%d",
                   layer_surface->namespace ? layer_surface->namespace : "(null)",
                   layer_surface->current.layer);
        }
      else
        {
          /* Check if strut-affecting properties changed while mapped */
          if (layer_surface->current.exclusive_zone != old_state.exclusive_zone ||
              layer_surface->current.anchor != old_state.anchor ||
              layer_surface->current.margin.top != old_state.margin.top ||
              layer_surface->current.margin.bottom != old_state.margin.bottom ||
              layer_surface->current.margin.left != old_state.margin.left ||
              layer_surface->current.margin.right != old_state.margin.right)
            {
              if (layer_surface->current.exclusive_zone > 0 ||
                  old_state.exclusive_zone > 0)
                struts_changed = TRUE;
            }
        }

      /* Sync actor state */
      meta_wayland_actor_surface_sync_actor_state (actor_surface);

      /* Update position */
      calculate_surface_position (layer_surface, &x, &y);
      clutter_actor_set_position (CLUTTER_ACTOR (surface_actor), x, y);
    }
  else if (had_buffer && !has_buffer)
    {
      /* Surface is being unmapped */
      ClutterActor *actor = CLUTTER_ACTOR (surface_actor);

      clutter_actor_set_reactive (actor, FALSE);

      if (clutter_actor_get_parent (actor))
        clutter_actor_remove_child (layer_container, actor);

      layer_surface->mapped = FALSE;

      /* Unmapping may affect struts */
      if (old_state.exclusive_zone > 0)
        struts_changed = TRUE;

      g_debug ("Layer surface unmapped: namespace=%s",
               layer_surface->namespace ? layer_surface->namespace : "(null)");
    }

  /* Update workspace struts if needed */
  if (struts_changed)
    meta_wayland_layer_shell_update_struts (surface->compositor);
}

static void
meta_wayland_layer_surface_send_configure (MetaWaylandLayerSurface *layer_surface)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (META_WAYLAND_SURFACE_ROLE (layer_surface));
  MetaWaylandLayerSurfaceState *state = &layer_surface->pending;
  MetaRectangle output_rect = { 0, 0, 0, 0 };
  MetaRectangle usable_area = { 0, 0, 0, 0 };
  MetaRectangle *bounds;
  MetaLogicalMonitor *logical_monitor = NULL;
  uint32_t width, height;
  uint32_t serial;

  if (!layer_surface->resource)
    return;

  /* Get output and workarea geometry for configure */
  if (layer_surface->output && layer_surface->output->logical_monitor)
    {
      logical_monitor = layer_surface->output->logical_monitor;
      output_rect = logical_monitor->rect;
    }
  else
    {
      MetaBackend *backend = meta_get_backend ();
      MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
      MetaLogicalMonitor *primary = meta_monitor_manager_get_primary_logical_monitor (monitor_manager);

      if (primary)
        {
          logical_monitor = primary;
          output_rect = primary->rect;
        }
      else
        {
          output_rect.width = 1920;
          output_rect.height = 1080;
        }
    }

  /* Get workarea if needed */
  if (state->exclusive_zone == -1)
    {
      usable_area = output_rect;
    }
  else if (logical_monitor)
    {
      MetaDisplay *display = meta_get_display ();
      MetaWorkspaceManager *workspace_manager;
      MetaWorkspace *workspace;

      usable_area = output_rect;
      if (display)
        {
          workspace_manager = meta_display_get_workspace_manager (display);
          if (workspace_manager)
            {
              workspace = meta_workspace_manager_get_active_workspace (workspace_manager);
              if (workspace)
                {
                  if (state->exclusive_zone > 0)
                    {
                      MetaSide side;
                      int other_surfaces_offset;

                      /* Use workarea excluding layer-shell struts */
                      meta_workspace_get_work_area_for_logical_monitor_excluding_layer_shell (
                          workspace, logical_monitor, &usable_area);

                      /* Also account for other layer surfaces on the same edge */
                      side = get_strut_side_from_anchor (state->anchor);
                      if (side != (MetaSide) -1 && surface && surface->compositor)
                        {
                          other_surfaces_offset =
                            get_other_layer_surfaces_exclusive_offset (layer_surface,
                                                                       surface->compositor,
                                                                       side);
                          switch (side)
                            {
                            case META_SIDE_TOP:
                              usable_area.y += other_surfaces_offset;
                              usable_area.height -= other_surfaces_offset;
                              break;
                            case META_SIDE_BOTTOM:
                              usable_area.height -= other_surfaces_offset;
                              break;
                            case META_SIDE_LEFT:
                              usable_area.x += other_surfaces_offset;
                              usable_area.width -= other_surfaces_offset;
                              break;
                            case META_SIDE_RIGHT:
                              usable_area.width -= other_surfaces_offset;
                              break;
                            default:
                              break;
                            }
                        }
                    }
                  else
                    {
                      /* Use full workarea for exclusive_zone == 0 */
                      meta_workspace_get_work_area_for_logical_monitor (workspace,
                                                                         logical_monitor,
                                                                         &usable_area);
                    }
                }
            }
        }
    }
  else
    {
      usable_area = output_rect;
    }

  /* Use appropriate bounds based on exclusive_zone:
   * -1: use full output (extend under panels)
   * 0 or >0: use workarea (respect builtin panels like Cinnamon's) */
  if (state->exclusive_zone == -1)
    bounds = &output_rect;
  else
    bounds = &usable_area;

  /* Calculate configure size based on anchors and desired size */
  if (state->desired_width != 0)
    width = state->desired_width;
  else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) &&
           (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT))
    width = bounds->width -
            state->margin.left -
            state->margin.right;
  else
    width = 0;

  if (state->desired_height != 0)
    height = state->desired_height;
  else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) &&
           (state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM))
    height = bounds->height -
             state->margin.top -
             state->margin.bottom;
  else
    height = 0;

  serial = wl_display_next_serial (surface->compositor->wayland_display);
  zwlr_layer_surface_v1_send_configure (layer_surface->resource, serial, width, height);

  g_debug ("Layer surface configured: serial=%u size=%ux%u", serial, width, height);
}

static void
meta_wayland_layer_surface_dispose (GObject *object)
{
  MetaWaylandLayerSurface *layer_surface = META_WAYLAND_LAYER_SURFACE (object);
  MetaWaylandActorSurface *actor_surface = META_WAYLAND_ACTOR_SURFACE (object);
  MetaWaylandSurface *surface;
  MetaSurfaceActor *surface_actor;
  gboolean had_struts;

  had_struts = layer_surface->mapped && layer_surface->current.exclusive_zone > 0;

  /* Remove from layer container */
  surface_actor = meta_wayland_actor_surface_get_actor (actor_surface);
  if (surface_actor)
    {
      ClutterActor *actor = CLUTTER_ACTOR (surface_actor);
      ClutterActor *parent = clutter_actor_get_parent (actor);

      if (parent)
        clutter_actor_remove_child (parent, actor);
    }

  /* Remove from tracking list and update struts */
  surface = meta_wayland_surface_role_get_surface (META_WAYLAND_SURFACE_ROLE (layer_surface));
  if (surface && surface->compositor)
    {
      MetaWaylandLayerShell *layer_shell =
        meta_wayland_layer_shell_from_compositor (surface->compositor);

      if (layer_shell)
        {
          layer_shell->layer_surfaces = g_list_remove (layer_shell->layer_surfaces,
                                                       layer_surface);
          if (had_struts)
            meta_wayland_layer_shell_update_struts (surface->compositor);
        }
    }

  g_clear_pointer (&layer_surface->namespace, g_free);

  G_OBJECT_CLASS (meta_wayland_layer_surface_parent_class)->dispose (object);
}

static void
meta_wayland_layer_surface_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  MetaWaylandLayerSurface *layer_surface = META_WAYLAND_LAYER_SURFACE (object);

  switch (prop_id)
    {
    case PROP_OUTPUT:
      layer_surface->output = g_value_get_pointer (value);
      break;
    case PROP_NAMESPACE:
      layer_surface->namespace = g_value_dup_string (value);
      break;
    case PROP_INITIAL_LAYER:
      layer_surface->initial_layer = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_wayland_layer_surface_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
  MetaWaylandLayerSurface *layer_surface = META_WAYLAND_LAYER_SURFACE (object);

  switch (prop_id)
    {
    case PROP_OUTPUT:
      g_value_set_pointer (value, layer_surface->output);
      break;
    case PROP_NAMESPACE:
      g_value_set_string (value, layer_surface->namespace);
      break;
    case PROP_INITIAL_LAYER:
      g_value_set_uint (value, layer_surface->initial_layer);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_wayland_layer_surface_constructed (GObject *object)
{
  MetaWaylandLayerSurface *layer_surface = META_WAYLAND_LAYER_SURFACE (object);

  G_OBJECT_CLASS (meta_wayland_layer_surface_parent_class)->constructed (object);

  /* Apply the initial layer from construction property */
  layer_surface->pending.layer = layer_surface->initial_layer;
}

static void
meta_wayland_layer_surface_init (MetaWaylandLayerSurface *layer_surface)
{
  /* Default state */
  layer_surface->pending.layer = META_LAYER_SHELL_LAYER_BACKGROUND;
  layer_surface->pending.keyboard_interactivity =
    ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
  layer_surface->pending.exclusive_zone = 0;
  layer_surface->configured = FALSE;
  layer_surface->mapped = FALSE;
}

static void
meta_wayland_layer_surface_assigned (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (meta_wayland_layer_surface_parent_class);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);

  surface->dnd.funcs = meta_wayland_data_device_get_drag_dest_funcs ();

  if (surface_role_class->assigned)
    surface_role_class->assigned (surface_role);
}

static void
meta_wayland_layer_surface_class_init (MetaWaylandLayerSurfaceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (klass);

  object_class->constructed = meta_wayland_layer_surface_constructed;
  object_class->dispose = meta_wayland_layer_surface_dispose;
  object_class->set_property = meta_wayland_layer_surface_set_property;
  object_class->get_property = meta_wayland_layer_surface_get_property;

  surface_role_class->assigned = meta_wayland_layer_surface_assigned;
  surface_role_class->apply_state = meta_wayland_layer_surface_apply_state;

  layer_surface_props[PROP_OUTPUT] =
    g_param_spec_pointer ("output", NULL, NULL,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  layer_surface_props[PROP_NAMESPACE] =
    g_param_spec_string ("namespace", NULL, NULL, NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  layer_surface_props[PROP_INITIAL_LAYER] =
    g_param_spec_uint ("initial-layer", NULL, NULL,
                       0, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
                       META_LAYER_SHELL_LAYER_BACKGROUND,
                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, N_PROPS, layer_surface_props);
}

MetaLayerShellLayer
meta_wayland_layer_surface_get_layer (MetaWaylandLayerSurface *layer_surface)
{
  return layer_surface->current.layer;
}

MetaWaylandOutput *
meta_wayland_layer_surface_get_output (MetaWaylandLayerSurface *layer_surface)
{
  return layer_surface->output;
}

/* Layer shell protocol implementation */
static void
layer_shell_get_layer_surface (struct wl_client   *client,
                               struct wl_resource *resource,
                               uint32_t            id,
                               struct wl_resource *surface_resource,
                               struct wl_resource *output_resource,
                               uint32_t            layer,
                               const char         *namespace)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandOutput *output = NULL;
  MetaWaylandLayerSurface *layer_surface;

  /* Check if surface already has a role */
  if (surface->role)
    {
      wl_resource_post_error (resource,
                              ZWLR_LAYER_SHELL_V1_ERROR_ROLE,
                              "Surface already has a role");
      return;
    }

  /* Validate layer value */
  if (layer > ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY)
    {
      wl_resource_post_error (resource,
                              ZWLR_LAYER_SHELL_V1_ERROR_INVALID_LAYER,
                              "Invalid layer value");
      return;
    }

  /* Check if surface already has a buffer */
  if (surface->buffer_ref.buffer)
    {
      wl_resource_post_error (resource,
                              ZWLR_LAYER_SHELL_V1_ERROR_ALREADY_CONSTRUCTED,
                              "Surface has a buffer attached");
      return;
    }

  if (output_resource)
    output = wl_resource_get_user_data (output_resource);

  /* Create layer surface role via meta_wayland_surface_assign_role.
   * This ensures the surface is associated with the role during construction,
   * which is required by MetaWaylandActorSurface's constructed() handler. */
  if (!meta_wayland_surface_assign_role (surface,
                                         META_TYPE_WAYLAND_LAYER_SURFACE,
                                         "output", output,
                                         "namespace", namespace,
                                         "initial-layer", layer,
                                         NULL))
    {
      wl_resource_post_error (resource,
                              ZWLR_LAYER_SHELL_V1_ERROR_ROLE,
                              "wl_surface@%d already has a different role",
                              wl_resource_get_id (surface_resource));
      return;
    }

  layer_surface = META_WAYLAND_LAYER_SURFACE (surface->role);

  layer_surface->resource =
    wl_resource_create (client,
                        &zwlr_layer_surface_v1_interface,
                        wl_resource_get_version (resource),
                        id);

  wl_resource_set_implementation (layer_surface->resource,
                                  &layer_surface_interface,
                                  layer_surface,
                                  layer_surface_resource_destroyed);

  /* Add to tracking list and ensure signal is connected */
  {
    MetaWaylandLayerShell *layer_shell = wl_resource_get_user_data (resource);
    layer_shell->layer_surfaces = g_list_prepend (layer_shell->layer_surfaces,
                                                  layer_surface);
    meta_wayland_layer_shell_ensure_signal_connected (layer_shell);
  }

  g_debug ("Layer surface created: namespace=%s layer=%d output=%p",
           namespace ? namespace : "(null)", layer, output);

  /* Send initial configure now that resource is ready */
  meta_wayland_layer_surface_send_configure (layer_surface);
}

static void
layer_shell_destroy (struct wl_client   *client,
                     struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct zwlr_layer_shell_v1_interface layer_shell_interface = {
  layer_shell_get_layer_surface,
  layer_shell_destroy,
};

static void
layer_shell_destructor (struct wl_resource *resource)
{
  MetaWaylandLayerShell *layer_shell = wl_resource_get_user_data (resource);

  layer_shell->shell_resources = g_list_remove (layer_shell->shell_resources,
                                                resource);
}

static void
bind_layer_shell (struct wl_client *client,
                  void             *data,
                  uint32_t          version,
                  uint32_t          id)
{
  MetaWaylandLayerShell *layer_shell = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &zwlr_layer_shell_v1_interface,
                                 version,
                                 id);
  wl_resource_set_implementation (resource,
                                  &layer_shell_interface,
                                  layer_shell,
                                  layer_shell_destructor);

  layer_shell->shell_resources = g_list_prepend (layer_shell->shell_resources,
                                                 resource);
}

static void
meta_wayland_layer_shell_dispose (GObject *object)
{
  MetaWaylandLayerShell *layer_shell = META_WAYLAND_LAYER_SHELL (object);

  if (layer_shell->workareas_changed_handler_id != 0)
    {
      MetaDisplay *display = meta_get_display ();
      if (display)
        g_signal_handler_disconnect (display, layer_shell->workareas_changed_handler_id);
      layer_shell->workareas_changed_handler_id = 0;
    }

  g_clear_pointer (&layer_shell->layer_surfaces, g_list_free);
  g_clear_pointer (&layer_shell->shell_resources, g_list_free);

  G_OBJECT_CLASS (meta_wayland_layer_shell_parent_class)->dispose (object);
}

static void
meta_wayland_layer_shell_init (MetaWaylandLayerShell *layer_shell)
{
}

static void
meta_wayland_layer_shell_class_init (MetaWaylandLayerShellClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_wayland_layer_shell_dispose;
}

static MetaWaylandLayerShell *
meta_wayland_layer_shell_new (MetaWaylandCompositor *compositor)
{
  MetaWaylandLayerShell *layer_shell;

  layer_shell = g_object_new (META_TYPE_WAYLAND_LAYER_SHELL, NULL);
  layer_shell->compositor = compositor;

  if (wl_global_create (compositor->wayland_display,
                        &zwlr_layer_shell_v1_interface,
                        META_ZWLR_LAYER_SHELL_V1_VERSION,
                        layer_shell, bind_layer_shell) == NULL)
    {
      g_warning ("Failed to register wlr_layer_shell_v1 global");
      g_object_unref (layer_shell);
      return NULL;
    }

  g_debug ("Layer shell protocol initialized (version %d)", META_ZWLR_LAYER_SHELL_V1_VERSION);

  return layer_shell;
}

void
meta_wayland_layer_shell_update_struts (MetaWaylandCompositor *compositor)
{
  MetaWaylandLayerShell *layer_shell;
  MetaDisplay *display;
  MetaWorkspaceManager *workspace_manager;
  GList *workspaces;
  GList *l;
  GSList *struts = NULL;

  layer_shell = meta_wayland_layer_shell_from_compositor (compositor);
  if (!layer_shell)
    return;

  display = meta_get_display ();
  if (!display)
    return;

  workspace_manager = meta_display_get_workspace_manager (display);
  if (!workspace_manager)
    return;

  for (l = layer_shell->layer_surfaces; l; l = l->next)
    {
      MetaWaylandLayerSurface *surface = l->data;
      MetaStrut *strut = meta_wayland_layer_surface_create_strut (surface);

      if (strut)
        struts = g_slist_prepend (struts, strut);
    }

  workspaces = meta_workspace_manager_get_workspaces (workspace_manager);
  for (l = workspaces; l; l = l->next)
    {
      MetaWorkspace *workspace = l->data;
      meta_workspace_set_layer_shell_struts (workspace, struts);
    }

  g_slist_free_full (struts, g_free);
}

void
meta_wayland_layer_shell_on_workarea_changed (MetaWaylandCompositor *compositor)
{
  MetaWaylandLayerShell *layer_shell;
  GList *l;

  layer_shell = meta_wayland_layer_shell_from_compositor (compositor);
  if (!layer_shell)
    return;

  for (l = layer_shell->layer_surfaces; l; l = l->next)
    {
      MetaWaylandLayerSurface *surface = l->data;

      /* Surfaces with exclusive_zone != -1 use workarea bounds and need
       * repositioning when workarea changes. Surfaces with exclusive_zone == -1
       * use full output and aren't affected. */
      if (surface->current.exclusive_zone != -1 && surface->mapped)
        {
          MetaWaylandActorSurface *actor_surface = META_WAYLAND_ACTOR_SURFACE (surface);
          MetaSurfaceActor *surface_actor = meta_wayland_actor_surface_get_actor (actor_surface);

          if (surface_actor)
            {
              int x, y;
              calculate_surface_position (surface, &x, &y);
              clutter_actor_set_position (CLUTTER_ACTOR (surface_actor), x, y);
            }

          /* Also send configure in case size changed */
          meta_wayland_layer_surface_send_configure (surface);
        }
    }

  /* Recalculate layer-shell struts since surface positions changed */
  meta_wayland_layer_shell_update_struts (compositor);
}

void
meta_wayland_init_layer_shell (MetaWaylandCompositor *compositor)
{
  g_object_set_data_full (G_OBJECT (compositor), "-meta-wayland-layer-shell",
                          meta_wayland_layer_shell_new (compositor),
                          g_object_unref);
}
