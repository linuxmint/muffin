/*
 * Copyright (C) 2013 Red Hat, Inc.
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

#ifndef META_WAYLAND_SURFACE_H
#define META_WAYLAND_SURFACE_H

#include <cairo.h>
#include <glib.h>
#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>

#include "backends/meta-monitor-manager-private.h"
#include "clutter/clutter.h"
#include "compositor/meta-shaped-texture-private.h"
#include "compositor/meta-surface-actor.h"
#include "meta/meta-cursor-tracker.h"
#include "wayland/meta-wayland-pointer-constraints.h"
#include "wayland/meta-wayland-types.h"

#define META_TYPE_WAYLAND_SURFACE (meta_wayland_surface_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandSurface,
                      meta_wayland_surface,
                      META, WAYLAND_SURFACE,
                      GObject);

#define META_TYPE_WAYLAND_SURFACE_ROLE (meta_wayland_surface_role_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaWaylandSurfaceRole, meta_wayland_surface_role,
                          META, WAYLAND_SURFACE_ROLE, GObject);

#define META_TYPE_WAYLAND_SURFACE_STATE (meta_wayland_surface_state_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandSurfaceState,
                      meta_wayland_surface_state,
                      META, WAYLAND_SURFACE_STATE,
                      GObject)

struct _MetaWaylandSurfaceRoleClass
{
  GObjectClass parent_class;

  void (*assigned) (MetaWaylandSurfaceRole *surface_role);
  void (*pre_apply_state) (MetaWaylandSurfaceRole  *surface_role,
                           MetaWaylandSurfaceState *pending);
  void (*apply_state) (MetaWaylandSurfaceRole  *surface_role,
                       MetaWaylandSurfaceState *pending);
  void (*post_apply_state) (MetaWaylandSurfaceRole  *surface_role,
                            MetaWaylandSurfaceState *pending);
  gboolean (*is_on_logical_monitor) (MetaWaylandSurfaceRole *surface_role,
                                     MetaLogicalMonitor     *logical_monitor);
  MetaWaylandSurface * (*get_toplevel) (MetaWaylandSurfaceRole *surface_role);
  gboolean (*should_cache_state) (MetaWaylandSurfaceRole *surface_role);
  void (*notify_subsurface_state_changed) (MetaWaylandSurfaceRole *surface_role);
  void (*get_relative_coordinates) (MetaWaylandSurfaceRole *surface_role,
                                    float                   abs_x,
                                    float                   abs_y,
                                    float                  *out_sx,
                                    float                  *out_sy);
  MetaWindow * (*get_window) (MetaWaylandSurfaceRole *surface_role);
};

struct _MetaWaylandSurfaceState
{
  GObject parent;

  /* wl_surface.attach */
  gboolean newly_attached;
  MetaWaylandBuffer *buffer;
  gulong buffer_destroy_handler_id;
  int32_t dx;
  int32_t dy;

  int scale;

  /* wl_surface.damage */
  cairo_region_t *surface_damage;
  /* wl_surface.damage_buffer */
  cairo_region_t *buffer_damage;

  cairo_region_t *input_region;
  gboolean input_region_set;
  cairo_region_t *opaque_region;
  gboolean opaque_region_set;

  /* wl_surface.frame */
  struct wl_list frame_callback_list;

  MetaRectangle new_geometry;
  gboolean has_new_geometry;

  gboolean has_acked_configure_serial;
  uint32_t acked_configure_serial;

  /* pending min/max size in window geometry coordinates */
  gboolean has_new_min_size;
  int new_min_width;
  int new_min_height;
  gboolean has_new_max_size;
  int new_max_width;
  int new_max_height;

  gboolean has_new_buffer_transform;
  MetaMonitorTransform buffer_transform;
  gboolean has_new_viewport_src_rect;
  graphene_rect_t viewport_src_rect;
  gboolean has_new_viewport_dst_size;
  int viewport_dst_width;
  int viewport_dst_height;

  GSList *subsurface_placement_ops;
};

struct _MetaWaylandDragDestFuncs
{
  void (* focus_in)  (MetaWaylandDataDevice *data_device,
                      MetaWaylandSurface    *surface,
                      MetaWaylandDataOffer  *offer);
  void (* focus_out) (MetaWaylandDataDevice *data_device,
                      MetaWaylandSurface    *surface);
  void (* motion)    (MetaWaylandDataDevice *data_device,
                      MetaWaylandSurface    *surface,
                      const ClutterEvent    *event);
  void (* drop)      (MetaWaylandDataDevice *data_device,
                      MetaWaylandSurface    *surface);
  void (* update)    (MetaWaylandDataDevice *data_device,
                      MetaWaylandSurface    *surface);
};

