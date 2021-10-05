/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009,2010,2011,2012 Intel Corporation.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 *
 * Authors:
 *  Neil Roberts   <neil@linux.intel.com>
 *  Robert Bragg   <robert@linux.intel.com>
 */

#include "cogl-config.h"

#include "cogl-context-private.h"
#include "cogl-primitives-private.h"
#include "cogl-primitive-private.h"
#include "driver/gl/cogl-util-gl-private.h"
#include "driver/gl/cogl-pipeline-opengl-private.h"
#include "driver/gl/cogl-clip-stack-gl-private.h"

#ifndef GL_CLIP_PLANE0
#define GL_CLIP_PLANE0 0x3000
#define GL_CLIP_PLANE1 0x3001
#define GL_CLIP_PLANE2 0x3002
#define GL_CLIP_PLANE3 0x3003
#define GL_CLIP_PLANE4 0x3004
#define GL_CLIP_PLANE5 0x3005
#endif

static void
add_stencil_clip_rectangle (CoglFramebuffer *framebuffer,
                            CoglMatrixEntry *modelview_entry,
                            float x_1,
                            float y_1,
                            float x_2,
                            float y_2,
                            gboolean merge)
{
  CoglMatrixStack *projection_stack =
    _cogl_framebuffer_get_projection_stack (framebuffer);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  CoglMatrixEntry *old_projection_entry, *old_modelview_entry;

  /* NB: This can be called while flushing the journal so we need
   * to be very conservative with what state we change.
   */
  old_projection_entry = g_steal_pointer (&ctx->current_projection_entry);
  old_modelview_entry = g_steal_pointer (&ctx->current_modelview_entry);

  ctx->current_projection_entry = projection_stack->last_entry;
  ctx->current_modelview_entry = modelview_entry;

  GE( ctx, glColorMask (FALSE, FALSE, FALSE, FALSE) );
  GE( ctx, glDepthMask (FALSE) );

  if (merge)
    {
      /* Add one to every pixel of the stencil buffer in the
	 rectangle */
      GE( ctx, glStencilFunc (GL_NEVER, 0x1, 0x3) );
      GE( ctx, glStencilOp (GL_INCR, GL_INCR, GL_INCR) );
      _cogl_rectangle_immediate (framebuffer,
                                 ctx->stencil_pipeline,
                                 x_1, y_1, x_2, y_2);

      /* Subtract one from all pixels in the stencil buffer so that
	 only pixels where both the original stencil buffer and the
	 rectangle are set will be valid */
      GE( ctx, glStencilOp (GL_DECR, GL_DECR, GL_DECR) );

      ctx->current_projection_entry = &ctx->identity_entry;
      ctx->current_modelview_entry = &ctx->identity_entry;

      _cogl_rectangle_immediate (framebuffer,
                                 ctx->stencil_pipeline,
                                 -1.0, -1.0, 1.0, 1.0);
    }
  else
    {
      GE( ctx, glEnable (GL_STENCIL_TEST) );
      GE( ctx, glStencilMask (0x1) );

      /* Initially disallow everything */
      GE( ctx, glClearStencil (0) );
      GE( ctx, glClear (GL_STENCIL_BUFFER_BIT) );

      /* Punch out a hole to allow the rectangle */
      GE( ctx, glStencilFunc (GL_ALWAYS, 0x1, 0x1) );
      GE( ctx, glStencilOp (GL_KEEP, GL_KEEP, GL_REPLACE) );
      _cogl_rectangle_immediate (framebuffer,
                                 ctx->stencil_pipeline,
                                 x_1, y_1, x_2, y_2);
    }

  ctx->current_projection_entry = old_projection_entry;
  ctx->current_modelview_entry = old_modelview_entry;

  /* Restore the stencil mode */
  GE( ctx, glDepthMask (TRUE) );
  GE( ctx, glColorMask (TRUE, TRUE, TRUE, TRUE) );
  GE( ctx, glStencilFunc (GL_EQUAL, 0x1, 0x1) );
  GE( ctx, glStencilOp (GL_KEEP, GL_KEEP, GL_KEEP) );
}

