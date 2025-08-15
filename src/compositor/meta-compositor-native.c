/*
 * Copyright (C) 2019 Red Hat Inc.
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

#include "compositor/meta-compositor-native.h"

#include "backends/meta-logical-monitor.h"
#include "compositor/meta-surface-actor-wayland.h"

struct _MetaCompositorNative
{
    MetaCompositorServer parent;
};

G_DEFINE_TYPE (MetaCompositorNative, meta_compositor_native,
               META_TYPE_COMPOSITOR_SERVER)

static MetaRendererView *
get_window_view (MetaRenderer *renderer,
                 MetaWindow   *window)
{
    GList *l;
    MetaRendererView *view_found = NULL;

    for (l = meta_renderer_get_views (renderer); l; l = l->next)
    {
        ClutterStageView *stage_view = l->data;
        MetaRectangle view_layout;

        clutter_stage_view_get_layout (stage_view, &view_layout);

        if (meta_rectangle_equal (&window->buffer_rect,
            &view_layout))
        {
            if (view_found)
                return NULL;
            view_found = META_RENDERER_VIEW (stage_view);
        }
    }

    return view_found;
}

static void
maybe_assign_primary_plane (MetaCompositor *compositor)
{
    MetaBackend *backend = meta_get_backend ();
    MetaRenderer *renderer = meta_backend_get_renderer (backend);
    MetaWindowActor *window_actor;
    MetaWindow *window;
    MetaRendererView *view;
    CoglFramebuffer *framebuffer;
    CoglOnscreen *onscreen;
    MetaSurfaceActor *surface_actor;
    MetaSurfaceActorWayland *surface_actor_wayland;
    g_autoptr (CoglScanout) scanout = NULL;

    if (meta_compositor_is_unredirect_inhibited (compositor))
        return;

    window_actor = meta_compositor_get_top_window_actor (compositor);
    if (!window_actor)
        return;

    if (meta_window_actor_effect_in_progress (window_actor))
        return;

    if (clutter_actor_has_transitions (CLUTTER_ACTOR (window_actor)))
        return;

    if (clutter_actor_get_n_children (CLUTTER_ACTOR (window_actor)) != 1)
        return;

    window = meta_window_actor_get_meta_window (window_actor);
    if (!window)
        return;

    view = get_window_view (renderer, window);
    if (!view)
        return;

    framebuffer = clutter_stage_view_get_framebuffer (CLUTTER_STAGE_VIEW (view));
    if (!cogl_is_onscreen (framebuffer))
        return;

    surface_actor = meta_window_actor_get_surface (window_actor);
    if (!META_IS_SURFACE_ACTOR_WAYLAND (surface_actor))
        return;

    surface_actor_wayland = META_SURFACE_ACTOR_WAYLAND (surface_actor);
    onscreen = COGL_ONSCREEN (framebuffer);
    scanout = meta_surface_actor_wayland_try_acquire_scanout (surface_actor_wayland,
                                                              onscreen);
    if (!scanout)
        return;

    clutter_stage_view_assign_next_scanout (CLUTTER_STAGE_VIEW (view), scanout);
}

static void
meta_compositor_native_pre_paint (MetaCompositor *compositor)
{
    MetaCompositorClass *parent_class;

    maybe_assign_primary_plane (compositor);

    parent_class = META_COMPOSITOR_CLASS (meta_compositor_native_parent_class);
    parent_class->pre_paint (compositor);
}

MetaCompositorNative *
meta_compositor_native_new (MetaDisplay *display)
{
    return g_object_new (META_TYPE_COMPOSITOR_NATIVE,
                         "display", display,
                         NULL);
}

static void
meta_compositor_native_init (MetaCompositorNative *compositor_native)
{
}

static void
meta_compositor_native_class_init (MetaCompositorNativeClass *klass)
{
    MetaCompositorClass *compositor_class = META_COMPOSITOR_CLASS (klass);

    compositor_class->pre_paint = meta_compositor_native_pre_paint;
}
