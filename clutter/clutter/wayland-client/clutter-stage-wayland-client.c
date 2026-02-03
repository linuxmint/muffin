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

#include "clutter-build-config.h"

#include <cogl/cogl.h>
#include <cogl/cogl-wayland-client.h>

#include "clutter-stage-wayland-client.h"
#include "clutter-backend-wayland-client.h"

#include "clutter-actor-private.h"
#include "clutter-backend-private.h"
#include "clutter-debug.h"
#include "clutter-event.h"
#include "clutter-main.h"
#include "clutter-muffin.h"
#include "clutter-paint-context.h"
#include "clutter-private.h"
#include "clutter-stage-private.h"
#include "clutter-stage-view.h"
#include "clutter-text.h"
#include "cogl/clutter-stage-cogl.h"

#include <cogl-pango/cogl-pango.h>

static void clutter_stage_window_iface_init (ClutterStageWindowInterface *iface);

G_DEFINE_TYPE_WITH_CODE (ClutterStageWaylandClient,
                         clutter_stage_wayland_client,
                         CLUTTER_TYPE_STAGE_COGL,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_STAGE_WINDOW,
                                                clutter_stage_window_iface_init))

/* Forward declarations */
static void schedule_frame_callback (ClutterStageWaylandClient *stage_wl);

/* Layer surface listeners */
static void
layer_surface_configure (void *data,
                         struct zwlr_layer_surface_v1 *layer_surface,
                         uint32_t serial,
                         uint32_t width,
                         uint32_t height)
{
    ClutterStageWaylandClient *stage_wl = data;
    ClutterStageCogl *stage_cogl = CLUTTER_STAGE_COGL (stage_wl);

    CLUTTER_NOTE (BACKEND, "Layer surface configure: %dx%d (serial %d)",
                  width, height, serial);

    zwlr_layer_surface_v1_ack_configure (layer_surface, serial);

    if (width == 0 || height == 0)
        return;

    stage_wl->width = width;
    stage_wl->height = height;

    /* Resize the onscreen (which resizes the internal wl_egl_window) */
    if (stage_wl->onscreen)
    {
        cogl_wayland_onscreen_resize (stage_wl->onscreen, width, height, 0, 0);
        CLUTTER_NOTE (BACKEND, "Resized onscreen to %dx%d", width, height);
    }

    stage_wl->configured = TRUE;

    /* Create or update the stage view for resource scale calculation */
    {
        cairo_rectangle_int_t view_layout = {
            .x = 0,
            .y = 0,
            .width = width,
            .height = height
        };

        if (stage_wl->view)
        {
            /* Update existing view layout */
            g_object_set (stage_wl->view,
                          "layout", &view_layout,
                          NULL);
        }
        else
        {
            /* Create new view */
            stage_wl->view = g_object_new (CLUTTER_TYPE_STAGE_VIEW_COGL,
                                           "layout", &view_layout,
                                           "framebuffer", COGL_FRAMEBUFFER (stage_wl->onscreen),
                                           "scale", 1.0f,
                                           NULL);
            CLUTTER_NOTE (BACKEND, "Created stage view for resource scale");
        }
    }

    /* Notify stage of size change and set the viewport immediately */
    clutter_actor_set_size (CLUTTER_ACTOR (stage_cogl->wrapper), width, height);
    _clutter_stage_set_viewport (stage_cogl->wrapper, 0, 0, width, height);

    /* Force allocation of the stage */
    {
        ClutterActorBox box = { 0, 0, width, height };
        clutter_actor_allocate (CLUTTER_ACTOR (stage_cogl->wrapper), &box,
                                CLUTTER_ALLOCATION_NONE);
    }

    /* Schedule initial redraw */
    clutter_stage_ensure_redraw (stage_cogl->wrapper);
}

static void
layer_surface_closed (void *data,
                      struct zwlr_layer_surface_v1 *layer_surface)
{
    ClutterStageWaylandClient *stage_wl = data;

    CLUTTER_NOTE (BACKEND, "Layer surface closed");

