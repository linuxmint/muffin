/*
 * Wayland Support
 *
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

#include "wayland/meta-wayland-surface.h"

#include <gobject/gvaluecollector.h>
#include <wayland-server.h>

#include "backends/meta-cursor-tracker-private.h"
#include "clutter/clutter.h"
#include "clutter/wayland/clutter-wayland-compositor.h"
#include "cogl/cogl-wayland-server.h"
#include "cogl/cogl.h"
#include "compositor/meta-surface-actor-wayland.h"
#include "compositor/meta-surface-actor.h"
#include "compositor/meta-window-actor-private.h"
#include "compositor/region-utils.h"
#include "core/display-private.h"
#include "core/window-private.h"
#include "wayland/meta-wayland-actor-surface.h"
#include "wayland/meta-wayland-buffer.h"
#include "wayland/meta-wayland-data-device.h"
#include "wayland/meta-wayland-gtk-shell.h"
#include "wayland/meta-wayland-keyboard.h"
#include "wayland/meta-wayland-legacy-xdg-shell.h"
#include "wayland/meta-wayland-outputs.h"
#include "wayland/meta-wayland-pointer.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-region.h"
#include "wayland/meta-wayland-seat.h"
#include "wayland/meta-wayland-subsurface.h"
#include "wayland/meta-wayland-viewporter.h"
#include "wayland/meta-wayland-wl-shell.h"
#include "wayland/meta-wayland-xdg-shell.h"
#include "wayland/meta-window-wayland.h"
#include "wayland/meta-xwayland-private.h"
#include "wayland/meta-xwayland-private.h"

enum
{
  SURFACE_STATE_SIGNAL_APPLIED,

  SURFACE_STATE_SIGNAL_N_SIGNALS
};

enum
{
  SURFACE_ROLE_PROP_0,

  SURFACE_ROLE_PROP_SURFACE,
};

static guint surface_state_signals[SURFACE_STATE_SIGNAL_N_SIGNALS];

typedef struct _MetaWaylandSurfaceRolePrivate
{
  MetaWaylandSurface *surface;
} MetaWaylandSurfaceRolePrivate;

G_DEFINE_TYPE (MetaWaylandSurface, meta_wayland_surface, G_TYPE_OBJECT);

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (MetaWaylandSurfaceRole,
                                     meta_wayland_surface_role,
                                     G_TYPE_OBJECT)

G_DEFINE_TYPE (MetaWaylandSurfaceState,
               meta_wayland_surface_state,
               G_TYPE_OBJECT)

enum
{
  SURFACE_DESTROY,
  SURFACE_UNMAPPED,
  SURFACE_CONFIGURE,
  SURFACE_SHORTCUTS_INHIBITED,
  SURFACE_SHORTCUTS_RESTORED,
  SURFACE_GEOMETRY_CHANGED,
  SURFACE_PRE_STATE_APPLIED,
  N_SURFACE_SIGNALS
};

guint surface_signals[N_SURFACE_SIGNALS] = { 0 };

static void
meta_wayland_surface_role_assigned (MetaWaylandSurfaceRole *surface_role);

static void
meta_wayland_surface_role_pre_apply_state (MetaWaylandSurfaceRole  *surface_role,
                                           MetaWaylandSurfaceState *pending);

static void
meta_wayland_surface_role_apply_state (MetaWaylandSurfaceRole  *surface_role,
                                       MetaWaylandSurfaceState *pending);

static void
meta_wayland_surface_role_post_apply_state (MetaWaylandSurfaceRole  *surface_role,
                                            MetaWaylandSurfaceState *pending);

static gboolean
meta_wayland_surface_role_is_on_logical_monitor (MetaWaylandSurfaceRole *surface_role,
                                                 MetaLogicalMonitor     *logical_monitor);

static MetaWaylandSurface *
meta_wayland_surface_role_get_toplevel (MetaWaylandSurfaceRole *surface_role);

static void
role_assignment_valist_to_properties (GType       role_type,
                                      const char *first_property_name,
                                      va_list     var_args,
                                      GArray     *names,
                                      GArray     *values)
{
  GObjectClass *object_class;
  const char *property_name = first_property_name;

  object_class = g_type_class_ref (role_type);

  while (property_name)
    {
      GValue value = G_VALUE_INIT;
      GParamSpec *pspec;
      GType ptype;
      gchar *error = NULL;

      pspec = g_object_class_find_property (object_class,
                                            property_name);
      g_assert (pspec);

      ptype = G_PARAM_SPEC_VALUE_TYPE (pspec);
      G_VALUE_COLLECT_INIT (&value, ptype, var_args, 0, &error);
      g_assert (!error);

      g_array_append_val (names, property_name);
      g_array_append_val (values, value);

      property_name = va_arg (var_args, const char *);
    }

  g_type_class_unref (object_class);
}

gboolean
meta_wayland_surface_assign_role (MetaWaylandSurface *surface,
                                  GType               role_type,
                                  const char         *first_property_name,
                                  ...)
{
  va_list var_args;

  if (!surface->role)
    {
      if (first_property_name)
        {
          GArray *names;
          GArray *values;
          const char *surface_prop_name;
          GValue surface_value = G_VALUE_INIT;
          GObject *role_object;

          names = g_array_new (FALSE, FALSE, sizeof (const char *));
          values = g_array_new (FALSE, FALSE, sizeof (GValue));
          g_array_set_clear_func (values, (GDestroyNotify) g_value_unset);

          va_start (var_args, first_property_name);
          role_assignment_valist_to_properties (role_type,
                                                first_property_name,
                                                var_args,
                                                names,
                                                values);
          va_end (var_args);

          surface_prop_name = "surface";
          g_value_init (&surface_value, META_TYPE_WAYLAND_SURFACE);
          g_value_set_object (&surface_value, surface);
          g_array_append_val (names, surface_prop_name);
          g_array_append_val (values, surface_value);

          role_object =
            g_object_new_with_properties (role_type,
                                          values->len,
                                          (const char **) names->data,
                                          (const GValue *) values->data);
          surface->role = META_WAYLAND_SURFACE_ROLE (role_object);

          g_array_free (names, TRUE);
          g_array_free (values, TRUE);
        }
      else
        {
          surface->role = g_object_new (role_type, "surface", surface, NULL);
        }

      meta_wayland_surface_role_assigned (surface->role);

      /* Release the use count held on behalf of the just assigned role. */
      if (surface->unassigned.buffer)
        {
          meta_wayland_surface_unref_buffer_use_count (surface);
          g_clear_object (&surface->unassigned.buffer);
        }

      return TRUE;
    }
  else if (G_OBJECT_TYPE (surface->role) != role_type)
    {
      return FALSE;
    }
  else
    {
      va_start (var_args, first_property_name);
      g_object_set_valist (G_OBJECT (surface->role),
                           first_property_name, var_args);
      va_end (var_args);

      meta_wayland_surface_role_assigned (surface->role);

      return TRUE;
    }
}

static int
get_buffer_width (MetaWaylandSurface *surface)
{
  MetaWaylandBuffer *buffer = meta_wayland_surface_get_buffer (surface);

  if (buffer)
    return cogl_texture_get_width (surface->texture);
  else
    return 0;
}