typedef struct _MetaWaylandBufferRef
{
  grefcount ref_count;
  MetaWaylandBuffer *buffer;
  unsigned int use_count;
} MetaWaylandBufferRef;

struct _MetaWaylandSurface
{
  GObject parent;

  /* Generic stuff */
  struct wl_resource *resource;
  MetaWaylandCompositor *compositor;
  MetaWaylandSurfaceRole *role;
  cairo_region_t *input_region;
  cairo_region_t *opaque_region;
  int scale;
  int32_t offset_x, offset_y;
  GNode *subsurface_branch_node;
  GNode *subsurface_leaf_node;
  GHashTable *outputs_to_destroy_notify_id;
  MetaMonitorTransform buffer_transform;

  CoglTexture *texture;

  MetaWaylandBufferRef *buffer_ref;

  /* Buffer renderer state. */
  gboolean buffer_held;

  /* Intermediate state for when no role has been assigned. */
  struct {
    struct wl_list pending_frame_callback_list;
    MetaWaylandBuffer *buffer;
  } unassigned;

  struct {
    const MetaWaylandDragDestFuncs *funcs;
  } dnd;

  /* All the pending state that wl_surface.commit will apply. */
  MetaWaylandSurfaceState *pending_state;
  /* State cached due to inter-surface synchronization such. */
  MetaWaylandSurfaceState *cached_state;

  /* Extension resources. */
  struct wl_resource *wl_subsurface;

  /* wl_subsurface stuff. */
  struct {
    MetaWaylandSurface *parent;
    struct wl_listener parent_destroy_listener;

    int x;
    int y;

    /* When the surface is synchronous, its state will be applied
     * when the parent is committed. This is done by moving the
     * "real" pending state below to here when this surface is
     * committed and in synchronous mode.
     *
     * When the parent surface is committed, we apply the pending
     * state here.
     */
    gboolean synchronous;

    int32_t pending_x;
    int32_t pending_y;
    gboolean pending_pos;
  } sub;

  /* wp_viewport */
  struct {
    struct wl_resource *resource;
    gulong destroy_handler_id;

    gboolean has_src_rect;
    graphene_rect_t src_rect;

    gboolean has_dst_size;
    int dst_width;
    int dst_height;
  } viewport;

  /* table of seats for which shortcuts are inhibited */
  GHashTable *shortcut_inhibited_seats;
};

void                meta_wayland_shell_init     (MetaWaylandCompositor *compositor);

MetaWaylandSurface *meta_wayland_surface_create (MetaWaylandCompositor *compositor,
                                                 struct wl_client      *client,
                                                 struct wl_resource    *compositor_resource,
                                                 guint32                id);

MetaWaylandSurfaceState *
                    meta_wayland_surface_get_pending_state (MetaWaylandSurface *surface);

MetaWaylandSurfaceState *
                    meta_wayland_surface_ensure_cached_state (MetaWaylandSurface *surface);

void                meta_wayland_surface_apply_cached_state (MetaWaylandSurface *surface);

gboolean            meta_wayland_surface_is_effectively_synchronized (MetaWaylandSurface *surface);

gboolean            meta_wayland_surface_assign_role (MetaWaylandSurface *surface,
                                                      GType               role_type,
                                                      const char         *first_property_name,
                                                      ...);

MetaWaylandBuffer  *meta_wayland_surface_get_buffer (MetaWaylandSurface *surface);

void                meta_wayland_surface_ref_buffer_use_count (MetaWaylandSurface *surface);

void                meta_wayland_surface_unref_buffer_use_count (MetaWaylandSurface *surface);

void                meta_wayland_surface_set_window (MetaWaylandSurface *surface,
                                                     MetaWindow         *window);

void                meta_wayland_surface_configure_notify (MetaWaylandSurface             *surface,
                                                           MetaWaylandWindowConfiguration *configuration);

void                meta_wayland_surface_ping (MetaWaylandSurface *surface,
                                               guint32             serial);
void                meta_wayland_surface_delete (MetaWaylandSurface *surface);

/* Drag dest functions */
void                meta_wayland_surface_drag_dest_focus_in  (MetaWaylandSurface   *surface,
                                                              MetaWaylandDataOffer *offer);
void                meta_wayland_surface_drag_dest_motion    (MetaWaylandSurface   *surface,
                                                              const ClutterEvent   *event);
