/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2008,2009,2010 Intel Corporation.
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

#include "cogl-config.h"

#include "cogl-context-private.h"
#include "cogl-color-private.h"
#include "cogl-blend-string.h"
#include "cogl-util.h"
#include "cogl-depth-state-private.h"
#include "cogl-pipeline-state-private.h"
#include "cogl-snippet-private.h"

#include <test-fixtures/test-unit.h>

#include "string.h"

#ifndef GL_FUNC_ADD
#define GL_FUNC_ADD 0x8006
#endif

CoglPipeline *
_cogl_pipeline_get_user_program (CoglPipeline *pipeline)
{
  CoglPipeline *authority;

  g_return_val_if_fail (cogl_is_pipeline (pipeline), NULL);

  authority =
    _cogl_pipeline_get_authority (pipeline, COGL_PIPELINE_STATE_USER_SHADER);

  return authority->big_state->user_program;
}

gboolean
_cogl_pipeline_color_equal (CoglPipeline *authority0,
                            CoglPipeline *authority1)
{
  return cogl_color_equal (&authority0->color, &authority1->color);
}

gboolean
_cogl_pipeline_alpha_func_state_equal (CoglPipeline *authority0,
                                       CoglPipeline *authority1)
{
  CoglPipelineAlphaFuncState *alpha_state0 =
    &authority0->big_state->alpha_state;
  CoglPipelineAlphaFuncState *alpha_state1 =
    &authority1->big_state->alpha_state;

  return alpha_state0->alpha_func == alpha_state1->alpha_func;
}

gboolean
_cogl_pipeline_alpha_func_reference_state_equal (CoglPipeline *authority0,
                                                 CoglPipeline *authority1)
{
  CoglPipelineAlphaFuncState *alpha_state0 =
    &authority0->big_state->alpha_state;
  CoglPipelineAlphaFuncState *alpha_state1 =
    &authority1->big_state->alpha_state;

  return (alpha_state0->alpha_func_reference ==
          alpha_state1->alpha_func_reference);
}

gboolean
_cogl_pipeline_blend_state_equal (CoglPipeline *authority0,
                                  CoglPipeline *authority1)
{
  CoglPipelineBlendState *blend_state0 = &authority0->big_state->blend_state;
  CoglPipelineBlendState *blend_state1 = &authority1->big_state->blend_state;

  _COGL_GET_CONTEXT (ctx, FALSE);

  if (blend_state0->blend_equation_rgb != blend_state1->blend_equation_rgb)
    return FALSE;

  if (blend_state0->blend_equation_alpha !=
      blend_state1->blend_equation_alpha)
    return FALSE;
  if (blend_state0->blend_src_factor_alpha !=
      blend_state1->blend_src_factor_alpha)
    return FALSE;
  if (blend_state0->blend_dst_factor_alpha !=
      blend_state1->blend_dst_factor_alpha)
    return FALSE;

  if (blend_state0->blend_src_factor_rgb !=
      blend_state1->blend_src_factor_rgb)
    return FALSE;
  if (blend_state0->blend_dst_factor_rgb !=
      blend_state1->blend_dst_factor_rgb)
    return FALSE;

  if (blend_state0->blend_src_factor_rgb == GL_ONE_MINUS_CONSTANT_COLOR ||
      blend_state0->blend_src_factor_rgb == GL_CONSTANT_COLOR ||
      blend_state0->blend_dst_factor_rgb == GL_ONE_MINUS_CONSTANT_COLOR ||
      blend_state0->blend_dst_factor_rgb == GL_CONSTANT_COLOR)
    {
      if (!cogl_color_equal (&blend_state0->blend_constant,
                             &blend_state1->blend_constant))
        return FALSE;
    }

  return TRUE;
}

gboolean
_cogl_pipeline_depth_state_equal (CoglPipeline *authority0,
                                  CoglPipeline *authority1)
{
  if (authority0->big_state->depth_state.test_enabled == FALSE &&
      authority1->big_state->depth_state.test_enabled == FALSE)
    return TRUE;
  else
    {
      CoglDepthState *s0 = &authority0->big_state->depth_state;
      CoglDepthState *s1 = &authority1->big_state->depth_state;
      return s0->test_enabled == s1->test_enabled &&
             s0->test_function == s1->test_function &&
             s0->write_enabled == s1->write_enabled &&
             s0->range_near == s1->range_near &&
             s0->range_far == s1->range_far;
    }
}

gboolean
_cogl_pipeline_non_zero_point_size_equal (CoglPipeline *authority0,
                                          CoglPipeline *authority1)
{
  return (authority0->big_state->non_zero_point_size ==
          authority1->big_state->non_zero_point_size);
}

gboolean
_cogl_pipeline_point_size_equal (CoglPipeline *authority0,
                                 CoglPipeline *authority1)
{
  return authority0->big_state->point_size == authority1->big_state->point_size;
}

gboolean
_cogl_pipeline_per_vertex_point_size_equal (CoglPipeline *authority0,
                                            CoglPipeline *authority1)
{
  return (authority0->big_state->per_vertex_point_size ==
          authority1->big_state->per_vertex_point_size);
}

gboolean
_cogl_pipeline_cull_face_state_equal (CoglPipeline *authority0,
                                      CoglPipeline *authority1)
{
  CoglPipelineCullFaceState *cull_face_state0
    = &authority0->big_state->cull_face_state;
  CoglPipelineCullFaceState *cull_face_state1
    = &authority1->big_state->cull_face_state;

  /* The cull face state is considered equal if two pipelines are both
     set to no culling. If the front winding property is ever used for
     anything else or the comparison is used not just for drawing then
     this would have to change */

  if (cull_face_state0->mode == COGL_PIPELINE_CULL_FACE_MODE_NONE)
    return cull_face_state1->mode == COGL_PIPELINE_CULL_FACE_MODE_NONE;

  return (cull_face_state0->mode == cull_face_state1->mode &&
          cull_face_state0->front_winding == cull_face_state1->front_winding);
}

gboolean
_cogl_pipeline_user_shader_equal (CoglPipeline *authority0,
                                  CoglPipeline *authority1)
{
  return (authority0->big_state->user_program ==
          authority1->big_state->user_program);
}

typedef struct
{
  const CoglBoxedValue **dst_values;
  const CoglBoxedValue *src_values;
  int override_count;
} GetUniformsClosure;

static gboolean
get_uniforms_cb (int uniform_num, void *user_data)
{
  GetUniformsClosure *data = user_data;

  if (data->dst_values[uniform_num] == NULL)
    data->dst_values[uniform_num] = data->src_values + data->override_count;

  data->override_count++;

  return TRUE;
}

