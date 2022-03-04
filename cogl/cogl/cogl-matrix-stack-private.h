/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2009,2010,2012 Intel Corporation.
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
 *   Havoc Pennington <hp@pobox.com> for litl
 *   Robert Bragg <robert@linux.intel.com>
 */

#ifndef _COGL_MATRIX_STACK_PRIVATE_H_
#define _COGL_MATRIX_STACK_PRIVATE_H_

#include "cogl-object-private.h"
#include "cogl-matrix-stack.h"
#include "cogl-context.h"
#include "cogl-framebuffer.h"

typedef enum _CoglMatrixOp
{
  COGL_MATRIX_OP_LOAD_IDENTITY,
  COGL_MATRIX_OP_TRANSLATE,
  COGL_MATRIX_OP_ROTATE,
  COGL_MATRIX_OP_ROTATE_EULER,
  COGL_MATRIX_OP_SCALE,
  COGL_MATRIX_OP_MULTIPLY,
  COGL_MATRIX_OP_LOAD,
  COGL_MATRIX_OP_SAVE,
} CoglMatrixOp;

struct _CoglMatrixEntry
{
  CoglMatrixEntry *parent;
  CoglMatrixOp op;
  unsigned int ref_count;

#ifdef COGL_DEBUG_ENABLED
  /* used for performance tracing */
  int composite_gets;
#endif
};

typedef struct _CoglMatrixEntryTranslate
{
  CoglMatrixEntry _parent_data;

  graphene_point3d_t translate;

} CoglMatrixEntryTranslate;

typedef struct _CoglMatrixEntryRotate
{
  CoglMatrixEntry _parent_data;

  float angle;
  graphene_vec3_t axis;

} CoglMatrixEntryRotate;

typedef struct _CoglMatrixEntryRotateEuler
{
  CoglMatrixEntry _parent_data;

  graphene_euler_t euler;
} CoglMatrixEntryRotateEuler;

typedef struct _CoglMatrixEntryScale
{
  CoglMatrixEntry _parent_data;

  float x;
  float y;
  float z;

} CoglMatrixEntryScale;

typedef struct _CoglMatrixEntryMultiply
{
  CoglMatrixEntry _parent_data;

  CoglMatrix *matrix;

} CoglMatrixEntryMultiply;

typedef struct _CoglMatrixEntryLoad
{
  CoglMatrixEntry _parent_data;

  CoglMatrix *matrix;

} CoglMatrixEntryLoad;

typedef struct _CoglMatrixEntrySave
{
  CoglMatrixEntry _parent_data;

  CoglMatrix *cache;
  gboolean cache_valid;

} CoglMatrixEntrySave;

typedef union _CoglMatrixEntryFull
{
  CoglMatrixEntry any;
  CoglMatrixEntryTranslate translate;
  CoglMatrixEntryRotate rotate;
  CoglMatrixEntryRotateEuler rotate_euler;
  CoglMatrixEntryScale scale;
  CoglMatrixEntryMultiply multiply;
  CoglMatrixEntryLoad load;
  CoglMatrixEntrySave save;
} CoglMatrixEntryFull;

struct _CoglMatrixStack
{
  CoglObject _parent;

  CoglContext *context;

  CoglMatrixEntry *last_entry;
};

typedef struct _CoglMatrixEntryCache
{
  CoglMatrixEntry *entry;
  gboolean flushed_identity;
  gboolean flipped;
} CoglMatrixEntryCache;

void
_cogl_matrix_entry_identity_init (CoglMatrixEntry *entry);

void
_cogl_matrix_entry_cache_init (CoglMatrixEntryCache *cache);

gboolean
_cogl_matrix_entry_cache_maybe_update (CoglMatrixEntryCache *cache,
                                       CoglMatrixEntry *entry,
                                       gboolean flip);

void
_cogl_matrix_entry_cache_destroy (CoglMatrixEntryCache *cache);

#endif /* _COGL_MATRIX_STACK_PRIVATE_H_ */
