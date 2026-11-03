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
#include "clutter-stage-private.h"

struct _ClutterPickContext
{
  grefcount ref_count;

  ClutterStage *stage;
  CoglFramebuffer *framebuffer;
};

G_DEFINE_BOXED_TYPE (ClutterPickContext, clutter_pick_context,
                     clutter_pick_context_ref,
                     clutter_pick_context_unref)

ClutterPickContext *
clutter_pick_context_new_for_view (ClutterStage     *stage,
                                   ClutterStageView *view)
{
  ClutterPickContext *pick_context;

  pick_context = g_new0 (ClutterPickContext, 1);
  g_ref_count_init (&pick_context->ref_count);
  pick_context->stage = stage;
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

gboolean
clutter_pick_context_transform_box_to_stage (ClutterPickContext    *pick_context,
                                             const ClutterActorBox *box,
                                             graphene_point_t       vertices[4])
{
  CoglMatrix stage_transform, inv_stage_transform;
  CoglMatrix modelview, transform_to_stage;
  int v;

  clutter_actor_get_transform (CLUTTER_ACTOR (pick_context->stage), &stage_transform);
  if (!cogl_matrix_get_inverse (&stage_transform, &inv_stage_transform))
    return FALSE;
  cogl_framebuffer_get_modelview_matrix (pick_context->framebuffer, &modelview);
  cogl_matrix_multiply (&transform_to_stage, &inv_stage_transform, &modelview);

  vertices[0].x = box->x1;
  vertices[0].y = box->y1;

  vertices[1].x = box->x2;
  vertices[1].y = box->y1;

  vertices[2].x = box->x2;
  vertices[2].y = box->y2;

  vertices[3].x = box->x1;
  vertices[3].y = box->y2;

  for (v = 0; v < 4; v++)
    {
      float z = 0.f;
      float w = 1.f;

      cogl_matrix_transform_point (&transform_to_stage,
                                   &vertices[v].x,
                                   &vertices[v].y,
                                   &z,
                                   &w);
    }

  return TRUE;
}

/**
 * clutter_pick_context_push_clip:
 * @pick_context: a #ClutterPickContext
 * @box: a #ClutterActorBox in the current actor's local coordinates
 *
 * Pushes a clip rectangle defined by @box into the pick stack. Pop with
 * clutter_pick_context_pop_clip() when done.
 */
void
clutter_pick_context_push_clip (ClutterPickContext    *pick_context,
                                const ClutterActorBox *box)
{
  /* If the box can't be transformed, the empty clip keeps the stack balanced */
  graphene_point_t vertices[4] = { 0 };

  g_return_if_fail (pick_context != NULL);
  g_return_if_fail (box != NULL);

  clutter_pick_context_transform_box_to_stage (pick_context, box, vertices);
  clutter_stage_push_pick_clip (pick_context->stage, vertices);
}

/**
 * clutter_pick_context_pop_clip:
 * @pick_context: a #ClutterPickContext
 *
 * Pops the current clip rectangle from the pick stack. It is a programming
 * error to call this without a corresponding clutter_pick_context_push_clip()
 * call first.
 */
void
clutter_pick_context_pop_clip (ClutterPickContext *pick_context)
{
  g_return_if_fail (pick_context != NULL);

  clutter_stage_pop_pick_clip (pick_context->stage);
}