static void
_cogl_pipeline_get_all_uniform_values (CoglPipeline *pipeline,
                                       const CoglBoxedValue **values)
{
  GetUniformsClosure data;

  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  memset (values, 0,
          sizeof (const CoglBoxedValue *) * ctx->n_uniform_names);

  data.dst_values = values;

  do
    {
      if ((pipeline->differences & COGL_PIPELINE_STATE_UNIFORMS))
        {
          const CoglPipelineUniformsState *uniforms_state =
            &pipeline->big_state->uniforms_state;

          data.override_count = 0;
          data.src_values = uniforms_state->override_values;

          _cogl_bitmask_foreach (&uniforms_state->override_mask,
                                 get_uniforms_cb,
                                 &data);
        }
      pipeline = _cogl_pipeline_get_parent (pipeline);
    }
  while (pipeline);
}

gboolean
_cogl_pipeline_uniforms_state_equal (CoglPipeline *authority0,
                                     CoglPipeline *authority1)
{
  unsigned long *differences;
  const CoglBoxedValue **values0, **values1;
  int n_longs;
  int i;

  _COGL_GET_CONTEXT (ctx, FALSE);

  if (authority0 == authority1)
    return TRUE;

  values0 = g_alloca (sizeof (const CoglBoxedValue *) * ctx->n_uniform_names);
  values1 = g_alloca (sizeof (const CoglBoxedValue *) * ctx->n_uniform_names);

  n_longs = COGL_FLAGS_N_LONGS_FOR_SIZE (ctx->n_uniform_names);
  differences = g_alloca (n_longs * sizeof (unsigned long));
  memset (differences, 0, sizeof (unsigned long) * n_longs);
  _cogl_pipeline_compare_uniform_differences (differences,
                                              authority0,
                                              authority1);

  _cogl_pipeline_get_all_uniform_values (authority0, values0);
  _cogl_pipeline_get_all_uniform_values (authority1, values1);

  COGL_FLAGS_FOREACH_START (differences, n_longs, i)
    {
      const CoglBoxedValue *value0 = values0[i];
      const CoglBoxedValue *value1 = values1[i];

      if (value0 == NULL)
        {
          if (value1 != NULL && value1->type != COGL_BOXED_NONE)
            return FALSE;
        }
      else if (value1 == NULL)
        {
          if (value0 != NULL && value0->type != COGL_BOXED_NONE)
            return FALSE;
        }
      else if (!_cogl_boxed_value_equal (value0, value1))
        return FALSE;
    }
  COGL_FLAGS_FOREACH_END;

  return TRUE;
}

gboolean
_cogl_pipeline_vertex_snippets_state_equal (CoglPipeline *authority0,
                                            CoglPipeline *authority1)
{
  return _cogl_pipeline_snippet_list_equal (&authority0->big_state->
                                            vertex_snippets,
                                            &authority1->big_state->
                                            vertex_snippets);
}

gboolean
_cogl_pipeline_fragment_snippets_state_equal (CoglPipeline *authority0,
                                              CoglPipeline *authority1)
{
  return _cogl_pipeline_snippet_list_equal (&authority0->big_state->
                                            fragment_snippets,
                                            &authority1->big_state->
                                            fragment_snippets);
}

void
cogl_pipeline_get_color (CoglPipeline *pipeline,
                         CoglColor    *color)
{
  CoglPipeline *authority;

  g_return_if_fail (cogl_is_pipeline (pipeline));

  authority =
    _cogl_pipeline_get_authority (pipeline, COGL_PIPELINE_STATE_COLOR);

  *color = authority->color;
}

/* This is used heavily by the cogl journal when logging quads */
void
_cogl_pipeline_get_colorubv (CoglPipeline *pipeline,
                             uint8_t *color)
{
  CoglPipeline *authority =
    _cogl_pipeline_get_authority (pipeline, COGL_PIPELINE_STATE_COLOR);

  _cogl_color_get_rgba_4ubv (&authority->color, color);
}

void
cogl_pipeline_set_color (CoglPipeline    *pipeline,
			 const CoglColor *color)
{
  CoglPipelineState state = COGL_PIPELINE_STATE_COLOR;
  CoglPipeline *authority;

  g_return_if_fail (cogl_is_pipeline (pipeline));

  authority = _cogl_pipeline_get_authority (pipeline, state);

  if (cogl_color_equal (color, &authority->color))
    return;

  /* - Flush journal primitives referencing the current state.
   * - Make sure the pipeline has no dependants so it may be modified.
   * - If the pipeline isn't currently an authority for the state being
   *   changed, then initialize that state from the current authority.
   */
  _cogl_pipeline_pre_change_notify (pipeline, state, color, FALSE);

  pipeline->color = *color;

  _cogl_pipeline_update_authority (pipeline, authority, state,
                                   _cogl_pipeline_color_equal);

  pipeline->dirty_real_blend_enable = TRUE;
}

void
cogl_pipeline_set_color4ub (CoglPipeline *pipeline,
			    uint8_t red,
                            uint8_t green,
                            uint8_t blue,
                            uint8_t alpha)
{
  CoglColor color;
  cogl_color_init_from_4ub (&color, red, green, blue, alpha);
  cogl_pipeline_set_color (pipeline, &color);
}

void
cogl_pipeline_set_color4f (CoglPipeline *pipeline,
			   float red,
                           float green,
                           float blue,
                           float alpha)
{
  CoglColor color;
  cogl_color_init_from_4f (&color, red, green, blue, alpha);
  cogl_pipeline_set_color (pipeline, &color);
}

static void
_cogl_pipeline_set_alpha_test_function (CoglPipeline *pipeline,
                                        CoglPipelineAlphaFunc alpha_func)
{
  CoglPipelineState state = COGL_PIPELINE_STATE_ALPHA_FUNC;
  CoglPipeline *authority;
  CoglPipelineAlphaFuncState *alpha_state;

  g_return_if_fail (cogl_is_pipeline (pipeline));

  authority = _cogl_pipeline_get_authority (pipeline, state);

  alpha_state = &authority->big_state->alpha_state;
  if (alpha_state->alpha_func == alpha_func)
    return;

  /* - Flush journal primitives referencing the current state.
   * - Make sure the pipeline has no dependants so it may be modified.
   * - If the pipeline isn't currently an authority for the state being
   *   changed, then initialize that state from the current authority.
   */
  _cogl_pipeline_pre_change_notify (pipeline, state, NULL, FALSE);

  alpha_state = &pipeline->big_state->alpha_state;
  alpha_state->alpha_func = alpha_func;

  _cogl_pipeline_update_authority (pipeline, authority, state,
                                   _cogl_pipeline_alpha_func_state_equal);
}