static void
add_stencil_clip_region (CoglFramebuffer *framebuffer,
                         cairo_region_t  *region,
                         gboolean         merge)
{
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  CoglMatrixEntry *old_projection_entry, *old_modelview_entry;
  CoglMatrix matrix;
  int num_rectangles = cairo_region_num_rectangles (region);
  int i;
  CoglVertexP2 *vertices;

  /* NB: This can be called while flushing the journal so we need
   * to be very conservative with what state we change.
   */
  old_projection_entry = g_steal_pointer (&ctx->current_projection_entry);
  old_modelview_entry = g_steal_pointer (&ctx->current_modelview_entry);

  ctx->current_projection_entry = &ctx->identity_entry;
  ctx->current_modelview_entry = &ctx->identity_entry;

  /* The coordinates in the region are meant to be window coordinates,
   * make a matrix that translates those across the viewport, and into
   * the default [-1, -1, 1, 1] range.
   */
  cogl_matrix_init_identity (&matrix);
  cogl_matrix_translate (&matrix, -1, 1, 0);
  cogl_matrix_scale (&matrix,
                     2.0 / framebuffer->viewport_width,
                     - 2.0 / framebuffer->viewport_height,
                     1);
  cogl_matrix_translate (&matrix,
                         - framebuffer->viewport_x,
                         - framebuffer->viewport_y,
                         0);

  GE( ctx, glColorMask (FALSE, FALSE, FALSE, FALSE) );
  GE( ctx, glDepthMask (FALSE) );

  if (merge)
    {
      GE( ctx, glStencilFunc (GL_ALWAYS, 0x1, 0x3) );
      GE( ctx, glStencilOp (GL_KEEP, GL_KEEP, GL_INCR) );
    }
  else
    {
      GE( ctx, glEnable (GL_STENCIL_TEST) );
      GE( ctx, glStencilMask (0x1) );

      /* Initially disallow everything */
      GE( ctx, glClearStencil (0) );
      GE( ctx, glClear (GL_STENCIL_BUFFER_BIT) );

      /* Punch out holes to allow the rectangles */
      GE( ctx, glStencilFunc (GL_ALWAYS, 0x1, 0x1) );
      GE( ctx, glStencilOp (GL_KEEP, GL_KEEP, GL_REPLACE) );
    }

  vertices = g_alloca (sizeof (CoglVertexP2) * num_rectangles * 6);

  for (i = 0; i < num_rectangles; i++)
    {
      cairo_rectangle_int_t rect;
      float x1, y1, z1, w1;
      float x2, y2, z2, w2;
      CoglVertexP2 *v = vertices + i * 6;

      cairo_region_get_rectangle (region, i, &rect);

      x1 = rect.x;
      y1 = rect.y;
      z1 = 0.f;
      w1 = 1.f;

      x2 = rect.x + rect.width;
      y2 = rect.y + rect.height;
      z2 = 0.f;
      w2 = 1.f;

      cogl_matrix_transform_point (&matrix, &x1, &y1, &z1, &w1);
      cogl_matrix_transform_point (&matrix, &x2, &y2, &z2, &w2);

      v[0].x = x1;
      v[0].y = y1;
      v[1].x = x1;
      v[1].y = y2;
      v[2].x = x2;
      v[2].y = y1;
      v[3].x = x1;
      v[3].y = y2;
      v[4].x = x2;
      v[4].y = y2;
      v[5].x = x2;
      v[5].y = y1;
    }

  cogl_2d_primitives_immediate (framebuffer,
                                ctx->stencil_pipeline,
                                COGL_VERTICES_MODE_TRIANGLES,
                                vertices,
                                6 * num_rectangles);

  if (merge)
    {
      /* Subtract one from all pixels in the stencil buffer so that
       * only pixels where both the original stencil buffer and the
       * region are set will be valid
       */
      GE( ctx, glStencilOp (GL_KEEP, GL_KEEP, GL_DECR) );
      _cogl_rectangle_immediate (framebuffer,
                                 ctx->stencil_pipeline,
                                 -1.0, -1.0, 1.0, 1.0);
    }

  ctx->current_projection_entry = old_projection_entry;
  ctx->current_modelview_entry = old_modelview_entry;

  /* Restore the stencil mode */
  GE (ctx, glDepthMask (TRUE));
  GE (ctx, glColorMask (TRUE, TRUE, TRUE, TRUE));
  GE( ctx, glStencilFunc (GL_EQUAL, 0x1, 0x1) );
  GE( ctx, glStencilOp (GL_KEEP, GL_KEEP, GL_KEEP) );
}

typedef void (*SilhouettePaintCallback) (CoglFramebuffer *framebuffer,
                                         CoglPipeline *pipeline,
                                         void *user_data);

