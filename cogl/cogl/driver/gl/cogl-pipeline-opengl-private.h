/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2010 Intel Corporation.
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
 *   Robert Bragg <robert@linux.intel.com>
 */

#ifndef __COGL_PIPELINE_OPENGL_PRIVATE_H
#define __COGL_PIPELINE_OPENGL_PRIVATE_H

#include "cogl-pipeline-private.h"
#include "cogl-matrix-stack.h"

/*
 * cogl-pipeline.c owns the GPU's texture unit state so we have some
 * private structures for describing the current state of a texture
 * unit that we track in a per context array (ctx->texture_units) that
 * grows according to the largest texture unit used so far...
 *
 * Roughly speaking the members in this structure are of two kinds:
 * either they are a low level reflection of the state we send to
 * OpenGL or they are for high level meta data assoicated with the
 * texture unit when flushing CoglPipelineLayers that is typically
 * used to optimize subsequent re-flushing of the same layer.
 *
 * The low level members are at the top, and the high level members
 * start with the .layer member.
 */
typedef struct _CoglTextureUnit
{
  /* The base 0 texture unit index which can be used with
   * glActiveTexture () */
  int                index;

  /* The GL target currently glEnabled or 0 if nothing is
   * enabled. This is only used by the fixed pipeline fragend */
  GLenum             enabled_gl_target;

  /* The raw GL texture object name for which we called glBindTexture when
   * we flushed the last layer. (NB: The CoglTexture associated
   * with a layer may represent more than one GL texture) */
  GLuint             gl_texture;
  /* The target of the GL texture object. This is just used so that we
   * can quickly determine the intended target to flush when
   * dirty_gl_texture == TRUE */
  GLenum             gl_target;

  /* We have many components in Cogl that need to temporarily bind arbitrary
   * textures e.g. to query texture object parameters and since we don't
   * want that to result in too much redundant reflushing of layer state
   * when all that's needed is to re-bind the layer's gl_texture we use this
   * to track when the unit->gl_texture state is out of sync with the GL
   * texture object really bound too (GL_TEXTURE0+unit->index).
   *
   * XXX: as a further optimization cogl-pipeline.c uses a convention
   * of always using texture unit 1 for these transient bindings so we
   * can assume this is only ever TRUE for unit 1.
   */
  gboolean           dirty_gl_texture;

  /* A matrix stack giving us the means to associate a texture
   * transform matrix with the texture unit. */
  CoglMatrixStack   *matrix_stack;

  /*
   * Higher level layer state associated with the unit...
   */

  /* The CoglPipelineLayer whos state was flushed to update this
   * texture unit last.
   *
   * This will be set to NULL if the layer is modified or freed which
   * means when we come to flush a layer; if this pointer is still
   * valid and == to the layer being flushed we don't need to update
   * any texture unit state. */
  CoglPipelineLayer *layer;

  /* To help minimize the state changes required we track the
   * difference flags associated with the layer whos state was last
   * flushed to update this texture unit.
   *
   * Note: we track this explicitly because .layer may get invalidated
   * if that layer is modified or deleted. Even if the layer is
   * invalidated though these flags can be used to optimize the state
   * flush of the next layer
   */
  unsigned long      layer_changes_since_flush;

  /* Whenever a CoglTexture's internal GL texture storage changes
   * cogl-pipeline.c is notified with a call to
   * _cogl_pipeline_texture_storage_change_notify which inturn sets
   * this to TRUE for each texture unit that it is currently bound
   * too. When we later come to flush some pipeline state then we will
   * always check this to potentially force an update of the texture
   * state even if the pipeline hasn't changed. */
  gboolean           texture_storage_changed;

} CoglTextureUnit;

CoglTextureUnit *
_cogl_get_texture_unit (int index_);

void
_cogl_destroy_texture_units (CoglContext *ctx);

void
_cogl_set_active_texture_unit (int unit_index);

void
_cogl_bind_gl_texture_transient (GLenum gl_target,
                                 GLuint gl_texture);

void
_cogl_delete_gl_texture (GLuint gl_texture);

void
_cogl_pipeline_flush_gl_state (CoglContext *context,
                               CoglPipeline *pipeline,
                               CoglFramebuffer *framebuffer,
                               gboolean skip_gl_state,
                               gboolean unknown_color_alpha);

#endif /* __COGL_PIPELINE_OPENGL_PRIVATE_H */

