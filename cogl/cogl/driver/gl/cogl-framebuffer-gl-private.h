/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2008,2009,2010,2011 Intel Corporation.
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

#ifndef __COGL_FRAMEBUFFER_GL_PRIVATE_H__
#define __COGL_FRAMEBUFFER_GL_PRIVATE_H__

gboolean
_cogl_offscreen_gl_allocate (CoglOffscreen *offscreen,
                             GError **error);

void
_cogl_offscreen_gl_free (CoglOffscreen *offscreen);

void
_cogl_framebuffer_gl_flush_state (CoglFramebuffer *draw_buffer,
                                  CoglFramebuffer *read_buffer,
                                  CoglFramebufferState state);

void
_cogl_framebuffer_gl_clear (CoglFramebuffer *framebuffer,
                            unsigned long buffers,
                            float red,
                            float green,
                            float blue,
                            float alpha);

void
_cogl_framebuffer_gl_query_bits (CoglFramebuffer *framebuffer,
                                 CoglFramebufferBits *bits);

void
_cogl_framebuffer_gl_finish (CoglFramebuffer *framebuffer);

void
_cogl_framebuffer_gl_flush (CoglFramebuffer *framebuffer);

void
_cogl_framebuffer_gl_discard_buffers (CoglFramebuffer *framebuffer,
                                      unsigned long buffers);

void
_cogl_framebuffer_gl_bind (CoglFramebuffer *framebuffer, GLenum target);

void
_cogl_framebuffer_gl_draw_attributes (CoglFramebuffer *framebuffer,
                                      CoglPipeline *pipeline,
                                      CoglVerticesMode mode,
                                      int first_vertex,
                                      int n_vertices,
                                      CoglAttribute **attributes,
                                      int n_attributes,
                                      CoglDrawFlags flags);

void
_cogl_framebuffer_gl_draw_indexed_attributes (CoglFramebuffer *framebuffer,
                                              CoglPipeline *pipeline,
                                              CoglVerticesMode mode,
                                              int first_vertex,
                                              int n_vertices,
                                              CoglIndices *indices,
                                              CoglAttribute **attributes,
                                              int n_attributes,
                                              CoglDrawFlags flags);

gboolean
_cogl_framebuffer_gl_read_pixels_into_bitmap (CoglFramebuffer *framebuffer,
                                              int x,
                                              int y,
                                              CoglReadPixelsFlags source,
                                              CoglBitmap *bitmap,
                                              GError **error);

#endif /* __COGL_FRAMEBUFFER_GL_PRIVATE_H__ */


