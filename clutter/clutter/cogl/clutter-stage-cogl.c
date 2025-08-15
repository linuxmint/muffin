/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2007,2008,2009,2010,2011  Intel Corporation.
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

 * Authors:
 *  Matthew Allum
 *  Robert Bragg
 *  Neil Roberts
 *  Emmanuele Bassi
 */

#include "clutter-build-config.h"

#define CLUTTER_ENABLE_EXPERIMENTAL_API

#include "clutter-config.h"

#include "clutter-stage-cogl.h"

#include <stdlib.h>
#include <math.h>

#include "clutter-actor-private.h"
#include "clutter-backend-private.h"
#include "clutter-debug.h"
#include "clutter-event.h"
#include "clutter-enum-types.h"
#include "clutter-feature.h"
#include "clutter-main.h"
#include "clutter-private.h"
#include "clutter-stage-private.h"
#include "clutter-stage-view-private.h"

#define MAX_STACK_RECTS 256

typedef struct _ClutterStageViewCoglPrivate
{
  /*
   * List of previous damaged areas in stage view framebuffer coordinate space.
   */
#define DAMAGE_HISTORY_MAX 16
#define DAMAGE_HISTORY(x) ((x) & (DAMAGE_HISTORY_MAX - 1))
  cairo_region_t * damage_history[DAMAGE_HISTORY_MAX];
  unsigned int damage_index;
} ClutterStageViewCoglPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (ClutterStageViewCogl, clutter_stage_view_cogl,
                            CLUTTER_TYPE_STAGE_VIEW)

static void
clutter_stage_window_iface_init (ClutterStageWindowInterface *iface);

G_DEFINE_TYPE_WITH_CODE (ClutterStageCogl,
                         _clutter_stage_cogl,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_STAGE_WINDOW,
                                                clutter_stage_window_iface_init));

enum
{
  PROP_0,
  PROP_WRAPPER,
  PROP_BACKEND,
  PROP_LAST
};

static void
clutter_stage_cogl_schedule_update (ClutterStageWindow *stage_window,
                                    gint                sync_delay);

static void
clutter_stage_cogl_unrealize (ClutterStageWindow *stage_window)
{
  CLUTTER_NOTE (BACKEND, "Unrealizing Cogl stage [%p]", stage_window);
}

void
_clutter_stage_cogl_presented (ClutterStageCogl *stage_cogl,
                               CoglFrameEvent    frame_event,
                               ClutterFrameInfo *frame_info)
{

  if (frame_event == COGL_FRAME_EVENT_SYNC)
    {
      /* Early versions of the swap_event implementation in Mesa
       * deliver BufferSwapComplete event when not selected for,
       * so if we get a swap event we aren't expecting, just ignore it.
       *
       * https://bugs.freedesktop.org/show_bug.cgi?id=27962
       *
       * FIXME: This issue can be hidden inside Cogl so we shouldn't
       * need to care about this bug here.
       */
      if (stage_cogl->pending_swaps > 0)
        stage_cogl->pending_swaps--;
    }
  else if (frame_event == COGL_FRAME_EVENT_COMPLETE)
    {
      gint64 presentation_time_cogl = frame_info->presentation_time;

      if (presentation_time_cogl != 0)
        {
          ClutterBackend *backend = stage_cogl->backend;
          CoglContext *context = clutter_backend_get_cogl_context (backend);
          gint64 current_time_cogl = cogl_get_clock_time (context);
          gint64 now = g_get_monotonic_time ();

          stage_cogl->last_presentation_time =
            now + (presentation_time_cogl - current_time_cogl) / 1000;
        }

      stage_cogl->refresh_rate = frame_info->refresh_rate;
    }

  _clutter_stage_presented (stage_cogl->wrapper, frame_event, frame_info);

  if (frame_event == COGL_FRAME_EVENT_COMPLETE &&
      stage_cogl->update_time != -1)
    {
      ClutterStageWindow *stage_window = CLUTTER_STAGE_WINDOW (stage_cogl);

      stage_cogl->update_time = -1;
      clutter_stage_cogl_schedule_update (stage_window,
                                          stage_cogl->last_sync_delay);
    }
}

static gboolean
clutter_stage_cogl_realize (ClutterStageWindow *stage_window)
{
  ClutterBackend *backend;

  CLUTTER_NOTE (BACKEND, "Realizing stage '%s' [%p]",
                G_OBJECT_TYPE_NAME (stage_window),
                stage_window);

  backend = clutter_get_default_backend ();

  if (backend->cogl_context == NULL)
    {
      g_warning ("Failed to realize stage: missing Cogl context");
      return FALSE;
    }

  return TRUE;
}