static void
_cogl_pipeline_set_alpha_test_function_reference (CoglPipeline *pipeline,
                                                  float alpha_reference)
{
  CoglPipelineState state = COGL_PIPELINE_STATE_ALPHA_FUNC_REFERENCE;
  CoglPipeline *authority;
  CoglPipelineAlphaFuncState *alpha_state;

  g_return_if_fail (cogl_is_pipeline (pipeline));

  authority = _cogl_pipeline_get_authority (pipeline, state);

  alpha_state = &authority->big_state->alpha_state;
  if (alpha_state->alpha_func_reference == alpha_reference)
    return;

  /* - Flush journal primitives referencing the current state.
   * - Make sure the pipeline has no dependants so it may be modified.
   * - If the pipeline isn't currently an authority for the state being
   *   changed, then initialize that state from the current authority.
   */
  _cogl_pipeline_pre_change_notify (pipeline, state, NULL, FALSE);

  alpha_state = &pipeline->big_state->alpha_state;
  alpha_state->alpha_func_reference = alpha_reference;

  _cogl_pipeline_update_authority
    (pipeline, authority, state,
     _cogl_pipeline_alpha_func_reference_state_equal);
}

void
cogl_pipeline_set_alpha_test_function (CoglPipeline *pipeline,
				       CoglPipelineAlphaFunc alpha_func,
				       float alpha_reference)
{
  _cogl_pipeline_set_alpha_test_function (pipeline, alpha_func);
  _cogl_pipeline_set_alpha_test_function_reference (pipeline, alpha_reference);
}

CoglPipelineAlphaFunc
cogl_pipeline_get_alpha_test_function (CoglPipeline *pipeline)
{
  CoglPipeline *authority;

  g_return_val_if_fail (cogl_is_pipeline (pipeline), 0);

  authority =
    _cogl_pipeline_get_authority (pipeline, COGL_PIPELINE_STATE_ALPHA_FUNC);

  return authority->big_state->alpha_state.alpha_func;
}

float
cogl_pipeline_get_alpha_test_reference (CoglPipeline *pipeline)
{
  CoglPipeline *authority;

  g_return_val_if_fail (cogl_is_pipeline (pipeline), 0.0f);

  authority =
    _cogl_pipeline_get_authority (pipeline,
                                  COGL_PIPELINE_STATE_ALPHA_FUNC_REFERENCE);

  return authority->big_state->alpha_state.alpha_func_reference;
}

static GLenum
arg_to_gl_blend_factor (CoglBlendStringArgument *arg)
{
  if (arg->source.is_zero)
    return GL_ZERO;
  if (arg->factor.is_one)
    return GL_ONE;
  else if (arg->factor.is_src_alpha_saturate)
    return GL_SRC_ALPHA_SATURATE;
  else if (arg->factor.source.info->type ==
           COGL_BLEND_STRING_COLOR_SOURCE_SRC_COLOR)
    {
      if (arg->factor.source.mask != COGL_BLEND_STRING_CHANNEL_MASK_ALPHA)
        {
          if (arg->factor.source.one_minus)
            return GL_ONE_MINUS_SRC_COLOR;
          else
            return GL_SRC_COLOR;
        }
      else
        {
          if (arg->factor.source.one_minus)
            return GL_ONE_MINUS_SRC_ALPHA;
          else
            return GL_SRC_ALPHA;
        }
    }
  else if (arg->factor.source.info->type ==
           COGL_BLEND_STRING_COLOR_SOURCE_DST_COLOR)
    {
      if (arg->factor.source.mask != COGL_BLEND_STRING_CHANNEL_MASK_ALPHA)
        {
          if (arg->factor.source.one_minus)
            return GL_ONE_MINUS_DST_COLOR;
          else
            return GL_DST_COLOR;
        }
      else
        {
          if (arg->factor.source.one_minus)
            return GL_ONE_MINUS_DST_ALPHA;
          else
            return GL_DST_ALPHA;
        }
    }
#if defined(HAVE_COGL_GLES2) || defined(HAVE_COGL_GL)
  else if (arg->factor.source.info->type ==
           COGL_BLEND_STRING_COLOR_SOURCE_CONSTANT)
    {
      if (arg->factor.source.mask != COGL_BLEND_STRING_CHANNEL_MASK_ALPHA)
        {
          if (arg->factor.source.one_minus)
            return GL_ONE_MINUS_CONSTANT_COLOR;
          else
            return GL_CONSTANT_COLOR;
        }
      else
        {
          if (arg->factor.source.one_minus)
            return GL_ONE_MINUS_CONSTANT_ALPHA;
          else
            return GL_CONSTANT_ALPHA;
        }
    }
#endif

  g_warning ("Unable to determine valid blend factor from blend string\n");
  return GL_ONE;
}

static void
setup_blend_state (CoglBlendStringStatement *statement,
                   GLenum *blend_equation,
                   GLint *blend_src_factor,
                   GLint *blend_dst_factor)
{
  switch (statement->function->type)
    {
    case COGL_BLEND_STRING_FUNCTION_ADD:
      *blend_equation = GL_FUNC_ADD;
      break;
    /* TODO - add more */
    default:
      g_warning ("Unsupported blend function given");
      *blend_equation = GL_FUNC_ADD;
    }

  *blend_src_factor = arg_to_gl_blend_factor (&statement->args[0]);
  *blend_dst_factor = arg_to_gl_blend_factor (&statement->args[1]);
}

gboolean
cogl_pipeline_set_blend (CoglPipeline *pipeline,
                         const char *blend_description,
                         GError **error)
{
  CoglPipelineState state = COGL_PIPELINE_STATE_BLEND;
  CoglPipeline *authority;
  CoglBlendStringStatement statements[2];
  CoglBlendStringStatement *rgb;
  CoglBlendStringStatement *a;
  int count;
  CoglPipelineBlendState *blend_state;

  _COGL_GET_CONTEXT (ctx, FALSE);

  g_return_val_if_fail (cogl_is_pipeline (pipeline), FALSE);

  count =
    _cogl_blend_string_compile (blend_description,
                                COGL_BLEND_STRING_CONTEXT_BLENDING,
                                statements,
                                error);
  if (!count)
    return FALSE;

  if (count == 1)
    rgb = a = statements;
  else
    {
      rgb = &statements[0];
      a = &statements[1];
    }

  authority =
    _cogl_pipeline_get_authority (pipeline, state);

  /* - Flush journal primitives referencing the current state.
   * - Make sure the pipeline has no dependants so it may be modified.
   * - If the pipeline isn't currently an authority for the state being
   *   changed, then initialize that state from the current authority.
   */
  _cogl_pipeline_pre_change_notify (pipeline, state, NULL, FALSE);

  blend_state = &pipeline->big_state->blend_state;

  setup_blend_state (rgb,
                     &blend_state->blend_equation_rgb,
                     &blend_state->blend_src_factor_rgb,
                     &blend_state->blend_dst_factor_rgb);
  setup_blend_state (a,
                     &blend_state->blend_equation_alpha,
                     &blend_state->blend_src_factor_alpha,
                     &blend_state->blend_dst_factor_alpha);

  /* If we are the current authority see if we can revert to one of our
   * ancestors being the authority */
  if (pipeline == authority &&
      _cogl_pipeline_get_parent (authority) != NULL)
    {
      CoglPipeline *parent = _cogl_pipeline_get_parent (authority);
      CoglPipeline *old_authority =
        _cogl_pipeline_get_authority (parent, state);

      if (_cogl_pipeline_blend_state_equal (authority, old_authority))
        pipeline->differences &= ~state;
    }

  /* If we weren't previously the authority on this state then we need
   * to extended our differences mask and so it's possible that some
   * of our ancestry will now become redundant, so we aim to reparent
   * ourselves if that's true... */
  if (pipeline != authority)
    {
      pipeline->differences |= state;
      _cogl_pipeline_prune_redundant_ancestry (pipeline);
    }

  pipeline->dirty_real_blend_enable = TRUE;

  return TRUE;
}

