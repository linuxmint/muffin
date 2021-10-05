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

#include "clutter-paint-context-private.h"

struct _ClutterPaintContext
{
  grefcount ref_count;

  ClutterPaintFlag paint_flags;

  GList *framebuffers;

  ClutterStageView *view;

  cairo_region_t *redraw_clip;
};

G_DEFINE_BOXED_TYPE (ClutterPaintContext, clutter_paint_context,
                     clutter_paint_context_ref,
                     clutter_paint_context_unref)

ClutterPaintContext *
clutter_paint_context_new_for_view (ClutterStageView     *view,
                                    const cairo_region_t *redraw_clip,
                                    ClutterPaintFlag      paint_flags)
{
  ClutterPaintContext *paint_context;
  CoglFramebuffer *framebuffer;

  paint_context = g_new0 (ClutterPaintContext, 1);
  g_ref_count_init (&paint_context->ref_count);
  paint_context->view = view;
  paint_context->redraw_clip = cairo_region_copy (redraw_clip);
  paint_context->paint_flags = paint_flags;

  framebuffer = clutter_stage_view_get_framebuffer (view);
  clutter_paint_context_push_framebuffer (paint_context, framebuffer);

  return paint_context;
}

/**
 * clutter_paint_context_new_for_framebuffer: (skip)
 */
ClutterPaintContext *
clutter_paint_context_new_for_framebuffer (CoglFramebuffer *framebuffer)
{
  ClutterPaintContext *paint_context;

  paint_context = g_new0 (ClutterPaintContext, 1);
  g_ref_count_init (&paint_context->ref_count);
  paint_context->paint_flags = (CLUTTER_PAINT_FLAG_NO_CURSORS |
                                CLUTTER_PAINT_FLAG_NO_PAINT_SIGNAL);

  clutter_paint_context_push_framebuffer (paint_context, framebuffer);

  return paint_context;
}

ClutterPaintContext *
clutter_paint_context_ref (ClutterPaintContext *paint_context)
{
  g_ref_count_inc (&paint_context->ref_count);
  return paint_context;
}

static void
clutter_paint_context_dispose (ClutterPaintContext *paint_context)
{
  g_list_free_full (paint_context->framebuffers,
                    cogl_object_unref);
  paint_context->framebuffers = NULL;
  g_clear_pointer (&paint_context->redraw_clip, cairo_region_destroy);
}

void
clutter_paint_context_unref (ClutterPaintContext *paint_context)
{
  if (g_ref_count_dec (&paint_context->ref_count))
    {
      clutter_paint_context_dispose (paint_context);
      g_free (paint_context);
    }
}

void
clutter_paint_context_destroy (ClutterPaintContext *paint_context)
{
  clutter_paint_context_dispose (paint_context);
  clutter_paint_context_unref (paint_context);
}

void
clutter_paint_context_push_framebuffer (ClutterPaintContext *paint_context,
                                        CoglFramebuffer     *framebuffer)
{
  paint_context->framebuffers = g_list_prepend (paint_context->framebuffers,
                                                cogl_object_ref (framebuffer));
}

void
clutter_paint_context_pop_framebuffer (ClutterPaintContext *paint_context)
{
  g_return_if_fail (paint_context->framebuffers);

  cogl_object_unref (paint_context->framebuffers->data);
  paint_context->framebuffers =
    g_list_delete_link (paint_context->framebuffers,
                        paint_context->framebuffers);
}

const cairo_region_t *
clutter_paint_context_get_redraw_clip (ClutterPaintContext *paint_context)
{
  return paint_context->redraw_clip;
}

/**
 * clutter_paint_context_get_framebuffer:
 * @paint_context: The #ClutterPaintContext
 *
 * Returns: (transfer none): The #CoglFramebuffer used for drawing
 */
CoglFramebuffer *
clutter_paint_context_get_framebuffer (ClutterPaintContext *paint_context)
{
  g_return_val_if_fail (paint_context->framebuffers, NULL);

  return paint_context->framebuffers->data;
}

CoglFramebuffer *
clutter_paint_context_get_base_framebuffer (ClutterPaintContext *paint_context)
{
  return g_list_last (paint_context->framebuffers)->data;
}

/**
 * clutter_paint_context_get_stage_view: (skip)
 */
ClutterStageView *
clutter_paint_context_get_stage_view (ClutterPaintContext *paint_context)
{
  return paint_context->view;
}

/**
 * clutter_paint_context_is_drawing_off_stage: (skip)
 *
 * Return %TRUE if the paint context is currently drawing off stage.
 * This happens if there are any framebuffers pushed, and the base framebuffer
 * comes from the stage view.
 */
gboolean
clutter_paint_context_is_drawing_off_stage (ClutterPaintContext *paint_context)
{
  if (g_list_length (paint_context->framebuffers) > 1)
    return TRUE;

  return !paint_context->view;
}

/**
 * clutter_paint_context_get_paint_flags: (skip)
 */
ClutterPaintFlag
clutter_paint_context_get_paint_flags (ClutterPaintContext *paint_context)
{
  return paint_context->paint_flags;
}