static int
get_buffer_height (MetaWaylandSurface *surface)
{
  MetaWaylandBuffer *buffer = meta_wayland_surface_get_buffer (surface);

  if (buffer)
    return cogl_texture_get_height (surface->texture);
  else
    return 0;
}

static void
surface_process_damage (MetaWaylandSurface *surface,
                        cairo_region_t     *surface_region,
                        cairo_region_t     *buffer_region)
{
  MetaWaylandBuffer *buffer = meta_wayland_surface_get_buffer (surface);
  cairo_rectangle_int_t surface_rect;
  cairo_rectangle_int_t buffer_rect;
  cairo_region_t *scaled_region;
  cairo_region_t *transformed_region;
  cairo_region_t *viewport_region;
  graphene_rect_t src_rect;
  MetaSurfaceActor *actor;

  /* If the client destroyed the buffer it attached before committing, but
   * still posted damage, or posted damage without any buffer, don't try to
   * process it on the non-existing buffer.
   */
  if (!buffer)
    return;

  buffer_rect = (cairo_rectangle_int_t) {
    .width = get_buffer_width (surface),
    .height = get_buffer_height (surface),
  };

  /* Intersect the damage region with the surface region before scaling in
   * order to avoid integer overflow when scaling a damage region is too large
   * (for example INT32_MAX which mesa passes). */
  surface_rect = (cairo_rectangle_int_t) {
    .width = meta_wayland_surface_get_width (surface),
    .height = meta_wayland_surface_get_height (surface),
  };
  cairo_region_intersect_rectangle (surface_region, &surface_rect);

  /* The damage region must be in the same coordinate space as the buffer,
   * i.e. scaled with surface->scale. */
  scaled_region = meta_region_scale (surface_region, surface->scale);
  if (surface->viewport.has_src_rect)
    {
      src_rect = (graphene_rect_t) {
        .origin.x = surface->viewport.src_rect.origin.x * surface->scale,
        .origin.y = surface->viewport.src_rect.origin.y * surface->scale,
        .size.width = surface->viewport.src_rect.size.width * surface->scale,
        .size.height = surface->viewport.src_rect.size.height * surface->scale
      };
    }
  else
    {
      src_rect = (graphene_rect_t) {
        .size.width = surface_rect.width * surface->scale,
        .size.height = surface_rect.height * surface->scale,
      };
    }
  viewport_region = meta_region_crop_and_scale (scaled_region,
                                                &src_rect,
                                                surface_rect.width *
                                                surface->scale,
                                                surface_rect.height *
                                                surface->scale);
  transformed_region = meta_region_transform (viewport_region,
                                              surface->buffer_transform,
                                              buffer_rect.width,
                                              buffer_rect.height);

  /* Now add the scaled, cropped and transformed damage region to the
   * buffer damage. Buffer damage is already in the correct coordinate space. */
  cairo_region_union (buffer_region, transformed_region);

  cairo_region_intersect_rectangle (buffer_region, &buffer_rect);

  meta_wayland_buffer_process_damage (buffer, surface->texture, buffer_region);

  actor = meta_wayland_surface_get_actor (surface);
  if (actor)
    {
      int i, n_rectangles;

      n_rectangles = cairo_region_num_rectangles (buffer_region);
      for (i = 0; i < n_rectangles; i++)
        {
          cairo_rectangle_int_t rect;
          cairo_region_get_rectangle (buffer_region, i, &rect);

          meta_surface_actor_process_damage (actor,
                                             rect.x, rect.y,
                                             rect.width, rect.height);
        }
    }

  cairo_region_destroy (viewport_region);
  cairo_region_destroy (scaled_region);
  cairo_region_destroy (transformed_region);
}

MetaWaylandBuffer *
meta_wayland_surface_get_buffer (MetaWaylandSurface *surface)
{
  return surface->buffer_ref.buffer;
}

void
meta_wayland_surface_ref_buffer_use_count (MetaWaylandSurface *surface)
{
  g_return_if_fail (surface->buffer_ref.buffer);
  g_warn_if_fail (surface->buffer_ref.buffer->resource);

  surface->buffer_ref.use_count++;
}

void
meta_wayland_surface_unref_buffer_use_count (MetaWaylandSurface *surface)
{
  MetaWaylandBuffer *buffer = surface->buffer_ref.buffer;

  g_return_if_fail (surface->buffer_ref.use_count != 0);

  surface->buffer_ref.use_count--;

  g_return_if_fail (buffer);

  if (surface->buffer_ref.use_count == 0 && buffer->resource)
    wl_buffer_send_release (buffer->resource);
}

static void
pending_buffer_resource_destroyed (MetaWaylandBuffer       *buffer,
                                   MetaWaylandSurfaceState *pending)
{
  g_clear_signal_handler (&pending->buffer_destroy_handler_id, buffer);
  pending->buffer = NULL;
}

static void
meta_wayland_surface_state_set_default (MetaWaylandSurfaceState *state)
{
  state->newly_attached = FALSE;
  state->buffer = NULL;
  state->buffer_destroy_handler_id = 0;
  state->dx = 0;
  state->dy = 0;
  state->scale = 0;

  state->input_region = NULL;
  state->input_region_set = FALSE;
  state->opaque_region = NULL;
  state->opaque_region_set = FALSE;

  state->surface_damage = cairo_region_create ();
  state->buffer_damage = cairo_region_create ();
  wl_list_init (&state->frame_callback_list);

  state->has_new_geometry = FALSE;
  state->has_acked_configure_serial = FALSE;
  state->has_new_min_size = FALSE;
  state->has_new_max_size = FALSE;

  state->has_new_buffer_transform = FALSE;
  state->has_new_viewport_src_rect = FALSE;
  state->has_new_viewport_dst_size = FALSE;

  state->subsurface_placement_ops = NULL;
}

static void
meta_wayland_surface_state_clear (MetaWaylandSurfaceState *state)
{
  MetaWaylandFrameCallback *cb, *next;

  g_clear_pointer (&state->surface_damage, cairo_region_destroy);
  g_clear_pointer (&state->buffer_damage, cairo_region_destroy);
  g_clear_pointer (&state->input_region, cairo_region_destroy);
  g_clear_pointer (&state->opaque_region, cairo_region_destroy);

  if (state->buffer)
    g_clear_signal_handler (&state->buffer_destroy_handler_id, state->buffer);

  wl_list_for_each_safe (cb, next, &state->frame_callback_list, link)
    wl_resource_destroy (cb->resource);

  if (state->subsurface_placement_ops)
    {
      g_slist_free_full (
        state->subsurface_placement_ops,
        (GDestroyNotify) meta_wayland_subsurface_placement_op_free);
    }
}

static void
meta_wayland_surface_state_reset (MetaWaylandSurfaceState *state)
{
  meta_wayland_surface_state_clear (state);
  meta_wayland_surface_state_set_default (state);
}

