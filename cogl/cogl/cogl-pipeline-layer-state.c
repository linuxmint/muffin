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
#include "cogl-pipeline-private.h"
#include "cogl-blend-string.h"
#include "cogl-util.h"
#include "cogl-matrix.h"
#include "cogl-snippet-private.h"
#include "cogl-texture-private.h"
#include "cogl-pipeline-layer-state-private.h"

#include "string.h"
#if 0
#include "cogl-context-private.h"
#include "cogl-color-private.h"

#endif

/*
 * XXX: consider special casing layer->unit_index so it's not a sparse
 * property so instead we can assume it's valid for all layer
 * instances.
 * - We would need to initialize ->unit_index in
 *   _cogl_pipeline_layer_copy ().
 *
 * XXX: If you use this API you should consider that the given layer
 * might not be writeable and so a new derived layer will be allocated
 * and modified instead. The layer modified will be returned so you
 * can identify when this happens.
 */
CoglPipelineLayer *
_cogl_pipeline_set_layer_unit (CoglPipeline *required_owner,
                               CoglPipelineLayer *layer,
                               int unit_index)
{
  CoglPipelineLayerState change = COGL_PIPELINE_LAYER_STATE_UNIT;
  CoglPipelineLayer *authority =
    _cogl_pipeline_layer_get_authority (layer, change);
  CoglPipelineLayer *new;

  if (authority->unit_index == unit_index)
    return layer;

  new =
    _cogl_pipeline_layer_pre_change_notify (required_owner,
                                            layer,
                                            change);
  if (new != layer)
    layer = new;
  else
    {
      /* If the layer we found is currently the authority on the state
       * we are changing see if we can revert to one of our ancestors
       * being the authority. */
      if (layer == authority &&
          _cogl_pipeline_layer_get_parent (authority) != NULL)
        {
          CoglPipelineLayer *parent =
            _cogl_pipeline_layer_get_parent (authority);
          CoglPipelineLayer *old_authority =
            _cogl_pipeline_layer_get_authority (parent, change);

          if (old_authority->unit_index == unit_index)
            {
              layer->differences &= ~change;
              return layer;
            }
        }
    }

  layer->unit_index = unit_index;

  /* If we weren't previously the authority on this state then we need
   * to extended our differences mask and so it's possible that some
   * of our ancestry will now become redundant, so we aim to reparent
   * ourselves if that's true... */
  if (layer != authority)
    {
      layer->differences |= change;
      _cogl_pipeline_layer_prune_redundant_ancestry (layer);
    }

  return layer;
}

CoglTexture *
_cogl_pipeline_layer_get_texture_real (CoglPipelineLayer *layer)
{
  CoglPipelineLayer *authority =
    _cogl_pipeline_layer_get_authority (layer,
                                        COGL_PIPELINE_LAYER_STATE_TEXTURE_DATA);

  return authority->texture;
}

CoglTexture *
cogl_pipeline_get_layer_texture (CoglPipeline *pipeline,
                                 int layer_index)
{
  CoglPipelineLayer *layer =
    _cogl_pipeline_get_layer (pipeline, layer_index);
  return _cogl_pipeline_layer_get_texture (layer);
}

static void
_cogl_pipeline_set_layer_texture_data (CoglPipeline *pipeline,
                                       int layer_index,
                                       CoglTexture *texture)
{
  CoglPipelineLayerState change = COGL_PIPELINE_LAYER_STATE_TEXTURE_DATA;
  CoglPipelineLayer *layer;
  CoglPipelineLayer *authority;
  CoglPipelineLayer *new;

  /* Note: this will ensure that the layer exists, creating one if it
   * doesn't already.
   *
   * Note: If the layer already existed it's possibly owned by another
   * pipeline. If the layer is created then it will be owned by
   * pipeline. */
  layer = _cogl_pipeline_get_layer (pipeline, layer_index);

  /* Now find the ancestor of the layer that is the authority for the
   * state we want to change */
  authority = _cogl_pipeline_layer_get_authority (layer, change);

  if (authority->texture == texture)
    return;

  new = _cogl_pipeline_layer_pre_change_notify (pipeline, layer, change);
  if (new != layer)
    layer = new;
  else
    {
      /* If the original layer we found is currently the authority on
       * the state we are changing see if we can revert to one of our
       * ancestors being the authority. */
      if (layer == authority &&
          _cogl_pipeline_layer_get_parent (authority) != NULL)
        {
          CoglPipelineLayer *parent =
            _cogl_pipeline_layer_get_parent (authority);
          CoglPipelineLayer *old_authority =
            _cogl_pipeline_layer_get_authority (parent, change);

          if (old_authority->texture == texture)
            {
              layer->differences &= ~change;

              if (layer->texture != NULL)
                cogl_object_unref (layer->texture);

              g_assert (layer->owner == pipeline);
              if (layer->differences == 0)
                _cogl_pipeline_prune_empty_layer_difference (pipeline,
                                                             layer);
              goto changed;
            }
        }
    }

  if (texture != NULL)
    cogl_object_ref (texture);
  if (layer == authority &&
      layer->texture != NULL)
    cogl_object_unref (layer->texture);
  layer->texture = texture;

  /* If we weren't previously the authority on this state then we need
   * to extended our differences mask and so it's possible that some
   * of our ancestry will now become redundant, so we aim to reparent
   * ourselves if that's true... */
  if (layer != authority)
    {
      layer->differences |= change;
      _cogl_pipeline_layer_prune_redundant_ancestry (layer);
    }

changed:

  pipeline->dirty_real_blend_enable = TRUE;
}

void
cogl_pipeline_set_layer_texture (CoglPipeline *pipeline,
                                 int layer_index,
                                 CoglTexture *texture)
{
  _cogl_pipeline_set_layer_texture_data (pipeline, layer_index, texture);
}

void
cogl_pipeline_set_layer_null_texture (CoglPipeline *pipeline,
                                      int layer_index)
{
  _cogl_pipeline_set_layer_texture_data (pipeline, layer_index, NULL);
}

