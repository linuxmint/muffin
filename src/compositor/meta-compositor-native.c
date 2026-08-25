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

#include "backends/meta-backend-private.h"
#include "backends/meta-crtc.h"
#include "backends/meta-cursor-renderer.h"
#include "backends/meta-logical-monitor.h"
#include "backends/native/meta-renderer-native.h"
#include "compositor/meta-surface-actor-wayland.h"
#include "meta/util.h"
#include "wayland/meta-wayland-surface.h"

struct _MetaCompositorNative
{
    MetaCompositorServer parent;

    unsigned int n_scanout_views;
};

G_DEFINE_TYPE (MetaCompositorNative, meta_compositor_native,
               META_TYPE_COMPOSITOR_SERVER)

static GQuark quark_view_scanout_candidate;

typedef struct _ViewScanoutCandidate
{
    MetaWaylandSurface *surface;
} ViewScanoutCandidate;

static void
view_scanout_candidate_free (gpointer data)
{
    ViewScanoutCandidate *view_candidate = data;

    if (view_candidate->surface)
      meta_wayland_surface_set_scanout_candidate (view_candidate->surface,
                                                  NULL);
    g_clear_weak_pointer (&view_candidate->surface);
    g_free (view_candidate);
}

static ViewScanoutCandidate *
ensure_view_scanout_candidate (ClutterStageView *stage_view)
{
    ViewScanoutCandidate *view_candidate;

    view_candidate = g_object_get_qdata (G_OBJECT (stage_view),
                                         quark_view_scanout_candidate);
    if (!view_candidate)
      {
          view_candidate = g_new0 (ViewScanoutCandidate, 1);
          g_object_set_qdata_full (G_OBJECT (stage_view),
                                   quark_view_scanout_candidate,
                                   view_candidate,
                                   view_scanout_candidate_free);
      }

    return view_candidate;
}

static MetaWindowActor *
find_top_window_actor_on_view (GList               *window_actors,
                               const MetaRectangle *view_layout)
{
    GList *l;

    for (l = g_list_last (window_actors); l; l = l->prev)
      {
          MetaWindowActor *window_actor = l->data;
          MetaWindow *window =
            meta_window_actor_get_meta_window (window_actor);

          if (!window || !window->visible_to_compositor)
            continue;

          if (meta_rectangle_overlap (&window->buffer_rect, view_layout))
            return window_actor;
      }

    return NULL;
}

/*
 * A cursor the backend can't put on a hardware plane is painted into the stage
 * framebuffer, which scanning out bypasses entirely — the pointer would vanish
 * over the scanned-out window.
 */
static gboolean
software_cursor_overlaps_view (const MetaRectangle *view_layout)
{
    MetaBackend *backend = meta_get_backend ();
    MetaCursorRenderer *cursor_renderer =
      meta_backend_get_cursor_renderer (backend);
    MetaCursorSprite *cursor_sprite;
    graphene_rect_t cursor_rect;
    graphene_rect_t view_rect;

    if (!cursor_renderer ||
        !meta_cursor_renderer_needs_overlay (cursor_renderer))
      return FALSE;

    cursor_sprite = meta_cursor_renderer_get_cursor (cursor_renderer);
    if (!cursor_sprite)
      return FALSE;

    cursor_rect = meta_cursor_renderer_calculate_rect (cursor_renderer,
                                                       cursor_sprite);
    view_rect = GRAPHENE_RECT_INIT (view_layout->x,
                                    view_layout->y,
                                    view_layout->width,
                                    view_layout->height);

    return graphene_rect_intersection (&view_rect, &cursor_rect, NULL);
}