static void
clutter_stage_cogl_schedule_update (ClutterStageWindow *stage_window,
                                    gint                sync_delay)
{
  ClutterStageCogl *stage_cogl = CLUTTER_STAGE_COGL (stage_window);
  gint64 now;
  float refresh_rate;
  gint64 refresh_interval;
  int64_t min_render_time_allowed;
  int64_t max_render_time_allowed;
  int64_t next_presentation_time;

  if (stage_cogl->update_time != -1)
    return;

  stage_cogl->last_sync_delay = sync_delay;

  now = g_get_monotonic_time ();

  if (sync_delay < 0)
    {
      stage_cogl->update_time = now;
      return;
    }

  refresh_rate = stage_cogl->refresh_rate;
  if (refresh_rate <= 0.0)
    refresh_rate = clutter_get_default_frame_rate ();

  refresh_interval = (gint64) (0.5 + G_USEC_PER_SEC / refresh_rate);
  if (refresh_interval == 0)
    {
      stage_cogl->update_time = now;
      return;
    }

  min_render_time_allowed = refresh_interval / 2;
  max_render_time_allowed = refresh_interval - 1000 * sync_delay;

  /* Be robust in the case of incredibly bogus refresh rate */
  if (max_render_time_allowed <= 0)
    {
      g_warning ("Unsupported monitor refresh rate detected. "
                 "(Refresh rate: %.3f, refresh interval: %" G_GINT64_FORMAT ")",
                 refresh_rate,
                 refresh_interval);
      stage_cogl->update_time = now;
      return;
    }

  if (min_render_time_allowed > max_render_time_allowed)
    min_render_time_allowed = max_render_time_allowed;

  next_presentation_time = stage_cogl->last_presentation_time + refresh_interval;

  /* Get next_presentation_time closer to its final value, to reduce
   * the number of while iterations below.
   */
  if (next_presentation_time < now)
    {
      int64_t last_virtual_presentation_time = now - now % refresh_interval;
      int64_t hardware_clock_phase =
        stage_cogl->last_presentation_time % refresh_interval;

      next_presentation_time =
        last_virtual_presentation_time + hardware_clock_phase;
    }

  while (next_presentation_time < now + min_render_time_allowed)
    next_presentation_time += refresh_interval;

  stage_cogl->update_time = next_presentation_time - max_render_time_allowed;

  if (stage_cogl->update_time == stage_cogl->last_update_time)
    {
      stage_cogl->update_time += refresh_interval;
      next_presentation_time += refresh_interval;
    }

  stage_cogl->next_presentation_time = next_presentation_time;
}

static gint64
clutter_stage_cogl_get_update_time (ClutterStageWindow *stage_window)
{
  ClutterStageCogl *stage_cogl = CLUTTER_STAGE_COGL (stage_window);

  if (stage_cogl->pending_swaps)
    return -1; /* in the future, indefinite */

  return stage_cogl->update_time;
}

static void
clutter_stage_cogl_clear_update_time (ClutterStageWindow *stage_window)
{
  ClutterStageCogl *stage_cogl = CLUTTER_STAGE_COGL (stage_window);

  stage_cogl->last_update_time = stage_cogl->update_time;
  stage_cogl->update_time = -1;
  stage_cogl->next_presentation_time = -1;
}

static int64_t
clutter_stage_cogl_get_next_presentation_time (ClutterStageWindow *stage_window)
{
  ClutterStageCogl *stage_cogl = CLUTTER_STAGE_COGL (stage_window);
  int64_t now = g_get_monotonic_time ();

  if (stage_cogl->next_presentation_time > 0 &&
      stage_cogl->next_presentation_time <= now)
    {
      CLUTTER_NOTE (BACKEND,
                    "Missed some frames. Something blocked for over "
                    "%" G_GINT64_FORMAT "ms.",
                    (now - stage_cogl->next_presentation_time) / 1000);

      stage_cogl->update_time = -1;
      clutter_stage_cogl_schedule_update (stage_window,
                                          stage_cogl->last_sync_delay);
    }

  return stage_cogl->next_presentation_time;
}

static ClutterActor *
clutter_stage_cogl_get_wrapper (ClutterStageWindow *stage_window)
{
  return CLUTTER_ACTOR (CLUTTER_STAGE_COGL (stage_window)->wrapper);
}

static void
clutter_stage_cogl_show (ClutterStageWindow *stage_window,
			 gboolean            do_raise)
{
  ClutterStageCogl *stage_cogl = CLUTTER_STAGE_COGL (stage_window);

  clutter_actor_map (CLUTTER_ACTOR (stage_cogl->wrapper));
}

static void
clutter_stage_cogl_hide (ClutterStageWindow *stage_window)
{
  ClutterStageCogl *stage_cogl = CLUTTER_STAGE_COGL (stage_window);

  clutter_actor_unmap (CLUTTER_ACTOR (stage_cogl->wrapper));
}