static void
_cogl_pipeline_set_layer_sampler_state (CoglPipeline *pipeline,
                                        CoglPipelineLayer *layer,
                                        CoglPipelineLayer *authority,
                                        const CoglSamplerCacheEntry *state)
{
  CoglPipelineLayer *new;
  CoglPipelineLayerState change = COGL_PIPELINE_LAYER_STATE_SAMPLER;

  if (authority->sampler_cache_entry == state)
    return;

  new = _cogl_pipeline_layer_pre_change_notify (pipeline, layer, change);
  if (new != layer)
    layer = new;
  else
    {
      /* If the original layer we found is currently the authority on
       * the state we are changing see if we can revert to one of our
       * ancestors being the authority. */
      if (layer == authority &&
          _cogl_pipeline_layer_get_parent (authority) != NULL)
        {
          CoglPipelineLayer *parent =
            _cogl_pipeline_layer_get_parent (authority);
          CoglPipelineLayer *old_authority =
            _cogl_pipeline_layer_get_authority (parent, change);

          if (old_authority->sampler_cache_entry == state)
            {
              layer->differences &= ~change;

              g_assert (layer->owner == pipeline);
              if (layer->differences == 0)
                _cogl_pipeline_prune_empty_layer_difference (pipeline,
                                                             layer);
              return;
            }
        }
    }

  layer->sampler_cache_entry = state;

  /* If we weren't previously the authority on this state then we need
   * to extended our differences mask and so it's possible that some
   * of our ancestry will now become redundant, so we aim to reparent
   * ourselves if that's true... */
  if (layer != authority)
    {
      layer->differences |= change;
      _cogl_pipeline_layer_prune_redundant_ancestry (layer);
    }
}

static CoglSamplerCacheWrapMode
public_to_internal_wrap_mode (CoglPipelineWrapMode mode)
{
  return (CoglSamplerCacheWrapMode)mode;
}

static CoglPipelineWrapMode
internal_to_public_wrap_mode (CoglSamplerCacheWrapMode internal_mode)
{
  g_return_val_if_fail (internal_mode !=
                        COGL_SAMPLER_CACHE_WRAP_MODE_CLAMP_TO_BORDER,
                        COGL_PIPELINE_WRAP_MODE_AUTOMATIC);
  return (CoglPipelineWrapMode)internal_mode;
}

void
cogl_pipeline_set_layer_wrap_mode_s (CoglPipeline *pipeline,
                                     int layer_index,
                                     CoglPipelineWrapMode mode)
{
  CoglPipelineLayerState       change = COGL_PIPELINE_LAYER_STATE_SAMPLER;
  CoglPipelineLayer           *layer;
  CoglPipelineLayer           *authority;
  CoglSamplerCacheWrapMode     internal_mode =
    public_to_internal_wrap_mode (mode);
  const CoglSamplerCacheEntry *sampler_state;

  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  g_return_if_fail (cogl_is_pipeline (pipeline));

  /* Note: this will ensure that the layer exists, creating one if it
   * doesn't already.
   *
   * Note: If the layer already existed it's possibly owned by another
   * pipeline. If the layer is created then it will be owned by
   * pipeline. */
  layer = _cogl_pipeline_get_layer (pipeline, layer_index);

  /* Now find the ancestor of the layer that is the authority for the
   * state we want to change */
  authority = _cogl_pipeline_layer_get_authority (layer, change);

  sampler_state =
    _cogl_sampler_cache_update_wrap_modes (ctx->sampler_cache,
                                           authority->sampler_cache_entry,
                                           internal_mode,
                                           authority->sampler_cache_entry->
                                           wrap_mode_t);
  _cogl_pipeline_set_layer_sampler_state (pipeline,
                                          layer,
                                          authority,
                                          sampler_state);
}

void
cogl_pipeline_set_layer_wrap_mode_t (CoglPipeline *pipeline,
                                     int layer_index,
                                     CoglPipelineWrapMode mode)
{
  CoglPipelineLayerState       change = COGL_PIPELINE_LAYER_STATE_SAMPLER;
  CoglPipelineLayer           *layer;
  CoglPipelineLayer           *authority;
  CoglSamplerCacheWrapMode     internal_mode =
    public_to_internal_wrap_mode (mode);
  const CoglSamplerCacheEntry *sampler_state;

  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  g_return_if_fail (cogl_is_pipeline (pipeline));

  /* Note: this will ensure that the layer exists, creating one if it
   * doesn't already.
   *
   * Note: If the layer already existed it's possibly owned by another
   * pipeline. If the layer is created then it will be owned by
   * pipeline. */
  layer = _cogl_pipeline_get_layer (pipeline, layer_index);

  /* Now find the ancestor of the layer that is the authority for the
   * state we want to change */
  authority = _cogl_pipeline_layer_get_authority (layer, change);

  sampler_state =
    _cogl_sampler_cache_update_wrap_modes (ctx->sampler_cache,
                                           authority->sampler_cache_entry,
                                           authority->sampler_cache_entry->
                                           wrap_mode_s,
                                           internal_mode);
  _cogl_pipeline_set_layer_sampler_state (pipeline,
                                          layer,
                                          authority,
                                          sampler_state);
}

void
cogl_pipeline_set_layer_wrap_mode (CoglPipeline *pipeline,
                                   int layer_index,
                                   CoglPipelineWrapMode mode)
{
  CoglPipelineLayerState       change = COGL_PIPELINE_LAYER_STATE_SAMPLER;
  CoglPipelineLayer           *layer;
  CoglPipelineLayer           *authority;
  CoglSamplerCacheWrapMode     internal_mode =
    public_to_internal_wrap_mode (mode);
  const CoglSamplerCacheEntry *sampler_state;

  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  g_return_if_fail (cogl_is_pipeline (pipeline));

  /* Note: this will ensure that the layer exists, creating one if it
   * doesn't already.
   *
   * Note: If the layer already existed it's possibly owned by another
   * pipeline. If the layer is created then it will be owned by
   * pipeline. */
  layer = _cogl_pipeline_get_layer (pipeline, layer_index);

  /* Now find the ancestor of the layer that is the authority for the
   * state we want to change */
  authority = _cogl_pipeline_layer_get_authority (layer, change);

  sampler_state =
    _cogl_sampler_cache_update_wrap_modes (ctx->sampler_cache,
                                           authority->sampler_cache_entry,
                                           internal_mode,
                                           internal_mode);
  _cogl_pipeline_set_layer_sampler_state (pipeline,
                                          layer,
                                          authority,
                                          sampler_state);
}

/* FIXME: deprecate this API */
CoglPipelineWrapMode
_cogl_pipeline_layer_get_wrap_mode_s (CoglPipelineLayer *layer)
{
  CoglPipelineLayerState change = COGL_PIPELINE_LAYER_STATE_SAMPLER;
  CoglPipelineLayer     *authority;
  const CoglSamplerCacheEntry *sampler_state;

  g_return_val_if_fail (_cogl_is_pipeline_layer (layer), FALSE);

  /* Now find the ancestor of the layer that is the authority for the
   * state we want to change */
  authority = _cogl_pipeline_layer_get_authority (layer, change);

  sampler_state = authority->sampler_cache_entry;
  return internal_to_public_wrap_mode (sampler_state->wrap_mode_s);
}