static void
meta_wayland_surface_state_merge_into (MetaWaylandSurfaceState *from,
                                       MetaWaylandSurfaceState *to)
{
  if (from->newly_attached)
    {
      if (to->buffer)
        g_clear_signal_handler (&to->buffer_destroy_handler_id, to->buffer);

      if (from->buffer)
        g_clear_signal_handler (&from->buffer_destroy_handler_id, from->buffer);

      to->newly_attached = TRUE;
      to->buffer = from->buffer;
      to->dx = from->dx;
      to->dy = from->dy;
    }

  wl_list_insert_list (&to->frame_callback_list, &from->frame_callback_list);

  cairo_region_union (to->surface_damage, from->surface_damage);
  cairo_region_union (to->buffer_damage, from->buffer_damage);
  cairo_region_destroy (from->surface_damage);
  cairo_region_destroy (from->buffer_damage);

  if (from->input_region_set)
    {
      if (to->input_region)
        cairo_region_union (to->input_region, from->input_region);
      else
        to->input_region = cairo_region_reference (from->input_region);

      to->input_region_set = TRUE;
      cairo_region_destroy (from->input_region);
    }

  if (from->opaque_region_set)
    {
      if (to->opaque_region)
        cairo_region_union (to->opaque_region, from->opaque_region);
      else
        to->opaque_region = cairo_region_reference (from->opaque_region);

      to->opaque_region_set = TRUE;
      cairo_region_destroy (from->opaque_region);
    }

  if (from->has_new_geometry)
    {
      to->new_geometry = from->new_geometry;
      to->has_new_geometry = TRUE;
    }

  if (from->has_acked_configure_serial)
    {
      to->acked_configure_serial = from->acked_configure_serial;
      to->has_acked_configure_serial = TRUE;
    }

  if (from->has_new_min_size)
    {
      to->new_min_width = from->new_min_width;
      to->new_min_height = from->new_min_height;
      to->has_new_min_size = TRUE;
    }

  if (from->has_new_max_size)
    {
      to->new_max_width = from->new_max_width;
      to->new_max_height = from->new_max_height;
      to->has_new_max_size = TRUE;
    }

  if (from->scale > 0)
    to->scale = from->scale;

  if (from->has_new_buffer_transform)
    {
      to->buffer_transform = from->buffer_transform;
      to->has_new_buffer_transform = TRUE;
    }

  if (from->has_new_viewport_src_rect)
    {
      to->viewport_src_rect.origin.x = from->viewport_src_rect.origin.x;
      to->viewport_src_rect.origin.y = from->viewport_src_rect.origin.y;
      to->viewport_src_rect.size.width = from->viewport_src_rect.size.width;
      to->viewport_src_rect.size.height = from->viewport_src_rect.size.height;
      to->has_new_viewport_src_rect = TRUE;
    }

  if (from->has_new_viewport_dst_size)
    {
      to->viewport_dst_width = from->viewport_dst_width;
      to->viewport_dst_height = from->viewport_dst_height;
      to->has_new_viewport_dst_size = TRUE;
    }

  if (to->buffer && to->buffer_destroy_handler_id == 0)
    {
      to->buffer_destroy_handler_id =
        g_signal_connect (to->buffer, "resource-destroyed",
                          G_CALLBACK (pending_buffer_resource_destroyed),
                          to);
    }

  if (from->subsurface_placement_ops != NULL)
    {
      if (to->subsurface_placement_ops != NULL)
        {
          to->subsurface_placement_ops =
            g_slist_concat (to->subsurface_placement_ops,
                            from->subsurface_placement_ops);
        }
      else
        {
          to->subsurface_placement_ops = from->subsurface_placement_ops;
        }

      from->subsurface_placement_ops = NULL;
    }

  meta_wayland_surface_state_set_default (from);
}

static void
meta_wayland_surface_state_finalize (GObject *object)
{
  MetaWaylandSurfaceState *state = META_WAYLAND_SURFACE_STATE (object);

  meta_wayland_surface_state_clear (state);

  G_OBJECT_CLASS (meta_wayland_surface_state_parent_class)->finalize (object);
}

static void
meta_wayland_surface_state_init (MetaWaylandSurfaceState *state)
{
  meta_wayland_surface_state_set_default (state);
}

