/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By: Emmanuele Bassi <ebassi@linux.intel.com>
 *
 * Copyright (C) 2009  Intel Corporation.
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
 */

/*
 * SECTION:clutter-master-clock-default
 * @short_description: The default master clock for all animations
 *
 * The #ClutterMasterClockDefault class is the default implementation
 * of #ClutterMasterClock.
 */

#ifdef HAVE_CONFIG_H
#include "clutter-build-config.h"
#endif

#include "clutter-master-clock.h"
#include "clutter-master-clock-default.h"
#include "clutter-debug.h"
#include "clutter-private.h"
#include "clutter-stage-manager-private.h"
#include "clutter-stage-private.h"
#include "clutter-muffin.h"

#ifdef CLUTTER_ENABLE_DEBUG
#define clutter_warn_if_over_budget(master_clock,start_time,section)    G_STMT_START  { \
  gint64 __delta = g_get_monotonic_time () - start_time;                                \
  gint64 __budget = master_clock->remaining_budget;                                     \
  if (__budget > 0 && __delta >= __budget) {                                            \
    _clutter_diagnostic_message ("%s took %" G_GINT64_FORMAT " microseconds "           \
                                 "more than the remaining budget of %" G_GINT64_FORMAT  \
                                 " microseconds",                                       \
                                 section, __delta - __budget, __budget);                \
  }                                                                     } G_STMT_END
#else
#define clutter_warn_if_over_budget(master_clock,start_time,section)
#endif

typedef enum _SyncMethod /* In order of priority */
{                        /* SUPPORTED  LATENCY      SMOOTHNESS             */
  SYNC_NONE = 0,         /* Always     High         Poor                   */
  SYNC_FALLBACK,         /* Always     Medium       Medium                 */
  SYNC_SWAP_THROTTLING,  /* Usually    Medium-high  Medium, sometimes best */
  SYNC_PRESENTATION_TIME /* Usually    Low          Good, sometimes best   */
                         /* ^ As you can see SWAP_THROTTLING doesn't add much
                              value. And it does create the the very real
                              risk of blocking the main loop for up to 16ms
                              at a time. So it might be a good idea to retire
                              it in future and instead just make the backends
                              use swap interval 0 + PRESENTATION_TIME. */
} SyncMethod;

typedef struct _ClutterClockSource              ClutterClockSource;

struct _ClutterMasterClockDefault
{
  GObject parent_instance;

  /* the list of timelines handled by the clock */
  GSList *timelines;

  SyncMethod preferred_sync_method, active_sync_method;

  /* the current state of the clock, in usecs */
  gint64 cur_tick;

  /* the previous state of the clock, in usecs, used to compute the delta */
  gint64 prev_tick;

#ifdef CLUTTER_ENABLE_DEBUG
  gint64 frame_budget;
  gint64 remaining_budget;
#endif

  /* an idle source, used by the Master Clock to queue
   * a redraw on the stage and drive the animations
   */
  GSource *source;

  /* If the master clock is idle that means it has
   * fallen back to idle polling for timeline
   * progressions and it may have been some time since
   * the last real stage update.
   */
  guint idle : 1;
  guint ensure_next_iteration : 1;

  guint paused : 1;
};

struct _ClutterClockSource
{
  GSource source;

  ClutterMasterClockDefault *master_clock;
};

static gboolean clutter_clock_prepare  (GSource     *source,
                                        gint        *timeout);
static gboolean clutter_clock_check    (GSource     *source);
static gboolean clutter_clock_dispatch (GSource     *source,
                                        GSourceFunc  callback,
                                        gpointer     user_data);

static GSourceFuncs clock_funcs = {
  clutter_clock_prepare,
  clutter_clock_check,
  clutter_clock_dispatch,
  NULL
};

static void clutter_master_clock_iface_init (ClutterMasterClockIface *iface);

static gint64 master_clock_next_frame_time (ClutterMasterClockDefault *);

#define clutter_master_clock_default_get_type   _clutter_master_clock_default_get_type

G_DEFINE_TYPE_WITH_CODE (ClutterMasterClockDefault,
                         clutter_master_clock_default,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_MASTER_CLOCK,
                                                clutter_master_clock_iface_init));

/*
 * master_clock_is_running:
 * @master_clock: a #ClutterMasterClock
 *
 * Checks if we should currently be advancing timelines or redrawing
 * stages.
 *
 * Return value: %TRUE if the #ClutterMasterClock has at least
 *   one running timeline
 */