CoglPipelineWrapMode
cogl_pipeline_get_layer_wrap_mode_s (CoglPipeline *pipeline, int layer_index)
{
  CoglPipelineLayer *layer;

  g_return_val_if_fail (cogl_is_pipeline (pipeline), FALSE);

  /* Note: this will ensure that the layer exists, creating one if it
   * doesn't already.
   *
   * Note: If the layer already existed it's possibly owned by another
   * pipeline. If the layer is created then it will be owned by
   * pipeline. */
  layer = _cogl_pipeline_get_layer (pipeline, layer_index);
  /* FIXME: we shouldn't ever construct a layer in a getter function */

  return _cogl_pipeline_layer_get_wrap_mode_s (layer);
}

/* FIXME: deprecate this API */
CoglPipelineWrapMode
_cogl_pipeline_layer_get_wrap_mode_t (CoglPipelineLayer *layer)
{
  CoglPipelineLayerState change = COGL_PIPELINE_LAYER_STATE_SAMPLER;
  CoglPipelineLayer     *authority;
  const CoglSamplerCacheEntry *sampler_state;

  g_return_val_if_fail (_cogl_is_pipeline_layer (layer), FALSE);

  /* Now find the ancestor of the layer that is the authority for the
   * state we want to change */
  authority = _cogl_pipeline_layer_get_authority (layer, change);

  sampler_state = authority->sampler_cache_entry;
  return internal_to_public_wrap_mode (sampler_state->wrap_mode_t);
}

CoglPipelineWrapMode
cogl_pipeline_get_layer_wrap_mode_t (CoglPipeline *pipeline, int layer_index)
{
  CoglPipelineLayer *layer;

  g_return_val_if_fail (cogl_is_pipeline (pipeline), FALSE);

  /* Note: this will ensure that the layer exists, creating one if it
   * doesn't already.
   *
   * Note: If the layer already existed it's possibly owned by another
   * pipeline. If the layer is created then it will be owned by
   * pipeline. */
  layer = _cogl_pipeline_get_layer (pipeline, layer_index);
  /* FIXME: we shouldn't ever construct a layer in a getter function */

  return _cogl_pipeline_layer_get_wrap_mode_t (layer);
}

void
_cogl_pipeline_layer_get_wrap_modes (CoglPipelineLayer *layer,
                                     CoglSamplerCacheWrapMode *wrap_mode_s,
                                     CoglSamplerCacheWrapMode *wrap_mode_t)
{
  CoglPipelineLayer *authority =
    _cogl_pipeline_layer_get_authority (layer,
                                        COGL_PIPELINE_LAYER_STATE_SAMPLER);

  *wrap_mode_s = authority->sampler_cache_entry->wrap_mode_s;
  *wrap_mode_t = authority->sampler_cache_entry->wrap_mode_t;
}

gboolean
cogl_pipeline_set_layer_point_sprite_coords_enabled (CoglPipeline *pipeline,
                                                     int layer_index,
                                                     gboolean enable,
                                                     GError **error)
{
  CoglPipelineLayerState       change =
    COGL_PIPELINE_LAYER_STATE_POINT_SPRITE_COORDS;
  CoglPipelineLayer           *layer;
  CoglPipelineLayer           *new;
  CoglPipelineLayer           *authority;

  _COGL_GET_CONTEXT (ctx, FALSE);

  g_return_val_if_fail (cogl_is_pipeline (pipeline), FALSE);

  /* Note: this will ensure that the layer exists, creating one if it
   * doesn't already.
   *
   * Note: If the layer already existed it's possibly owned by another
   * pipeline. If the layer is created then it will be owned by
   * pipeline. */
  layer = _cogl_pipeline_get_layer (pipeline, layer_index);

  /* Now find the ancestor of the layer that is the authority for the
   * state we want to change */
  authority = _cogl_pipeline_layer_get_authority (layer, change);

  if (authority->big_state->point_sprite_coords == enable)
    return TRUE;

  new = _cogl_pipeline_layer_pre_change_notify (pipeline, layer, change);
  if (new != layer)
    layer = new;
  else
    {
      /* If the original layer we found is currently the authority on
       * the state we are changing see if we can revert to one of our
       * ancestors being the authority. */
      if (layer == authority &&
          _cogl_pipeline_layer_get_parent (authority) != NULL)
        {
          CoglPipelineLayer *parent =
            _cogl_pipeline_layer_get_parent (authority);
          CoglPipelineLayer *old_authority =
            _cogl_pipeline_layer_get_authority (parent, change);

          if (old_authority->big_state->point_sprite_coords == enable)
            {
              layer->differences &= ~change;

              g_assert (layer->owner == pipeline);
              if (layer->differences == 0)
                _cogl_pipeline_prune_empty_layer_difference (pipeline,
                                                             layer);
              return TRUE;
            }
        }
    }

  layer->big_state->point_sprite_coords = enable;

  /* If we weren't previously the authority on this state then we need
   * to extended our differences mask and so it's possible that some
   * of our ancestry will now become redundant, so we aim to reparent
   * ourselves if that's true... */
  if (layer != authority)
    {
      layer->differences |= change;
      _cogl_pipeline_layer_prune_redundant_ancestry (layer);
    }

  return TRUE;
}

gboolean
cogl_pipeline_get_layer_point_sprite_coords_enabled (CoglPipeline *pipeline,
                                                     int layer_index)
{
  CoglPipelineLayerState       change =
    COGL_PIPELINE_LAYER_STATE_POINT_SPRITE_COORDS;
  CoglPipelineLayer *layer;
  CoglPipelineLayer *authority;

  g_return_val_if_fail (cogl_is_pipeline (pipeline), FALSE);

  /* Note: this will ensure that the layer exists, creating one if it
   * doesn't already.
   *
   * Note: If the layer already existed it's possibly owned by another
   * pipeline. If the layer is created then it will be owned by
   * pipeline. */
  layer = _cogl_pipeline_get_layer (pipeline, layer_index);
  /* FIXME: we shouldn't ever construct a layer in a getter function */

  authority = _cogl_pipeline_layer_get_authority (layer, change);

  return authority->big_state->point_sprite_coords;
}

