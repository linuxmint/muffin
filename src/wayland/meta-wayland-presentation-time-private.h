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

#pragma once

#include <wayland-server.h>

#include "clutter/clutter.h"
#include "wayland/meta-wayland-types.h"

typedef struct _CoglContext CoglContext;

typedef struct _MetaWaylandPresentationFeedback
{
  struct wl_list link;
  struct wl_resource *resource;

  MetaWaylandSurface *surface;
} MetaWaylandPresentationFeedback;

typedef struct _MetaWaylandPresentationTime
{
  /*
   * List of surfaces that have pending presentation-time feedbacks.
   * Analogous to MetaWaylandCompositor.frame_callback_surfaces.
   */
  GList *feedback_surfaces;

  /*
   * Feedbacks collected after painting, waiting to be delivered on
   * frame completion. Since Muffin has a single frame clock, we don't
   * need per-view tracking - all feedbacks go into one list.
   */
  struct wl_list pending_feedbacks;

  /*
   * CoglContext used for converting frame timestamps from the Cogl
   * clock domain to CLOCK_MONOTONIC.
   */
  CoglContext *cogl_context;
} MetaWaylandPresentationTime;

void meta_wayland_presentation_time_finalize (MetaWaylandCompositor *compositor);

void meta_wayland_init_presentation_time (MetaWaylandCompositor *compositor);

void meta_wayland_presentation_feedback_discard (MetaWaylandPresentationFeedback *feedback);

void meta_wayland_surface_discard_presentation_feedback (MetaWaylandSurface *surface);

void meta_wayland_surface_state_discard_presentation_feedback (MetaWaylandSurfaceState *state);