static gboolean
master_clock_is_running (ClutterMasterClockDefault *master_clock)
{
  ClutterStageManager *stage_manager = clutter_stage_manager_get_default ();
  const GSList *stages, *l;

  stages = clutter_stage_manager_peek_stages (stage_manager);

  if (master_clock->paused)
    return FALSE;

  if (master_clock->timelines)
    return TRUE;

  for (l = stages; l; l = l->next)
    {
      if (clutter_actor_is_mapped (l->data) &&
          (_clutter_stage_has_queued_events (l->data) ||
           _clutter_stage_needs_update (l->data)))
        return TRUE;
    }

  if (master_clock->ensure_next_iteration)
    {
      master_clock->ensure_next_iteration = FALSE;
      return TRUE;
    }

  return FALSE;
}

static gint64
master_clock_get_hw_update_time (ClutterMasterClockDefault *master_clock)
{
  ClutterStageManager *stage_manager = clutter_stage_manager_get_default ();
  const GSList *stages, *l;
  gint64 min_update_time = -1;

  stages = clutter_stage_manager_peek_stages (stage_manager);

  for (l = stages; l != NULL; l = l->next)
    {
      gint64 update_time = _clutter_stage_get_update_time (l->data);
      if (min_update_time == -1 ||
          (update_time != -1 && update_time < min_update_time))
        min_update_time = update_time;
    }

  return min_update_time;
}

static void
master_clock_schedule_stage_updates (ClutterMasterClockDefault *master_clock)
{
  ClutterStageManager *stage_manager = clutter_stage_manager_get_default ();
  const GSList *stages, *l;

  stages = clutter_stage_manager_peek_stages (stage_manager);

  for (l = stages; l != NULL; l = l->next)
    _clutter_stage_schedule_update (l->data);
}

static GSList *
master_clock_list_ready_stages (ClutterMasterClockDefault *master_clock)
{
  ClutterStageManager *stage_manager = clutter_stage_manager_get_default ();
  const GSList *stages, *l;
  GSList *result;

  stages = clutter_stage_manager_peek_stages (stage_manager);

  result = NULL;
  for (l = stages; l != NULL; l = l->next)
    {
      gint64 update_time = -1;

      if (master_clock->active_sync_method == SYNC_PRESENTATION_TIME)
        update_time = _clutter_stage_get_update_time (l->data);

      /* We carefully avoid to update stages that aren't mapped, because
       * they have nothing to render and this could cause a deadlock with
       * some of the SwapBuffers implementations (in particular
       * GLX_INTEL_swap_event is not emitted if nothing was rendered).
       */

      if (clutter_actor_is_mapped (l->data) &&
          update_time <= master_clock->cur_tick)
        result = g_slist_prepend (result, g_object_ref (l->data));
    }

  return g_slist_reverse (result);
}

static void
master_clock_reschedule_stage_updates (ClutterMasterClockDefault *master_clock,
                                       GSList                    *stages)
{
  const GSList *l;

  for (l = stages; l != NULL; l = l->next)
    {
      /* Clear the old update time */
      _clutter_stage_clear_update_time (l->data);

      /* And if there is still work to be done, schedule a new one */
      if (master_clock->timelines ||
          _clutter_stage_has_queued_events (l->data) ||
          _clutter_stage_needs_update (l->data))
        _clutter_stage_schedule_update (l->data);
    }
}

/*
 * master_clock_next_frame_time:
 * @master_clock: a #ClutterMasterClock
 *
 * Computes the optimal next frame time for dispatching the master clock.
 * Wherever possible this will be higher than master_clock->prev_tick.
 *
 * The goal here is to trigger dispatches as seldom as possible, but often
 * enough to maintain full frame rate.
 *
 * Return value: A valid timestamp in microseconds. Never fails.
 */