static void
_cogl_pipeline_layer_add_vertex_snippet (CoglPipeline *pipeline,
                                         int layer_index,
                                         CoglSnippet *snippet)
{
  CoglPipelineLayerState change = COGL_PIPELINE_LAYER_STATE_VERTEX_SNIPPETS;
  CoglPipelineLayer *layer, *authority;

  /* Note: this will ensure that the layer exists, creating one if it
   * doesn't already.
   *
   * Note: If the layer already existed it's possibly owned by another
   * pipeline. If the layer is created then it will be owned by
   * pipeline. */
  layer = _cogl_pipeline_get_layer (pipeline, layer_index);

  /* Now find the ancestor of the layer that is the authority for the
   * state we want to change */
  authority = _cogl_pipeline_layer_get_authority (layer, change);

  layer = _cogl_pipeline_layer_pre_change_notify (pipeline, layer, change);

  _cogl_pipeline_snippet_list_add (&layer->big_state->vertex_snippets,
                                   snippet);

  /* If we weren't previously the authority on this state then we need
   * to extended our differences mask and so it's possible that some
   * of our ancestry will now become redundant, so we aim to reparent
   * ourselves if that's true... */
  if (layer != authority)
    {
      layer->differences |= change;
      _cogl_pipeline_layer_prune_redundant_ancestry (layer);
    }
}

static void
_cogl_pipeline_layer_add_fragment_snippet (CoglPipeline *pipeline,
                                           int layer_index,
                                           CoglSnippet *snippet)
{
  CoglPipelineLayerState change = COGL_PIPELINE_LAYER_STATE_FRAGMENT_SNIPPETS;
  CoglPipelineLayer *layer, *authority;

  /* Note: this will ensure that the layer exists, creating one if it
   * doesn't already.
   *
   * Note: If the layer already existed it's possibly owned by another
   * pipeline. If the layer is created then it will be owned by
   * pipeline. */
  layer = _cogl_pipeline_get_layer (pipeline, layer_index);

  /* Now find the ancestor of the layer that is the authority for the
   * state we want to change */
  authority = _cogl_pipeline_layer_get_authority (layer, change);

  layer = _cogl_pipeline_layer_pre_change_notify (pipeline, layer, change);

  _cogl_pipeline_snippet_list_add (&layer->big_state->fragment_snippets,
                                   snippet);

  /* If we weren't previously the authority on this state then we need
   * to extended our differences mask and so it's possible that some
   * of our ancestry will now become redundant, so we aim to reparent
   * ourselves if that's true... */
  if (layer != authority)
    {
      layer->differences |= change;
      _cogl_pipeline_layer_prune_redundant_ancestry (layer);
    }
}

void
cogl_pipeline_add_layer_snippet (CoglPipeline *pipeline,
                                 int layer_index,
                                 CoglSnippet *snippet)
{
  g_return_if_fail (cogl_is_pipeline (pipeline));
  g_return_if_fail (cogl_is_snippet (snippet));
  g_return_if_fail (snippet->hook >= COGL_SNIPPET_FIRST_LAYER_HOOK);

  if (snippet->hook < COGL_SNIPPET_FIRST_LAYER_FRAGMENT_HOOK)
    _cogl_pipeline_layer_add_vertex_snippet (pipeline,
                                             layer_index,
                                             snippet);
  else
    _cogl_pipeline_layer_add_fragment_snippet (pipeline,
                                               layer_index,
                                               snippet);
}

gboolean
_cogl_pipeline_layer_texture_data_equal (CoglPipelineLayer *authority0,
                                         CoglPipelineLayer *authority1,
                                         CoglPipelineEvalFlags flags)
{
  if (authority0->texture == NULL)
    {
      if (authority1->texture == NULL)
        return TRUE;
      else
        return FALSE;
    }
  else if (authority1->texture == NULL)
    return FALSE;
  else
    {
      GLuint gl_handle0, gl_handle1;

      cogl_texture_get_gl_texture (authority0->texture, &gl_handle0, NULL);
      cogl_texture_get_gl_texture (authority1->texture, &gl_handle1, NULL);

      return gl_handle0 == gl_handle1;
    }
}

gboolean
_cogl_pipeline_layer_combine_state_equal (CoglPipelineLayer *authority0,
                                          CoglPipelineLayer *authority1)
{
  CoglPipelineLayerBigState *big_state0 = authority0->big_state;
  CoglPipelineLayerBigState *big_state1 = authority1->big_state;
  int n_args;
  int i;

  if (big_state0->texture_combine_rgb_func !=
      big_state1->texture_combine_rgb_func)
    return FALSE;

  if (big_state0->texture_combine_alpha_func !=
      big_state1->texture_combine_alpha_func)
    return FALSE;

  n_args =
    _cogl_get_n_args_for_combine_func (big_state0->texture_combine_rgb_func);
  for (i = 0; i < n_args; i++)
    {
      if ((big_state0->texture_combine_rgb_src[i] !=
           big_state1->texture_combine_rgb_src[i]) ||
          (big_state0->texture_combine_rgb_op[i] !=
           big_state1->texture_combine_rgb_op[i]))
        return FALSE;
    }

  n_args =
    _cogl_get_n_args_for_combine_func (big_state0->texture_combine_alpha_func);
  for (i = 0; i < n_args; i++)
    {
      if ((big_state0->texture_combine_alpha_src[i] !=
           big_state1->texture_combine_alpha_src[i]) ||
          (big_state0->texture_combine_alpha_op[i] !=
           big_state1->texture_combine_alpha_op[i]))
        return FALSE;
    }

  return TRUE;
}

gboolean
_cogl_pipeline_layer_combine_constant_equal (CoglPipelineLayer *authority0,
                                             CoglPipelineLayer *authority1)
{
  return memcmp (authority0->big_state->texture_combine_constant,
                 authority1->big_state->texture_combine_constant,
                 sizeof (float) * 4) == 0 ? TRUE : FALSE;
}

gboolean
_cogl_pipeline_layer_sampler_equal (CoglPipelineLayer *authority0,
                                    CoglPipelineLayer *authority1)
{
  /* We compare the actual sampler objects rather than just the entry
     pointers because two states with different values can lead to the
     same state in GL terms when AUTOMATIC is used as a wrap mode */
  return (authority0->sampler_cache_entry->sampler_object ==
          authority1->sampler_cache_entry->sampler_object);
}

gboolean
_cogl_pipeline_layer_user_matrix_equal (CoglPipelineLayer *authority0,
                                        CoglPipelineLayer *authority1)
{
  CoglPipelineLayerBigState *big_state0 = authority0->big_state;
  CoglPipelineLayerBigState *big_state1 = authority1->big_state;

  if (!cogl_matrix_equal (&big_state0->matrix, &big_state1->matrix))
    return FALSE;

  return TRUE;
}

gboolean
_cogl_pipeline_layer_point_sprite_coords_equal (CoglPipelineLayer *authority0,
                                                CoglPipelineLayer *authority1)
{
  CoglPipelineLayerBigState *big_state0 = authority0->big_state;
  CoglPipelineLayerBigState *big_state1 = authority1->big_state;

  return big_state0->point_sprite_coords == big_state1->point_sprite_coords;
}