static void
maybe_assign_primary_plane (MetaCompositor *compositor)
{
    MetaCompositorNative *compositor_native = META_COMPOSITOR_NATIVE (compositor);
    MetaBackend *backend = meta_get_backend ();
    MetaRenderer *renderer = meta_backend_get_renderer (backend);
    MetaDisplay *display = meta_compositor_get_display (compositor);
    GList *window_actors;
    gboolean gated;
    unsigned int n_engaged = 0;
    const char *engaged_title = NULL;
    const char *blocked_reason = NULL;
    GList *l;
    g_autoptr (GHashTable) claimed_surfaces = g_hash_table_new (NULL, NULL);
    static int disable_direct_scanout = -1;

    if (disable_direct_scanout == -1)
      {
          disable_direct_scanout =
            g_getenv ("MUFFIN_DEBUG_DISABLE_DIRECT_SCANOUT") != NULL;

          if (disable_direct_scanout)
            g_message ("DMABUF: direct scanout disabled by "
                       "MUFFIN_DEBUG_DISABLE_DIRECT_SCANOUT");
      }

    gated = disable_direct_scanout ||
            meta_compositor_is_unredirect_inhibited (compositor);

    window_actors = meta_get_window_actors (display);

    for (l = meta_renderer_get_views (renderer); l; l = l->next)
      {
          ClutterStageView *stage_view = l->data;
          ViewScanoutCandidate *view_candidate =
            ensure_view_scanout_candidate (stage_view);
          MetaWaylandSurface *old_candidate = view_candidate->surface;
          MetaWaylandSurface *new_candidate = NULL;
          MetaCrtc *crtc = NULL;
          MetaRectangle view_layout;
          MetaWindowActor *window_actor;
          MetaWindow *window;
          CoglFramebuffer *framebuffer;
          CoglOnscreen *onscreen;
          MetaSurfaceActor *surface_actor;
          MetaSurfaceActorWayland *surface_actor_wayland;
          MetaWaylandSurface *surface;
          const char *reason = NULL;
          g_autoptr (CoglScanout) scanout = NULL;

          reason = "disabled";
          if (gated)
            goto reconcile;

          clutter_stage_view_get_layout (stage_view, &view_layout);

          reason = "no window on view";
          window_actor = find_top_window_actor_on_view (window_actors,
                                                        &view_layout);
          if (!window_actor)
            goto reconcile;

          reason = "actor has extra children";
          if (clutter_actor_get_n_children (CLUTTER_ACTOR (window_actor)) != 1)
            goto reconcile;

          reason = "no window";
          window = meta_window_actor_get_meta_window (window_actor);
          if (!window)
            goto reconcile;

          reason = "window does not cover view";
          if (!meta_rectangle_equal (&window->buffer_rect, &view_layout))
            {
                /* Only interesting where a candidate is being lost; ordinary
                 * non-fullscreen windows fail this every frame. */
                if (old_candidate)
                  meta_topic (META_DEBUG_SCANOUT,
                              "candidate lost: window %d,%d %dx%d != "
                              "view %d,%d %dx%d\n",
                              window->buffer_rect.x, window->buffer_rect.y,
                              window->buffer_rect.width,
                              window->buffer_rect.height,
                              view_layout.x, view_layout.y,
                              view_layout.width, view_layout.height);
                goto reconcile;
            }

          reason = "view has no onscreen framebuffer";
          framebuffer = clutter_stage_view_get_framebuffer (stage_view);
          if (!cogl_is_onscreen (framebuffer))
            goto reconcile;
          onscreen = COGL_ONSCREEN (framebuffer);

          reason = "not a KMS CRTC";
          crtc = meta_onscreen_native_get_crtc (onscreen);
          if (!crtc || !META_IS_GPU_KMS (crtc->gpu))
            goto reconcile;

          reason = "not a wayland surface";
          surface_actor = meta_window_actor_get_surface (window_actor);
          if (!META_IS_SURFACE_ACTOR_WAYLAND (surface_actor))
            goto reconcile;

          reason = "surface obscured";
          if (meta_surface_actor_is_obscured (surface_actor))
            goto reconcile;

          /* Scanout substitutes opaque formats, so translucent content must
           * keep compositing or it would render opaque on the plane. */
          reason = "surface not opaque";
          if (!meta_surface_actor_is_opaque (surface_actor))
            goto reconcile;

          reason = "no wayland surface";
          surface_actor_wayland = META_SURFACE_ACTOR_WAYLAND (surface_actor);
          surface = meta_surface_actor_wayland_get_surface (surface_actor_wayland);
          if (!surface)
            goto reconcile;

          /* Checked before candidacy so that surfaces which can never scan out
           * are not steered into scanout-capable buffer allocations for
           * nothing. */
          reason = "surface not scanout-capable";
          if (!crtc->config || !crtc->config->mode ||
              !meta_wayland_surface_can_scanout_untransformed (surface,
                                                               crtc->config->mode->width,
                                                               crtc->config->mode->height))
            goto reconcile;

          /* Mirrored monitors hand several views the same layout and the same
           * surface, but a surface tracks a single candidate CRTC. Letting
           * every view claim it would rewrite it — and re-send the client's
           * feedback — twice a frame, forever. The first view owns candidacy;
           * the others can still scan out. */
          if (g_hash_table_add (claimed_surfaces, surface))
            new_candidate = surface;

          /* Transient, unlike the checks above, so they are tested only after
           * candidacy is claimed: the surface stays a scanout candidate
           * through an animation, it just cannot be flipped during one.
           * Dropping candidacy would clear the client's scanout tranche and
           * cost it a full buffer renegotiation once the effect ends. */
          reason = "effect in progress";
          if (meta_window_actor_effect_in_progress (window_actor))
            goto reconcile;

          reason = "actor has transitions";
          if (clutter_actor_has_transitions (CLUTTER_ACTOR (window_actor)))
            goto reconcile;

          reason = "software cursor over view";
          if (software_cursor_overlaps_view (&view_layout))
            goto reconcile;

          reason = "buffer not scanout-compatible";
          scanout = meta_surface_actor_wayland_try_acquire_scanout (surface_actor_wayland,
                                                                    onscreen);
          if (!scanout)
            goto reconcile;

          reason = NULL;
          clutter_stage_view_assign_next_scanout (stage_view, scanout);

          n_engaged++;
          if (!engaged_title)
            engaged_title = meta_window_get_title (window);

reconcile:
          if (reason)
            blocked_reason = reason;

          /* A view only consumes its assignment if it goes on to redraw, so an
           * assignment left from an earlier frame would be flipped once this
           * view finally repaints — showing a stale buffer instead of the
           * content that made it stop qualifying. */
          if (!scanout)
            clutter_stage_view_assign_next_scanout (stage_view, NULL);

          if (old_candidate && old_candidate != new_candidate)
            {
                meta_wayland_surface_set_scanout_candidate (old_candidate, NULL);
                g_clear_weak_pointer (&view_candidate->surface);
            }

          if (new_candidate)
            {
                if (new_candidate != old_candidate)
                  meta_topic (META_DEBUG_SCANOUT,
                              "scanout candidate set (crtc %ld)\n",
                              crtc->crtc_id);

                meta_wayland_surface_set_scanout_candidate (new_candidate, crtc);
                g_set_weak_pointer (&view_candidate->surface, new_candidate);
            }
      }

    if (n_engaged != compositor_native->n_scanout_views)
      {
          if (n_engaged > 0)
            meta_topic (META_DEBUG_SCANOUT,
                        "direct scanout engaged on %u view(s) ('%s')\n",
                        n_engaged,
                        engaged_title ? engaged_title : "(untitled)");
          else
            meta_topic (META_DEBUG_SCANOUT, "direct scanout disengaged: %s\n",
                        blocked_reason ? blocked_reason : "no reason recorded");

          compositor_native->n_scanout_views = n_engaged;
      }
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

    quark_view_scanout_candidate =
      g_quark_from_static_string ("-meta-compositor-native-view-scanout-candidate");
}