void
cogl_pipeline_set_blend_constant (CoglPipeline *pipeline,
                                  const CoglColor *constant_color)
{
  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  g_return_if_fail (cogl_is_pipeline (pipeline));

#if defined(HAVE_COGL_GLES2) || defined(HAVE_COGL_GL)
  {
    CoglPipelineState state = COGL_PIPELINE_STATE_BLEND;
    CoglPipeline *authority;
    CoglPipelineBlendState *blend_state;

    authority = _cogl_pipeline_get_authority (pipeline, state);

    blend_state = &authority->big_state->blend_state;
    if (cogl_color_equal (constant_color, &blend_state->blend_constant))
      return;

    /* - Flush journal primitives referencing the current state.
     * - Make sure the pipeline has no dependants so it may be modified.
     * - If the pipeline isn't currently an authority for the state being
     *   changed, then initialize that state from the current authority.
     */
    _cogl_pipeline_pre_change_notify (pipeline, state, NULL, FALSE);

    blend_state = &pipeline->big_state->blend_state;
    blend_state->blend_constant = *constant_color;

    _cogl_pipeline_update_authority (pipeline, authority, state,
                                     _cogl_pipeline_blend_state_equal);

    pipeline->dirty_real_blend_enable = TRUE;
  }
#endif
}

CoglHandle
cogl_pipeline_get_user_program (CoglPipeline *pipeline)
{
  CoglPipeline *authority;

  g_return_val_if_fail (cogl_is_pipeline (pipeline), NULL);

  authority =
    _cogl_pipeline_get_authority (pipeline, COGL_PIPELINE_STATE_USER_SHADER);

  return authority->big_state->user_program;
}

/* XXX: for now we don't mind if the program has vertex shaders
 * attached but if we ever make a similar API public we should only
 * allow attaching of programs containing fragment shaders. Eventually
 * we will have a CoglPipeline abstraction to also cover vertex
 * processing.
 */
void
cogl_pipeline_set_user_program (CoglPipeline *pipeline,
                                CoglHandle program)
{
  CoglPipelineState state = COGL_PIPELINE_STATE_USER_SHADER;
  CoglPipeline *authority;

  g_return_if_fail (cogl_is_pipeline (pipeline));

  authority = _cogl_pipeline_get_authority (pipeline, state);

  if (authority->big_state->user_program == program)
    return;

  /* - Flush journal primitives referencing the current state.
   * - Make sure the pipeline has no dependants so it may be modified.
   * - If the pipeline isn't currently an authority for the state being
   *   changed, then initialize that state from the current authority.
   */
  _cogl_pipeline_pre_change_notify (pipeline, state, NULL, FALSE);

  /* If we are the current authority see if we can revert to one of our
   * ancestors being the authority */
  if (pipeline == authority &&
      _cogl_pipeline_get_parent (authority) != NULL)
    {
      CoglPipeline *parent = _cogl_pipeline_get_parent (authority);
      CoglPipeline *old_authority =
        _cogl_pipeline_get_authority (parent, state);

      if (old_authority->big_state->user_program == program)
        pipeline->differences &= ~state;
    }
  else if (pipeline != authority)
    {
      /* If we weren't previously the authority on this state then we
       * need to extended our differences mask and so it's possible
       * that some of our ancestry will now become redundant, so we
       * aim to reparent ourselves if that's true... */
      pipeline->differences |= state;
      _cogl_pipeline_prune_redundant_ancestry (pipeline);
    }

  if (program != NULL)
    cogl_object_ref (program);
  if (authority == pipeline &&
      pipeline->big_state->user_program != NULL)
    cogl_object_unref (pipeline->big_state->user_program);
  pipeline->big_state->user_program = program;

  pipeline->dirty_real_blend_enable = TRUE;
}

gboolean
cogl_pipeline_set_depth_state (CoglPipeline *pipeline,
                               const CoglDepthState *depth_state,
                               GError **error)
{
  CoglPipelineState state = COGL_PIPELINE_STATE_DEPTH;
  CoglPipeline *authority;
  CoglDepthState *orig_state;

  _COGL_GET_CONTEXT (ctx, FALSE);

  g_return_val_if_fail (cogl_is_pipeline (pipeline), FALSE);
  g_return_val_if_fail (depth_state->magic == COGL_DEPTH_STATE_MAGIC, FALSE);

  authority = _cogl_pipeline_get_authority (pipeline, state);

  orig_state = &authority->big_state->depth_state;
  if (orig_state->test_enabled == depth_state->test_enabled &&
      orig_state->write_enabled == depth_state->write_enabled &&
      orig_state->test_function == depth_state->test_function &&
      orig_state->range_near == depth_state->range_near &&
      orig_state->range_far == depth_state->range_far)
    return TRUE;

  /* - Flush journal primitives referencing the current state.
   * - Make sure the pipeline has no dependants so it may be modified.
   * - If the pipeline isn't currently an authority for the state being
   *   changed, then initialize that state from the current authority.
   */
  _cogl_pipeline_pre_change_notify (pipeline, state, NULL, FALSE);

  pipeline->big_state->depth_state = *depth_state;

  _cogl_pipeline_update_authority (pipeline, authority, state,
                                   _cogl_pipeline_depth_state_equal);

  return TRUE;
}

void
cogl_pipeline_get_depth_state (CoglPipeline *pipeline,
                               CoglDepthState *state)
{
  CoglPipeline *authority;

  g_return_if_fail (cogl_is_pipeline (pipeline));

  authority =
    _cogl_pipeline_get_authority (pipeline, COGL_PIPELINE_STATE_DEPTH);
  *state = authority->big_state->depth_state;
}

void
cogl_pipeline_set_cull_face_mode (CoglPipeline *pipeline,
                                  CoglPipelineCullFaceMode cull_face_mode)
{
  CoglPipelineState state = COGL_PIPELINE_STATE_CULL_FACE;
  CoglPipeline *authority;
  CoglPipelineCullFaceState *cull_face_state;

  g_return_if_fail (cogl_is_pipeline (pipeline));

  authority = _cogl_pipeline_get_authority (pipeline, state);

  cull_face_state = &authority->big_state->cull_face_state;

  if (cull_face_state->mode == cull_face_mode)
    return;

  /* - Flush journal primitives referencing the current state.
   * - Make sure the pipeline has no dependants so it may be modified.
   * - If the pipeline isn't currently an authority for the state being
   *   changed, then initialize that state from the current authority.
   */
  _cogl_pipeline_pre_change_notify (pipeline, state, NULL, FALSE);

  pipeline->big_state->cull_face_state.mode = cull_face_mode;

  _cogl_pipeline_update_authority (pipeline, authority, state,
                                   _cogl_pipeline_cull_face_state_equal);
}