gboolean
_cogl_pipeline_layer_vertex_snippets_equal (CoglPipelineLayer *authority0,
                                            CoglPipelineLayer *authority1)
{
  return _cogl_pipeline_snippet_list_equal (&authority0->big_state->
                                            vertex_snippets,
                                            &authority1->big_state->
                                            vertex_snippets);
}

gboolean
_cogl_pipeline_layer_fragment_snippets_equal (CoglPipelineLayer *authority0,
                                              CoglPipelineLayer *authority1)
{
  return _cogl_pipeline_snippet_list_equal (&authority0->big_state->
                                            fragment_snippets,
                                            &authority1->big_state->
                                            fragment_snippets);
}

static void
setup_texture_combine_state (CoglBlendStringStatement *statement,
                             CoglPipelineCombineFunc *texture_combine_func,
                             CoglPipelineCombineSource *texture_combine_src,
                             CoglPipelineCombineOp *texture_combine_op)
{
  int i;

  switch (statement->function->type)
    {
    case COGL_BLEND_STRING_FUNCTION_REPLACE:
      *texture_combine_func = COGL_PIPELINE_COMBINE_FUNC_REPLACE;
      break;
    case COGL_BLEND_STRING_FUNCTION_MODULATE:
      *texture_combine_func = COGL_PIPELINE_COMBINE_FUNC_MODULATE;
      break;
    case COGL_BLEND_STRING_FUNCTION_ADD:
      *texture_combine_func = COGL_PIPELINE_COMBINE_FUNC_ADD;
      break;
    case COGL_BLEND_STRING_FUNCTION_ADD_SIGNED:
      *texture_combine_func = COGL_PIPELINE_COMBINE_FUNC_ADD_SIGNED;
      break;
    case COGL_BLEND_STRING_FUNCTION_INTERPOLATE:
      *texture_combine_func = COGL_PIPELINE_COMBINE_FUNC_INTERPOLATE;
      break;
    case COGL_BLEND_STRING_FUNCTION_SUBTRACT:
      *texture_combine_func = COGL_PIPELINE_COMBINE_FUNC_SUBTRACT;
      break;
    case COGL_BLEND_STRING_FUNCTION_DOT3_RGB:
      *texture_combine_func = COGL_PIPELINE_COMBINE_FUNC_DOT3_RGB;
      break;
    case COGL_BLEND_STRING_FUNCTION_DOT3_RGBA:
      *texture_combine_func = COGL_PIPELINE_COMBINE_FUNC_DOT3_RGBA;
      break;
    }

  for (i = 0; i < statement->function->argc; i++)
    {
      CoglBlendStringArgument *arg = &statement->args[i];

      switch (arg->source.info->type)
        {
        case COGL_BLEND_STRING_COLOR_SOURCE_CONSTANT:
          texture_combine_src[i] = COGL_PIPELINE_COMBINE_SOURCE_CONSTANT;
          break;
        case COGL_BLEND_STRING_COLOR_SOURCE_TEXTURE:
          texture_combine_src[i] = COGL_PIPELINE_COMBINE_SOURCE_TEXTURE;
          break;
        case COGL_BLEND_STRING_COLOR_SOURCE_TEXTURE_N:
          texture_combine_src[i] =
            COGL_PIPELINE_COMBINE_SOURCE_TEXTURE0 + arg->source.texture;
          break;
        case COGL_BLEND_STRING_COLOR_SOURCE_PRIMARY:
          texture_combine_src[i] = COGL_PIPELINE_COMBINE_SOURCE_PRIMARY_COLOR;
          break;
        case COGL_BLEND_STRING_COLOR_SOURCE_PREVIOUS:
          texture_combine_src[i] = COGL_PIPELINE_COMBINE_SOURCE_PREVIOUS;
          break;
        default:
          g_warning ("Unexpected texture combine source");
          texture_combine_src[i] = COGL_PIPELINE_COMBINE_SOURCE_TEXTURE;
        }

      if (arg->source.mask == COGL_BLEND_STRING_CHANNEL_MASK_RGB)
        {
          if (statement->args[i].source.one_minus)
            texture_combine_op[i] =
              COGL_PIPELINE_COMBINE_OP_ONE_MINUS_SRC_COLOR;
          else
            texture_combine_op[i] = COGL_PIPELINE_COMBINE_OP_SRC_COLOR;
        }
      else
        {
          if (statement->args[i].source.one_minus)
            texture_combine_op[i] =
              COGL_PIPELINE_COMBINE_OP_ONE_MINUS_SRC_ALPHA;
          else
            texture_combine_op[i] = COGL_PIPELINE_COMBINE_OP_SRC_ALPHA;
        }
    }
}

gboolean
cogl_pipeline_set_layer_combine (CoglPipeline *pipeline,
				 int layer_index,
				 const char *combine_description,
                                 GError **error)
{
  CoglPipelineLayerState state = COGL_PIPELINE_LAYER_STATE_COMBINE;
  CoglPipelineLayer *authority;
  CoglPipelineLayer *layer;
  CoglBlendStringStatement statements[2];
  CoglBlendStringStatement split[2];
  CoglBlendStringStatement *rgb;
  CoglBlendStringStatement *a;
  int count;

  g_return_val_if_fail (cogl_is_pipeline (pipeline), FALSE);

  /* Note: this will ensure that the layer exists, creating one if it
   * doesn't already.
   *
   * Note: If the layer already existed it's possibly owned by another
   * pipeline. If the layer is created then it will be owned by
   * pipeline. */
  layer = _cogl_pipeline_get_layer (pipeline, layer_index);

  /* Now find the ancestor of the layer that is the authority for the
   * state we want to change */
  authority = _cogl_pipeline_layer_get_authority (layer, state);

  count =
    _cogl_blend_string_compile (combine_description,
                                COGL_BLEND_STRING_CONTEXT_TEXTURE_COMBINE,
                                statements,
                                error);
  if (!count)
    return FALSE;

  if (statements[0].mask == COGL_BLEND_STRING_CHANNEL_MASK_RGBA)
    {
      _cogl_blend_string_split_rgba_statement (statements,
                                               &split[0], &split[1]);
      rgb = &split[0];
      a = &split[1];
    }
  else
    {
      rgb = &statements[0];
      a = &statements[1];
    }

  /* FIXME: compare the new state with the current state! */

  /* possibly flush primitives referencing the current state... */
  layer = _cogl_pipeline_layer_pre_change_notify (pipeline, layer, state);

  setup_texture_combine_state (rgb,
                               &layer->big_state->texture_combine_rgb_func,
                               layer->big_state->texture_combine_rgb_src,
                               layer->big_state->texture_combine_rgb_op);

  setup_texture_combine_state (a,
                               &layer->big_state->texture_combine_alpha_func,
                               layer->big_state->texture_combine_alpha_src,
                               layer->big_state->texture_combine_alpha_op);

  /* If the original layer we found is currently the authority on
   * the state we are changing see if we can revert to one of our
   * ancestors being the authority. */
  if (layer == authority &&
      _cogl_pipeline_layer_get_parent (authority) != NULL)
    {
      CoglPipelineLayer *parent = _cogl_pipeline_layer_get_parent (authority);
      CoglPipelineLayer *old_authority =
        _cogl_pipeline_layer_get_authority (parent, state);

      if (_cogl_pipeline_layer_combine_state_equal (authority,
                                                    old_authority))
        {
          layer->differences &= ~state;

          g_assert (layer->owner == pipeline);
          if (layer->differences == 0)
            _cogl_pipeline_prune_empty_layer_difference (pipeline,
                                                         layer);
          goto changed;
        }
    }

  /* If we weren't previously the authority on this state then we need
   * to extended our differences mask and so it's possible that some
   * of our ancestry will now become redundant, so we aim to reparent
   * ourselves if that's true... */
  if (layer != authority)
    {
      layer->differences |= state;
      _cogl_pipeline_layer_prune_redundant_ancestry (layer);
    }

changed:

  pipeline->dirty_real_blend_enable = TRUE;
  return TRUE;
}

