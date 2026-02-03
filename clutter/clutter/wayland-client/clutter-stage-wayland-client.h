/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2024 Linux Mint
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *   Michael Webster <miketwebster@gmail.com>
 */

#ifndef __CLUTTER_STAGE_WAYLAND_CLIENT_H__
#define __CLUTTER_STAGE_WAYLAND_CLIENT_H__

#include <wayland-client.h>
#include <wayland-egl.h>

#include <clutter/clutter-backend.h>
#include <clutter/clutter-stage.h>

#include "cogl/clutter-stage-cogl.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_STAGE_WAYLAND_CLIENT (clutter_stage_wayland_client_get_type ())

G_DECLARE_FINAL_TYPE (ClutterStageWaylandClient,
                      clutter_stage_wayland_client,
                      CLUTTER, STAGE_WAYLAND_CLIENT,
                      ClutterStageCogl)

/* Layer shell configuration */
typedef enum {
    CLUTTER_LAYER_SHELL_LAYER_BACKGROUND = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
    CLUTTER_LAYER_SHELL_LAYER_BOTTOM = ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
    CLUTTER_LAYER_SHELL_LAYER_TOP = ZWLR_LAYER_SHELL_V1_LAYER_TOP,
    CLUTTER_LAYER_SHELL_LAYER_OVERLAY = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
} ClutterLayerShellLayer;

typedef enum {
    CLUTTER_LAYER_SHELL_ANCHOR_TOP    = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP,
    CLUTTER_LAYER_SHELL_ANCHOR_BOTTOM = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
    CLUTTER_LAYER_SHELL_ANCHOR_LEFT   = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT,
    CLUTTER_LAYER_SHELL_ANCHOR_RIGHT  = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
} ClutterLayerShellAnchor;

struct _ClutterStageWaylandClient
{
    ClutterStageCogl parent_instance;

    /* Cogl onscreen (owns the wl_surface and egl_window) */
    CoglOnscreen *onscreen;

    /* Wayland surface (owned by Cogl, don't destroy directly) */
    struct wl_surface *wl_surface;

    /* Layer shell surface (we own this) */
    struct zwlr_layer_surface_v1 *layer_surface;

    /* Layer shell configuration */
    ClutterLayerShellLayer layer;
    uint32_t anchor;
    int32_t exclusive_zone;
    int32_t margin_top;
    int32_t margin_bottom;
    int32_t margin_left;
    int32_t margin_right;

    /* State */
    gboolean configured;
    gboolean shown;
    int width;
    int height;

    /* Frame callback */
    struct wl_callback *frame_callback;

    /* Stage view for resource scale */
    ClutterStageView *view;
};

/* Configuration API */
void clutter_stage_wayland_client_set_layer (ClutterStageWaylandClient *stage,
                                             ClutterLayerShellLayer     layer);
void clutter_stage_wayland_client_set_anchor (ClutterStageWaylandClient *stage,
                                              uint32_t                   anchor);
void clutter_stage_wayland_client_set_exclusive_zone (ClutterStageWaylandClient *stage,
                                                      int32_t                    zone);
void clutter_stage_wayland_client_set_margin (ClutterStageWaylandClient *stage,
                                              int32_t top, int32_t right,
                                              int32_t bottom, int32_t left);

G_END_DECLS

#endif /* __CLUTTER_STAGE_WAYLAND_CLIENT_H__ */