static void
meta_wayland_surface_state_class_init (MetaWaylandSurfaceStateClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_wayland_surface_state_finalize;

  surface_state_signals[SURFACE_STATE_SIGNAL_APPLIED] =
    g_signal_new ("applied",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
meta_wayland_surface_apply_state (MetaWaylandSurface      *surface,
                                  MetaWaylandSurfaceState *state)
{
  MetaWaylandSurface *subsurface_surface;
  gboolean had_damage = FALSE;

  g_signal_emit (surface, surface_signals[SURFACE_PRE_STATE_APPLIED], 0);

  if (surface->role)
    {
      meta_wayland_surface_role_pre_apply_state (surface->role, state);
    }
  else
    {
      if (state->newly_attached && surface->unassigned.buffer)
        {
          meta_wayland_surface_unref_buffer_use_count (surface);
          g_clear_object (&surface->unassigned.buffer);
        }
    }

  if (state->newly_attached)
    {
      /* Always release any previously held buffer. If the buffer held is same
       * as the newly attached buffer, we still need to release it here, because
       * wl_surface.attach+commit and wl_buffer.release on the attached buffer
       * is symmetric.
       */
      if (surface->buffer_held)
        meta_wayland_surface_unref_buffer_use_count (surface);

      g_set_object (&surface->buffer_ref.buffer, state->buffer);

      if (state->buffer)
        meta_wayland_surface_ref_buffer_use_count (surface);

      if (state->buffer)
        {
          GError *error = NULL;

          if (!meta_wayland_buffer_attach (state->buffer,
                                           &surface->texture,
                                           &error))
            {
              g_warning ("Could not import pending buffer: %s", error->message);
              wl_resource_post_error (surface->resource, WL_DISPLAY_ERROR_NO_MEMORY,
                                      "Failed to attach buffer to surface %i: %s",
                                      wl_resource_get_id (surface->resource),
                                      error->message);
              g_error_free (error);
              goto cleanup;
            }
        }
      else
        {
          cogl_clear_object (&surface->texture);
        }

      /* If the newly attached buffer is going to be accessed directly without
       * making a copy, such as an EGL buffer, mark it as in-use don't release
       * it until is replaced by a subsequent wl_surface.commit or when the
       * wl_surface is destroyed.
       */
      surface->buffer_held = (state->buffer &&
                              !wl_shm_buffer_get (state->buffer->resource));
    }

  if (state->scale > 0)
    surface->scale = state->scale;

  if (state->has_new_buffer_transform)
    surface->buffer_transform = state->buffer_transform;

  if (state->has_new_viewport_src_rect)
    {
      surface->viewport.src_rect.origin.x = state->viewport_src_rect.origin.x;
      surface->viewport.src_rect.origin.y = state->viewport_src_rect.origin.y;
      surface->viewport.src_rect.size.width = state->viewport_src_rect.size.width;
      surface->viewport.src_rect.size.height = state->viewport_src_rect.size.height;
      surface->viewport.has_src_rect = surface->viewport.src_rect.size.width > 0;
    }

  if (state->has_new_viewport_dst_size)
    {
      surface->viewport.dst_width = state->viewport_dst_width;
      surface->viewport.dst_height = state->viewport_dst_height;
      surface->viewport.has_dst_size = surface->viewport.dst_width > 0;
    }

  if (!cairo_region_is_empty (state->surface_damage) ||
      !cairo_region_is_empty (state->buffer_damage))
    {
      surface_process_damage (surface,
                              state->surface_damage,
                              state->buffer_damage);
      had_damage = TRUE;
    }

  surface->offset_x += state->dx;
  surface->offset_y += state->dy;

  if (state->opaque_region_set)
    {
      if (surface->opaque_region)
        cairo_region_destroy (surface->opaque_region);
      if (state->opaque_region)
        surface->opaque_region = cairo_region_reference (state->opaque_region);
      else
        surface->opaque_region = NULL;
    }

  if (state->input_region_set)
    {
      if (surface->input_region)
        cairo_region_destroy (surface->input_region);
      if (state->input_region)
        surface->input_region = cairo_region_reference (state->input_region);
      else
        surface->input_region = NULL;
    }

  if (surface->role)
    {
      meta_wayland_surface_role_apply_state (surface->role, state);
      g_assert (wl_list_empty (&state->frame_callback_list));
    }
  else
    {
      wl_list_insert_list (surface->unassigned.pending_frame_callback_list.prev,
                           &state->frame_callback_list);
      wl_list_init (&state->frame_callback_list);

      if (state->newly_attached)
        {
          /* The need to keep the wl_buffer from being released depends on what
           * role the surface is given. That means we need to also keep a use
           * count for wl_buffer's that are used by unassigned wl_surface's.
           */
          g_set_object (&surface->unassigned.buffer, surface->buffer_ref.buffer);
          if (surface->unassigned.buffer)
            meta_wayland_surface_ref_buffer_use_count (surface);
        }
    }

  if (state->subsurface_placement_ops)
    {
      GSList *l;

      for (l = state->subsurface_placement_ops; l; l = l->next)
        {
          MetaWaylandSubsurfacePlacementOp *op = l->data;
          GNode *sibling_node;

          if (!op->surface || !op->sibling)
            continue;

          if (op->sibling == surface)
            sibling_node = surface->subsurface_leaf_node;
          else
            sibling_node = op->sibling->subsurface_branch_node;

          g_node_unlink (op->surface->subsurface_branch_node);

          switch (op->placement)
            {
            case META_WAYLAND_SUBSURFACE_PLACEMENT_ABOVE:
              g_node_insert_after (surface->subsurface_branch_node,
                                   sibling_node,
                                   op->surface->subsurface_branch_node);
              break;
            case META_WAYLAND_SUBSURFACE_PLACEMENT_BELOW:
              g_node_insert_before (surface->subsurface_branch_node,
                                    sibling_node,
                                    op->surface->subsurface_branch_node);
              break;
            }
        }

      meta_wayland_surface_notify_subsurface_state_changed (surface);
    }

cleanup:
  /* If we have a buffer that we are not using, decrease the use count so it may
   * be released if no-one else has a use-reference to it.
   */
  if (state->newly_attached &&
      !surface->buffer_held && surface->buffer_ref.buffer)
    meta_wayland_surface_unref_buffer_use_count (surface);

  g_signal_emit (state,
                 surface_state_signals[SURFACE_STATE_SIGNAL_APPLIED],
                 0);

  META_WAYLAND_SURFACE_FOREACH_SUBSURFACE (surface, subsurface_surface)
    {
      MetaWaylandSubsurface *subsurface;

      subsurface = META_WAYLAND_SUBSURFACE (subsurface_surface->role);
      meta_wayland_subsurface_parent_state_applied (subsurface);
    }

  if (had_damage)
    {
      MetaWindow *toplevel_window;

      toplevel_window = meta_wayland_surface_get_toplevel_window (surface);
      if (toplevel_window)
        {
          MetaWindowActor *toplevel_window_actor;

          toplevel_window_actor =
            meta_window_actor_from_window (toplevel_window);
          if (toplevel_window_actor)
            meta_window_actor_notify_damaged (toplevel_window_actor);
        }
    }

  if (surface->role)
    meta_wayland_surface_role_post_apply_state (surface->role, state);

  meta_wayland_surface_state_reset (state);
}

void
meta_wayland_surface_apply_cached_state (MetaWaylandSurface *surface)
{
  if (!surface->cached_state)
    return;

  meta_wayland_surface_apply_state (surface, surface->cached_state);
}

MetaWaylandSurfaceState *
meta_wayland_surface_get_pending_state (MetaWaylandSurface *surface)
{
  return surface->pending_state;
}

MetaWaylandSurfaceState *
meta_wayland_surface_ensure_cached_state (MetaWaylandSurface *surface)
{
  if (!surface->cached_state)
    surface->cached_state = g_object_new (META_TYPE_WAYLAND_SURFACE_STATE,
                                          NULL);
  return surface->cached_state;
}

static void
meta_wayland_surface_commit (MetaWaylandSurface *surface)
{
  MetaWaylandSurfaceState *pending = surface->pending_state;

  COGL_TRACE_BEGIN_SCOPED (MetaWaylandSurfaceCommit,
                           "WaylandSurface (commit)");

  if (pending->buffer &&
      !meta_wayland_buffer_is_realized (pending->buffer))
    meta_wayland_buffer_realize (pending->buffer);

  /*
   * If this is a sub-surface and it is in effective synchronous mode, only
   * cache the pending surface state until either one of the following two
   * scenarios happens:
   *  1) Its parent surface gets its state applied.
   *  2) Its mode changes from synchronized to desynchronized and its parent
   *     surface is in effective desynchronized mode.
   */
  if (meta_wayland_surface_should_cache_state (surface))
    {
      MetaWaylandSurfaceState *cached_state;

      cached_state = meta_wayland_surface_ensure_cached_state (surface);
      meta_wayland_surface_state_merge_into (pending, cached_state);
    }
  else
    {
      meta_wayland_surface_apply_state (surface, surface->pending_state);
    }
}

static void
wl_surface_destroy (struct wl_client *client,
                    struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
wl_surface_attach (struct wl_client *client,
                   struct wl_resource *surface_resource,
                   struct wl_resource *buffer_resource,
                   gint32 dx, gint32 dy)
{
  MetaWaylandSurface *surface =
    wl_resource_get_user_data (surface_resource);
  MetaWaylandSurfaceState *pending = surface->pending_state;
  MetaWaylandBuffer *buffer;

  /* X11 unmanaged window */
  if (!surface)
    return;

  if (buffer_resource)
    buffer = meta_wayland_buffer_from_resource (buffer_resource);
  else
    buffer = NULL;

  if (surface->pending_state->buffer)
    {
      g_clear_signal_handler (&pending->buffer_destroy_handler_id,
                              pending->buffer);
    }

  pending->newly_attached = TRUE;
  pending->buffer = buffer;
  pending->dx = dx;
  pending->dy = dy;

  if (buffer)
    {
      pending->buffer_destroy_handler_id =
        g_signal_connect (buffer, "resource-destroyed",
                          G_CALLBACK (pending_buffer_resource_destroyed),
                          pending);
    }
}

static void
wl_surface_damage (struct wl_client   *client,
                   struct wl_resource *surface_resource,
                   int32_t             x,
                   int32_t             y,
                   int32_t             width,
                   int32_t             height)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandSurfaceState *pending = surface->pending_state;
  cairo_rectangle_int_t rectangle;

  /* X11 unmanaged window */
  if (!surface)
    return;

  rectangle = (cairo_rectangle_int_t) {
    .x = x,
    .y = y,
    .width = width,
    .height = height
  };
  cairo_region_union_rectangle (pending->surface_damage, &rectangle);
}

static void
destroy_frame_callback (struct wl_resource *callback_resource)
{
  MetaWaylandFrameCallback *callback =
    wl_resource_get_user_data (callback_resource);

  wl_list_remove (&callback->link);
  g_slice_free (MetaWaylandFrameCallback, callback);
}

static void
wl_surface_frame (struct wl_client *client,
                  struct wl_resource *surface_resource,
                  guint32 callback_id)
{
  MetaWaylandFrameCallback *callback;
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandSurfaceState *pending = surface->pending_state;

  /* X11 unmanaged window */
  if (!surface)
    return;

  callback = g_slice_new0 (MetaWaylandFrameCallback);
  callback->surface = surface;
  callback->resource = wl_resource_create (client,
                                           &wl_callback_interface,
                                           META_WL_CALLBACK_VERSION,
                                           callback_id);
  wl_resource_set_implementation (callback->resource, NULL, callback,
                                  destroy_frame_callback);

  wl_list_insert (pending->frame_callback_list.prev, &callback->link);
}

static void
wl_surface_set_opaque_region (struct wl_client *client,
                              struct wl_resource *surface_resource,
                              struct wl_resource *region_resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandSurfaceState *pending = surface->pending_state;

  /* X11 unmanaged window */
  if (!surface)
    return;

  g_clear_pointer (&pending->opaque_region, cairo_region_destroy);
  if (region_resource)
    {
      MetaWaylandRegion *region = wl_resource_get_user_data (region_resource);
      cairo_region_t *cr_region = meta_wayland_region_peek_cairo_region (region);
      pending->opaque_region = cairo_region_copy (cr_region);
    }
  pending->opaque_region_set = TRUE;
}

static void
wl_surface_set_input_region (struct wl_client *client,
                             struct wl_resource *surface_resource,
                             struct wl_resource *region_resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandSurfaceState *pending = surface->pending_state;

  /* X11 unmanaged window */
  if (!surface)
    return;

  g_clear_pointer (&pending->input_region, cairo_region_destroy);
  if (region_resource)
    {
      MetaWaylandRegion *region = wl_resource_get_user_data (region_resource);
      cairo_region_t *cr_region = meta_wayland_region_peek_cairo_region (region);
      pending->input_region = cairo_region_copy (cr_region);
    }
  pending->input_region_set = TRUE;
}

static void
wl_surface_commit (struct wl_client *client,
                   struct wl_resource *resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (resource);

  /* X11 unmanaged window */
  if (!surface)
    return;

  meta_wayland_surface_commit (surface);
}

static MetaMonitorTransform
transform_from_wl_output_transform (int32_t transform_value)
{
  enum wl_output_transform transform = transform_value;

  switch (transform)
    {
    case WL_OUTPUT_TRANSFORM_NORMAL:
      return META_MONITOR_TRANSFORM_NORMAL;
    case WL_OUTPUT_TRANSFORM_90:
      return META_MONITOR_TRANSFORM_90;
    case WL_OUTPUT_TRANSFORM_180:
      return META_MONITOR_TRANSFORM_180;
    case WL_OUTPUT_TRANSFORM_270:
      return META_MONITOR_TRANSFORM_270;
    case WL_OUTPUT_TRANSFORM_FLIPPED:
      return META_MONITOR_TRANSFORM_FLIPPED;
    case WL_OUTPUT_TRANSFORM_FLIPPED_90:
      return META_MONITOR_TRANSFORM_FLIPPED_90;
    case WL_OUTPUT_TRANSFORM_FLIPPED_180:
      return META_MONITOR_TRANSFORM_FLIPPED_180;
    case WL_OUTPUT_TRANSFORM_FLIPPED_270:
      return META_MONITOR_TRANSFORM_FLIPPED_270;
    default:
      return -1;
    }
}

static void
wl_surface_set_buffer_transform (struct wl_client   *client,
                                 struct wl_resource *resource,
                                 int32_t             transform)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (resource);
  MetaWaylandSurfaceState *pending = surface->pending_state;
  MetaMonitorTransform buffer_transform;

  buffer_transform = transform_from_wl_output_transform (transform);

  if (buffer_transform == -1)
    {
      wl_resource_post_error (resource,
                              WL_SURFACE_ERROR_INVALID_TRANSFORM,
                              "Trying to set invalid buffer_transform of %d\n",
                              transform);
      return;
    }

  pending->buffer_transform = buffer_transform;
  pending->has_new_buffer_transform = TRUE;
}

static void
wl_surface_set_buffer_scale (struct wl_client *client,
                             struct wl_resource *resource,
                             int scale)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (resource);
  MetaWaylandSurfaceState *pending = surface->pending_state;

  if (scale <= 0)
    {
      wl_resource_post_error (resource,
                              WL_SURFACE_ERROR_INVALID_SCALE,
                              "Trying to set invalid buffer_scale of %d\n",
                              scale);
      return;
    }

  pending->scale = scale;
}