void
cogl_pipeline_set_layer_combine_constant (CoglPipeline *pipeline,
				          int layer_index,
                                          const CoglColor *constant_color)
{
  CoglPipelineLayerState state = COGL_PIPELINE_LAYER_STATE_COMBINE_CONSTANT;
  CoglPipelineLayer     *layer;
  CoglPipelineLayer     *authority;
  CoglPipelineLayer     *new;
  float                  color_as_floats[4];

  g_return_if_fail (cogl_is_pipeline (pipeline));

  /* Note: this will ensure that the layer exists, creating one if it
   * doesn't already.
   *
   * Note: If the layer already existed it's possibly owned by another
   * pipeline. If the layer is created then it will be owned by
   * pipeline. */
  layer = _cogl_pipeline_get_layer (pipeline, layer_index);

  /* Now find the ancestor of the layer that is the authority for the
   * state we want to change */
  authority = _cogl_pipeline_layer_get_authority (layer, state);

  color_as_floats[0] = cogl_color_get_red_float (constant_color);
  color_as_floats[1] = cogl_color_get_green_float (constant_color);
  color_as_floats[2] = cogl_color_get_blue_float (constant_color);
  color_as_floats[3] = cogl_color_get_alpha_float (constant_color);

  if (memcmp (authority->big_state->texture_combine_constant,
              color_as_floats, sizeof (float) * 4) == 0)
    return;

  new = _cogl_pipeline_layer_pre_change_notify (pipeline, layer, state);
  if (new != layer)
    layer = new;
  else
    {
      /* If the original layer we found is currently the authority on
       * the state we are changing see if we can revert to one of our
       * ancestors being the authority. */
      if (layer == authority &&
          _cogl_pipeline_layer_get_parent (authority) != NULL)
        {
          CoglPipelineLayer *parent =
            _cogl_pipeline_layer_get_parent (authority);
          CoglPipelineLayer *old_authority =
            _cogl_pipeline_layer_get_authority (parent, state);
          CoglPipelineLayerBigState *old_big_state = old_authority->big_state;

          if (memcmp (old_big_state->texture_combine_constant,
                      color_as_floats, sizeof (float) * 4) == 0)
            {
              layer->differences &= ~state;

              g_assert (layer->owner == pipeline);
              if (layer->differences == 0)
                _cogl_pipeline_prune_empty_layer_difference (pipeline,
                                                             layer);
              goto changed;
            }
        }
    }

  memcpy (layer->big_state->texture_combine_constant,
          color_as_floats,
          sizeof (color_as_floats));

  /* If we weren't previously the authority on this state then we need
   * to extended our differences mask and so it's possible that some
   * of our ancestry will now become redundant, so we aim to reparent
   * ourselves if that's true... */
  if (layer != authority)
    {
      layer->differences |= state;
      _cogl_pipeline_layer_prune_redundant_ancestry (layer);
    }

changed:

  pipeline->dirty_real_blend_enable = TRUE;
}

void
_cogl_pipeline_get_layer_combine_constant (CoglPipeline *pipeline,
                                           int layer_index,
                                           float *constant)
{
  CoglPipelineLayerState       change =
    COGL_PIPELINE_LAYER_STATE_COMBINE_CONSTANT;
  CoglPipelineLayer *layer;
  CoglPipelineLayer *authority;

  g_return_if_fail (cogl_is_pipeline (pipeline));

  /* Note: this will ensure that the layer exists, creating one if it
   * doesn't already.
   *
   * Note: If the layer already existed it's possibly owned by another
   * pipeline. If the layer is created then it will be owned by
   * pipeline. */
  layer = _cogl_pipeline_get_layer (pipeline, layer_index);
  /* FIXME: we shouldn't ever construct a layer in a getter function */

  authority = _cogl_pipeline_layer_get_authority (layer, change);
  memcpy (constant, authority->big_state->texture_combine_constant,
          sizeof (float) * 4);
}

/* We should probably make a public API version of this that has a
   matrix out-param. For an internal API it's good to be able to avoid
   copying the matrix */
const CoglMatrix *
_cogl_pipeline_get_layer_matrix (CoglPipeline *pipeline, int layer_index)
{
  CoglPipelineLayerState       change =
    COGL_PIPELINE_LAYER_STATE_USER_MATRIX;
  CoglPipelineLayer *layer;
  CoglPipelineLayer *authority;

  g_return_val_if_fail (cogl_is_pipeline (pipeline), NULL);

  layer = _cogl_pipeline_get_layer (pipeline, layer_index);

  authority = _cogl_pipeline_layer_get_authority (layer, change);
  return &authority->big_state->matrix;
}