static void
add_stencil_clip_silhouette (CoglFramebuffer *framebuffer,
                             SilhouettePaintCallback silhouette_callback,
                             CoglMatrixEntry *modelview_entry,
                             float bounds_x1,
                             float bounds_y1,
                             float bounds_x2,
                             float bounds_y2,
                             gboolean merge,
                             gboolean need_clear,
                             void *user_data)
{
  CoglMatrixStack *projection_stack =
    _cogl_framebuffer_get_projection_stack (framebuffer);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  CoglMatrixEntry *old_projection_entry, *old_modelview_entry;

  /* NB: This can be called while flushing the journal so we need
   * to be very conservative with what state we change.
   */
  old_projection_entry = g_steal_pointer (&ctx->current_projection_entry);
  old_modelview_entry = g_steal_pointer (&ctx->current_modelview_entry);

  ctx->current_projection_entry = projection_stack->last_entry;
  ctx->current_modelview_entry = modelview_entry;

  _cogl_pipeline_flush_gl_state (ctx, ctx->stencil_pipeline,
                                 framebuffer, FALSE, FALSE);

  GE( ctx, glEnable (GL_STENCIL_TEST) );

  GE( ctx, glColorMask (FALSE, FALSE, FALSE, FALSE) );
  GE( ctx, glDepthMask (FALSE) );

  if (merge)
    {
      GE (ctx, glStencilMask (2));
      GE (ctx, glStencilFunc (GL_LEQUAL, 0x2, 0x6));
    }
  else
    {
      /* If we're not using the stencil buffer for clipping then we
         don't need to clear the whole stencil buffer, just the area
         that will be drawn */
      if (need_clear)
        /* If this is being called from the clip stack code then it
           will have set up a scissor for the minimum bounding box of
           all of the clips. That box will likely mean that this
           _cogl_clear won't need to clear the entire
           buffer. _cogl_framebuffer_clear_without_flush4f is used instead
           of cogl_clear because it won't try to flush the journal */
        _cogl_framebuffer_clear_without_flush4f (framebuffer,
                                                 COGL_BUFFER_BIT_STENCIL,
                                                 0, 0, 0, 0);
      else
        {
          /* Just clear the bounding box */
          GE( ctx, glStencilMask (~(GLuint) 0) );
          GE( ctx, glStencilOp (GL_ZERO, GL_ZERO, GL_ZERO) );
          _cogl_rectangle_immediate (framebuffer,
                                     ctx->stencil_pipeline,
                                     bounds_x1, bounds_y1,
                                     bounds_x2, bounds_y2);
        }
      GE (ctx, glStencilMask (1));
      GE (ctx, glStencilFunc (GL_LEQUAL, 0x1, 0x3));
    }

  GE (ctx, glStencilOp (GL_INVERT, GL_INVERT, GL_INVERT));

  silhouette_callback (framebuffer, ctx->stencil_pipeline, user_data);

  if (merge)
    {
      /* Now we have the new stencil buffer in bit 1 and the old
         stencil buffer in bit 0 so we need to intersect them */
      GE (ctx, glStencilMask (3));
      GE (ctx, glStencilFunc (GL_NEVER, 0x2, 0x3));
      GE (ctx, glStencilOp (GL_DECR, GL_DECR, GL_DECR));
      /* Decrement all of the bits twice so that only pixels where the
         value is 3 will remain */

      ctx->current_projection_entry = &ctx->identity_entry;
      ctx->current_modelview_entry = &ctx->identity_entry;

      _cogl_rectangle_immediate (framebuffer, ctx->stencil_pipeline,
                                 -1.0, -1.0, 1.0, 1.0);
      _cogl_rectangle_immediate (framebuffer, ctx->stencil_pipeline,
                                 -1.0, -1.0, 1.0, 1.0);
    }

  ctx->current_projection_entry = old_projection_entry;
  ctx->current_modelview_entry = old_modelview_entry;

  GE (ctx, glStencilMask (~(GLuint) 0));
  GE (ctx, glDepthMask (TRUE));
  GE (ctx, glColorMask (TRUE, TRUE, TRUE, TRUE));

  GE (ctx, glStencilFunc (GL_EQUAL, 0x1, 0x1));
  GE (ctx, glStencilOp (GL_KEEP, GL_KEEP, GL_KEEP));
}

static void
paint_primitive_silhouette (CoglFramebuffer *framebuffer,
                            CoglPipeline *pipeline,
                            void *user_data)
{
  _cogl_primitive_draw (user_data,
                        framebuffer,
                        pipeline,
                        COGL_DRAW_SKIP_JOURNAL_FLUSH |
                        COGL_DRAW_SKIP_PIPELINE_VALIDATION |
                        COGL_DRAW_SKIP_FRAMEBUFFER_FLUSH);
}

static void
add_stencil_clip_primitive (CoglFramebuffer *framebuffer,
                            CoglMatrixEntry *modelview_entry,
                            CoglPrimitive *primitive,
                            float bounds_x1,
                            float bounds_y1,
                            float bounds_x2,
                            float bounds_y2,
                            gboolean merge,
                            gboolean need_clear)
{
  add_stencil_clip_silhouette (framebuffer,
                               paint_primitive_silhouette,
                               modelview_entry,
                               bounds_x1,
                               bounds_y1,
                               bounds_x2,
                               bounds_y2,
                               merge,
                               need_clear,
                               primitive);
}