static void
clutter_stage_cogl_resize (ClutterStageWindow *stage_window,
                           gint                width,
                           gint                height)
{
}

static inline gboolean
valid_buffer_age (ClutterStageViewCogl *view_cogl,
                  int                   age)
{
  ClutterStageViewCoglPrivate *view_priv =
    clutter_stage_view_cogl_get_instance_private (view_cogl);

  if (age <= 0)
    return FALSE;

  return age < MIN (view_priv->damage_index, DAMAGE_HISTORY_MAX);
}

static void
paint_damage_region (ClutterStageWindow *stage_window,
                     ClutterStageView   *view,
                     cairo_region_t     *swap_region,
                     cairo_region_t     *queued_redraw_clip)
{
  CoglFramebuffer *framebuffer = clutter_stage_view_get_onscreen (view);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  static CoglPipeline *overlay_blue = NULL;
  ClutterStageCogl *stage_cogl = CLUTTER_STAGE_COGL (stage_window);
  ClutterActor *actor = CLUTTER_ACTOR (stage_cogl->wrapper);
  CoglMatrix transform;
  int n_rects, i;

  cogl_framebuffer_push_matrix (framebuffer);
  clutter_actor_get_transform (actor, &transform);
  cogl_framebuffer_transform (framebuffer, &transform);

  /* Blue for the swap region */
  if (G_UNLIKELY (overlay_blue == NULL))
    {
      overlay_blue = cogl_pipeline_new (ctx);
      cogl_pipeline_set_color4ub (overlay_blue, 0x00, 0x00, 0x33, 0x33);
    }

  n_rects = cairo_region_num_rectangles (swap_region);
  for (i = 0; i < n_rects; i++)
    {
      cairo_rectangle_int_t rect;
      float x_1, x_2, y_1, y_2;

      cairo_region_get_rectangle (swap_region, i, &rect);
      x_1 = rect.x;
      x_2 = rect.x + rect.width;
      y_1 = rect.y;
      y_2 = rect.y + rect.height;

      cogl_framebuffer_draw_rectangle (framebuffer, overlay_blue, x_1, y_1, x_2, y_2);
    }

  /* Red for the clip */
  if (queued_redraw_clip)
    {
      static CoglPipeline *overlay_red = NULL;

      if (G_UNLIKELY (overlay_red == NULL))
        {
          overlay_red = cogl_pipeline_new (ctx);
          cogl_pipeline_set_color4ub (overlay_red, 0x33, 0x00, 0x00, 0x33);
        }

      n_rects = cairo_region_num_rectangles (queued_redraw_clip);
      for (i = 0; i < n_rects; i++)
        {
          cairo_rectangle_int_t rect;
          float x_1, x_2, y_1, y_2;

          cairo_region_get_rectangle (queued_redraw_clip, i, &rect);
          x_1 = rect.x;
          x_2 = rect.x + rect.width;
          y_1 = rect.y;
          y_2 = rect.y + rect.height;

          cogl_framebuffer_draw_rectangle (framebuffer, overlay_red, x_1, y_1, x_2, y_2);
        }
    }

  cogl_framebuffer_pop_matrix (framebuffer);
}

static gboolean
swap_framebuffer (ClutterStageWindow *stage_window,
                  ClutterStageView   *view,
                  cairo_region_t     *swap_region,
                  gboolean            swap_with_damage)
{
  CoglFramebuffer *framebuffer = clutter_stage_view_get_onscreen (view);
  int *damage, n_rects, i;

  n_rects = cairo_region_num_rectangles (swap_region);
  damage = g_newa (int, n_rects * 4);
  for (i = 0; i < n_rects; i++)
    {
      cairo_rectangle_int_t rect;

      cairo_region_get_rectangle (swap_region, i, &rect);
      damage[i * 4] = rect.x;
      damage[i * 4 + 1] = rect.y;
      damage[i * 4 + 2] = rect.width;
      damage[i * 4 + 3] = rect.height;
    }

  if (cogl_is_onscreen (framebuffer))
    {
      CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);

      /* push on the screen */
      if (n_rects > 0 && !swap_with_damage)
        {
          CLUTTER_NOTE (BACKEND,
                        "cogl_onscreen_swap_region (onscreen: %p)",
                        onscreen);

          cogl_onscreen_swap_region (onscreen,
                                     damage, n_rects);

          return FALSE;
        }
      else
        {
          CLUTTER_NOTE (BACKEND, "cogl_onscreen_swap_buffers (onscreen: %p)",
                        onscreen);

          cogl_onscreen_swap_buffers_with_damage (onscreen,
                                                  damage, n_rects);

          return TRUE;
        }
    }
  else
    {
      CLUTTER_NOTE (BACKEND, "cogl_framebuffer_finish (framebuffer: %p)",
                    framebuffer);
      cogl_framebuffer_finish (framebuffer);

      return FALSE;
    }
}