void
cogl_pipeline_set_front_face_winding (CoglPipeline *pipeline,
                                      CoglWinding front_winding)
{
  CoglPipelineState state = COGL_PIPELINE_STATE_CULL_FACE;
  CoglPipeline *authority;
  CoglPipelineCullFaceState *cull_face_state;

  g_return_if_fail (cogl_is_pipeline (pipeline));

  authority = _cogl_pipeline_get_authority (pipeline, state);

  cull_face_state = &authority->big_state->cull_face_state;

  if (cull_face_state->front_winding == front_winding)
    return;

  /* - Flush journal primitives referencing the current state.
   * - Make sure the pipeline has no dependants so it may be modified.
   * - If the pipeline isn't currently an authority for the state being
   *   changed, then initialize that state from the current authority.
   */
  _cogl_pipeline_pre_change_notify (pipeline, state, NULL, FALSE);

  pipeline->big_state->cull_face_state.front_winding = front_winding;

  _cogl_pipeline_update_authority (pipeline, authority, state,
                                   _cogl_pipeline_cull_face_state_equal);
}

CoglPipelineCullFaceMode
cogl_pipeline_get_cull_face_mode (CoglPipeline *pipeline)
{
  CoglPipelineState state = COGL_PIPELINE_STATE_CULL_FACE;
  CoglPipeline *authority;

  g_return_val_if_fail (cogl_is_pipeline (pipeline),
                        COGL_PIPELINE_CULL_FACE_MODE_NONE);

  authority = _cogl_pipeline_get_authority (pipeline, state);

  return authority->big_state->cull_face_state.mode;
}

CoglWinding
cogl_pipeline_get_front_face_winding (CoglPipeline *pipeline)
{
  CoglPipelineState state = COGL_PIPELINE_STATE_CULL_FACE;
  CoglPipeline *authority;

  g_return_val_if_fail (cogl_is_pipeline (pipeline),
                        COGL_WINDING_CLOCKWISE);

  authority = _cogl_pipeline_get_authority (pipeline, state);

  return authority->big_state->cull_face_state.front_winding;
}

float
cogl_pipeline_get_point_size (CoglPipeline *pipeline)
{
  CoglPipeline *authority;

  g_return_val_if_fail (cogl_is_pipeline (pipeline), FALSE);

  authority =
    _cogl_pipeline_get_authority (pipeline, COGL_PIPELINE_STATE_POINT_SIZE);

  return authority->big_state->point_size;
}

static void
_cogl_pipeline_set_non_zero_point_size (CoglPipeline *pipeline,
                                        gboolean value)
{
  CoglPipelineState state = COGL_PIPELINE_STATE_NON_ZERO_POINT_SIZE;
  CoglPipeline *authority;

  g_return_if_fail (cogl_is_pipeline (pipeline));

  authority = _cogl_pipeline_get_authority (pipeline, state);

  /* - Flush journal primitives referencing the current state.
   * - Make sure the pipeline has no dependants so it may be modified.
   * - If the pipeline isn't currently an authority for the state being
   *   changed, then initialize that state from the current authority.
   */
  _cogl_pipeline_pre_change_notify (pipeline, state, NULL, FALSE);

  pipeline->big_state->non_zero_point_size = !!value;

  _cogl_pipeline_update_authority (pipeline, authority, state,
                                   _cogl_pipeline_non_zero_point_size_equal);
}

void
cogl_pipeline_set_point_size (CoglPipeline *pipeline,
                              float point_size)
{
  CoglPipelineState state = COGL_PIPELINE_STATE_POINT_SIZE;
  CoglPipeline *authority;

  g_return_if_fail (cogl_is_pipeline (pipeline));

  authority = _cogl_pipeline_get_authority (pipeline, state);

  if (authority->big_state->point_size == point_size)
    return;

  /* Changing the point size may additionally modify
   * COGL_PIPELINE_STATE_NON_ZERO_POINT_SIZE. */

  if ((authority->big_state->point_size > 0.0f) != (point_size > 0.0f))
    _cogl_pipeline_set_non_zero_point_size (pipeline, point_size > 0.0f);

  /* - Flush journal primitives referencing the current state.
   * - Make sure the pipeline has no dependants so it may be modified.
   * - If the pipeline isn't currently an authority for the state being
   *   changed, then initialize that state from the current authority.
   */
  _cogl_pipeline_pre_change_notify (pipeline, state, NULL, FALSE);

  pipeline->big_state->point_size = point_size;

  _cogl_pipeline_update_authority (pipeline, authority, state,
                                   _cogl_pipeline_point_size_equal);
}

gboolean
cogl_pipeline_set_per_vertex_point_size (CoglPipeline *pipeline,
                                         gboolean enable,
                                         GError **error)
{
  CoglPipelineState state = COGL_PIPELINE_STATE_PER_VERTEX_POINT_SIZE;
  CoglPipeline *authority;

  _COGL_GET_CONTEXT (ctx, FALSE);
  g_return_val_if_fail (cogl_is_pipeline (pipeline), FALSE);

  authority = _cogl_pipeline_get_authority (pipeline, state);

  enable = !!enable;

  if (authority->big_state->per_vertex_point_size == enable)
    return TRUE;

  /* - Flush journal primitives referencing the current state.
   * - Make sure the pipeline has no dependants so it may be modified.
   * - If the pipeline isn't currently an authority for the state being
   *   changed, then initialize that state from the current authority.
   */
  _cogl_pipeline_pre_change_notify (pipeline, state, NULL, FALSE);

  pipeline->big_state->per_vertex_point_size = enable;

  _cogl_pipeline_update_authority (pipeline, authority, state,
                                   _cogl_pipeline_point_size_equal);

  return TRUE;
}

gboolean
cogl_pipeline_get_per_vertex_point_size (CoglPipeline *pipeline)
{
  CoglPipeline *authority;

  g_return_val_if_fail (cogl_is_pipeline (pipeline), FALSE);

  authority =
    _cogl_pipeline_get_authority (pipeline,
                                  COGL_PIPELINE_STATE_PER_VERTEX_POINT_SIZE);

  return authority->big_state->per_vertex_point_size;
}