static void
wl_surface_damage_buffer (struct wl_client   *client,
                          struct wl_resource *surface_resource,
                          int32_t             x,
                          int32_t             y,
                          int32_t             width,
                          int32_t             height)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandSurfaceState *pending = surface->pending_state;
  cairo_rectangle_int_t rectangle;

  /* X11 unmanaged window */
  if (!surface)
    return;

  rectangle = (cairo_rectangle_int_t) {
    .x = x,
    .y = y,
    .width = width,
    .height = height
  };
  cairo_region_union_rectangle (pending->buffer_damage, &rectangle);
}

static const struct wl_surface_interface meta_wayland_wl_surface_interface = {
  wl_surface_destroy,
  wl_surface_attach,
  wl_surface_damage,
  wl_surface_frame,
  wl_surface_set_opaque_region,
  wl_surface_set_input_region,
  wl_surface_commit,
  wl_surface_set_buffer_transform,
  wl_surface_set_buffer_scale,
  wl_surface_damage_buffer,
};

static void
surface_entered_output (MetaWaylandSurface *surface,
                        MetaWaylandOutput *wayland_output)
{
  GList *iter;
  struct wl_resource *resource;

  for (iter = wayland_output->resources; iter != NULL; iter = iter->next)
    {
      resource = iter->data;

      if (wl_resource_get_client (resource) !=
          wl_resource_get_client (surface->resource))
        continue;

      wl_surface_send_enter (surface->resource, resource);
    }
}

static void
surface_left_output (MetaWaylandSurface *surface,
                     MetaWaylandOutput *wayland_output)
{
  GList *iter;
  struct wl_resource *resource;

  for (iter = wayland_output->resources; iter != NULL; iter = iter->next)
    {
      resource = iter->data;

      if (wl_resource_get_client (resource) !=
          wl_resource_get_client (surface->resource))
        continue;

      wl_surface_send_leave (surface->resource, resource);
    }
}

static void
set_surface_is_on_output (MetaWaylandSurface *surface,
                          MetaWaylandOutput *wayland_output,
                          gboolean is_on_output);