static void
scale_and_clamp_rect (const graphene_rect_t *rect,
                      float                  scale,
                      cairo_rectangle_int_t *dest)

{
  graphene_rect_t tmp = *rect;

  graphene_rect_scale (&tmp, scale, scale, &tmp);
  _clutter_util_rectangle_int_extents (&tmp, dest);
}

static cairo_region_t *
offset_scale_and_clamp_region (const cairo_region_t *region,
                               int                   offset_x,
                               int                   offset_y,
                               float                 scale)
{
  int n_rects, i;
  cairo_rectangle_int_t *rects;
  g_autofree cairo_rectangle_int_t *freeme = NULL;

  n_rects = cairo_region_num_rectangles (region);

  if (n_rects == 0)
    return cairo_region_create ();

  if (n_rects < MAX_STACK_RECTS)
    rects = g_newa (cairo_rectangle_int_t, n_rects);
  else
    rects = freeme = g_new (cairo_rectangle_int_t, n_rects);

  for (i = 0; i < n_rects; i++)
    cairo_region_get_rectangle (region, i, &rects[i]);

  for (i = 0; i < n_rects; i++)
    {
      graphene_rect_t tmp;

      _clutter_util_rect_from_rectangle (&rects[i], &tmp);
      graphene_rect_offset (&tmp, offset_x, offset_y);
      scale_and_clamp_rect (&tmp, scale, &rects[i]);
    }

  return cairo_region_create_rectangles (rects, n_rects);
}

static void
paint_stage (ClutterStageCogl *stage_cogl,
             ClutterStageView *view,
             cairo_region_t   *redraw_clip)
{
  ClutterStage *stage = stage_cogl->wrapper;

  _clutter_stage_maybe_setup_viewport (stage, view);
  clutter_stage_paint_view (stage, view, redraw_clip);

  clutter_stage_view_after_paint (view);
}

static void
fill_current_damage_history (ClutterStageView *view,
                             cairo_region_t   *damage)
{
  ClutterStageViewCogl *view_cogl = CLUTTER_STAGE_VIEW_COGL (view);
  ClutterStageViewCoglPrivate *view_priv =
    clutter_stage_view_cogl_get_instance_private (view_cogl);
  cairo_region_t **current_fb_damage;

  current_fb_damage =
    &view_priv->damage_history[DAMAGE_HISTORY (view_priv->damage_index)];

  g_clear_pointer (current_fb_damage, cairo_region_destroy);
  *current_fb_damage = cairo_region_copy (damage);
  view_priv->damage_index++;
}

static void
fill_current_damage_history_rectangle (ClutterStageView            *view,
                                       const cairo_rectangle_int_t *rect)
{
  cairo_region_t *damage;

  damage = cairo_region_create_rectangle (rect);
  fill_current_damage_history (view, damage);
  cairo_region_destroy (damage);
}

static cairo_region_t *
transform_swap_region_to_onscreen (ClutterStageView *view,
                                   cairo_region_t   *swap_region)
{
  CoglFramebuffer *framebuffer;
  cairo_rectangle_int_t layout;
  gint width, height;
  int n_rects, i;
  cairo_rectangle_int_t *rects;
  cairo_region_t *transformed_region;

  framebuffer = clutter_stage_view_get_onscreen (view);
  clutter_stage_view_get_layout (view, &layout);

  width = cogl_framebuffer_get_width (framebuffer);
  height = cogl_framebuffer_get_height (framebuffer);

  n_rects = cairo_region_num_rectangles (swap_region);
  rects = g_newa (cairo_rectangle_int_t, n_rects);
  for (i = 0; i < n_rects; i++)
    {
      gfloat x1, y1, x2, y2;

      cairo_region_get_rectangle (swap_region, i, &rects[i]);

      x1 = (float) rects[i].x / layout.width;
      y1 = (float) rects[i].y / layout.height;
      x2 = (float) (rects[i].x + rects[i].width) / layout.width;
      y2 = (float) (rects[i].y + rects[i].height) / layout.height;

      clutter_stage_view_transform_to_onscreen (view, &x1, &y1);
      clutter_stage_view_transform_to_onscreen (view, &x2, &y2);

      x1 = floor (x1 * width);
      y1 = floor (height - (y1 * height));
      x2 = ceil (x2 * width);
      y2 = ceil (height - (y2 * height));

      rects[i].x = x1;
      rects[i].y = y1;
      rects[i].width = x2 - x1;
      rects[i].height = y2 - y1;
    }
  transformed_region = cairo_region_create_rectangles (rects, n_rects);

  return transformed_region;
}

