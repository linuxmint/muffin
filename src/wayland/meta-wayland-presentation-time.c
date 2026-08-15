/*
 * presentation-time protocol
 *
 * Copyright (C) 2020 Ivan Molodetskikh <yalterz@gmail.com>
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <glib.h>

#include "clutter/clutter.h"
#include "cogl/cogl.h"
#include "compositor/meta-surface-actor.h"
#include "wayland/meta-wayland-presentation-time-private.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-outputs.h"
#include "wayland/meta-wayland-versions.h"

#include "presentation-time-server-protocol.h"

static inline uint64_t
ns2s (int64_t ns)
{
  return (uint64_t) ns / 1000000000;
}

static inline uint32_t
ns2ns_rem (int64_t ns)
{
  return (uint32_t) ((uint64_t) ns % 1000000000);
}

void
meta_wayland_surface_state_discard_presentation_feedback (MetaWaylandSurfaceState *state)
{
  MetaWaylandPresentationFeedback *feedback, *next;

  wl_list_for_each_safe (feedback, next,
                         &state->presentation_feedback_list,
                         link)
    {
      meta_wayland_presentation_feedback_discard (feedback);
    }
}

void
meta_wayland_surface_discard_presentation_feedback (MetaWaylandSurface *surface)
{
  MetaWaylandPresentationFeedback *feedback, *next;

  wl_list_for_each_safe (feedback, next,
                         &surface->presentation_time.feedback_list,
                         link)
    {
      meta_wayland_presentation_feedback_discard (feedback);
    }

  meta_wayland_compositor_remove_presentation_feedback_surface (surface->compositor,
                                                                surface);
}

static void
wp_presentation_feedback_destructor (struct wl_resource *resource)
{
  MetaWaylandPresentationFeedback *feedback =
    wl_resource_get_user_data (resource);

  wl_list_remove (&feedback->link);
  g_clear_object (&feedback->surface);
  g_free (feedback);
}

static void
wp_presentation_destroy (struct wl_client   *client,
                         struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
wp_presentation_feedback (struct wl_client   *client,
                          struct wl_resource *resource,
                          struct wl_resource *surface_resource,
                          uint32_t            callback_id)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandSurfaceState *pending;
  MetaWaylandPresentationFeedback *feedback;

  feedback = g_new0 (MetaWaylandPresentationFeedback, 1);
  wl_list_init (&feedback->link);
  feedback->resource = wl_resource_create (client,
                                           &wp_presentation_feedback_interface,
                                           wl_resource_get_version (resource),
                                           callback_id);
  wl_resource_set_implementation (feedback->resource,
                                  NULL,
                                  feedback,
                                  wp_presentation_feedback_destructor);

  if (surface == NULL)
    {
      g_warn_if_reached ();
      meta_wayland_presentation_feedback_discard (feedback);
      return;
    }

  pending = meta_wayland_surface_get_pending_state (surface);
  wl_list_insert (&pending->presentation_feedback_list, &feedback->link);

  feedback->surface = g_object_ref (surface);
}

static const struct wp_presentation_interface
meta_wayland_presentation_interface = {
  wp_presentation_destroy,
  wp_presentation_feedback,
};

static void
wp_presentation_bind (struct wl_client *client,
                      void             *data,
                      uint32_t          version,
                      uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &wp_presentation_interface,
                                 version,
                                 id);
  wl_resource_set_implementation (resource,
                                  &meta_wayland_presentation_interface,
                                  NULL,
                                  NULL);

  wp_presentation_send_clock_id (resource, CLOCK_MONOTONIC);
}

static void
on_after_paint (ClutterStage          *stage,
                MetaWaylandCompositor *compositor)
{
  GList *l;

  l = compositor->presentation_time.feedback_surfaces;
  while (l)
    {
      MetaWaylandSurface *surface = l->data;
      MetaSurfaceActor *actor;

      l = l->next;

      actor = meta_wayland_surface_get_actor (surface);
      if (!actor)
        {
          /* Surface has no actor; discard its feedbacks */
          meta_wayland_surface_discard_presentation_feedback (surface);
        }
      else if (!wl_list_empty (&surface->presentation_time.feedback_list))
        {
          wl_list_insert_list (&compositor->presentation_time.pending_feedbacks,
                               &surface->presentation_time.feedback_list);
          wl_list_init (&surface->presentation_time.feedback_list);
        }

      meta_wayland_compositor_remove_presentation_feedback_surface (compositor,
                                                                    surface);
    }
}

static MetaWaylandOutput *
get_output_for_feedback (MetaWaylandCompositor         *compositor,
                         MetaWaylandPresentationFeedback *feedback)
{
  GHashTableIter iter;
  gpointer key, value;

  /* Find the output that the feedback's surface is on */
  g_hash_table_iter_init (&iter, compositor->outputs);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      MetaWaylandOutput *wayland_output = value;

      if (g_hash_table_contains (feedback->surface->outputs_to_destroy_notify_id,
                                 wayland_output))
        return wayland_output;
    }

  /* Fallback: return the first available output */
  g_hash_table_iter_init (&iter, compositor->outputs);
  if (g_hash_table_iter_next (&iter, &key, &value))
    return value;

  return NULL;
}