static gint64
master_clock_next_frame_time (ClutterMasterClockDefault *master_clock)
{
  gint64 next, now, interval;

  if (master_clock->preferred_sync_method >= SYNC_PRESENTATION_TIME)
    {
      next = master_clock_get_hw_update_time (master_clock);
      if (next >= 0)
        {
          master_clock->active_sync_method = SYNC_PRESENTATION_TIME;
          return next;
        }
    }

  now = g_source_get_time (master_clock->source);

  if (!master_clock->prev_tick ||
      master_clock->preferred_sync_method == SYNC_NONE)
    {
      master_clock->active_sync_method = SYNC_NONE;
      return now;
    }

  /* When we have sync-to-vblank, we count on swap-buffer requests (or
   * swap-buffer-complete events if supported in the backend) to throttle our
   * frame rate so no additional delay is needed to start the next frame.
   *
   * If the master-clock has become idle due to no timeline progression causing
   * redraws then we can no longer rely on vblank synchronization because the
   * last real stage update/redraw may have happened a long time ago and so we
   * fallback to polling for timeline progressions every 1/frame_rate seconds.
   *
   * (NB: if there aren't even any timelines running then the master clock will
   * be completely stopped in master_clock_is_running())
   */
  if (master_clock->preferred_sync_method >= SYNC_SWAP_THROTTLING &&
      clutter_feature_available (CLUTTER_FEATURE_SYNC_TO_VBLANK) &&
      !master_clock->idle)
    {
      master_clock->active_sync_method = SYNC_SWAP_THROTTLING;
      return now;
    }

  master_clock->active_sync_method = SYNC_FALLBACK;
  interval = 1000000 / clutter_get_default_frame_rate ();
  next = master_clock->prev_tick + interval;
  if (next < (now - interval))  /* Too old? Must have been sleeping. */
    next = now;
  return next;
}

/*
 * master_clock_next_frame_delay:
 * @master_clock: a #ClutterMasterClock
 *
 * Computes the number of delay before we need to draw the next frame.
 *
 * Return value: -1 if there is no next frame pending, otherwise the
 *  number of millseconds before the we need to draw the next frame
 */
static gint
master_clock_next_frame_delay (ClutterMasterClockDefault *master_clock)
{
  gint64 now, next;
  gint delay_ms;

  if (!master_clock_is_running (master_clock))
    return -1;

  now = g_source_get_time (master_clock->source);
  next = master_clock_next_frame_time (master_clock);

  /* Round your microseconds UP to milliseconds! If we were to round down
   * then we'd spend an entire millisecond per frame continuously
   * dispatching without any throttling. Thus spinning the CPU at around 6%
   * for a 60Hz display.
   */
  if (next > now)
    delay_ms = (next - now + 999) / 1000;  /* Always round up! */
  else
    delay_ms = 0;

  CLUTTER_NOTE (SCHEDULER, "Waiting %d ms", delay_ms);

  return delay_ms;
}

static void
master_clock_process_events (ClutterMasterClockDefault *master_clock,
                             GSList                    *stages)
{
  GSList *l;
#ifdef CLUTTER_ENABLE_DEBUG
  gint64 start = g_get_monotonic_time ();
#endif

  /* Process queued events */
  for (l = stages; l != NULL; l = l->next)
    _clutter_stage_process_queued_events (l->data);

#ifdef CLUTTER_ENABLE_DEBUG
  if (_clutter_diagnostic_enabled ())
    clutter_warn_if_over_budget (master_clock, start, "Event processing");

  master_clock->remaining_budget -= (g_get_monotonic_time () - start);
#endif
}

/*
 * master_clock_advance_timelines:
 * @master_clock: a #ClutterMasterClock
 *
 * Advances all the timelines held by the master clock. This function
 * should be called before calling _clutter_stage_do_update() to
 * make sure that all the timelines are advanced and the scene is updated.
 */
static void
master_clock_advance_timelines (ClutterMasterClockDefault *master_clock)
{
  GSList *timelines, *l;
#ifdef CLUTTER_ENABLE_DEBUG
  gint64 start = g_get_monotonic_time ();
#endif

  /* we protect ourselves from timelines being removed during
   * the advancement by other timelines by copying the list of
   * timelines, taking a reference on them, iterating over the
   * copied list and then releasing the reference.
   *
   * we cannot simply take a reference on the timelines and still
   * use the list held by the master clock because the do_tick()
   * might result in the creation of a new timeline, which gets
   * added at the end of the list with no reference increase and
   * thus gets disposed at the end of the iteration.
   *
   * this implies that a newly added timeline will not be advanced
   * by this clock iteration, which is perfectly fine since we're
   * in its first cycle.
   *
   * we also cannot steal the master clock timelines list because
   * a timeline might be removed as the direct result of do_tick()
   * and remove_timeline() would not find the timeline, failing
   * and leaving a dangling pointer behind.
   */
  timelines = g_slist_copy (master_clock->timelines);
  g_slist_foreach (timelines, (GFunc) g_object_ref, NULL);

  for (l = timelines; l != NULL; l = l->next)
    _clutter_timeline_do_tick (l->data, master_clock->cur_tick / 1000);

  g_slist_foreach (timelines, (GFunc) g_object_unref, NULL);
  g_slist_free (timelines);

#ifdef CLUTTER_ENABLE_DEBUG
  if (_clutter_diagnostic_enabled ())
    clutter_warn_if_over_budget (master_clock, start, "Animations");

  master_clock->remaining_budget -= (g_get_monotonic_time () - start);
#endif
}