void
_cogl_clip_stack_gl_flush (CoglClipStack *stack,
                           CoglFramebuffer *framebuffer)
{
  CoglContext *ctx = framebuffer->context;
  gboolean using_stencil_buffer = FALSE;
  int scissor_x0;
  int scissor_y0;
  int scissor_x1;
  int scissor_y1;
  CoglClipStack *entry;
  int scissor_y_start;

  /* If we have already flushed this state then we don't need to do
     anything */
  if (ctx->current_clip_stack_valid)
    {
      if (ctx->current_clip_stack == stack)
        return;

      _cogl_clip_stack_unref (ctx->current_clip_stack);
    }

  ctx->current_clip_stack_valid = TRUE;
  ctx->current_clip_stack = _cogl_clip_stack_ref (stack);

  GE( ctx, glDisable (GL_STENCIL_TEST) );

  /* If the stack is empty then there's nothing else to do
   */
  if (stack == NULL)
    {
      COGL_NOTE (CLIPPING, "Flushed empty clip stack");

      GE (ctx, glDisable (GL_SCISSOR_TEST));
      return;
    }

  /* Calculate the scissor rect first so that if we eventually have to
     clear the stencil buffer then the clear will be clipped to the
     intersection of all of the bounding boxes. This saves having to
     clear the whole stencil buffer */
  _cogl_clip_stack_get_bounds (stack,
                               &scissor_x0, &scissor_y0,
                               &scissor_x1, &scissor_y1);

  /* Enable scissoring as soon as possible */
  if (scissor_x0 >= scissor_x1 || scissor_y0 >= scissor_y1)
    scissor_x0 = scissor_y0 = scissor_x1 = scissor_y1 = scissor_y_start = 0;
  else
    {
      /* We store the entry coordinates in Cogl coordinate space
       * but OpenGL requires the window origin to be the bottom
       * left so we may need to convert the incoming coordinates.
       *
       * NB: Cogl forces all offscreen rendering to be done upside
       * down so in this case no conversion is needed.
       */

      if (cogl_is_offscreen (framebuffer))
        scissor_y_start = scissor_y0;
      else
        {
          int framebuffer_height =
            cogl_framebuffer_get_height (framebuffer);

          scissor_y_start = framebuffer_height - scissor_y1;
        }
    }

  COGL_NOTE (CLIPPING, "Flushing scissor to (%i, %i, %i, %i)",
             scissor_x0, scissor_y0,
             scissor_x1, scissor_y1);

  GE (ctx, glEnable (GL_SCISSOR_TEST));
  GE (ctx, glScissor (scissor_x0, scissor_y_start,
                      scissor_x1 - scissor_x0,
                      scissor_y1 - scissor_y0));

  /* Add all of the entries. This will end up adding them in the
     reverse order that they were specified but as all of the clips
     are intersecting it should work out the same regardless of the
     order */
  for (entry = stack; entry; entry = entry->parent)
    {
      switch (entry->type)
        {
        case COGL_CLIP_STACK_PRIMITIVE:
            {
              CoglClipStackPrimitive *primitive_entry =
                (CoglClipStackPrimitive *) entry;

              COGL_NOTE (CLIPPING, "Adding stencil clip for primitive");

              add_stencil_clip_primitive (framebuffer,
                                          primitive_entry->matrix_entry,
                                          primitive_entry->primitive,
                                          primitive_entry->bounds_x1,
                                          primitive_entry->bounds_y1,
                                          primitive_entry->bounds_x2,
                                          primitive_entry->bounds_y2,
                                          using_stencil_buffer,
                                          TRUE);

              using_stencil_buffer = TRUE;
              break;
            }
        case COGL_CLIP_STACK_RECT:
            {
              CoglClipStackRect *rect = (CoglClipStackRect *) entry;

              /* We don't need to do anything extra if the clip for this
                 rectangle was entirely described by its scissor bounds */
              if (!rect->can_be_scissor)
                {
                  COGL_NOTE (CLIPPING, "Adding stencil clip for rectangle");

                  add_stencil_clip_rectangle (framebuffer,
                                              rect->matrix_entry,
                                              rect->x0,
                                              rect->y0,
                                              rect->x1,
                                              rect->y1,
                                              using_stencil_buffer);
                  using_stencil_buffer = TRUE;
                }
              break;
            }
        case COGL_CLIP_STACK_REGION:
            {
              CoglClipStackRegion *region = (CoglClipStackRegion *) entry;

              /* If nrectangles <= 1, it can be fully represented with the
               * scissor clip.
               */
              if (cairo_region_num_rectangles (region->region) > 1)
                {
                  COGL_NOTE (CLIPPING, "Adding stencil clip for region");

                  add_stencil_clip_region (framebuffer, region->region,
                                           using_stencil_buffer);
                  using_stencil_buffer = TRUE;
                }
              break;
            }
        case COGL_CLIP_STACK_WINDOW_RECT:
          break;
          /* We don't need to do anything for window space rectangles because
           * their functionality is entirely implemented by the entry bounding
           * box */
        }
    }
}
