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

#include "backends/meta-stage-private.h"

#include "backends/meta-backend-private.h"
#include "clutter/clutter-mutter.h"
#include "meta/meta-backend.h"
#include "meta/meta-monitor-manager.h"
#include "meta/util.h"

#define N_WATCH_MODES 4

enum
{
  ACTORS_PAINTED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

struct _MetaStageWatch
{
  ClutterStageView *view;
  MetaStageWatchFunc callback;
  gpointer user_data;
};

struct _MetaOverlay
{
  gboolean enabled;

  CoglPipeline *pipeline;
  CoglTexture *texture;

  graphene_rect_t current_rect;
  graphene_rect_t previous_rect;
  gboolean previous_is_valid;
};

struct _MetaStage
{
  ClutterStage parent;

  GPtrArray *watchers[N_WATCH_MODES];

  GList *overlays;
  gboolean is_active;
};

G_DEFINE_TYPE (MetaStage, meta_stage, CLUTTER_TYPE_STAGE);

static MetaOverlay *
meta_overlay_new (void)
{
  MetaOverlay *overlay;
  CoglContext *ctx = clutter_backend_get_cogl_context (clutter_get_default_backend ());

  overlay = g_slice_new0 (MetaOverlay);
  overlay->pipeline = cogl_pipeline_new (ctx);

  return overlay;
}

static void
meta_overlay_free (MetaOverlay *overlay)
{
  if (overlay->pipeline)
    cogl_object_unref (overlay->pipeline);

  g_slice_free (MetaOverlay, overlay);
}

static void
meta_overlay_set (MetaOverlay     *overlay,
                  CoglTexture     *texture,
                  graphene_rect_t *rect)
{
  if (overlay->texture != texture)
    {
      overlay->texture = texture;

      if (texture)
        {
          cogl_pipeline_set_layer_texture (overlay->pipeline, 0, texture);
          overlay->enabled = TRUE;
        }
      else
        {
          cogl_pipeline_set_layer_texture (overlay->pipeline, 0, NULL);
          overlay->enabled = FALSE;
        }
    }

  overlay->current_rect = *rect;
}

static void
meta_overlay_paint (MetaOverlay         *overlay,
                    ClutterPaintContext *paint_context)
{
  CoglFramebuffer *framebuffer;

  if (!overlay->enabled)
    return;

  g_assert (meta_is_wayland_compositor ());

  framebuffer = clutter_paint_context_get_framebuffer (paint_context);
  cogl_framebuffer_draw_rectangle (framebuffer,
                                   overlay->pipeline,
                                   overlay->current_rect.origin.x,
                                   overlay->current_rect.origin.y,
                                   (overlay->current_rect.origin.x +
                                    overlay->current_rect.size.width),
                                   (overlay->current_rect.origin.y +
                                    overlay->current_rect.size.height));

  if (!graphene_rect_equal (&overlay->previous_rect, &overlay->current_rect))
    {
      overlay->previous_rect = overlay->current_rect;
      overlay->previous_is_valid = TRUE;
    }
}

static void
meta_stage_finalize (GObject *object)
{
  MetaStage *stage = META_STAGE (object);
  GList *l;
  int i;

  l = stage->overlays;
  while (l)
    {
      meta_overlay_free (l->data);
      l = g_list_delete_link (l, l);
    }

  for (i = 0; i < N_WATCH_MODES; i++)
    g_clear_pointer (&stage->watchers[i], g_ptr_array_unref);

  G_OBJECT_CLASS (meta_stage_parent_class)->finalize (object);
}

static void
notify_watchers_for_mode (MetaStage           *stage,
                          ClutterStageView    *view,
                          ClutterPaintContext *paint_context,
                          MetaStageWatchPhase  watch_phase)
{
  GPtrArray *watchers;
  int i;

  watchers = stage->watchers[watch_phase];

  for (i = 0; i < watchers->len; i++)
    {
      MetaStageWatch *watch = g_ptr_array_index (watchers, i);

      if (watch->view && view != watch->view)
        continue;

      watch->callback (stage, view, paint_context, watch->user_data);
    }
}

static void
meta_stage_paint (ClutterActor        *actor,
                  ClutterPaintContext *paint_context)
{
  MetaStage *stage = META_STAGE (actor);
  ClutterStageView *view;
  GList *l;

  CLUTTER_ACTOR_CLASS (meta_stage_parent_class)->paint (actor, paint_context);

  view = clutter_paint_context_get_stage_view (paint_context);
  if (view)
    {
      notify_watchers_for_mode (stage, view, paint_context,
                                META_STAGE_WATCH_AFTER_ACTOR_PAINT);
    }

  if (!(clutter_paint_context_get_paint_flags (paint_context) &
        CLUTTER_PAINT_FLAG_NO_PAINT_SIGNAL))
    g_signal_emit (stage, signals[ACTORS_PAINTED], 0);

  if (!(clutter_paint_context_get_paint_flags (paint_context) &
        CLUTTER_PAINT_FLAG_NO_CURSORS))
    {
      for (l = stage->overlays; l; l = l->next)
        meta_overlay_paint (l->data, paint_context);
    }

  if (view)
    {
      notify_watchers_for_mode (stage, view, paint_context,
                                META_STAGE_WATCH_AFTER_OVERLAY_PAINT);
    }
}

static void
meta_stage_paint_view (ClutterStage         *stage,
                       ClutterStageView     *view,
                       const cairo_region_t *redraw_clip)
{
  MetaStage *meta_stage = META_STAGE (stage);

  notify_watchers_for_mode (meta_stage, view, NULL,
                            META_STAGE_WATCH_BEFORE_PAINT);

  CLUTTER_STAGE_CLASS (meta_stage_parent_class)->paint_view (stage, view,
                                                             redraw_clip);

  notify_watchers_for_mode (meta_stage, view, NULL,
                            META_STAGE_WATCH_AFTER_PAINT);
}

static void
meta_stage_activate (ClutterStage *actor)
{
  MetaStage *stage = META_STAGE (actor);

  CLUTTER_STAGE_CLASS (meta_stage_parent_class)->activate (actor);

  stage->is_active = TRUE;
}

static void
meta_stage_deactivate (ClutterStage *actor)
{
  MetaStage *stage = META_STAGE (actor);

  CLUTTER_STAGE_CLASS (meta_stage_parent_class)->deactivate (actor);

  stage->is_active = FALSE;
}

static void
on_power_save_changed (MetaMonitorManager *monitor_manager,
                       MetaStage          *stage)
{
  if (meta_monitor_manager_get_power_save_mode (monitor_manager) ==
      META_POWER_SAVE_ON)
    clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));
}