static gboolean
master_clock_update_stages (ClutterMasterClockDefault *master_clock,
                            GSList                    *stages)
{
  gboolean stages_updated = FALSE;
  GSList *l;
#ifdef CLUTTER_ENABLE_DEBUG
  gint64 start = g_get_monotonic_time ();
#endif

  _clutter_run_repaint_functions (CLUTTER_REPAINT_FLAGS_PRE_PAINT);

  /* Update any stage that needs redraw/relayout after the clock
   * is advanced.
   */
  for (l = stages; l != NULL; l = l->next)
    stages_updated |= _clutter_stage_do_update (l->data);

  _clutter_run_repaint_functions (CLUTTER_REPAINT_FLAGS_POST_PAINT);

#ifdef CLUTTER_ENABLE_DEBUG
  if (_clutter_diagnostic_enabled ())
    clutter_warn_if_over_budget (master_clock, start, "Updating the stage");

  master_clock->remaining_budget -= (g_get_monotonic_time () - start);
#endif

  return stages_updated;
}

/*
 * clutter_clock_source_new:
 * @master_clock: a #ClutterMasterClock for the source
 *
 * The #ClutterClockSource is an idle GSource that will queue a redraw
 * if @master_clock has at least a running #ClutterTimeline. The redraw
 * will cause @master_clock to advance all timelines, thus advancing all
 * animations as well.
 *
 * Return value: the newly created #GSource
 */
static GSource *
clutter_clock_source_new (ClutterMasterClockDefault *master_clock)
{
  GSource *source = g_source_new (&clock_funcs, sizeof (ClutterClockSource));
  ClutterClockSource *clock_source = (ClutterClockSource *) source;

  g_source_set_name (source, "Clutter master clock");
  clock_source->master_clock = master_clock;

  return source;
}

static gboolean
clutter_clock_prepare (GSource *source,
                       gint    *timeout)
{
  ClutterClockSource *clock_source = (ClutterClockSource *) source;
  ClutterMasterClockDefault *master_clock = clock_source->master_clock;
  int delay;

  _clutter_threads_acquire_lock ();

  if (G_UNLIKELY (clutter_paint_debug_flags &
                  CLUTTER_DEBUG_CONTINUOUS_REDRAW))
    {
      ClutterStageManager *stage_manager = clutter_stage_manager_get_default ();
      const GSList *stages, *l;

      stages = clutter_stage_manager_peek_stages (stage_manager);

      /* Queue a full redraw on all of the stages */
      for (l = stages; l != NULL; l = l->next)
        clutter_actor_queue_redraw (l->data);
    }

  delay = master_clock_next_frame_delay (master_clock);

  _clutter_threads_release_lock ();

  *timeout = delay;

  return delay == 0;
}

static gboolean
clutter_clock_check (GSource *source)
{
  ClutterClockSource *clock_source = (ClutterClockSource *) source;
  ClutterMasterClockDefault *master_clock = clock_source->master_clock;
  int delay;

  _clutter_threads_acquire_lock ();
  delay = master_clock_next_frame_delay (master_clock);
  _clutter_threads_release_lock ();

  return delay == 0;
}