void
cogl_pipeline_set_layer_matrix (CoglPipeline *pipeline,
				int layer_index,
                                const CoglMatrix *matrix)
{
  CoglPipelineLayerState state = COGL_PIPELINE_LAYER_STATE_USER_MATRIX;
  CoglPipelineLayer     *layer;
  CoglPipelineLayer     *authority;
  CoglPipelineLayer     *new;

  g_return_if_fail (cogl_is_pipeline (pipeline));

  /* Note: this will ensure that the layer exists, creating one if it
   * doesn't already.
   *
   * Note: If the layer already existed it's possibly owned by another
   * pipeline. If the layer is created then it will be owned by
   * pipeline. */
  layer = _cogl_pipeline_get_layer (pipeline, layer_index);

  /* Now find the ancestor of the layer that is the authority for the
   * state we want to change */
  authority = _cogl_pipeline_layer_get_authority (layer, state);

  if (cogl_matrix_equal (matrix, &authority->big_state->matrix))
    return;

  new = _cogl_pipeline_layer_pre_change_notify (pipeline, layer, state);
  if (new != layer)
    layer = new;
  else
    {
      /* If the original layer we found is currently the authority on
       * the state we are changing see if we can revert to one of our
       * ancestors being the authority. */
      if (layer == authority &&
          _cogl_pipeline_layer_get_parent (authority) != NULL)
        {
          CoglPipelineLayer *parent =
            _cogl_pipeline_layer_get_parent (authority);
          CoglPipelineLayer *old_authority =
            _cogl_pipeline_layer_get_authority (parent, state);

          if (cogl_matrix_equal (matrix, &old_authority->big_state->matrix))
            {
              layer->differences &= ~state;

              g_assert (layer->owner == pipeline);
              if (layer->differences == 0)
                _cogl_pipeline_prune_empty_layer_difference (pipeline,
                                                             layer);
              return;
            }
        }
    }

  layer->big_state->matrix = *matrix;

  /* If we weren't previously the authority on this state then we need
   * to extended our differences mask and so it's possible that some
   * of our ancestry will now become redundant, so we aim to reparent
   * ourselves if that's true... */
  if (layer != authority)
    {
      layer->differences |= state;
      _cogl_pipeline_layer_prune_redundant_ancestry (layer);
    }
}

CoglTexture *
_cogl_pipeline_layer_get_texture (CoglPipelineLayer *layer)
{
  g_return_val_if_fail (_cogl_is_pipeline_layer (layer), NULL);

  return _cogl_pipeline_layer_get_texture_real (layer);
}

gboolean
_cogl_pipeline_layer_has_user_matrix (CoglPipeline *pipeline,
                                      int layer_index)
{
  CoglPipelineLayer *layer;
  CoglPipelineLayer *authority;

  layer = _cogl_pipeline_get_layer (pipeline, layer_index);

  authority =
    _cogl_pipeline_layer_get_authority (layer,
                                        COGL_PIPELINE_LAYER_STATE_USER_MATRIX);

  /* If the authority is the default pipeline then no, otherwise yes */
  return _cogl_pipeline_layer_get_parent (authority) ? TRUE : FALSE;
}

void
_cogl_pipeline_layer_get_filters (CoglPipelineLayer *layer,
                                  CoglPipelineFilter *min_filter,
                                  CoglPipelineFilter *mag_filter)
{
  CoglPipelineLayer *authority =
    _cogl_pipeline_layer_get_authority (layer,
                                        COGL_PIPELINE_LAYER_STATE_SAMPLER);

  *min_filter = authority->sampler_cache_entry->min_filter;
  *mag_filter = authority->sampler_cache_entry->mag_filter;
}

void
_cogl_pipeline_get_layer_filters (CoglPipeline *pipeline,
                                  int layer_index,
                                  CoglPipelineFilter *min_filter,
                                  CoglPipelineFilter *mag_filter)
{
  CoglPipelineLayer *layer;
  CoglPipelineLayer *authority;

  g_return_if_fail (cogl_is_pipeline (pipeline));

  layer = _cogl_pipeline_get_layer (pipeline, layer_index);

  authority =
    _cogl_pipeline_layer_get_authority (layer,
                                        COGL_PIPELINE_LAYER_STATE_SAMPLER);

  *min_filter = authority->sampler_cache_entry->min_filter;
  *mag_filter = authority->sampler_cache_entry->mag_filter;
}

CoglPipelineFilter
cogl_pipeline_get_layer_min_filter (CoglPipeline *pipeline,
                                    int layer_index)
{
  CoglPipelineFilter min_filter;
  CoglPipelineFilter mag_filter;

  _cogl_pipeline_get_layer_filters (pipeline, layer_index,
                                    &min_filter, &mag_filter);
  return min_filter;
}

CoglPipelineFilter
cogl_pipeline_get_layer_mag_filter (CoglPipeline *pipeline,
                                    int layer_index)
{
  CoglPipelineFilter min_filter;
  CoglPipelineFilter mag_filter;

  _cogl_pipeline_get_layer_filters (pipeline, layer_index,
                                    &min_filter, &mag_filter);
  return mag_filter;
}

CoglPipelineFilter
_cogl_pipeline_layer_get_min_filter (CoglPipelineLayer *layer)
{
  CoglPipelineLayer *authority;

  g_return_val_if_fail (_cogl_is_pipeline_layer (layer), 0);

  authority =
    _cogl_pipeline_layer_get_authority (layer,
                                        COGL_PIPELINE_LAYER_STATE_SAMPLER);

  return authority->sampler_cache_entry->min_filter;
}

CoglPipelineFilter
_cogl_pipeline_layer_get_mag_filter (CoglPipelineLayer *layer)
{
  CoglPipelineLayer *authority;

  g_return_val_if_fail (_cogl_is_pipeline_layer (layer), 0);

  authority =
    _cogl_pipeline_layer_get_authority (layer,
                                        COGL_PIPELINE_LAYER_STATE_SAMPLER);

  return authority->sampler_cache_entry->mag_filter;
}

void
cogl_pipeline_set_layer_filters (CoglPipeline      *pipeline,
                                 int                layer_index,
                                 CoglPipelineFilter min_filter,
                                 CoglPipelineFilter mag_filter)
{
  CoglPipelineLayerState state = COGL_PIPELINE_LAYER_STATE_SAMPLER;
  CoglPipelineLayer *layer;
  CoglPipelineLayer *authority;
  const CoglSamplerCacheEntry *sampler_state;

  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  g_return_if_fail (cogl_is_pipeline (pipeline));

  g_return_if_fail (mag_filter == COGL_PIPELINE_FILTER_NEAREST ||
                    mag_filter == COGL_PIPELINE_FILTER_LINEAR);

  /* Note: this will ensure that the layer exists, creating one if it
   * doesn't already.
   *
   * Note: If the layer already existed it's possibly owned by another
   * pipeline. If the layer is created then it will be owned by
   * pipeline. */
  layer = _cogl_pipeline_get_layer (pipeline, layer_index);

  /* Now find the ancestor of the layer that is the authority for the
   * state we want to change */
  authority = _cogl_pipeline_layer_get_authority (layer, state);

  sampler_state =
    _cogl_sampler_cache_update_filters (ctx->sampler_cache,
                                        authority->sampler_cache_entry,
                                        min_filter,
                                        mag_filter);
  _cogl_pipeline_set_layer_sampler_state (pipeline,
                                          layer,
                                          authority,
                                          sampler_state);
}

