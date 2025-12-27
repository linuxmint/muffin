/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2010,2013 Intel Corporation.
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
 */

#ifndef __COGL_PRIVATE_H__
#define __COGL_PRIVATE_H__

#include <cogl/cogl-pipeline.h>

#include "cogl-context.h"
#include "cogl-flags.h"

G_BEGIN_DECLS

typedef enum
{
  COGL_PRIVATE_FEATURE_TEXTURE_2D_FROM_EGL_IMAGE,
  COGL_PRIVATE_FEATURE_MESA_PACK_INVERT,
  COGL_PRIVATE_FEATURE_PBOS,
  COGL_PRIVATE_FEATURE_EXT_PACKED_DEPTH_STENCIL,
  COGL_PRIVATE_FEATURE_OES_PACKED_DEPTH_STENCIL,
  COGL_PRIVATE_FEATURE_TEXTURE_FORMAT_BGRA8888,
  COGL_PRIVATE_FEATURE_TEXTURE_FORMAT_RGBA1010102,
  COGL_PRIVATE_FEATURE_TEXTURE_FORMAT_HALF_FLOAT,
  COGL_PRIVATE_FEATURE_UNPACK_SUBIMAGE,
  COGL_PRIVATE_FEATURE_SAMPLER_OBJECTS,
  COGL_PRIVATE_FEATURE_READ_PIXELS_ANY_FORMAT,
  COGL_PRIVATE_FEATURE_FORMAT_CONVERSION,
  COGL_PRIVATE_FEATURE_QUERY_FRAMEBUFFER_BITS,
  COGL_PRIVATE_FEATURE_QUERY_TEXTURE_PARAMETERS,
  COGL_PRIVATE_FEATURE_ALPHA_TEXTURES,
  COGL_PRIVATE_FEATURE_TEXTURE_SWIZZLE,
  COGL_PRIVATE_FEATURE_TEXTURE_MAX_LEVEL,
  COGL_PRIVATE_FEATURE_OES_EGL_SYNC,
  /* If this is set then the winsys is responsible for queueing dirty
   * events. Otherwise a dirty event will be queued when the onscreen
   * is first allocated or when it is shown or resized */
  COGL_PRIVATE_FEATURE_DIRTY_EVENTS,
  /* This feature allows for explicitly selecting a GL-based backend,
   * as opposed to nop or (in the future) Vulkan.
   */
  COGL_PRIVATE_FEATURE_ANY_GL,

  COGL_N_PRIVATE_FEATURES
} CoglPrivateFeature;

/* Sometimes when evaluating pipelines, either during comparisons or
 * if calculating a hash value we need to tweak the evaluation
 * semantics */
typedef enum _CoglPipelineEvalFlags
{
  COGL_PIPELINE_EVAL_FLAG_NONE = 0
} CoglPipelineEvalFlags;

void
_cogl_transform_point (const CoglMatrix *matrix_mv,
                       const CoglMatrix *matrix_p,
                       const float *viewport,
                       float *x,
                       float *y);

gboolean
_cogl_check_extension (const char *name, char * const *ext);

void
_cogl_init (void);

#define _cogl_has_private_feature(ctx, feature) \
  COGL_FLAGS_GET ((ctx)->private_features, (feature))

G_END_DECLS

#endif /* __COGL_PRIVATE_H__ */