static void
meta_stage_class_init (MetaStageClass *klass)
{
  ClutterStageClass *stage_class = (ClutterStageClass *) klass;
  ClutterActorClass *actor_class = (ClutterActorClass *) klass;
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = meta_stage_finalize;

  actor_class->paint = meta_stage_paint;

  stage_class->activate = meta_stage_activate;
  stage_class->deactivate = meta_stage_deactivate;
  stage_class->paint_view = meta_stage_paint_view;

  signals[ACTORS_PAINTED] = g_signal_new ("actors-painted",
                                          G_TYPE_FROM_CLASS (klass),
                                          G_SIGNAL_RUN_LAST,
                                          0,
                                          NULL, NULL, NULL,
                                          G_TYPE_NONE, 0);
}

static void
meta_stage_init (MetaStage *stage)
{
  int i;

  for (i = 0; i < N_WATCH_MODES; i++)
    stage->watchers[i] = g_ptr_array_new_with_free_func (g_free);
}

ClutterActor *
meta_stage_new (MetaBackend *backend)
{
  MetaStage *stage;
  MetaMonitorManager *monitor_manager;

  stage = g_object_new (META_TYPE_STAGE,
                        "cursor-visible", FALSE,
                        NULL);

  monitor_manager = meta_backend_get_monitor_manager (backend);
  g_signal_connect (monitor_manager, "power-save-mode-changed",
                    G_CALLBACK (on_power_save_changed),
                    stage);

  return CLUTTER_ACTOR (stage);
}