static CoglBoxedValue *
_cogl_pipeline_override_uniform (CoglPipeline *pipeline,
                                 int location)
{
  CoglPipelineState state = COGL_PIPELINE_STATE_UNIFORMS;
  CoglPipelineUniformsState *uniforms_state;
  int override_index;

  _COGL_GET_CONTEXT (ctx, NULL);

  g_return_val_if_fail (cogl_is_pipeline (pipeline), NULL);
  g_return_val_if_fail (location >= 0, NULL);
  g_return_val_if_fail (location < ctx->n_uniform_names, NULL);

  /* - Flush journal primitives referencing the current state.
   * - Make sure the pipeline has no dependants so it may be modified.
   * - If the pipeline isn't currently an authority for the state being
   *   changed, then initialize that state from the current authority.
   */
  _cogl_pipeline_pre_change_notify (pipeline, state, NULL, FALSE);

  uniforms_state = &pipeline->big_state->uniforms_state;

  /* Count the number of bits that are set below this location. That
     should give us the position where our new value should lie */
  override_index = _cogl_bitmask_popcount_upto (&uniforms_state->override_mask,
                                                location);

  _cogl_bitmask_set (&uniforms_state->changed_mask, location, TRUE);

  /* If this pipeline already has an override for this value then we
     can just use it directly */
  if (_cogl_bitmask_get (&uniforms_state->override_mask, location))
    return uniforms_state->override_values + override_index;

  /* We need to create a new override value in the right position
     within the array. This is pretty inefficient but the hope is that
     it will be much more common to modify an existing uniform rather
     than modify a new one so it is more important to optimise the
     former case. */

  if (uniforms_state->override_values == NULL)
    {
      g_assert (override_index == 0);
      uniforms_state->override_values = g_new (CoglBoxedValue, 1);
    }
  else
    {
      /* We need to grow the array and copy in the old values */
      CoglBoxedValue *old_values = uniforms_state->override_values;
      int old_size = _cogl_bitmask_popcount (&uniforms_state->override_mask);

      uniforms_state->override_values = g_new (CoglBoxedValue, old_size + 1);

      /* Copy in the old values leaving a gap for the new value */
      memcpy (uniforms_state->override_values,
              old_values,
              sizeof (CoglBoxedValue) * override_index);
      memcpy (uniforms_state->override_values + override_index + 1,
              old_values + override_index,
              sizeof (CoglBoxedValue) * (old_size - override_index));

      g_free (old_values);
    }

  _cogl_boxed_value_init (uniforms_state->override_values + override_index);

  _cogl_bitmask_set (&uniforms_state->override_mask, location, TRUE);

  return uniforms_state->override_values + override_index;
}

void
cogl_pipeline_set_uniform_1f (CoglPipeline *pipeline,
                              int uniform_location,
                              float value)
{
  CoglBoxedValue *boxed_value;

  boxed_value = _cogl_pipeline_override_uniform (pipeline, uniform_location);

  _cogl_boxed_value_set_1f (boxed_value, value);
}

void
cogl_pipeline_set_uniform_1i (CoglPipeline *pipeline,
                              int uniform_location,
                              int value)
{
  CoglBoxedValue *boxed_value;

  boxed_value = _cogl_pipeline_override_uniform (pipeline, uniform_location);

  _cogl_boxed_value_set_1i (boxed_value, value);
}

void
cogl_pipeline_set_uniform_float (CoglPipeline *pipeline,
                                 int uniform_location,
                                 int n_components,
                                 int count,
                                 const float *value)
{
  CoglBoxedValue *boxed_value;

  boxed_value = _cogl_pipeline_override_uniform (pipeline, uniform_location);

  _cogl_boxed_value_set_float (boxed_value, n_components, count, value);
}

void
cogl_pipeline_set_uniform_int (CoglPipeline *pipeline,
                               int uniform_location,
                               int n_components,
                               int count,
                               const int *value)
{
  CoglBoxedValue *boxed_value;

  boxed_value = _cogl_pipeline_override_uniform (pipeline, uniform_location);

  _cogl_boxed_value_set_int (boxed_value, n_components, count, value);
}

void
cogl_pipeline_set_uniform_matrix (CoglPipeline *pipeline,
                                  int uniform_location,
                                  int dimensions,
                                  int count,
                                  gboolean transpose,
                                  const float *value)
{
  CoglBoxedValue *boxed_value;

  boxed_value = _cogl_pipeline_override_uniform (pipeline, uniform_location);

  _cogl_boxed_value_set_matrix (boxed_value,
                                dimensions,
                                count,
                                transpose,
                                value);
}

static void
_cogl_pipeline_add_vertex_snippet (CoglPipeline *pipeline,
                                   CoglSnippet *snippet)
{
  CoglPipelineState state = COGL_PIPELINE_STATE_VERTEX_SNIPPETS;

  /* - Flush journal primitives referencing the current state.
   * - Make sure the pipeline has no dependants so it may be modified.
   * - If the pipeline isn't currently an authority for the state being
   *   changed, then initialize that state from the current authority.
   */
  _cogl_pipeline_pre_change_notify (pipeline, state, NULL, FALSE);

  _cogl_pipeline_snippet_list_add (&pipeline->big_state->vertex_snippets,
                                   snippet);
}

static void
_cogl_pipeline_add_fragment_snippet (CoglPipeline *pipeline,
                                     CoglSnippet *snippet)
{
  CoglPipelineState state = COGL_PIPELINE_STATE_FRAGMENT_SNIPPETS;

  /* - Flush journal primitives referencing the current state.
   * - Make sure the pipeline has no dependants so it may be modified.
   * - If the pipeline isn't currently an authority for the state being
   *   changed, then initialize that state from the current authority.
   */
  _cogl_pipeline_pre_change_notify (pipeline, state, NULL, FALSE);

  _cogl_pipeline_snippet_list_add (&pipeline->big_state->fragment_snippets,
                                   snippet);
}

void
cogl_pipeline_add_snippet (CoglPipeline *pipeline,
                           CoglSnippet *snippet)
{
  g_return_if_fail (cogl_is_pipeline (pipeline));
  g_return_if_fail (cogl_is_snippet (snippet));
  g_return_if_fail (snippet->hook < COGL_SNIPPET_FIRST_LAYER_HOOK);

  if (snippet->hook < COGL_SNIPPET_FIRST_PIPELINE_FRAGMENT_HOOK)
    _cogl_pipeline_add_vertex_snippet (pipeline, snippet);
  else
    _cogl_pipeline_add_fragment_snippet (pipeline, snippet);
}

gboolean
_cogl_pipeline_has_non_layer_vertex_snippets (CoglPipeline *pipeline)
{
  CoglPipeline *authority =
    _cogl_pipeline_get_authority (pipeline,
                                  COGL_PIPELINE_STATE_VERTEX_SNIPPETS);

  return authority->big_state->vertex_snippets.entries != NULL;
}

static gboolean
check_layer_has_vertex_snippet (CoglPipelineLayer *layer,
                                void *user_data)
{
  unsigned long state = COGL_PIPELINE_LAYER_STATE_VERTEX_SNIPPETS;
  CoglPipelineLayer *authority =
    _cogl_pipeline_layer_get_authority (layer, state);
  gboolean *found_vertex_snippet = user_data;

  if (authority->big_state->vertex_snippets.entries)
    {
      *found_vertex_snippet = TRUE;
      return FALSE;
    }

  return TRUE;
}

