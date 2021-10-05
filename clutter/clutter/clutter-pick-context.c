/*
 * Copyright (C) 2019 Red Hat Inc.
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

#include "clutter-build-config.h"

#include "clutter-pick-context-private.h"

struct _ClutterPickContext
{
  grefcount ref_count;

  CoglFramebuffer *framebuffer;
};

G_DEFINE_BOXED_TYPE (ClutterPickContext, clutter_pick_context,
                     clutter_pick_context_ref,
                     clutter_pick_context_unref)

ClutterPickContext *
clutter_pick_context_new_for_view (ClutterStageView *view)
{
  ClutterPickContext *pick_context;

  pick_context = g_new0 (ClutterPickContext, 1);
  g_ref_count_init (&pick_context->ref_count);
  pick_context->framebuffer =
    cogl_object_ref (clutter_stage_view_get_framebuffer (view));

  return pick_context;
}

ClutterPickContext *
clutter_pick_context_ref (ClutterPickContext *pick_context)
{
  g_ref_count_inc (&pick_context->ref_count);
  return pick_context;
}

static void
clutter_pick_context_dispose (ClutterPickContext *pick_context)
{
  g_clear_pointer (&pick_context->framebuffer, cogl_object_unref);
}

void
clutter_pick_context_unref (ClutterPickContext *pick_context)
{
  if (g_ref_count_dec (&pick_context->ref_count))
    {
      clutter_pick_context_dispose (pick_context);
      g_free (pick_context);
    }
}

void
clutter_pick_context_destroy (ClutterPickContext *pick_context)
{
  clutter_pick_context_dispose (pick_context);
  clutter_pick_context_unref (pick_context);
}

/**
 * clutter_pick_context_get_framebuffer: (skip)
 */
CoglFramebuffer *
clutter_pick_context_get_framebuffer (ClutterPickContext *pick_context)
{
  return pick_context->framebuffer;
}
