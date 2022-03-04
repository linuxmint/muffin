/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009 Intel Corporation.
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

#ifndef __COGL_JOURNAL_PRIVATE_H
#define __COGL_JOURNAL_PRIVATE_H

#include "cogl-texture.h"
#include "cogl-object-private.h"
#include "cogl-clip-stack.h"
#include "cogl-fence-private.h"

#define COGL_JOURNAL_VBO_POOL_SIZE 8

typedef struct _CoglJournal
{
  CoglObject _parent;

  /* A pointer the framebuffer that is using this journal. This is
     only valid when the journal is not empty. It *does* take a
     reference on the framebuffer. Although this creates a circular
     reference, the framebuffer has special code to handle the case
     where the journal is the only thing holding a reference and it
     will cause the journal to flush */
  CoglFramebuffer *framebuffer;

  GArray *entries;
  GArray *vertices;
  size_t needed_vbo_len;

  /* A pool of attribute buffers is used so that we can avoid repeatedly
     reallocating buffers. Only one of these buffers at a time will be
     used by Cogl but we keep more than one alive anyway in case the
     GL driver is internally using the buffer and it would have to
     allocate a new one when we start writing to it */
  CoglAttributeBuffer *vbo_pool[COGL_JOURNAL_VBO_POOL_SIZE];
  /* The next vbo to use from the pool. We just cycle through them in
     order */
  unsigned int next_vbo_in_pool;

  int fast_read_pixel_count;

  CoglList pending_fences;

} CoglJournal;

/* To improve batching of geometry when submitting vertices to OpenGL we
 * log the texture rectangles we want to draw to a journal, so when we
 * later flush the journal we aim to batch data, and gl draw calls. */
typedef struct _CoglJournalEntry
{
  CoglPipeline            *pipeline;
  CoglMatrixEntry         *modelview_entry;
  CoglClipStack           *clip_stack;
  float                    viewport[4];
  gboolean                 dither_enabled;
  /* Offset into ctx->logged_vertices */
  size_t                   array_offset;
  int                      n_layers;
} CoglJournalEntry;

CoglJournal *
_cogl_journal_new (CoglFramebuffer *framebuffer);

void
_cogl_journal_log_quad (CoglJournal  *journal,
                        const float  *position,
                        CoglPipeline *pipeline,
                        int           n_layers,
                        CoglTexture  *layer0_override_texture,
                        const float  *tex_coords,
                        unsigned int  tex_coords_len);

void
_cogl_journal_flush (CoglJournal *journal);

void
_cogl_journal_discard (CoglJournal *journal);

gboolean
_cogl_journal_all_entries_within_bounds (CoglJournal *journal,
                                         float clip_x0,
                                         float clip_y0,
                                         float clip_x1,
                                         float clip_y1);

gboolean
_cogl_journal_try_read_pixel (CoglJournal *journal,
                              int x,
                              int y,
                              CoglBitmap *bitmap,
                              gboolean *found_intersection);

gboolean
_cogl_is_journal (void *object);

#endif /* __COGL_JOURNAL_PRIVATE_H */
