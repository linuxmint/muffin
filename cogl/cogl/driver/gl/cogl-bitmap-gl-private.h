/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007 OpenedHand
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

#ifndef __COGL_BITMAP_GL_PRIVATE_H
#define __COGL_BITMAP_GL_PRIVATE_H

#include "cogl-bitmap-private.h"

/* These two are replacements for map and unmap that should used when
 * the pointer is going to be passed to GL for pixel packing or
 * unpacking. The address might not be valid for reading if the bitmap
 * was created with new_from_buffer but it will however be good to
 * pass to glTexImage2D for example. The access should be READ for
 * unpacking and WRITE for packing. It can not be both
 */
uint8_t *
_cogl_bitmap_gl_bind (CoglBitmap *bitmap,
                      CoglBufferAccess access,
                      CoglBufferMapHint hints,
                      GError **error);

void
_cogl_bitmap_gl_unbind (CoglBitmap *bitmap);

#endif /* __COGL_BITMAP_GL_PRIVATE_H */