static void
surface_handle_output_destroy (MetaWaylandOutput *wayland_output,
                               MetaWaylandSurface *surface)
{
  set_surface_is_on_output (surface, wayland_output, FALSE);
}

static void
set_surface_is_on_output (MetaWaylandSurface *surface,
                          MetaWaylandOutput *wayland_output,
                          gboolean is_on_output)
{
  gpointer orig_id;
  gboolean was_on_output = g_hash_table_lookup_extended (surface->outputs_to_destroy_notify_id,
                                                         wayland_output,
                                                         NULL, &orig_id);

  if (!was_on_output && is_on_output)
    {
      gulong id;

      id = g_signal_connect (wayland_output, "output-destroyed",
                             G_CALLBACK (surface_handle_output_destroy),
                             surface);
      g_hash_table_insert (surface->outputs_to_destroy_notify_id, wayland_output,
                           GSIZE_TO_POINTER ((gsize)id));
      surface_entered_output (surface, wayland_output);
    }
  else if (was_on_output && !is_on_output)
    {
      g_hash_table_remove (surface->outputs_to_destroy_notify_id, wayland_output);
      g_signal_handler_disconnect (wayland_output, (gulong) GPOINTER_TO_SIZE (orig_id));
      surface_left_output (surface, wayland_output);
    }
}

static void
update_surface_output_state (gpointer key, gpointer value, gpointer user_data)
{
  MetaWaylandOutput *wayland_output = value;
  MetaWaylandSurface *surface = user_data;
  MetaLogicalMonitor *logical_monitor;
  gboolean is_on_logical_monitor;

  g_assert (surface->role);

  logical_monitor = wayland_output->logical_monitor;
  if (!logical_monitor)
    {
      set_surface_is_on_output (surface, wayland_output, FALSE);
      return;
    }

  is_on_logical_monitor =
    meta_wayland_surface_role_is_on_logical_monitor (surface->role,
                                                     logical_monitor);
  set_surface_is_on_output (surface, wayland_output, is_on_logical_monitor);
}

static void
surface_output_disconnect_signal (gpointer key, gpointer value, gpointer user_data)
{
  g_signal_handler_disconnect (key, (gulong) GPOINTER_TO_SIZE (value));
}

void
meta_wayland_surface_update_outputs (MetaWaylandSurface *surface)
{
  if (!surface->compositor)
    return;

  g_hash_table_foreach (surface->compositor->outputs,
                        update_surface_output_state,
                        surface);
}

void
meta_wayland_surface_update_outputs_recursively (MetaWaylandSurface *surface)
{
  MetaWaylandSurface *subsurface_surface;

  meta_wayland_surface_update_outputs (surface);

  META_WAYLAND_SURFACE_FOREACH_SUBSURFACE (surface, subsurface_surface)
    meta_wayland_surface_update_outputs_recursively (subsurface_surface);
}

void
meta_wayland_surface_notify_unmapped (MetaWaylandSurface *surface)
{
  g_signal_emit (surface, surface_signals[SURFACE_UNMAPPED], 0);
}

static void
unlink_note (GNode    *node,
             gpointer  data)
{
  g_node_unlink (node);
}

static void
wl_surface_destructor (struct wl_resource *resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (resource);
  MetaWaylandCompositor *compositor = surface->compositor;
  MetaWaylandFrameCallback *cb, *next;

  g_signal_emit (surface, surface_signals[SURFACE_DESTROY], 0);

  g_clear_object (&surface->role);

  if (surface->unassigned.buffer)
    {
      meta_wayland_surface_unref_buffer_use_count (surface);
      g_clear_object (&surface->unassigned.buffer);
    }

  if (surface->buffer_held)
    meta_wayland_surface_unref_buffer_use_count (surface);
  g_clear_pointer (&surface->texture, cogl_object_unref);
  g_clear_object (&surface->buffer_ref.buffer);

  g_clear_object (&surface->cached_state);
  g_clear_object (&surface->pending_state);

  if (surface->opaque_region)
    cairo_region_destroy (surface->opaque_region);
  if (surface->input_region)
    cairo_region_destroy (surface->input_region);

  meta_wayland_compositor_remove_frame_callback_surface (compositor, surface);

  g_hash_table_foreach (surface->outputs_to_destroy_notify_id,
                        surface_output_disconnect_signal,
                        surface);
  g_hash_table_unref (surface->outputs_to_destroy_notify_id);

  wl_list_for_each_safe (cb, next,
                         &surface->unassigned.pending_frame_callback_list,
                         link)
    wl_resource_destroy (cb->resource);

  if (surface->resource)
    wl_resource_set_user_data (surface->resource, NULL);

  if (surface->wl_subsurface)
    wl_resource_destroy (surface->wl_subsurface);

  if (surface->subsurface_branch_node)
    {
      g_node_children_foreach (surface->subsurface_branch_node,
                               G_TRAVERSE_NON_LEAVES,
                               unlink_note,
                               NULL);
      g_clear_pointer (&surface->subsurface_branch_node, g_node_destroy);
    }

  g_hash_table_destroy (surface->shortcut_inhibited_seats);

  g_object_unref (surface);

  meta_wayland_compositor_repick (compositor);
}

MetaWaylandSurface *
meta_wayland_surface_create (MetaWaylandCompositor *compositor,
                             struct wl_client      *client,
                             struct wl_resource    *compositor_resource,
                             guint32                id)
{
  MetaWaylandSurface *surface = g_object_new (META_TYPE_WAYLAND_SURFACE, NULL);
  int surface_version;

  surface->compositor = compositor;
  surface->scale = 1;

  surface_version = wl_resource_get_version (compositor_resource);
  surface->resource = wl_resource_create (client,
                                          &wl_surface_interface,
                                          surface_version,
                                          id);
  wl_resource_set_implementation (surface->resource,
                                  &meta_wayland_wl_surface_interface,
                                  surface,
                                  wl_surface_destructor);

  wl_list_init (&surface->unassigned.pending_frame_callback_list);

  surface->outputs_to_destroy_notify_id = g_hash_table_new (NULL, NULL);
  surface->shortcut_inhibited_seats = g_hash_table_new (NULL, NULL);

  meta_wayland_compositor_notify_surface_id (compositor, id, surface);

  return surface;
}

gboolean
meta_wayland_surface_begin_grab_op (MetaWaylandSurface *surface,
                                    MetaWaylandSeat    *seat,
                                    MetaGrabOp          grab_op,
                                    gfloat              x,
                                    gfloat              y)
{
  MetaWindow *window = meta_wayland_surface_get_window (surface);

  if (grab_op == META_GRAB_OP_NONE)
    return FALSE;

  /* This is an input driven operation so we set frame_action to
     constrain it in the same way as it would be if the window was
     being moved/resized via a SSD event. */
  return meta_display_begin_grab_op (window->display,
                                     window,
                                     grab_op,
                                     TRUE, /* pointer_already_grabbed */
                                     TRUE, /* frame_action */
                                     1, /* button. XXX? */
                                     0, /* modmask */
                                     meta_display_get_current_time_roundtrip (window->display),
                                     x, y);
}

/**
 * meta_wayland_shell_init:
 * @compositor: The #MetaWaylandCompositor object
 *
 * Initializes the Wayland interfaces providing features that deal with
 * desktop-specific conundrums, like XDG shell, wl_shell (deprecated), etc.
 */