    /* Emit delete-event on stage */
    ClutterEvent *event = clutter_event_new (CLUTTER_DELETE);
    event->any.stage = CLUTTER_STAGE_COGL (stage_wl)->wrapper;
    event->any.time = g_get_monotonic_time () / 1000;
    clutter_stage_event (CLUTTER_STAGE_COGL (stage_wl)->wrapper, event);
    clutter_event_free (event);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

/* Frame callback */
static void
frame_callback_done (void *data,
                     struct wl_callback *callback,
                     uint32_t time)
{
    ClutterStageWaylandClient *stage_wl = data;

    wl_callback_destroy (callback);
    stage_wl->frame_callback = NULL;

    /* Schedule next frame if needed */
    if (stage_wl->shown && stage_wl->configured)
    {
        clutter_stage_schedule_update (CLUTTER_STAGE_COGL (stage_wl)->wrapper);
    }
}

static const struct wl_callback_listener frame_callback_listener = {
    .done = frame_callback_done,
};

static void
schedule_frame_callback (ClutterStageWaylandClient *stage_wl)
{
    if (stage_wl->frame_callback)
        return;

    stage_wl->frame_callback = wl_surface_frame (stage_wl->wl_surface);
    wl_callback_add_listener (stage_wl->frame_callback,
                              &frame_callback_listener,
                              stage_wl);
}

/* Stage window interface implementation */
static gboolean
clutter_stage_wayland_client_realize (ClutterStageWindow *stage_window)
{
    ClutterStageWaylandClient *stage_wl = CLUTTER_STAGE_WAYLAND_CLIENT (stage_window);
    ClutterStageCogl *stage_cogl = CLUTTER_STAGE_COGL (stage_wl);
    ClutterBackendWaylandClient *backend_wl;
    CoglContext *cogl_context;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wl_output *output;
    GError *error = NULL;

    CLUTTER_NOTE (BACKEND, "Realizing Wayland client stage");

    backend_wl = CLUTTER_BACKEND_WAYLAND_CLIENT (stage_cogl->backend);
    cogl_context = clutter_backend_get_cogl_context (stage_cogl->backend);

    layer_shell = clutter_backend_wayland_client_get_layer_shell (backend_wl);
    output = clutter_backend_wayland_client_get_output (backend_wl);

    /* Create CoglOnscreen first - this creates the wl_surface via Cogl winsys */
    stage_wl->onscreen = cogl_onscreen_new (cogl_context, 1, 40);

    if (!cogl_framebuffer_allocate (COGL_FRAMEBUFFER (stage_wl->onscreen), &error))
    {
        g_warning ("Failed to allocate onscreen framebuffer: %s", error->message);
        g_error_free (error);
        return FALSE;
    }

    /* Get the wl_surface created by Cogl */
    stage_wl->wl_surface = cogl_wayland_onscreen_get_wl_surface (stage_wl->onscreen);
    if (!stage_wl->wl_surface)
    {
        g_warning ("Failed to get wl_surface from onscreen");
        cogl_object_unref (stage_wl->onscreen);
        stage_wl->onscreen = NULL;
        return FALSE;
    }

    /* Create layer surface using the Cogl-created wl_surface */
    stage_wl->layer_surface = zwlr_layer_shell_v1_get_layer_surface (
        layer_shell,
        stage_wl->wl_surface,
        output,
        stage_wl->layer,
        "clutter-stage"
    );

    if (!stage_wl->layer_surface)
    {
        g_warning ("Failed to create layer surface");
        cogl_object_unref (stage_wl->onscreen);
        stage_wl->onscreen = NULL;
        stage_wl->wl_surface = NULL;
        return FALSE;
    }

    /* Apply layer-shell configuration */
    zwlr_layer_surface_v1_set_anchor (stage_wl->layer_surface, stage_wl->anchor);

    if (stage_wl->exclusive_zone >= 0)
        zwlr_layer_surface_v1_set_exclusive_zone (stage_wl->layer_surface,
                                                  stage_wl->exclusive_zone);

    zwlr_layer_surface_v1_set_margin (stage_wl->layer_surface,
                                      stage_wl->margin_top,
                                      stage_wl->margin_right,
                                      stage_wl->margin_bottom,
                                      stage_wl->margin_left);

    /* Set initial size (0 = let compositor decide based on anchors) */
    zwlr_layer_surface_v1_set_size (stage_wl->layer_surface, 0, 40);

    zwlr_layer_surface_v1_add_listener (stage_wl->layer_surface,
                                        &layer_surface_listener,
                                        stage_wl);

    /* Initial commit to get configure event */
    wl_surface_commit (stage_wl->wl_surface);

    CLUTTER_NOTE (BACKEND, "Wayland client stage realized, waiting for configure");

    return TRUE;
}

static void
clutter_stage_wayland_client_unrealize (ClutterStageWindow *stage_window)
{
    ClutterStageWaylandClient *stage_wl = CLUTTER_STAGE_WAYLAND_CLIENT (stage_window);

    CLUTTER_NOTE (BACKEND, "Unrealizing Wayland client stage");

    if (stage_wl->frame_callback)
    {
        wl_callback_destroy (stage_wl->frame_callback);
        stage_wl->frame_callback = NULL;
    }

    /* Destroy stage view */
    g_clear_object (&stage_wl->view);

    /* Destroy layer surface before onscreen (which owns wl_surface) */
    if (stage_wl->layer_surface)
    {
        zwlr_layer_surface_v1_destroy (stage_wl->layer_surface);
        stage_wl->layer_surface = NULL;
    }

    /* Destroying onscreen will destroy the wl_surface and egl_window */
    if (stage_wl->onscreen)
    {
        cogl_object_unref (stage_wl->onscreen);
        stage_wl->onscreen = NULL;
    }

    stage_wl->wl_surface = NULL;
    stage_wl->configured = FALSE;
}

static void
clutter_stage_wayland_client_show (ClutterStageWindow *stage_window,
                                   gboolean            do_raise)
{
    ClutterStageWaylandClient *stage_wl = CLUTTER_STAGE_WAYLAND_CLIENT (stage_window);

    CLUTTER_NOTE (BACKEND, "Showing Wayland client stage");

    stage_wl->shown = TRUE;

    /* Map the actor */
    clutter_actor_map (CLUTTER_ACTOR (CLUTTER_STAGE_COGL (stage_wl)->wrapper));
}

static void
clutter_stage_wayland_client_hide (ClutterStageWindow *stage_window)
{
    ClutterStageWaylandClient *stage_wl = CLUTTER_STAGE_WAYLAND_CLIENT (stage_window);

    CLUTTER_NOTE (BACKEND, "Hiding Wayland client stage");

    stage_wl->shown = FALSE;

    /* Unmap the actor */
    clutter_actor_unmap (CLUTTER_ACTOR (CLUTTER_STAGE_COGL (stage_wl)->wrapper));
}

static void
clutter_stage_wayland_client_resize (ClutterStageWindow *stage_window,
                                     gint                width,
                                     gint                height)
{
    ClutterStageWaylandClient *stage_wl = CLUTTER_STAGE_WAYLAND_CLIENT (stage_window);

    CLUTTER_NOTE (BACKEND, "Resize request: %dx%d", width, height);

    /* For layer shell, we can request a specific size, but the compositor decides */
    if (stage_wl->layer_surface)
    {
        zwlr_layer_surface_v1_set_size (stage_wl->layer_surface, width, height);
        wl_surface_commit (stage_wl->wl_surface);
    }
}

static void
clutter_stage_wayland_client_get_geometry (ClutterStageWindow    *stage_window,
                                           cairo_rectangle_int_t *geometry)
{
    ClutterStageWaylandClient *stage_wl = CLUTTER_STAGE_WAYLAND_CLIENT (stage_window);

    geometry->x = 0;
    geometry->y = 0;
    geometry->width = stage_wl->width;
    geometry->height = stage_wl->height;
}

static GList *
clutter_stage_wayland_client_get_views (ClutterStageWindow *stage_window)
{
    ClutterStageWaylandClient *stage_wl = CLUTTER_STAGE_WAYLAND_CLIENT (stage_window);

    if (stage_wl->view)
        return g_list_prepend (NULL, stage_wl->view);

    return NULL;
}

static gboolean
clutter_stage_wayland_client_can_clip_redraws (ClutterStageWindow *stage_window)
{
    return TRUE;
}

static void
clutter_stage_wayland_client_redraw (ClutterStageWindow *stage_window)
{
    ClutterStageWaylandClient *stage_wl = CLUTTER_STAGE_WAYLAND_CLIENT (stage_window);
    ClutterStageCogl *stage_cogl = CLUTTER_STAGE_COGL (stage_wl);

    if (!stage_wl->configured || !stage_wl->onscreen)
        return;

    if (stage_cogl->wrapper)
    {
        ClutterStage *stage = stage_cogl->wrapper;
        CoglFramebuffer *fb = COGL_FRAMEBUFFER (stage_wl->onscreen);
        ClutterPaintContext *paint_context;
        CoglMatrix identity;

        /* Ensure actors are laid out before painting */
        _clutter_stage_maybe_relayout (CLUTTER_ACTOR (stage));

        /* Clear the framebuffer */
        cogl_framebuffer_clear4f (fb, COGL_BUFFER_BIT_COLOR | COGL_BUFFER_BIT_DEPTH,
                                  0.2f, 0.2f, 0.3f, 1.0f);

        /* Use orthographic projection for 2D panel */
        cogl_matrix_init_identity (&identity);
        cogl_framebuffer_set_viewport (fb, 0, 0, stage_wl->width, stage_wl->height);
        cogl_framebuffer_orthographic (fb, 0, 0, stage_wl->width, stage_wl->height, -1, 1);
        cogl_framebuffer_set_modelview_matrix (fb, &identity);

        /* Disable the stage's model-view transform (designed for perspective) */
        _clutter_actor_set_enable_model_view_transform (CLUTTER_ACTOR (stage), FALSE);

        /* Create paint context and paint the stage */
        paint_context = clutter_paint_context_new_for_framebuffer (fb);
        clutter_actor_paint (CLUTTER_ACTOR (stage), paint_context);
        clutter_paint_context_unref (paint_context);

        /* Re-enable for future use */
        _clutter_actor_set_enable_model_view_transform (CLUTTER_ACTOR (stage), TRUE);

        cogl_onscreen_swap_buffers (stage_wl->onscreen);
    }

    /* Schedule frame callback */
    schedule_frame_callback (stage_wl);
    wl_surface_commit (stage_wl->wl_surface);
}

static void
clutter_stage_wayland_client_finish_frame (ClutterStageWindow *stage_window)
{
    ClutterStageWaylandClient *stage_wl = CLUTTER_STAGE_WAYLAND_CLIENT (stage_window);
    ClutterBackendWaylandClient *backend_wl;

    backend_wl = CLUTTER_BACKEND_WAYLAND_CLIENT (CLUTTER_STAGE_COGL (stage_wl)->backend);

    /* Flush Wayland connection */
    wl_display_flush (clutter_backend_wayland_client_get_wl_display (backend_wl));
}

static void
clutter_stage_window_iface_init (ClutterStageWindowInterface *iface)
{
    iface->realize = clutter_stage_wayland_client_realize;
    iface->unrealize = clutter_stage_wayland_client_unrealize;
    iface->show = clutter_stage_wayland_client_show;
    iface->hide = clutter_stage_wayland_client_hide;
    iface->resize = clutter_stage_wayland_client_resize;
    iface->get_geometry = clutter_stage_wayland_client_get_geometry;
    iface->get_views = clutter_stage_wayland_client_get_views;
    iface->can_clip_redraws = clutter_stage_wayland_client_can_clip_redraws;
    iface->redraw = clutter_stage_wayland_client_redraw;
    iface->finish_frame = clutter_stage_wayland_client_finish_frame;
}

static void
clutter_stage_wayland_client_finalize (GObject *object)
{
    ClutterStageWaylandClient *stage_wl = CLUTTER_STAGE_WAYLAND_CLIENT (object);

    clutter_stage_wayland_client_unrealize (CLUTTER_STAGE_WINDOW (stage_wl));

    G_OBJECT_CLASS (clutter_stage_wayland_client_parent_class)->finalize (object);
}

static void
clutter_stage_wayland_client_class_init (ClutterStageWaylandClientClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = clutter_stage_wayland_client_finalize;
}

static void
clutter_stage_wayland_client_init (ClutterStageWaylandClient *stage)
{
    /* Default to top layer, bottom-anchored panel */
    stage->layer = CLUTTER_LAYER_SHELL_LAYER_TOP;
    stage->anchor = CLUTTER_LAYER_SHELL_ANCHOR_BOTTOM |
                    CLUTTER_LAYER_SHELL_ANCHOR_LEFT |
                    CLUTTER_LAYER_SHELL_ANCHOR_RIGHT;
    stage->exclusive_zone = 40;
    stage->margin_top = 0;
    stage->margin_bottom = 0;
    stage->margin_left = 0;
    stage->margin_right = 0;
}

/* Configuration API */
void
clutter_stage_wayland_client_set_layer (ClutterStageWaylandClient *stage,
                                        ClutterLayerShellLayer     layer)
{
    g_return_if_fail (CLUTTER_IS_STAGE_WAYLAND_CLIENT (stage));
    stage->layer = layer;
}

void
clutter_stage_wayland_client_set_anchor (ClutterStageWaylandClient *stage,
                                         uint32_t                   anchor)
{
    g_return_if_fail (CLUTTER_IS_STAGE_WAYLAND_CLIENT (stage));
    stage->anchor = anchor;

    if (stage->layer_surface)
    {
        zwlr_layer_surface_v1_set_anchor (stage->layer_surface, anchor);
        wl_surface_commit (stage->wl_surface);
    }
}

void
clutter_stage_wayland_client_set_exclusive_zone (ClutterStageWaylandClient *stage,
                                                 int32_t                    zone)
{
    g_return_if_fail (CLUTTER_IS_STAGE_WAYLAND_CLIENT (stage));
    stage->exclusive_zone = zone;

    if (stage->layer_surface)
    {
        zwlr_layer_surface_v1_set_exclusive_zone (stage->layer_surface, zone);
        wl_surface_commit (stage->wl_surface);
    }
}

void
clutter_stage_wayland_client_set_margin (ClutterStageWaylandClient *stage,
                                         int32_t top, int32_t right,
                                         int32_t bottom, int32_t left)
{
    g_return_if_fail (CLUTTER_IS_STAGE_WAYLAND_CLIENT (stage));
    stage->margin_top = top;
    stage->margin_right = right;
    stage->margin_bottom = bottom;
    stage->margin_left = left;

    if (stage->layer_surface)
    {
        zwlr_layer_surface_v1_set_margin (stage->layer_surface,
                                          top, right, bottom, left);
        wl_surface_commit (stage->wl_surface);
    }
}
