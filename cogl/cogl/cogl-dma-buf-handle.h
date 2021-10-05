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
                         gpointer         data,
                         GDestroyNotify   destroy_func);

/**
 * cogl_dma_buf_handle_free: (skip)
 *
 * Releases @dmabuf_handle; it is a programming error to release
 * an already released handle.
 */
COGL_EXPORT void
cogl_dma_buf_handle_free (CoglDmaBufHandle *dmabuf_handle);

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


#endif /* __COGL_DMA_BUF_HANDLE_H__ */