gboolean
_cogl_pipeline_has_vertex_snippets (CoglPipeline *pipeline)
{
  gboolean found_vertex_snippet = FALSE;

  if (_cogl_pipeline_has_non_layer_vertex_snippets (pipeline))
    return TRUE;

  _cogl_pipeline_foreach_layer_internal (pipeline,
                                         check_layer_has_vertex_snippet,
                                         &found_vertex_snippet);

  return found_vertex_snippet;
}

gboolean
_cogl_pipeline_has_non_layer_fragment_snippets (CoglPipeline *pipeline)
{
  CoglPipeline *authority =
    _cogl_pipeline_get_authority (pipeline,
                                  COGL_PIPELINE_STATE_FRAGMENT_SNIPPETS);

  return authority->big_state->fragment_snippets.entries != NULL;
}

static gboolean
check_layer_has_fragment_snippet (CoglPipelineLayer *layer,
                                  void *user_data)
{
  unsigned long state = COGL_PIPELINE_LAYER_STATE_FRAGMENT_SNIPPETS;
  CoglPipelineLayer *authority =
    _cogl_pipeline_layer_get_authority (layer, state);
  gboolean *found_fragment_snippet = user_data;

  if (authority->big_state->fragment_snippets.entries)
    {
      *found_fragment_snippet = TRUE;
      return FALSE;
    }

  return TRUE;
}

gboolean
_cogl_pipeline_has_fragment_snippets (CoglPipeline *pipeline)
{
  gboolean found_fragment_snippet = FALSE;

  if (_cogl_pipeline_has_non_layer_fragment_snippets (pipeline))
    return TRUE;

  _cogl_pipeline_foreach_layer_internal (pipeline,
                                         check_layer_has_fragment_snippet,
                                         &found_fragment_snippet);

  return found_fragment_snippet;
}

void
_cogl_pipeline_hash_color_state (CoglPipeline *authority,
                                 CoglPipelineHashState *state)
{
  state->hash = _cogl_util_one_at_a_time_hash (state->hash, &authority->color,
                                               _COGL_COLOR_DATA_SIZE);
}

void
_cogl_pipeline_hash_alpha_func_state (CoglPipeline *authority,
                                      CoglPipelineHashState *state)
{
  CoglPipelineAlphaFuncState *alpha_state = &authority->big_state->alpha_state;
  state->hash =
    _cogl_util_one_at_a_time_hash (state->hash, &alpha_state->alpha_func,
                                   sizeof (alpha_state->alpha_func));
}

void
_cogl_pipeline_hash_alpha_func_reference_state (CoglPipeline *authority,
                                                CoglPipelineHashState *state)
{
  CoglPipelineAlphaFuncState *alpha_state = &authority->big_state->alpha_state;
  float ref = alpha_state->alpha_func_reference;
  state->hash =
    _cogl_util_one_at_a_time_hash (state->hash, &ref, sizeof (float));
}

void
_cogl_pipeline_hash_blend_state (CoglPipeline *authority,
                                 CoglPipelineHashState *state)
{
  CoglPipelineBlendState *blend_state = &authority->big_state->blend_state;
  unsigned int hash;

  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  if (!authority->real_blend_enable)
    return;

  hash = state->hash;

  hash =
    _cogl_util_one_at_a_time_hash (hash, &blend_state->blend_equation_rgb,
                                   sizeof (blend_state->blend_equation_rgb));
  hash =
    _cogl_util_one_at_a_time_hash (hash, &blend_state->blend_equation_alpha,
                                   sizeof (blend_state->blend_equation_alpha));
  hash =
    _cogl_util_one_at_a_time_hash (hash, &blend_state->blend_src_factor_alpha,
                                   sizeof (blend_state->blend_src_factor_alpha));
  hash =
    _cogl_util_one_at_a_time_hash (hash, &blend_state->blend_dst_factor_alpha,
                                   sizeof (blend_state->blend_dst_factor_alpha));

  if (blend_state->blend_src_factor_rgb == GL_ONE_MINUS_CONSTANT_COLOR ||
      blend_state->blend_src_factor_rgb == GL_CONSTANT_COLOR ||
      blend_state->blend_dst_factor_rgb == GL_ONE_MINUS_CONSTANT_COLOR ||
      blend_state->blend_dst_factor_rgb == GL_CONSTANT_COLOR)
    {
      hash =
        _cogl_util_one_at_a_time_hash (hash, &blend_state->blend_constant,
                                       sizeof (blend_state->blend_constant));
    }

  hash =
    _cogl_util_one_at_a_time_hash (hash, &blend_state->blend_src_factor_rgb,
                                   sizeof (blend_state->blend_src_factor_rgb));
  hash =
    _cogl_util_one_at_a_time_hash (hash, &blend_state->blend_dst_factor_rgb,
                                   sizeof (blend_state->blend_dst_factor_rgb));

  state->hash = hash;
}

void
_cogl_pipeline_hash_user_shader_state (CoglPipeline *authority,
                                       CoglPipelineHashState *state)
{
  CoglHandle user_program = authority->big_state->user_program;
  state->hash = _cogl_util_one_at_a_time_hash (state->hash, &user_program,
                                               sizeof (user_program));
}

void
_cogl_pipeline_hash_depth_state (CoglPipeline *authority,
                                 CoglPipelineHashState *state)
{
  CoglDepthState *depth_state = &authority->big_state->depth_state;
  unsigned int hash = state->hash;

  if (depth_state->test_enabled)
    {
      uint8_t enabled = depth_state->test_enabled;
      CoglDepthTestFunction function = depth_state->test_function;
      hash = _cogl_util_one_at_a_time_hash (hash, &enabled, sizeof (enabled));
      hash = _cogl_util_one_at_a_time_hash (hash, &function, sizeof (function));
    }

  if (depth_state->write_enabled)
    {
      uint8_t enabled = depth_state->write_enabled;
      float near_val = depth_state->range_near;
      float far_val = depth_state->range_far;
      hash = _cogl_util_one_at_a_time_hash (hash, &enabled, sizeof (enabled));
      hash = _cogl_util_one_at_a_time_hash (hash, &near_val, sizeof (near_val));
      hash = _cogl_util_one_at_a_time_hash (hash, &far_val, sizeof (far_val));
    }

  state->hash = hash;
}

void
_cogl_pipeline_hash_non_zero_point_size_state (CoglPipeline *authority,
                                               CoglPipelineHashState *state)
{
  gboolean non_zero_point_size = authority->big_state->non_zero_point_size;

  state->hash = _cogl_util_one_at_a_time_hash (state->hash,
                                               &non_zero_point_size,
                                               sizeof (non_zero_point_size));
}

