/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2020 Endless, Inc.
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
 *   Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
 */


#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#ifndef __COGL_DMA_BUF_HANDLE_H__
#define __COGL_DMA_BUF_HANDLE_H__

#include <cogl/cogl-types.h>
#include <cogl/cogl-framebuffer.h>

/**
 * cogl_dma_buf_handle_new: (skip)
 */
COGL_EXPORT CoglDmaBufHandle *
cogl_dma_buf_handle_new (CoglFramebuffer *framebuffer,
                         int              dmabuf_fd,
                         int              width,
                         int              height,
                         int              stride,
                         int              offset,
                         int              bpp,
                         gpointer         user_data,
                         GDestroyNotify   destroy_func);

/**
 * cogl_dma_buf_handle_free: (skip)
 *
 * Releases @dmabuf_handle; it is a programming error to release
 * an already released handle.
 */
COGL_EXPORT void
cogl_dma_buf_handle_free (CoglDmaBufHandle *dmabuf_handle);

COGL_EXPORT gboolean
cogl_dma_buf_handle_sync_read_start (CoglDmaBufHandle  *dmabuf_handle,
                                     GError           **error);

COGL_EXPORT gboolean
cogl_dma_buf_handle_sync_read_end (CoglDmaBufHandle  *dmabuf_handle,
                                   GError           **error);

COGL_EXPORT gpointer
cogl_dma_buf_handle_mmap (CoglDmaBufHandle  *dmabuf_handle,
                          GError           **error);

COGL_EXPORT gboolean
cogl_dma_buf_handle_munmap (CoglDmaBufHandle  *dmabuf_handle,
                            gpointer           data,
                            GError           **error);

/**
 * cogl_dma_buf_handle_get_framebuffer: (skip)
 *
 * Retrieves the #CoglFramebuffer, backed by an exported DMABuf buffer,
 * of @dmabuf_handle.
 *
 * Returns: (transfer none): a #CoglFramebuffer
 */
COGL_EXPORT CoglFramebuffer *
cogl_dma_buf_handle_get_framebuffer (CoglDmaBufHandle *dmabuf_handle);

/**
 * cogl_dma_buf_handle_get_fd: (skip)
 *
 * Retrieves the file descriptor of @dmabuf_handle.
 *
 * Returns: a valid file descriptor
 */
COGL_EXPORT int
cogl_dma_buf_handle_get_fd (CoglDmaBufHandle *dmabuf_handle);

/**
 * cogl_dmabuf_handle_get_width: (skip)
 *
 * Returns: the buffer width
 */
COGL_EXPORT int
cogl_dma_buf_handle_get_width (CoglDmaBufHandle *dmabuf_handle);

/**
 * cogl_dmabuf_handle_get_height: (skip)
 *
 * Returns: the buffer height
 */
COGL_EXPORT int
cogl_dma_buf_handle_get_height (CoglDmaBufHandle *dmabuf_handle);

/**
 * cogl_dmabuf_handle_get_stride: (skip)
 *
 * Returns: the buffer stride
 */
COGL_EXPORT int
cogl_dma_buf_handle_get_stride (CoglDmaBufHandle *dmabuf_handle);

/**
 * cogl_dmabuf_handle_get_offset: (skip)
 *
 * Returns: the buffer offset
 */
COGL_EXPORT int
cogl_dma_buf_handle_get_offset (CoglDmaBufHandle *dmabuf_handle);

/**
 * cogl_dmabuf_handle_get_bpp: (skip)
 *
 * Returns: the number of bytes per pixel
 */
COGL_EXPORT int
cogl_dma_buf_handle_get_bpp (CoglDmaBufHandle *dmabuf_handle);


#endif /* __COGL_DMA_BUF_HANDLE_H__ */