void
meta_wayland_shell_init (MetaWaylandCompositor *compositor)
{
  meta_wayland_xdg_shell_init (compositor);
  meta_wayland_legacy_xdg_shell_init (compositor);
  meta_wayland_wl_shell_init (compositor);
  meta_wayland_init_gtk_shell (compositor);
  meta_wayland_init_viewporter (compositor);
}

void
meta_wayland_surface_configure_notify (MetaWaylandSurface             *surface,
                                       MetaWaylandWindowConfiguration *configuration)
{
  MetaWaylandShellSurface *shell_surface =
    META_WAYLAND_SHELL_SURFACE (surface->role);

  g_signal_emit (surface, surface_signals[SURFACE_CONFIGURE], 0);

  meta_wayland_shell_surface_configure (shell_surface, configuration);
}

void
meta_wayland_surface_ping (MetaWaylandSurface *surface,
                           guint32             serial)
{
  MetaWaylandShellSurface *shell_surface =
    META_WAYLAND_SHELL_SURFACE (surface->role);

  meta_wayland_shell_surface_ping (shell_surface, serial);
}

void
meta_wayland_surface_delete (MetaWaylandSurface *surface)
{
  MetaWaylandShellSurface *shell_surface =
    META_WAYLAND_SHELL_SURFACE (surface->role);

  meta_wayland_shell_surface_close (shell_surface);
}

void
meta_wayland_surface_window_managed (MetaWaylandSurface *surface,
                                     MetaWindow         *window)
{
  MetaWaylandShellSurface *shell_surface =
    META_WAYLAND_SHELL_SURFACE (surface->role);

  meta_wayland_shell_surface_managed (shell_surface, window);
}

void
meta_wayland_surface_drag_dest_focus_in (MetaWaylandSurface   *surface,
                                         MetaWaylandDataOffer *offer)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaWaylandDataDevice *data_device = &compositor->seat->data_device;

  surface->dnd.funcs->focus_in (data_device, surface, offer);
}

void
meta_wayland_surface_drag_dest_motion (MetaWaylandSurface *surface,
                                       const ClutterEvent *event)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaWaylandDataDevice *data_device = &compositor->seat->data_device;

  surface->dnd.funcs->motion (data_device, surface, event);
}

void
meta_wayland_surface_drag_dest_focus_out (MetaWaylandSurface *surface)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaWaylandDataDevice *data_device = &compositor->seat->data_device;

  surface->dnd.funcs->focus_out (data_device, surface);
}

void
meta_wayland_surface_drag_dest_drop (MetaWaylandSurface *surface)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaWaylandDataDevice *data_device = &compositor->seat->data_device;

  surface->dnd.funcs->drop (data_device, surface);
}

void
meta_wayland_surface_drag_dest_update (MetaWaylandSurface *surface)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaWaylandDataDevice *data_device = &compositor->seat->data_device;

  surface->dnd.funcs->update (data_device, surface);
}

MetaWaylandSurface *
meta_wayland_surface_get_toplevel (MetaWaylandSurface *surface)
{
  if (surface->role)
    return meta_wayland_surface_role_get_toplevel (surface->role);
  else
    return NULL;
}

MetaWindow *
meta_wayland_surface_get_toplevel_window (MetaWaylandSurface *surface)
{
  MetaWaylandSurface *toplevel;

  toplevel = meta_wayland_surface_get_toplevel (surface);
  if (toplevel)
    return meta_wayland_surface_get_window (toplevel);
  else
    return NULL;
}

void
meta_wayland_surface_get_relative_coordinates (MetaWaylandSurface *surface,
                                               float               abs_x,
                                               float               abs_y,
                                               float               *sx,
                                               float               *sy)
{
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_GET_CLASS (surface->role);

  surface_role_class->get_relative_coordinates (surface->role,
                                                abs_x, abs_y,
                                                sx, sy);
}

void
meta_wayland_surface_get_absolute_coordinates (MetaWaylandSurface *surface,
                                               float               sx,
                                               float               sy,
                                               float               *x,
                                               float               *y)
{
  ClutterActor *actor =
    CLUTTER_ACTOR (meta_wayland_surface_get_actor (surface));
  graphene_point3d_t sv = {
    .x = sx,
    .y = sy,
  };
  graphene_point3d_t v = { 0 };

  clutter_actor_apply_relative_transform_to_point (actor, NULL, &sv, &v);

  *x = v.x;
  *y = v.y;
}

static void
meta_wayland_surface_init (MetaWaylandSurface *surface)
{
  surface->pending_state = g_object_new (META_TYPE_WAYLAND_SURFACE_STATE, NULL);
  surface->subsurface_branch_node = g_node_new (surface);
  surface->subsurface_leaf_node =
    g_node_prepend_data (surface->subsurface_branch_node, surface);

  g_signal_connect (surface, "geometry-changed",
                    G_CALLBACK (meta_wayland_surface_update_outputs_recursively),
                    NULL);
}