static void
calculate_scissor_region (cairo_rectangle_int_t *fb_clip_region,
                          int                    subpixel_compensation,
                          int                    fb_width,
                          int                    fb_height,
                          cairo_rectangle_int_t *out_scissor_rect)
{
  *out_scissor_rect = *fb_clip_region;

  if (subpixel_compensation == 0)
    return;

  if (fb_clip_region->x > 0)
    out_scissor_rect->x += subpixel_compensation;
  if (fb_clip_region->y > 0)
    out_scissor_rect->y += subpixel_compensation;
  if (fb_clip_region->x + fb_clip_region->width < fb_width)
    out_scissor_rect->width -= 2 * subpixel_compensation;
  if (fb_clip_region->y + fb_clip_region->height < fb_height)
    out_scissor_rect->height -= 2 * subpixel_compensation;
}

static inline gboolean
is_buffer_age_enabled (void)
{
  /* Buffer age is disabled when running with CLUTTER_PAINT=damage-region,
   * to ensure the red damage represents the currently damaged area */
  return !(clutter_paint_debug_flags & CLUTTER_DEBUG_PAINT_DAMAGE_REGION) &&
         cogl_clutter_winsys_has_feature (COGL_WINSYS_FEATURE_BUFFER_AGE);
}

static gboolean
clutter_stage_cogl_redraw_view (ClutterStageWindow *stage_window,
                                ClutterStageView   *view)
{
  ClutterStageCogl *stage_cogl = CLUTTER_STAGE_COGL (stage_window);
  ClutterStageViewCogl *view_cogl = CLUTTER_STAGE_VIEW_COGL (view);
  ClutterStageViewCoglPrivate *view_priv =
    clutter_stage_view_cogl_get_instance_private (view_cogl);
  CoglFramebuffer *fb = clutter_stage_view_get_framebuffer (view);
  cairo_rectangle_int_t view_rect;
  gboolean is_full_redraw;
  gboolean may_use_clipped_redraw;
  gboolean use_clipped_redraw;
  gboolean can_blit_sub_buffer;
  gboolean has_buffer_age;
  gboolean do_swap_buffer;
  gboolean swap_with_damage;
  ClutterActor *wrapper;
  cairo_region_t *redraw_clip;
  cairo_region_t *queued_redraw_clip = NULL;
  cairo_region_t *fb_clip_region;
  cairo_region_t *swap_region;
  cairo_rectangle_int_t redraw_rect;
  gboolean clip_region_empty;
  float fb_scale;
  int subpixel_compensation = 0;
  int fb_width, fb_height;
  int buffer_age;

  wrapper = CLUTTER_ACTOR (stage_cogl->wrapper);

  clutter_stage_view_get_layout (view, &view_rect);
  fb_scale = clutter_stage_view_get_scale (view);
  fb_width = cogl_framebuffer_get_width (fb);
  fb_height = cogl_framebuffer_get_height (fb);

  can_blit_sub_buffer =
    cogl_is_onscreen (fb) &&
    cogl_clutter_winsys_has_feature (COGL_WINSYS_FEATURE_SWAP_REGION);

  has_buffer_age = cogl_is_onscreen (fb) && is_buffer_age_enabled ();

  redraw_clip = clutter_stage_view_take_redraw_clip (view);
  if (G_UNLIKELY (clutter_paint_debug_flags & CLUTTER_DEBUG_PAINT_DAMAGE_REGION))
    queued_redraw_clip = cairo_region_copy (redraw_clip);

  /* NB: a NULL redraw clip == full stage redraw */
  if (!redraw_clip)
    is_full_redraw = TRUE;
  else
    is_full_redraw = FALSE;

  may_use_clipped_redraw =
    _clutter_stage_window_can_clip_redraws (stage_window) &&
    (can_blit_sub_buffer || has_buffer_age) &&
    !is_full_redraw &&
    /* some drivers struggle to get going and produce some junk
     * frames when starting up... */
    cogl_onscreen_get_frame_counter (COGL_ONSCREEN (fb)) > 3;

  if (has_buffer_age)
    {
      buffer_age = cogl_onscreen_get_buffer_age (COGL_ONSCREEN (fb));
      if (!valid_buffer_age (view_cogl, buffer_age))
        {
          CLUTTER_NOTE (CLIPPING, "Invalid back buffer(age=%d): forcing full redraw\n", buffer_age);
          may_use_clipped_redraw = FALSE;
        }
    }

  if (may_use_clipped_redraw)
    {
      fb_clip_region = offset_scale_and_clamp_region (redraw_clip,
                                                      -view_rect.x,
                                                      -view_rect.y,
                                                      fb_scale);

      if (fb_scale != floorf (fb_scale))
        {
          int n_rects, i;
          cairo_rectangle_int_t *rects;

          subpixel_compensation = ceilf (fb_scale);

          n_rects = cairo_region_num_rectangles (fb_clip_region);
          rects = g_newa (cairo_rectangle_int_t, n_rects);
          for (i = 0; i < n_rects; i++)
            {
              cairo_region_get_rectangle (fb_clip_region, i, &rects[i]);
              rects[i].x -= subpixel_compensation;
              rects[i].y -= subpixel_compensation;
              rects[i].width += 2 * subpixel_compensation;
              rects[i].height += 2 * subpixel_compensation;
            }
          cairo_region_destroy (fb_clip_region);
          fb_clip_region = cairo_region_create_rectangles (rects, n_rects);
        }
    }
  else
    {
      cairo_rectangle_int_t fb_rect;

      fb_rect = (cairo_rectangle_int_t) {
        .width = fb_width,
        .height = fb_height,
      };
      fb_clip_region = cairo_region_create_rectangle (&fb_rect);

      g_clear_pointer (&redraw_clip, cairo_region_destroy);
      redraw_clip = cairo_region_create_rectangle (&view_rect);
    }

  if (may_use_clipped_redraw &&
      G_LIKELY (!(clutter_paint_debug_flags & CLUTTER_DEBUG_DISABLE_CLIPPED_REDRAWS)))
    use_clipped_redraw = TRUE;
  else
    use_clipped_redraw = FALSE;

  clip_region_empty = may_use_clipped_redraw && cairo_region_is_empty (fb_clip_region);

  swap_with_damage = FALSE;
  if (has_buffer_age)
    {
      if (use_clipped_redraw && !clip_region_empty)
        {
          cairo_region_t *fb_damage;
          cairo_region_t *view_damage;
          int i;

          fill_current_damage_history (view, fb_clip_region);

          fb_damage = cairo_region_create ();

          for (i = 1; i <= buffer_age; i++)
            {
              int damage_index;

              damage_index = DAMAGE_HISTORY (view_priv->damage_index - i - 1);
              cairo_region_union (fb_damage,
                                  view_priv->damage_history[damage_index]);
            }

          /* Update the fb clip region with the extra damage. */
          cairo_region_union (fb_clip_region, fb_damage);

          view_damage = offset_scale_and_clamp_region (fb_damage,
                                                       0, 0,
                                                       1.0f / fb_scale);
          cairo_region_translate (view_damage, view_rect.x, view_rect.y);
          cairo_region_intersect_rectangle (view_damage, &view_rect);

          /* Update the redraw clip region with the extra damage. */
          cairo_region_union (redraw_clip, view_damage);

          cairo_region_destroy (view_damage);
          cairo_region_destroy (fb_damage);

          CLUTTER_NOTE (CLIPPING, "Reusing back buffer(age=%d) - repairing region: num rects: %d\n",
                        buffer_age,
                        cairo_region_num_rectangles (fb_clip_region));

          swap_with_damage = TRUE;
        }
      else if (!use_clipped_redraw)
        {
          cairo_rectangle_int_t fb_damage;

          fb_damage = (cairo_rectangle_int_t) {
            .x = 0,
            .y = 0,
            .width = ceilf (view_rect.width * fb_scale),
            .height = ceilf (view_rect.height * fb_scale)
          };
          fill_current_damage_history_rectangle (view, &fb_damage);
        }
    }

  if (use_clipped_redraw && clip_region_empty)
    {
      CLUTTER_NOTE (CLIPPING, "Empty stage output paint\n");
    }
  else if (use_clipped_redraw)
    {
      cairo_rectangle_int_t clip_rect;
      cairo_rectangle_int_t scissor_rect;

      if (cairo_region_num_rectangles (fb_clip_region) == 1)
        {
          cairo_region_get_extents (fb_clip_region, &clip_rect);

          calculate_scissor_region (&clip_rect,
                                    subpixel_compensation,
                                    fb_width, fb_height,
                                    &scissor_rect);

          CLUTTER_NOTE (CLIPPING,
                        "Stage clip pushed: x=%d, y=%d, width=%d, height=%d\n",
                        scissor_rect.x,
                        scissor_rect.y,
                        scissor_rect.width,
                        scissor_rect.height);

          cogl_framebuffer_push_scissor_clip (fb,
                                              scissor_rect.x,
                                              scissor_rect.y,
                                              scissor_rect.width,
                                              scissor_rect.height);
        }
      else
        {
          cogl_framebuffer_push_region_clip (fb, fb_clip_region);
        }

      paint_stage (stage_cogl, view, redraw_clip);

      cogl_framebuffer_pop_clip (fb);
    }
  else
    {
      CLUTTER_NOTE (CLIPPING, "Unclipped stage paint\n");

      /* If we are trying to debug redraw issues then we want to pass
       * the redraw_clip so it can be visualized */
      if (G_UNLIKELY (clutter_paint_debug_flags & CLUTTER_DEBUG_DISABLE_CLIPPED_REDRAWS) &&
          may_use_clipped_redraw &&
          !clip_region_empty)
        {
          cairo_rectangle_int_t clip_rect;
          cairo_rectangle_int_t scissor_rect;

          cairo_region_get_extents (fb_clip_region, &clip_rect);

          calculate_scissor_region (&clip_rect,
                                    subpixel_compensation,
                                    fb_width, fb_height,
                                    &scissor_rect);

          cogl_framebuffer_push_scissor_clip (fb,
                                              scissor_rect.x,
                                              scissor_rect.y,
                                              scissor_rect.width,
                                              scissor_rect.height);

          paint_stage (stage_cogl, view, redraw_clip);

          cogl_framebuffer_pop_clip (fb);
        }
      else
        {
          paint_stage (stage_cogl, view, redraw_clip);
        }
    }

  cairo_region_get_extents (redraw_clip, &redraw_rect);

  if (may_use_clipped_redraw &&
      G_UNLIKELY ((clutter_paint_debug_flags & CLUTTER_DEBUG_REDRAWS)))
    {
      CoglContext *ctx = cogl_framebuffer_get_context (fb);
      static CoglPipeline *outline = NULL;
      ClutterActor *actor = CLUTTER_ACTOR (wrapper);
      float x_1 = redraw_rect.x;
      float x_2 = redraw_rect.x + redraw_rect.width;
      float y_1 = redraw_rect.y;
      float y_2 = redraw_rect.y + redraw_rect.height;
      CoglVertexP2 quad[4] = {
        { x_1, y_1 },
        { x_2, y_1 },
        { x_2, y_2 },
        { x_1, y_2 }
      };
      CoglPrimitive *prim;
      CoglMatrix modelview;

      if (outline == NULL)
        {
          outline = cogl_pipeline_new (ctx);
          cogl_pipeline_set_color4ub (outline, 0xff, 0x00, 0x00, 0xff);
        }

      prim = cogl_primitive_new_p2 (ctx,
                                    COGL_VERTICES_MODE_LINE_LOOP,
                                    4, /* n_vertices */
                                    quad);

      cogl_framebuffer_push_matrix (fb);
      cogl_matrix_init_identity (&modelview);
      _clutter_actor_apply_modelview_transform (actor, &modelview);
      cogl_framebuffer_set_modelview_matrix (fb, &modelview);
      cogl_framebuffer_draw_primitive (fb, outline, prim);
      cogl_framebuffer_pop_matrix (fb);
      cogl_object_unref (prim);
    }

  /* XXX: It seems there will be a race here in that the stage
   * window may be resized before the cogl_onscreen_swap_region
   * is handled and so we may copy the wrong region. I can't
   * really see how we can handle this with the current state of X
   * but at least in this case a full redraw should be queued by
   * the resize anyway so it should only exhibit temporary
   * artefacts.
   */
  if (use_clipped_redraw)
    {
      if (clip_region_empty)
        {
          do_swap_buffer = FALSE;
        }
      else
        {
          swap_region = cairo_region_reference (fb_clip_region);
          do_swap_buffer = TRUE;
        }
    }
  else
    {
      swap_region = cairo_region_create ();
      do_swap_buffer = TRUE;
    }

  g_clear_pointer (&redraw_clip, cairo_region_destroy);
  g_clear_pointer (&fb_clip_region, cairo_region_destroy);

  if (do_swap_buffer)
    {
      gboolean res;

      COGL_TRACE_BEGIN_SCOPED (ClutterStageCoglRedrawViewSwapFramebuffer,
                               "Paint (swap framebuffer)");

      if (clutter_stage_view_get_onscreen (view) !=
          clutter_stage_view_get_framebuffer (view))
        {
          cairo_region_t *transformed_swap_region;

          transformed_swap_region =
            transform_swap_region_to_onscreen (view, swap_region);
          cairo_region_destroy (swap_region);
          swap_region = transformed_swap_region;
        }

      if (queued_redraw_clip)
        {
          paint_damage_region (stage_window, view,
                               swap_region, queued_redraw_clip);
          cairo_region_destroy (queued_redraw_clip);
        }

      res = swap_framebuffer (stage_window,
                              view,
                              swap_region,
                              swap_with_damage);

      cairo_region_destroy (swap_region);

      return res;
    }
  else
    {
      g_clear_pointer (&queued_redraw_clip, cairo_region_destroy);
      return FALSE;
    }
}