static void
on_presented (ClutterStage          *stage,
              CoglFrameEvent        frame_event,
              ClutterFrameInfo     *frame_info,
              MetaWaylandCompositor *compositor)
{
  MetaWaylandPresentationFeedback *feedback, *next;
  gint64 presentation_time_cogl;
  gint64 current_cogl_time;
  gint64 current_monotonic_time;
  gint64 presentation_time_us;

  if (frame_event != COGL_FRAME_EVENT_COMPLETE)
    return;

  if (wl_list_empty (&compositor->presentation_time.pending_feedbacks))
    return;

  presentation_time_cogl = frame_info->presentation_time;
  current_cogl_time = cogl_get_clock_time (compositor->presentation_time.cogl_context);
  current_monotonic_time = g_get_monotonic_time ();

  if (presentation_time_cogl != 0)
    {
      presentation_time_us =
        current_monotonic_time + (presentation_time_cogl - current_cogl_time) / 1000;
    }
  else
    {
      presentation_time_us = current_monotonic_time;
    }

  wl_list_for_each_safe (feedback, next,
                         &compositor->presentation_time.pending_feedbacks,
                         link)
    {
      uint64_t time_s;
      uint32_t tv_sec_hi, tv_sec_lo, tv_nsec;
      uint32_t refresh_interval_ns;
      uint32_t seq_hi, seq_lo;
      uint32_t flags;
      MetaWaylandOutput *output;
      const GList *l;

      time_s = ns2s (presentation_time_us * 1000);
      tv_sec_hi = (uint32_t) (time_s >> 32);
      tv_sec_lo = (uint32_t) time_s;
      tv_nsec = ns2ns_rem (presentation_time_us * 1000);

      if (frame_info->refresh_rate > 0)
        refresh_interval_ns =
          (uint32_t) (0.5 + 1000000000.0 / frame_info->refresh_rate);
      else
        refresh_interval_ns = 0;

      /* Use a simple monotonic sequence counter per surface */
      feedback->surface->presentation_time.sequence++;

      seq_hi = (uint32_t) (feedback->surface->presentation_time.sequence >> 32);
      seq_lo = (uint32_t) (feedback->surface->presentation_time.sequence);

      flags = WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION |
              WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK;

      output = get_output_for_feedback (compositor, feedback);
      if (output != NULL)
        {
          for (l = output->resources; l; l = l->next)
            {
              struct wl_resource *output_resource = l->data;

              if (feedback->resource->client == output_resource->client)
                {
                  wp_presentation_feedback_send_sync_output (feedback->resource,
                                                             output_resource);
                }
            }
        }

      wp_presentation_feedback_send_presented (feedback->resource,
                                               tv_sec_hi,
                                               tv_sec_lo,
                                               tv_nsec,
                                               refresh_interval_ns,
                                               seq_hi,
                                               seq_lo,
                                               flags);

      wl_resource_destroy (feedback->resource);
    }

  wl_list_init (&compositor->presentation_time.pending_feedbacks);
}

static void
on_monitors_changed (MetaMonitorManager    *manager,
                     MetaWaylandCompositor *compositor)
{
  /*
   * Outputs were re-created; discard all pending feedbacks since the
   * output references are now invalid and the frame timing information
   * no longer applies to the new output configuration.
   */
  MetaWaylandPresentationFeedback *feedback, *next;

  wl_list_for_each_safe (feedback, next,
                         &compositor->presentation_time.pending_feedbacks,
                         link)
    {
      meta_wayland_presentation_feedback_discard (feedback);
    }

  wl_list_init (&compositor->presentation_time.pending_feedbacks);
}

void
meta_wayland_presentation_time_finalize (MetaWaylandCompositor *compositor)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  ClutterActor *stage = meta_backend_get_stage (backend);

  g_signal_handlers_disconnect_by_func (stage, on_after_paint, compositor);
  g_signal_handlers_disconnect_by_func (stage, on_presented, compositor);
  g_signal_handlers_disconnect_by_func (monitor_manager, on_monitors_changed,
                                        compositor);

  /* Discard any remaining pending feedbacks */
  {
    MetaWaylandPresentationFeedback *feedback, *next;

    wl_list_for_each_safe (feedback, next,
                           &compositor->presentation_time.pending_feedbacks,
                           link)
      {
        meta_wayland_presentation_feedback_discard (feedback);
      }
  }

  g_list_free (compositor->presentation_time.feedback_surfaces);
  compositor->presentation_time.feedback_surfaces = NULL;
}

void
meta_wayland_init_presentation_time (MetaWaylandCompositor *compositor)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  ClutterActor *stage = meta_backend_get_stage (backend);
  ClutterBackend *clutter_backend = clutter_get_default_backend ();
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);

  wl_list_init (&compositor->presentation_time.pending_feedbacks);
  compositor->presentation_time.feedback_surfaces = NULL;
  compositor->presentation_time.cogl_context = cogl_context;

  g_signal_connect (monitor_manager, "monitors-changed-internal",
                    G_CALLBACK (on_monitors_changed), compositor);

  g_signal_connect (stage, "after-paint",
                    G_CALLBACK (on_after_paint), compositor);

  g_signal_connect (stage, "presented",
                    G_CALLBACK (on_presented), compositor);

  if (wl_global_create (compositor->wayland_display,
                        &wp_presentation_interface,
                        META_WP_PRESENTATION_VERSION,
                        NULL,
                        wp_presentation_bind) == NULL)
    g_error ("Failed to register a global wp_presentation object");
}

void
meta_wayland_presentation_feedback_discard (MetaWaylandPresentationFeedback *feedback)
{
  wp_presentation_feedback_send_discarded (feedback->resource);
  wl_resource_destroy (feedback->resource);
}