static gboolean
clutter_clock_dispatch (GSource     *source,
                        GSourceFunc  callback,
                        gpointer     user_data)
{
  ClutterClockSource *clock_source = (ClutterClockSource *) source;
  ClutterMasterClockDefault *master_clock = clock_source->master_clock;
  gboolean stages_updated = FALSE;
  GSList *stages;

  CLUTTER_NOTE (SCHEDULER, "Master clock [tick]");

  _clutter_threads_acquire_lock ();

  /* Get the time to use for this frame */
  master_clock->cur_tick = master_clock_next_frame_time (master_clock);

#ifdef CLUTTER_ENABLE_DEBUG
  master_clock->remaining_budget = master_clock->frame_budget;
#endif

  /* We need to protect ourselves against stages being destroyed during
   * event handling - master_clock_list_ready_stages() returns a
   * list of referenced that we'll unref afterwards.
   */
  stages = master_clock_list_ready_stages (master_clock);

  master_clock->idle = FALSE;

  /* Each frame is split into three separate phases: */

  /* 1. process all the events; each stage goes through its events queue
   *    and processes each event according to its type, then emits the
   *    various signals that are associated with the event
   */
  master_clock_process_events (master_clock, stages);

  /* 2. advance the timelines */
  master_clock_advance_timelines (master_clock);

  /* 3. relayout and redraw the stages */
  stages_updated = master_clock_update_stages (master_clock, stages);

  /* The master clock goes idle if no stages were updated and falls back
   * to polling for timeline progressions... */
  if (!stages_updated)
    master_clock->idle = TRUE;

  master_clock_reschedule_stage_updates (master_clock, stages);

  g_slist_foreach (stages, (GFunc) g_object_unref, NULL);
  g_slist_free (stages);

  master_clock->prev_tick = master_clock->cur_tick;

  _clutter_threads_release_lock ();

  return TRUE;
}

static void
clutter_master_clock_default_finalize (GObject *gobject)
{
  ClutterMasterClockDefault *master_clock = CLUTTER_MASTER_CLOCK_DEFAULT (gobject);

  g_slist_free (master_clock->timelines);

  G_OBJECT_CLASS (clutter_master_clock_default_parent_class)->finalize (gobject);
}

static void
clutter_master_clock_default_class_init (ClutterMasterClockDefaultClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = clutter_master_clock_default_finalize;
}

static void
clutter_master_clock_default_init (ClutterMasterClockDefault *self)
{
  GSource *source;

  source = clutter_clock_source_new (self);
  self->source = source;

  self->preferred_sync_method = _clutter_get_sync_to_vblank () ?
                                SYNC_PRESENTATION_TIME :
                                SYNC_NONE;
  self->active_sync_method = self->preferred_sync_method;

  self->idle = FALSE;
  self->ensure_next_iteration = FALSE;
  self->paused = FALSE;

#ifdef CLUTTER_ENABLE_DEBUG
  self->frame_budget = G_USEC_PER_SEC / 60;
#endif

  g_source_set_priority (source, CLUTTER_PRIORITY_REDRAW);
  g_source_set_can_recurse (source, FALSE);
  g_source_attach (source, NULL);
}

static void
clutter_master_clock_default_add_timeline (ClutterMasterClock *clock,
                                           ClutterTimeline    *timeline)
{
  ClutterMasterClockDefault *master_clock = (ClutterMasterClockDefault *) clock;
  gboolean is_first;

  if (g_slist_find (master_clock->timelines, timeline))
    return;

  is_first = master_clock->timelines == NULL;

  master_clock->timelines = g_slist_prepend (master_clock->timelines,
                                             timeline);

  if (is_first)
    {
      master_clock_schedule_stage_updates (master_clock);
      _clutter_master_clock_start_running (clock);
    }
}

static void
clutter_master_clock_default_remove_timeline (ClutterMasterClock *clock,
                                              ClutterTimeline    *timeline)
{
  ClutterMasterClockDefault *master_clock = (ClutterMasterClockDefault *) clock;

  master_clock->timelines = g_slist_remove (master_clock->timelines,
                                            timeline);
}

static void
clutter_master_clock_default_start_running (ClutterMasterClock *master_clock)
{
  /* If called from a different thread, we need to wake up the
   * main loop to start running the timelines
   */
  g_main_context_wakeup (NULL);
}

static void
clutter_master_clock_default_ensure_next_iteration (ClutterMasterClock *clock)
{
  ClutterMasterClockDefault *master_clock = (ClutterMasterClockDefault *) clock;

  master_clock->ensure_next_iteration = TRUE;
}

static void
clutter_master_clock_default_set_paused (ClutterMasterClock *clock,
                                         gboolean            paused)
{
  ClutterMasterClockDefault *master_clock = (ClutterMasterClockDefault *) clock;

  master_clock->paused = !!paused;
}

static void
clutter_master_clock_iface_init (ClutterMasterClockIface *iface)
{
  iface->add_timeline = clutter_master_clock_default_add_timeline;
  iface->remove_timeline = clutter_master_clock_default_remove_timeline;
  iface->start_running = clutter_master_clock_default_start_running;
  iface->ensure_next_iteration = clutter_master_clock_default_ensure_next_iteration;
  iface->set_paused = clutter_master_clock_default_set_paused;
}