static void
clutter_stage_cogl_scanout_view (ClutterStageCogl *stage_cogl,
                                 ClutterStageView *view,
                                 CoglScanout      *scanout)
{
  CoglFramebuffer *framebuffer = clutter_stage_view_get_framebuffer (view);
  CoglOnscreen *onscreen;

  g_return_if_fail (cogl_is_onscreen (framebuffer));

  onscreen = COGL_ONSCREEN (framebuffer);
  cogl_onscreen_direct_scanout (onscreen, scanout);
}

static void
clutter_stage_cogl_redraw (ClutterStageWindow *stage_window)
{
  ClutterStageCogl *stage_cogl = CLUTTER_STAGE_COGL (stage_window);
  gboolean has_redraw_clip = FALSE;
  gboolean swap_event = FALSE;
  GList *l;

  COGL_TRACE_BEGIN (ClutterStageCoglRedraw, "Paint (Cogl Redraw)");

  for (l = _clutter_stage_window_get_views (stage_window); l; l = l->next)
    {
      ClutterStageView *view = l->data;

      if (!clutter_stage_view_has_redraw_clip (view))
        continue;

      has_redraw_clip = TRUE;
      break;
    }

  if (has_redraw_clip)
    clutter_stage_emit_before_paint (stage_cogl->wrapper);

  for (l = _clutter_stage_window_get_views (stage_window); l; l = l->next)
    {
      ClutterStageView *view = l->data;
      g_autoptr (CoglScanout) scanout = NULL;

      if (!clutter_stage_view_has_redraw_clip (view))
        continue;

      scanout = clutter_stage_view_take_scanout (view);
      if (scanout)
        {
          clutter_stage_cogl_scanout_view (stage_cogl,
                                           view,
                                           scanout);
          swap_event = TRUE;
        }
      else
        {
          swap_event |= clutter_stage_cogl_redraw_view (stage_window, view);
        }

      swap_event |= clutter_stage_cogl_redraw_view (stage_window, view);
    }

  if (has_redraw_clip)
    clutter_stage_emit_after_paint (stage_cogl->wrapper);

  _clutter_stage_window_finish_frame (stage_window);

  if (swap_event)
    {
      /* If we have swap buffer events then cogl_onscreen_swap_buffers
       * will return immediately and we need to track that there is a
       * swap in progress... */
      if (clutter_feature_available (CLUTTER_FEATURE_SWAP_EVENTS))
        stage_cogl->pending_swaps++;
    }

  stage_cogl->frame_count++;

  COGL_TRACE_END (ClutterStageCoglRedraw);
}

