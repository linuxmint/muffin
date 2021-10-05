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

#include "cogl-config.h"

#include "cogl-dma-buf-handle.h"
#include "cogl-object.h"

#include <unistd.h>

struct _CoglDmaBufHandle
{
  CoglFramebuffer *framebuffer;
  int dmabuf_fd;
  gpointer user_data;
  GDestroyNotify destroy_func;
};

CoglDmaBufHandle *
cogl_dma_buf_handle_new (CoglFramebuffer *framebuffer,
                         int              dmabuf_fd,
                         gpointer         user_data,
                         GDestroyNotify   destroy_func)
{
  CoglDmaBufHandle *dmabuf_handle;

  g_assert (framebuffer);
  g_assert (dmabuf_fd != -1);

  dmabuf_handle = g_new0 (CoglDmaBufHandle, 1);
  dmabuf_handle->framebuffer = cogl_object_ref (framebuffer);
  dmabuf_handle->dmabuf_fd = dmabuf_fd;
  dmabuf_handle->user_data = user_data;
  dmabuf_handle->destroy_func = destroy_func;

  return dmabuf_handle;
}

void
cogl_dma_buf_handle_free (CoglDmaBufHandle *dmabuf_handle)
{
  g_return_if_fail (dmabuf_handle != NULL);

  g_clear_pointer (&dmabuf_handle->framebuffer, cogl_object_unref);

  if (dmabuf_handle->destroy_func)
    g_clear_pointer (&dmabuf_handle->user_data, dmabuf_handle->destroy_func);

  if (dmabuf_handle->dmabuf_fd != -1)
    close (dmabuf_handle->dmabuf_fd);

  g_free (dmabuf_handle);
}

CoglFramebuffer *
cogl_dma_buf_handle_get_framebuffer (CoglDmaBufHandle *dmabuf_handle)
{
  return dmabuf_handle->framebuffer;
}

int
cogl_dma_buf_handle_get_fd (CoglDmaBufHandle *dmabuf_handle)
{
  return dmabuf_handle->dmabuf_fd;
}

