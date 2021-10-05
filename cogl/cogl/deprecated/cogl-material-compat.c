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
 * Authors:
 *  Robert Bragg <robert@linux.intel.com>
 */

#include "cogl-config.h"

#include <cogl-pipeline.h>
#include <cogl-pipeline-private.h>
#include <cogl-types.h>
#include <cogl-matrix.h>
#include <cogl-context-private.h>
#include <deprecated/cogl-material-compat.h>

G_DEFINE_BOXED_TYPE (CoglMaterial, cogl_material,
                     cogl_object_ref, cogl_object_unref)

CoglMaterial *
cogl_material_new (void)
{
  _COGL_GET_CONTEXT(ctx, NULL);
  return COGL_MATERIAL (cogl_pipeline_new (ctx));
}

void
cogl_material_set_color (CoglMaterial    *material,
                         const CoglColor *color)
{
  cogl_pipeline_set_color (COGL_PIPELINE (material), color);
}

void
cogl_material_set_color4ub (CoglMaterial *material,
			    uint8_t red,
                            uint8_t green,
                            uint8_t blue,
                            uint8_t alpha)
{
  cogl_pipeline_set_color4ub (COGL_PIPELINE (material),
                              red, green, blue, alpha);
}

gboolean
cogl_material_set_blend (CoglMaterial *material,
                         const char   *blend_string,
                         GError **error)
{
  return cogl_pipeline_set_blend (COGL_PIPELINE (material),
                                  blend_string,
                                  error);
}

void
cogl_material_set_blend_constant (CoglMaterial *material,
                                  const CoglColor *constant_color)
{
  cogl_pipeline_set_blend_constant (COGL_PIPELINE (material), constant_color);
}

void
cogl_material_set_point_size (CoglMaterial *material,
                              float         point_size)
{
  cogl_pipeline_set_point_size (COGL_PIPELINE (material), point_size);
}

void
cogl_material_set_user_program (CoglMaterial *material,
                                CoglHandle program)
{
  cogl_pipeline_set_user_program (COGL_PIPELINE (material), program);
}

void
cogl_material_set_layer (CoglMaterial *material,
			 int           layer_index,
			 CoglHandle    texture)
{
  cogl_pipeline_set_layer_texture (COGL_PIPELINE (material),
                                   layer_index, texture);
}

gboolean
cogl_material_set_layer_combine (CoglMaterial *material,
				 int           layer_index,
				 const char   *blend_string,
                                 GError **error)
{
  return cogl_pipeline_set_layer_combine (COGL_PIPELINE (material),
                                          layer_index,
                                          blend_string,
                                          error);
}

void
cogl_material_set_layer_combine_constant (CoglMaterial    *material,
                                          int              layer_index,
                                          const CoglColor *constant)
{
  cogl_pipeline_set_layer_combine_constant (COGL_PIPELINE (material),
                                            layer_index,
                                            constant);
}

void
cogl_material_set_layer_matrix (CoglMaterial     *material,
				int               layer_index,
				const CoglMatrix *matrix)
{
  cogl_pipeline_set_layer_matrix (COGL_PIPELINE (material),
                                  layer_index, matrix);
}

void
cogl_material_set_layer_filters (CoglMaterial      *material,
                                 int                layer_index,
                                 CoglMaterialFilter min_filter,
                                 CoglMaterialFilter mag_filter)
{
  cogl_pipeline_set_layer_filters (COGL_PIPELINE (material),
                                   layer_index,
                                   min_filter,
                                   mag_filter);
}

gboolean
cogl_material_set_layer_point_sprite_coords_enabled (CoglMaterial *material,
                                                     int           layer_index,
                                                     gboolean      enable,
                                                     GError      **error)
{
  CoglPipeline *pipeline = COGL_PIPELINE (material);
  return cogl_pipeline_set_layer_point_sprite_coords_enabled (pipeline,
                                                              layer_index,
                                                              enable,
                                                              error);
}