void
cogl_pipeline_set_layer_max_mipmap_level (CoglPipeline *pipeline,
                                          int           layer,
                                          int           max_level)
{
  CoglTexture *texture = cogl_pipeline_get_layer_texture (pipeline, layer);

  if (texture != NULL)
    cogl_texture_set_max_level (texture, max_level);
}

const CoglSamplerCacheEntry *
_cogl_pipeline_layer_get_sampler_state (CoglPipelineLayer *layer)
{
  CoglPipelineLayer *authority;

  authority =
    _cogl_pipeline_layer_get_authority (layer,
                                        COGL_PIPELINE_LAYER_STATE_SAMPLER);

  return authority->sampler_cache_entry;
}

void
_cogl_pipeline_layer_hash_unit_state (CoglPipelineLayer *authority,
                                      CoglPipelineLayer **authorities,
                                      CoglPipelineHashState *state)
{
  int unit = authority->unit_index;
  state->hash =
    _cogl_util_one_at_a_time_hash (state->hash, &unit, sizeof (unit));
}

void
_cogl_pipeline_layer_hash_texture_data_state (CoglPipelineLayer *authority,
                                              CoglPipelineLayer **authorities,
                                              CoglPipelineHashState *state)
{
  GLuint gl_handle;

  cogl_texture_get_gl_texture (authority->texture, &gl_handle, NULL);

  state->hash =
    _cogl_util_one_at_a_time_hash (state->hash, &gl_handle, sizeof (gl_handle));
}

void
_cogl_pipeline_layer_hash_sampler_state (CoglPipelineLayer *authority,
                                         CoglPipelineLayer **authorities,
                                         CoglPipelineHashState *state)
{
  state->hash =
    _cogl_util_one_at_a_time_hash (state->hash,
                                   &authority->sampler_cache_entry,
                                   sizeof (authority->sampler_cache_entry));
}

void
_cogl_pipeline_layer_hash_combine_state (CoglPipelineLayer *authority,
                                         CoglPipelineLayer **authorities,
                                         CoglPipelineHashState *state)
{
  unsigned int hash = state->hash;
  CoglPipelineLayerBigState *b = authority->big_state;
  int n_args;
  int i;

  hash = _cogl_util_one_at_a_time_hash (hash, &b->texture_combine_rgb_func,
                                        sizeof (b->texture_combine_rgb_func));
  n_args = _cogl_get_n_args_for_combine_func (b->texture_combine_rgb_func);
  for (i = 0; i < n_args; i++)
    {
      hash =
        _cogl_util_one_at_a_time_hash (hash, &b->texture_combine_rgb_src[i],
                                       sizeof (b->texture_combine_rgb_src[i]));
      hash =
        _cogl_util_one_at_a_time_hash (hash, &b->texture_combine_rgb_op[i],
                                       sizeof (b->texture_combine_rgb_op[i]));
    }

  hash = _cogl_util_one_at_a_time_hash (hash, &b->texture_combine_alpha_func,
                                        sizeof (b->texture_combine_alpha_func));
  n_args = _cogl_get_n_args_for_combine_func (b->texture_combine_alpha_func);
  for (i = 0; i < n_args; i++)
    {
      hash =
        _cogl_util_one_at_a_time_hash (hash, &b->texture_combine_alpha_src[i],
                                       sizeof (b->texture_combine_alpha_src[i]));
      hash =
        _cogl_util_one_at_a_time_hash (hash, &b->texture_combine_alpha_op[i],
                                       sizeof (b->texture_combine_alpha_op[i]));
    }

  state->hash = hash;
}

void
_cogl_pipeline_layer_hash_combine_constant_state (CoglPipelineLayer *authority,
                                                  CoglPipelineLayer **authorities,
                                                  CoglPipelineHashState *state)
{
  CoglPipelineLayerBigState *b = authority->big_state;
  gboolean need_hash = FALSE;
  int n_args;
  int i;

  /* XXX: If the user also asked to hash the ALPHA_FUNC_STATE then it
   * would be nice if we could combine the n_args loops in this
   * function and _cogl_pipeline_layer_hash_combine_state.
   */

  n_args = _cogl_get_n_args_for_combine_func (b->texture_combine_rgb_func);
  for (i = 0; i < n_args; i++)
    {
      if (b->texture_combine_rgb_src[i] ==
          COGL_PIPELINE_COMBINE_SOURCE_CONSTANT)
        {
          /* XXX: should we be careful to only hash the alpha
           * component in the COGL_PIPELINE_COMBINE_OP_SRC_ALPHA case? */
          need_hash = TRUE;
          goto done;
        }
    }

  n_args = _cogl_get_n_args_for_combine_func (b->texture_combine_alpha_func);
  for (i = 0; i < n_args; i++)
    {
      if (b->texture_combine_alpha_src[i] ==
          COGL_PIPELINE_COMBINE_SOURCE_CONSTANT)
        {
          /* XXX: should we be careful to only hash the alpha
           * component in the COGL_PIPELINE_COMBINE_OP_SRC_ALPHA case? */
          need_hash = TRUE;
          goto done;
        }
    }

done:
  if (need_hash)
    {
      float *constant = b->texture_combine_constant;
      state->hash = _cogl_util_one_at_a_time_hash (state->hash, constant,
                                                   sizeof (float) * 4);
    }
}

void
_cogl_pipeline_layer_hash_user_matrix_state (CoglPipelineLayer *authority,
                                             CoglPipelineLayer **authorities,
                                             CoglPipelineHashState *state)
{
  CoglPipelineLayerBigState *big_state = authority->big_state;
  state->hash = _cogl_util_one_at_a_time_hash (state->hash, &big_state->matrix,
                                               sizeof (float) * 16);
}

void
_cogl_pipeline_layer_hash_point_sprite_state (CoglPipelineLayer *authority,
                                              CoglPipelineLayer **authorities,
                                              CoglPipelineHashState *state)
{
  CoglPipelineLayerBigState *big_state = authority->big_state;
  state->hash =
    _cogl_util_one_at_a_time_hash (state->hash, &big_state->point_sprite_coords,
                                   sizeof (big_state->point_sprite_coords));
}

void
_cogl_pipeline_layer_hash_vertex_snippets_state (CoglPipelineLayer *authority,
                                                 CoglPipelineLayer **authorities,
                                                 CoglPipelineHashState *state)
{
  _cogl_pipeline_snippet_list_hash (&authority->big_state->vertex_snippets,
                                    &state->hash);
}

void
_cogl_pipeline_layer_hash_fragment_snippets_state (CoglPipelineLayer *authority,
                                                   CoglPipelineLayer **authorities,
                                                   CoglPipelineHashState *state)
{
  _cogl_pipeline_snippet_list_hash (&authority->big_state->fragment_snippets,
                                    &state->hash);
}
