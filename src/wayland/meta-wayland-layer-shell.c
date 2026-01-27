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
#include "wayland/meta-wayland-outputs.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-surface.h"
#include "wayland/meta-wayland-versions.h"

#include "wlr-layer-shell-unstable-v1-server-protocol.h"

/* Layer shell global */
struct _MetaWaylandLayerShell
{
  GObject parent;
  GList *shell_resources;
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
  /* TODO: Implement popup support */
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

/* Calculate surface position based on anchor, margin, and output geometry */
static void
calculate_surface_position (MetaWaylandLayerSurface *layer_surface,
                            int                     *out_x,
                            int                     *out_y)
{
  MetaRectangle output_rect = { 0, 0, 0, 0 };
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

  /* Get output geometry */
  if (layer_surface->output && layer_surface->output->logical_monitor)
    {
      output_rect = layer_surface->output->logical_monitor->rect;
    }
  else
    {
      /* Fallback to primary monitor */
      MetaBackend *backend = meta_get_backend ();
      MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
      MetaLogicalMonitor *primary = meta_monitor_manager_get_primary_logical_monitor (monitor_manager);

      if (primary)
        output_rect = primary->rect;
      else
        {
          output_rect.width = 1920;
          output_rect.height = 1080;
        }
    }

  width = meta_wayland_surface_get_width (surface);
  height = meta_wayland_surface_get_height (surface);
  anchor = layer_surface->current.anchor;

  /* Calculate X position */
  if ((anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) &&
      (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT))
    {
      /* Horizontally centered, stretched */
      x = output_rect.x + layer_surface->current.margin.left;
    }
  else if (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)
    {
      x = output_rect.x + layer_surface->current.margin.left;
    }
  else if (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)
    {
      x = output_rect.x + output_rect.width - width - layer_surface->current.margin.right;
    }
  else
    {
      /* Horizontally centered */
      x = output_rect.x + (output_rect.width - width) / 2;
    }

  /* Calculate Y position */
  if ((anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) &&
      (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM))
    {
      /* Vertically centered, stretched */
      y = output_rect.y + layer_surface->current.margin.top;
    }
  else if (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)
    {
      y = output_rect.y + layer_surface->current.margin.top;
    }
  else if (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)
    {
      y = output_rect.y + output_rect.height - height - layer_surface->current.margin.bottom;
    }
  else
    {
      /* Vertically centered */
      y = output_rect.y + (output_rect.height - height) / 2;
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
  int x, y;

  had_buffer = layer_surface->mapped;
  has_buffer = surface->buffer_ref.buffer != NULL;

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

          clutter_actor_add_child (layer_container, actor);
          layer_surface->mapped = TRUE;

          g_debug ("Layer surface mapped: namespace=%s layer=%d",
                   layer_surface->namespace ? layer_surface->namespace : "(null)",
                   layer_surface->current.layer);
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

      if (clutter_actor_get_parent (actor))
        clutter_actor_remove_child (layer_container, actor);

      layer_surface->mapped = FALSE;

      g_debug ("Layer surface unmapped: namespace=%s",
               layer_surface->namespace ? layer_surface->namespace : "(null)");
    }
}

static void
meta_wayland_layer_surface_send_configure (MetaWaylandLayerSurface *layer_surface)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (META_WAYLAND_SURFACE_ROLE (layer_surface));
  MetaRectangle output_rect = { 0, 0, 0, 0 };
  uint32_t width, height;
  uint32_t serial;

  if (!layer_surface->resource)
    return;

  /* Get output geometry for configure */
  if (layer_surface->output && layer_surface->output->logical_monitor)
    {
      output_rect = layer_surface->output->logical_monitor->rect;
    }
  else
    {
      MetaBackend *backend = meta_get_backend ();
      MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
      MetaLogicalMonitor *primary = meta_monitor_manager_get_primary_logical_monitor (monitor_manager);

      if (primary)
        output_rect = primary->rect;
      else
        {
          output_rect.width = 1920;
          output_rect.height = 1080;
        }
    }

  /* Calculate configure size based on anchors and desired size */
  if (layer_surface->pending.desired_width != 0)
    width = layer_surface->pending.desired_width;
  else if ((layer_surface->pending.anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) &&
           (layer_surface->pending.anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT))
    width = output_rect.width -
            layer_surface->pending.margin.left -
            layer_surface->pending.margin.right;
  else
    width = 0;

  if (layer_surface->pending.desired_height != 0)
    height = layer_surface->pending.desired_height;
  else if ((layer_surface->pending.anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) &&
           (layer_surface->pending.anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM))
    height = output_rect.height -
             layer_surface->pending.margin.top -
             layer_surface->pending.margin.bottom;
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
  MetaSurfaceActor *surface_actor;

  /* Remove from layer container */
  surface_actor = meta_wayland_actor_surface_get_actor (actor_surface);
  if (surface_actor)
    {
      ClutterActor *actor = CLUTTER_ACTOR (surface_actor);
      ClutterActor *parent = clutter_actor_get_parent (actor);

      if (parent)
        clutter_actor_remove_child (parent, actor);
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
meta_wayland_layer_surface_class_init (MetaWaylandLayerSurfaceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (klass);

  object_class->constructed = meta_wayland_layer_surface_constructed;
  object_class->dispose = meta_wayland_layer_surface_dispose;
  object_class->set_property = meta_wayland_layer_surface_set_property;
  object_class->get_property = meta_wayland_layer_surface_get_property;

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
meta_wayland_layer_shell_init (MetaWaylandLayerShell *layer_shell)
{
}

static void
meta_wayland_layer_shell_class_init (MetaWaylandLayerShellClass *klass)
{
}

static MetaWaylandLayerShell *
meta_wayland_layer_shell_new (MetaWaylandCompositor *compositor)
{
  MetaWaylandLayerShell *layer_shell;

  layer_shell = g_object_new (META_TYPE_WAYLAND_LAYER_SHELL, NULL);

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
meta_wayland_init_layer_shell (MetaWaylandCompositor *compositor)
{
  g_object_set_data_full (G_OBJECT (compositor), "-meta-wayland-layer-shell",
                          meta_wayland_layer_shell_new (compositor),
                          g_object_unref);
}