static void
clutter_stage_window_iface_init (ClutterStageWindowInterface *iface)
{
  iface->realize = clutter_stage_cogl_realize;
  iface->unrealize = clutter_stage_cogl_unrealize;
  iface->get_wrapper = clutter_stage_cogl_get_wrapper;
  iface->resize = clutter_stage_cogl_resize;
  iface->show = clutter_stage_cogl_show;
  iface->hide = clutter_stage_cogl_hide;
  iface->schedule_update = clutter_stage_cogl_schedule_update;
  iface->get_update_time = clutter_stage_cogl_get_update_time;
  iface->clear_update_time = clutter_stage_cogl_clear_update_time;
  iface->get_next_presentation_time = clutter_stage_cogl_get_next_presentation_time;
  iface->redraw = clutter_stage_cogl_redraw;
}

static void
clutter_stage_cogl_set_property (GObject      *gobject,
				 guint         prop_id,
				 const GValue *value,
				 GParamSpec   *pspec)
{
  ClutterStageCogl *self = CLUTTER_STAGE_COGL (gobject);

  switch (prop_id)
    {
    case PROP_WRAPPER:
      self->wrapper = g_value_get_object (value);
      break;

    case PROP_BACKEND:
      self->backend = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
_clutter_stage_cogl_class_init (ClutterStageCoglClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = clutter_stage_cogl_set_property;

  g_object_class_override_property (gobject_class, PROP_WRAPPER, "wrapper");
  g_object_class_override_property (gobject_class, PROP_BACKEND, "backend");
}

static void
_clutter_stage_cogl_init (ClutterStageCogl *stage)
{
  stage->last_presentation_time = 0;
  stage->refresh_rate = 0.0;

  stage->update_time = -1;
  stage->next_presentation_time = -1;
}

static void
clutter_stage_view_cogl_init (ClutterStageViewCogl *view_cogl)
{
}

static void
clutter_stage_view_cogl_class_init (ClutterStageViewCoglClass *klass)
{
}