void
_cogl_pipeline_hash_point_size_state (CoglPipeline *authority,
                                      CoglPipelineHashState *state)
{
  float point_size = authority->big_state->point_size;
  state->hash = _cogl_util_one_at_a_time_hash (state->hash, &point_size,
                                               sizeof (point_size));
}

void
_cogl_pipeline_hash_per_vertex_point_size_state (CoglPipeline *authority,
                                                 CoglPipelineHashState *state)
{
  gboolean per_vertex_point_size = authority->big_state->per_vertex_point_size;
  state->hash = _cogl_util_one_at_a_time_hash (state->hash,
                                               &per_vertex_point_size,
                                               sizeof (per_vertex_point_size));
}

void
_cogl_pipeline_hash_cull_face_state (CoglPipeline *authority,
                                     CoglPipelineHashState *state)
{
  CoglPipelineCullFaceState *cull_face_state
    = &authority->big_state->cull_face_state;

  /* The cull face state is considered equal if two pipelines are both
     set to no culling. If the front winding property is ever used for
     anything else or the hashing is used not just for drawing then
     this would have to change */
  if (cull_face_state->mode == COGL_PIPELINE_CULL_FACE_MODE_NONE)
    state->hash =
      _cogl_util_one_at_a_time_hash (state->hash,
                                     &cull_face_state->mode,
                                     sizeof (CoglPipelineCullFaceMode));
  else
    state->hash =
      _cogl_util_one_at_a_time_hash (state->hash,
                                     cull_face_state,
                                     sizeof (CoglPipelineCullFaceState));
}

void
_cogl_pipeline_hash_uniforms_state (CoglPipeline *authority,
                                    CoglPipelineHashState *state)
{
  /* This isn't used anywhere yet because the uniform state doesn't
     affect program generation. It's quite a hassle to implement so
     let's just leave it until something actually needs it */
  g_warn_if_reached ();
}

void
_cogl_pipeline_compare_uniform_differences (unsigned long *differences,
                                            CoglPipeline *pipeline0,
                                            CoglPipeline *pipeline1)
{
  GSList *head0 = NULL;
  GSList *head1 = NULL;
  CoglPipeline *node0;
  CoglPipeline *node1;
  int len0 = 0;
  int len1 = 0;
  int count;
  GSList *common_ancestor0;
  GSList *common_ancestor1;

  /* This algorithm is copied from
     _cogl_pipeline_compare_differences(). It might be nice to share
     the code more */

  for (node0 = pipeline0; node0; node0 = _cogl_pipeline_get_parent (node0))
    {
      GSList *link = alloca (sizeof (GSList));
      link->next = head0;
      link->data = node0;
      head0 = link;
      len0++;
    }
  for (node1 = pipeline1; node1; node1 = _cogl_pipeline_get_parent (node1))
    {
      GSList *link = alloca (sizeof (GSList));
      link->next = head1;
      link->data = node1;
      head1 = link;
      len1++;
    }

  /* NB: There's no point looking at the head entries since we know both
   * pipelines must have the same default pipeline as their root node. */
  common_ancestor0 = head0;
  common_ancestor1 = head1;
  head0 = head0->next;
  head1 = head1->next;
  count = MIN (len0, len1) - 1;
  while (count--)
    {
      if (head0->data != head1->data)
        break;
      common_ancestor0 = head0;
      common_ancestor1 = head1;
      head0 = head0->next;
      head1 = head1->next;
    }

  for (head0 = common_ancestor0->next; head0; head0 = head0->next)
    {
      node0 = head0->data;
      if ((node0->differences & COGL_PIPELINE_STATE_UNIFORMS))
        {
          const CoglPipelineUniformsState *uniforms_state =
            &node0->big_state->uniforms_state;
          _cogl_bitmask_set_flags (&uniforms_state->override_mask,
                                   differences);
        }
    }
  for (head1 = common_ancestor1->next; head1; head1 = head1->next)
    {
      node1 = head1->data;
      if ((node1->differences & COGL_PIPELINE_STATE_UNIFORMS))
        {
          const CoglPipelineUniformsState *uniforms_state =
            &node1->big_state->uniforms_state;
          _cogl_bitmask_set_flags (&uniforms_state->override_mask,
                                   differences);
        }
    }
}

void
_cogl_pipeline_hash_vertex_snippets_state (CoglPipeline *authority,
                                           CoglPipelineHashState *state)
{
  _cogl_pipeline_snippet_list_hash (&authority->big_state->vertex_snippets,
                                    &state->hash);
}

void
_cogl_pipeline_hash_fragment_snippets_state (CoglPipeline *authority,
                                             CoglPipelineHashState *state)
{
  _cogl_pipeline_snippet_list_hash (&authority->big_state->fragment_snippets,
                                    &state->hash);
}

UNIT_TEST (check_blend_constant_ancestry,
           0 /* no requirements */,
           0 /* no known failures */)
{
  CoglPipeline *pipeline = cogl_pipeline_new (test_ctx);
  CoglNode *node;
  int pipeline_length = 0;
  int i;

  /* Repeatedly making a copy of a pipeline and changing the same
   * state (in this case the blend constant) shouldn't cause a long
   * chain of pipelines to be created because the redundant ancestry
   * should be pruned. */

  for (i = 0; i < 20; i++)
    {
      CoglColor color;
      CoglPipeline *tmp_pipeline;

      cogl_color_init_from_4f (&color, i / 20.0f, 0.0f, 0.0f, 1.0f);

      tmp_pipeline = cogl_pipeline_copy (pipeline);
      cogl_object_unref (pipeline);
      pipeline = tmp_pipeline;

      cogl_pipeline_set_blend_constant (pipeline, &color);
    }

  for (node = (CoglNode *) pipeline; node; node = node->parent)
    pipeline_length++;

  g_assert_cmpint (pipeline_length, <=, 2);

  cogl_object_unref (pipeline);
}

UNIT_TEST (check_uniform_ancestry,
           0 /* no requirements */,
           TEST_KNOWN_FAILURE)
{
  CoglPipeline *pipeline = cogl_pipeline_new (test_ctx);
  CoglNode *node;
  int pipeline_length = 0;
  int i;

  /* Repeatedly making a copy of a pipeline and changing a uniform
   * shouldn't cause a long chain of pipelines to be created */

  for (i = 0; i < 20; i++)
    {
      CoglPipeline *tmp_pipeline;
      int uniform_location;

      tmp_pipeline = cogl_pipeline_copy (pipeline);
      cogl_object_unref (pipeline);
      pipeline = tmp_pipeline;

      uniform_location =
        cogl_pipeline_get_uniform_location (pipeline, "a_uniform");

      cogl_pipeline_set_uniform_1i (pipeline, uniform_location, i);
    }

  for (node = (CoglNode *) pipeline; node; node = node->parent)
    pipeline_length++;

  g_assert_cmpint (pipeline_length, <=, 2);

  cogl_object_unref (pipeline);
}