static void
meta_wayland_surface_class_init (MetaWaylandSurfaceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  surface_signals[SURFACE_DESTROY] =
    g_signal_new ("destroy",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  surface_signals[SURFACE_UNMAPPED] =
    g_signal_new ("unmapped",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  surface_signals[SURFACE_CONFIGURE] =
    g_signal_new ("configure",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  surface_signals[SURFACE_SHORTCUTS_INHIBITED] =
    g_signal_new ("shortcuts-inhibited",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  surface_signals[SURFACE_SHORTCUTS_RESTORED] =
    g_signal_new ("shortcuts-restored",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
  surface_signals[SURFACE_GEOMETRY_CHANGED] =
    g_signal_new ("geometry-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
  surface_signals[SURFACE_PRE_STATE_APPLIED] =
    g_signal_new ("pre-state-applied",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

static void
meta_wayland_surface_role_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  MetaWaylandSurfaceRole *surface_role = META_WAYLAND_SURFACE_ROLE (object);
  MetaWaylandSurfaceRolePrivate *priv =
    meta_wayland_surface_role_get_instance_private (surface_role);

  switch (prop_id)
    {
    case SURFACE_ROLE_PROP_SURFACE:
      priv->surface = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_wayland_surface_role_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  MetaWaylandSurfaceRole *surface_role = META_WAYLAND_SURFACE_ROLE (object);
  MetaWaylandSurfaceRolePrivate *priv =
    meta_wayland_surface_role_get_instance_private (surface_role);

  switch (prop_id)
    {
    case SURFACE_ROLE_PROP_SURFACE:
      g_value_set_object (value, priv->surface);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_wayland_surface_role_init (MetaWaylandSurfaceRole *role)
{
}

static void
meta_wayland_surface_role_class_init (MetaWaylandSurfaceRoleClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = meta_wayland_surface_role_set_property;
  object_class->get_property = meta_wayland_surface_role_get_property;

  g_object_class_install_property (object_class,
                                   SURFACE_ROLE_PROP_SURFACE,
                                   g_param_spec_object ("surface",
                                                        "MetaWaylandSurface",
                                                        "The MetaWaylandSurface instance",
                                                        META_TYPE_WAYLAND_SURFACE,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

static void
meta_wayland_surface_role_assigned (MetaWaylandSurfaceRole *surface_role)
{
  META_WAYLAND_SURFACE_ROLE_GET_CLASS (surface_role)->assigned (surface_role);
}

static void
meta_wayland_surface_role_pre_apply_state (MetaWaylandSurfaceRole  *surface_role,
                                           MetaWaylandSurfaceState *pending)
{
  MetaWaylandSurfaceRoleClass *klass;

  klass = META_WAYLAND_SURFACE_ROLE_GET_CLASS (surface_role);
  if (klass->pre_apply_state)
    klass->pre_apply_state (surface_role, pending);
}

static void
meta_wayland_surface_role_post_apply_state (MetaWaylandSurfaceRole  *surface_role,
                                            MetaWaylandSurfaceState *pending)
{
  MetaWaylandSurfaceRoleClass *klass;

  klass = META_WAYLAND_SURFACE_ROLE_GET_CLASS (surface_role);
  if (klass->post_apply_state)
    klass->post_apply_state (surface_role, pending);
}

static void
meta_wayland_surface_role_apply_state (MetaWaylandSurfaceRole  *surface_role,
                                       MetaWaylandSurfaceState *pending)
{
  META_WAYLAND_SURFACE_ROLE_GET_CLASS (surface_role)->apply_state (surface_role,
                                                                   pending);
}

static gboolean
meta_wayland_surface_role_is_on_logical_monitor (MetaWaylandSurfaceRole *surface_role,
                                                 MetaLogicalMonitor     *logical_monitor)
{
  MetaWaylandSurfaceRoleClass *klass;

  klass = META_WAYLAND_SURFACE_ROLE_GET_CLASS (surface_role);
  if (klass->is_on_logical_monitor)
    return klass->is_on_logical_monitor (surface_role, logical_monitor);
  else
    return FALSE;
}

static MetaWaylandSurface *
meta_wayland_surface_role_get_toplevel (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurfaceRoleClass *klass;

  klass = META_WAYLAND_SURFACE_ROLE_GET_CLASS (surface_role);
  if (klass->get_toplevel)
    return klass->get_toplevel (surface_role);
  else
    return NULL;
}

static MetaWindow *
meta_wayland_surface_role_get_window (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurfaceRoleClass *klass;

  klass = META_WAYLAND_SURFACE_ROLE_GET_CLASS (surface_role);

  if (klass->get_window)
    return klass->get_window (surface_role);
  else
    return NULL;
}

MetaWindow *
meta_wayland_surface_get_window (MetaWaylandSurface *surface)
{
  if (!surface->role)
    return NULL;

  return meta_wayland_surface_role_get_window (surface->role);
}

static gboolean
meta_wayland_surface_role_should_cache_state (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurfaceRoleClass *klass;

  klass = META_WAYLAND_SURFACE_ROLE_GET_CLASS (surface_role);
  if (klass->should_cache_state)
    return klass->should_cache_state (surface_role);
  else
    return FALSE;
}

gboolean
meta_wayland_surface_should_cache_state (MetaWaylandSurface *surface)
{
  if (!surface->role)
    return FALSE;

  return meta_wayland_surface_role_should_cache_state (surface->role);
}

static void
meta_wayland_surface_role_notify_subsurface_state_changed (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurfaceRoleClass *klass;

  klass = META_WAYLAND_SURFACE_ROLE_GET_CLASS (surface_role);
  g_return_if_fail (klass->notify_subsurface_state_changed);

  klass->notify_subsurface_state_changed (surface_role);
}

void
meta_wayland_surface_notify_subsurface_state_changed (MetaWaylandSurface *surface)
{
  if (surface->role)
    meta_wayland_surface_role_notify_subsurface_state_changed (surface->role);
}

MetaWaylandSurface *
meta_wayland_surface_role_get_surface (MetaWaylandSurfaceRole *role)
{
  MetaWaylandSurfaceRolePrivate *priv =
    meta_wayland_surface_role_get_instance_private (role);

  return priv->surface;
}

cairo_region_t *
meta_wayland_surface_calculate_input_region (MetaWaylandSurface *surface)
{
  cairo_region_t *region;
  cairo_rectangle_int_t buffer_rect;

  if (!surface->buffer_ref.buffer)
    return NULL;

  buffer_rect = (cairo_rectangle_int_t) {
    .width = meta_wayland_surface_get_width (surface),
    .height = meta_wayland_surface_get_height (surface),
  };
  region = cairo_region_create_rectangle (&buffer_rect);

  if (surface->input_region)
    cairo_region_intersect (region, surface->input_region);

  return region;
}

void
meta_wayland_surface_inhibit_shortcuts (MetaWaylandSurface *surface,
                                        MetaWaylandSeat    *seat)
{
  g_hash_table_add (surface->shortcut_inhibited_seats, seat);
  g_signal_emit (surface, surface_signals[SURFACE_SHORTCUTS_INHIBITED], 0);
}

void
meta_wayland_surface_restore_shortcuts (MetaWaylandSurface *surface,
                                        MetaWaylandSeat    *seat)
{
  g_signal_emit (surface, surface_signals[SURFACE_SHORTCUTS_RESTORED], 0);
  g_hash_table_remove (surface->shortcut_inhibited_seats, seat);
}

gboolean
meta_wayland_surface_is_shortcuts_inhibited (MetaWaylandSurface *surface,
                                             MetaWaylandSeat    *seat)
{
  if (surface->shortcut_inhibited_seats == NULL)
    return FALSE;

  return g_hash_table_contains (surface->shortcut_inhibited_seats, seat);
}

CoglTexture *
meta_wayland_surface_get_texture (MetaWaylandSurface *surface)
{
  return surface->texture;
}

MetaSurfaceActor *
meta_wayland_surface_get_actor (MetaWaylandSurface *surface)
{
  if (!surface->role || !META_IS_WAYLAND_ACTOR_SURFACE (surface->role))
    return NULL;

  return meta_wayland_actor_surface_get_actor (META_WAYLAND_ACTOR_SURFACE (surface->role));
}

void
meta_wayland_surface_notify_geometry_changed (MetaWaylandSurface *surface)
{
  g_signal_emit (surface, surface_signals[SURFACE_GEOMETRY_CHANGED], 0);
}

int
meta_wayland_surface_get_width (MetaWaylandSurface *surface)
{
  if (surface->viewport.has_dst_size)
    {
      return surface->viewport.dst_width;
    }
  else if (surface->viewport.has_src_rect)
    {
      return ceilf (surface->viewport.src_rect.size.width);
    }
  else
    {
      int width;

      if (meta_monitor_transform_is_rotated (surface->buffer_transform))
        width = get_buffer_height (surface);
      else
        width = get_buffer_width (surface);

      return width / surface->scale;
    }
}

int
meta_wayland_surface_get_height (MetaWaylandSurface *surface)
{
  if (surface->viewport.has_dst_size)
    {
      return surface->viewport.dst_height;
    }
  else if (surface->viewport.has_src_rect)
    {
      return ceilf (surface->viewport.src_rect.size.height);
    }
  else
    {
      int height;

      if (meta_monitor_transform_is_rotated (surface->buffer_transform))
        height = get_buffer_width (surface);
      else
        height = get_buffer_height (surface);

      return height / surface->scale;
    }
}