static void
queue_redraw_clutter_rect (MetaStage       *stage,
                           MetaOverlay     *overlay,
                           graphene_rect_t *rect)
{
  cairo_rectangle_int_t clip = {
    .x = floorf (rect->origin.x),
    .y = floorf (rect->origin.y),
    .width = ceilf (rect->size.width),
    .height = ceilf (rect->size.height)
  };

  /* Since we're flooring the coordinates, we need to enlarge the clip by the
   * difference between the actual coordinate and the floored value */
  clip.width += ceilf (rect->origin.x - clip.x) * 2;
  clip.height += ceilf (rect->origin.y - clip.y) * 2;

  clutter_actor_queue_redraw_with_clip (CLUTTER_ACTOR (stage), &clip);
}

static void
queue_redraw_for_overlay (MetaStage   *stage,
                          MetaOverlay *overlay)
{
  /* Clear the location the overlay was at before, if we need to. */
  if (overlay->previous_is_valid)
    {
      queue_redraw_clutter_rect (stage, overlay, &overlay->previous_rect);
      overlay->previous_is_valid = FALSE;
    }

  /* Draw the overlay at the new position */
  if (overlay->enabled)
    queue_redraw_clutter_rect (stage, overlay, &overlay->current_rect);
}

MetaOverlay *
meta_stage_create_cursor_overlay (MetaStage *stage)
{
  MetaOverlay *overlay;

  overlay = meta_overlay_new ();
  stage->overlays = g_list_prepend (stage->overlays, overlay);

  return overlay;
}

void
meta_stage_remove_cursor_overlay (MetaStage   *stage,
                                  MetaOverlay *overlay)
{
  GList *link;

  link = g_list_find (stage->overlays, overlay);
  if (!link)
    return;

  stage->overlays = g_list_delete_link (stage->overlays, link);
  meta_overlay_free (overlay);
}

void
meta_stage_update_cursor_overlay (MetaStage       *stage,
                                  MetaOverlay     *overlay,
                                  CoglTexture     *texture,
                                  graphene_rect_t *rect)
{
  g_assert (meta_is_wayland_compositor () || texture == NULL);

  meta_overlay_set (overlay, texture, rect);
  queue_redraw_for_overlay (stage, overlay);
}

void
meta_stage_set_active (MetaStage *stage,
                       gboolean   is_active)
{
  ClutterEvent *event;

  /* Used by the native backend to inform accessibility technologies
   * about when the stage loses and gains input focus.
   *
   * For the X11 backend, clutter transparently takes care of this
   * for us.
   */

  if (stage->is_active == is_active)
    return;

  event = clutter_event_new (CLUTTER_STAGE_STATE);
  clutter_event_set_stage (event, CLUTTER_STAGE (stage));
  event->stage_state.changed_mask = CLUTTER_STAGE_STATE_ACTIVATED;

  if (is_active)
    event->stage_state.new_state = CLUTTER_STAGE_STATE_ACTIVATED;

  /* Emitting this StageState event will result in the stage getting
   * activated or deactivated (with the activated or deactivated signal
   * getting emitted from the stage)
   *
   * FIXME: This won't update ClutterStage's own notion of its
   * activeness. For that we would need to somehow trigger a
   * _clutter_stage_update_state call, which will probably
   * require new API in clutter. In practice, nothing relies
   * on the ClutterStage's own notion of activeness when using
   * the EGL backend.
   *
   * See http://bugzilla.gnome.org/746670
   */
  clutter_stage_event (CLUTTER_STAGE (stage), event);
  clutter_event_free (event);
}

MetaStageWatch *
meta_stage_watch_view (MetaStage           *stage,
                       ClutterStageView    *view,
                       MetaStageWatchPhase  watch_phase,
                       MetaStageWatchFunc   callback,
                       gpointer             user_data)
{
  MetaStageWatch *watch;
  GPtrArray *watchers;

  watch = g_new0 (MetaStageWatch, 1);
  watch->view = view;
  watch->callback = callback;
  watch->user_data = user_data;

  watchers = stage->watchers[watch_phase];
  g_ptr_array_add (watchers, watch);

  return watch;
}

void
meta_stage_remove_watch (MetaStage      *stage,
                         MetaStageWatch *watch)
{
  GPtrArray *watchers;
  gboolean removed = FALSE;
  int i;

  for (i = 0; i < N_WATCH_MODES; i++)
    {
      watchers = stage->watchers[i];
      removed = g_ptr_array_remove_fast (watchers, watch);

      if (removed)
        break;
    }

  g_assert (removed);
}