void                meta_wayland_surface_drag_dest_focus_out (MetaWaylandSurface   *surface);
void                meta_wayland_surface_drag_dest_drop      (MetaWaylandSurface   *surface);
void                meta_wayland_surface_drag_dest_update    (MetaWaylandSurface   *surface);

void                meta_wayland_surface_update_outputs (MetaWaylandSurface *surface);

MetaWaylandSurface *meta_wayland_surface_get_toplevel (MetaWaylandSurface *surface);

MetaWindow *        meta_wayland_surface_get_window (MetaWaylandSurface *surface);

gboolean            meta_wayland_surface_should_cache_state (MetaWaylandSurface *surface);

MetaWindow *        meta_wayland_surface_get_toplevel_window (MetaWaylandSurface *surface);

void                meta_wayland_surface_queue_pending_frame_callbacks (MetaWaylandSurface *surface);

void                meta_wayland_surface_queue_pending_state_frame_callbacks (MetaWaylandSurface      *surface,
                                                                              MetaWaylandSurfaceState *pending);

void                meta_wayland_surface_get_relative_coordinates (MetaWaylandSurface *surface,
                                                                   float               abs_x,
                                                                   float               abs_y,
                                                                   float              *sx,
                                                                   float              *sy);

void                meta_wayland_surface_get_absolute_coordinates (MetaWaylandSurface *surface,
                                                                   float               sx,
                                                                   float               sy,
                                                                   float              *x,
                                                                   float              *y);

MetaWaylandSurface * meta_wayland_surface_role_get_surface (MetaWaylandSurfaceRole *role);

cairo_region_t *    meta_wayland_surface_calculate_input_region (MetaWaylandSurface *surface);


gboolean            meta_wayland_surface_begin_grab_op (MetaWaylandSurface *surface,
                                                        MetaWaylandSeat    *seat,
                                                        MetaGrabOp          grab_op,
                                                        gfloat              x,
                                                        gfloat              y);

void                meta_wayland_surface_window_managed (MetaWaylandSurface *surface,
                                                         MetaWindow         *window);

void                meta_wayland_surface_inhibit_shortcuts (MetaWaylandSurface *surface,
                                                            MetaWaylandSeat    *seat);

void                meta_wayland_surface_restore_shortcuts (MetaWaylandSurface *surface,
                                                            MetaWaylandSeat    *seat);

gboolean            meta_wayland_surface_is_shortcuts_inhibited (MetaWaylandSurface *surface,
                                                                 MetaWaylandSeat    *seat);

CoglTexture *       meta_wayland_surface_get_texture (MetaWaylandSurface *surface);

MetaSurfaceActor *  meta_wayland_surface_get_actor (MetaWaylandSurface *surface);

void                meta_wayland_surface_notify_geometry_changed (MetaWaylandSurface *surface);

void                meta_wayland_surface_notify_subsurface_state_changed (MetaWaylandSurface *surface);

void                meta_wayland_surface_notify_unmapped (MetaWaylandSurface *surface);

void                meta_wayland_surface_update_outputs_recursively (MetaWaylandSurface *surface);

int                 meta_wayland_surface_get_width (MetaWaylandSurface *surface);
int                 meta_wayland_surface_get_height (MetaWaylandSurface *surface);

CoglScanout *       meta_wayland_surface_try_acquire_scanout (MetaWaylandSurface *surface,
                                                              CoglOnscreen       *onscreen);

static inline GNode *
meta_get_next_subsurface_sibling (GNode *n)
{
  GNode *next;

  if (!n)
    return NULL;

  next = g_node_next_sibling (n);
  if (!next)
    return NULL;
  if (!G_NODE_IS_LEAF (next))
    return next;
  else
    return meta_get_next_subsurface_sibling (next);
}

static inline GNode *
meta_get_first_subsurface_node (MetaWaylandSurface *surface)
{
  GNode *n;

  n = g_node_first_child (surface->subsurface_branch_node);
  if (!G_NODE_IS_LEAF (n))
    return n;
  else
    return meta_get_next_subsurface_sibling (n);
}

#define META_WAYLAND_SURFACE_FOREACH_SUBSURFACE(surface, subsurface) \
  for (GNode *G_PASTE(__n, __LINE__) = meta_get_first_subsurface_node ((surface)); \
       (subsurface = (G_PASTE (__n, __LINE__) ? G_PASTE (__n, __LINE__)->data : NULL)); \
       G_PASTE (__n, __LINE__) = meta_get_next_subsurface_sibling (G_PASTE (__n, __LINE__)))

#endif
