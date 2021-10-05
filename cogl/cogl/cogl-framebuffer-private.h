/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009 Intel Corporation.
 * Copyright (C) 2019 DisplayLink (UK) Ltd.
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

#ifndef __COGL_FRAMEBUFFER_PRIVATE_H
#define __COGL_FRAMEBUFFER_PRIVATE_H

#include "cogl-object-private.h"
#include "cogl-matrix-stack-private.h"
#include "cogl-journal-private.h"
#include "winsys/cogl-winsys-private.h"
#include "cogl-attribute-private.h"
#include "cogl-offscreen.h"
#include "cogl-gl-header.h"
#include "cogl-clip-stack.h"

typedef enum _CoglFramebufferType {
  COGL_FRAMEBUFFER_TYPE_ONSCREEN,
  COGL_FRAMEBUFFER_TYPE_OFFSCREEN
} CoglFramebufferType;

typedef struct
{
  CoglSwapChain *swap_chain;
  gboolean need_stencil;
  int samples_per_pixel;
  gboolean stereo_enabled;
} CoglFramebufferConfig;

/* Flags to pass to _cogl_offscreen_new_with_texture_full */
typedef enum
{
  COGL_OFFSCREEN_DISABLE_DEPTH_AND_STENCIL = 1
} CoglOffscreenFlags;

/* XXX: The order of these indices determines the order they are
 * flushed.
 *
 * Flushing clip state may trash the modelview and projection matrices
 * so we must do it before flushing the matrices.
 */
typedef enum _CoglFramebufferStateIndex
{
  COGL_FRAMEBUFFER_STATE_INDEX_BIND               = 0,
  COGL_FRAMEBUFFER_STATE_INDEX_VIEWPORT           = 1,
  COGL_FRAMEBUFFER_STATE_INDEX_CLIP               = 2,
  COGL_FRAMEBUFFER_STATE_INDEX_DITHER             = 3,
  COGL_FRAMEBUFFER_STATE_INDEX_MODELVIEW          = 4,
  COGL_FRAMEBUFFER_STATE_INDEX_PROJECTION         = 5,
  COGL_FRAMEBUFFER_STATE_INDEX_FRONT_FACE_WINDING = 6,
  COGL_FRAMEBUFFER_STATE_INDEX_DEPTH_WRITE        = 7,
  COGL_FRAMEBUFFER_STATE_INDEX_STEREO_MODE        = 8,
  COGL_FRAMEBUFFER_STATE_INDEX_MAX                = 9
} CoglFramebufferStateIndex;

typedef enum _CoglFramebufferState
{
  COGL_FRAMEBUFFER_STATE_BIND               = 1<<0,
  COGL_FRAMEBUFFER_STATE_VIEWPORT           = 1<<1,
  COGL_FRAMEBUFFER_STATE_CLIP               = 1<<2,
  COGL_FRAMEBUFFER_STATE_DITHER             = 1<<3,
  COGL_FRAMEBUFFER_STATE_MODELVIEW          = 1<<4,
  COGL_FRAMEBUFFER_STATE_PROJECTION         = 1<<5,
  COGL_FRAMEBUFFER_STATE_FRONT_FACE_WINDING = 1<<6,
  COGL_FRAMEBUFFER_STATE_DEPTH_WRITE        = 1<<7,
  COGL_FRAMEBUFFER_STATE_STEREO_MODE        = 1<<8
} CoglFramebufferState;

#define COGL_FRAMEBUFFER_STATE_ALL ((1<<COGL_FRAMEBUFFER_STATE_INDEX_MAX) - 1)

/* Private flags that can internally be added to CoglReadPixelsFlags */
typedef enum
{
  /* If this is set then the data will not be flipped to compensate
     for GL's upside-down coordinate system but instead will be left
     in whatever order GL gives us (which will depend on whether the
     framebuffer is offscreen or not) */
  COGL_READ_PIXELS_NO_FLIP = 1L << 30
} CoglPrivateReadPixelsFlags;

typedef struct
{
  int red;
  int blue;
  int green;
  int alpha;
  int depth;
  int stencil;
} CoglFramebufferBits;

struct _CoglFramebuffer
{
  CoglObject          _parent;
  CoglContext        *context;
  CoglFramebufferType  type;

  /* The user configuration before allocation... */
  CoglFramebufferConfig config;

  int                 width;
  int                 height;
  /* Format of the pixels in the framebuffer (including the expected
     premult state) */
  CoglPixelFormat     internal_format;
  gboolean            allocated;

  CoglMatrixStack    *modelview_stack;
  CoglMatrixStack    *projection_stack;
  float               viewport_x;
  float               viewport_y;
  float               viewport_width;
  float               viewport_height;
  int                 viewport_age;
  int                 viewport_age_for_scissor_workaround;

  CoglClipStack      *clip_stack;

  gboolean            dither_enabled;
  gboolean            depth_writing_enabled;
  CoglStereoMode      stereo_mode;

  /* We journal the textured rectangles we want to submit to OpenGL so
   * we have an oppertunity to batch them together into less draw
   * calls. */
  CoglJournal        *journal;

  /* The scene of a given framebuffer may depend on images in other
   * framebuffers... */
  GList              *deps;

  /* As part of an optimization for reading-back single pixels from a
   * framebuffer in some simple cases where the geometry is still
   * available in the journal we need to track the bounds of the last
   * region cleared, its color and we need to track when something
   * does in fact draw to that region so it is no longer clear.
   */
  float               clear_color_red;
  float               clear_color_green;
  float               clear_color_blue;
  float               clear_color_alpha;
  int                 clear_clip_x0;
  int                 clear_clip_y0;
  int                 clear_clip_x1;
  int                 clear_clip_y1;
  gboolean            clear_clip_dirty;

  /* driver specific */
  gboolean            dirty_bitmasks;
  CoglFramebufferBits bits;

  int                 samples_per_pixel;

  /* Whether the depth buffer was enabled for this framebuffer,
   * usually means it needs to be cleared before being reused next.
   */
  gboolean            depth_buffer_clear_needed;
};

typedef enum
{
  COGL_OFFSCREEN_ALLOCATE_FLAG_DEPTH_STENCIL    = 1L<<0,
  COGL_OFFSCREEN_ALLOCATE_FLAG_DEPTH            = 1L<<1,
  COGL_OFFSCREEN_ALLOCATE_FLAG_STENCIL          = 1L<<2
} CoglOffscreenAllocateFlags;

typedef struct _CoglGLFramebuffer
{
  GLuint fbo_handle;
  GList *renderbuffers;
  int samples_per_pixel;
} CoglGLFramebuffer;

struct _CoglOffscreen
{
  CoglFramebuffer  _parent;

  CoglGLFramebuffer gl_framebuffer;

  CoglTexture    *texture;
  int             texture_level;

  CoglTexture *depth_texture;

  CoglOffscreenAllocateFlags allocation_flags;

  /* FIXME: _cogl_offscreen_new_with_texture_full should be made to use
   * fb->config to configure if we want a depth or stencil buffer so
   * we can get rid of these flags */
  CoglOffscreenFlags create_flags;
};

void
_cogl_framebuffer_init (CoglFramebuffer *framebuffer,
                        CoglContext *ctx,
                        CoglFramebufferType type,
                        int width,
                        int height);

/* XXX: For a public api we might instead want a way to explicitly
 * set the _premult status of a framebuffer or what components we
 * care about instead of exposing the CoglPixelFormat
 * internal_format.
 *
 * The current use case for this api is where we create an offscreen
 * framebuffer for a shared atlas texture that has a format of
 * RGBA_8888 disregarding the premultiplied alpha status for
 * individual atlased textures or whether the alpha component is being
 * discarded. We want to overried the internal_format that will be
 * derived from the texture.
 */
void
_cogl_framebuffer_set_internal_format (CoglFramebuffer *framebuffer,
                                       CoglPixelFormat internal_format);

void _cogl_framebuffer_free (CoglFramebuffer *framebuffer);

const CoglWinsysVtable *
_cogl_framebuffer_get_winsys (CoglFramebuffer *framebuffer);

void
_cogl_framebuffer_clear_without_flush4f (CoglFramebuffer *framebuffer,
                                         unsigned long buffers,
                                         float red,
                                         float green,
                                         float blue,
                                         float alpha);

void
_cogl_framebuffer_mark_clear_clip_dirty (CoglFramebuffer *framebuffer);

/*
 * _cogl_framebuffer_get_clip_stack:
 * @framebuffer: A #CoglFramebuffer
 *
 * Gets a pointer to the current clip stack. A reference is not taken on the
 * stack so if you want to keep it you should call
 * _cogl_clip_stack_ref().
 *
 * Return value: a pointer to the @framebuffer clip stack.
 */
CoglClipStack *
_cogl_framebuffer_get_clip_stack (CoglFramebuffer *framebuffer);

COGL_EXPORT CoglMatrixStack *
_cogl_framebuffer_get_modelview_stack (CoglFramebuffer *framebuffer);

COGL_EXPORT CoglMatrixStack *
_cogl_framebuffer_get_projection_stack (CoglFramebuffer *framebuffer);

void
_cogl_framebuffer_add_dependency (CoglFramebuffer *framebuffer,
                                  CoglFramebuffer *dependency);

void
_cogl_framebuffer_flush_journal (CoglFramebuffer *framebuffer);

void
_cogl_framebuffer_flush_dependency_journals (CoglFramebuffer *framebuffer);

void
_cogl_framebuffer_flush_state (CoglFramebuffer *draw_buffer,
                               CoglFramebuffer *read_buffer,
                               CoglFramebufferState state);

CoglFramebuffer *
_cogl_get_read_framebuffer (void);

GSList *
_cogl_create_framebuffer_stack (void);

void
_cogl_free_framebuffer_stack (GSList *stack);

/*
 * _cogl_offscreen_new_with_texture_full:
 * @texture: A #CoglTexture pointer
 * @create_flags: Flags specifying how to create the FBO
 * @level: The mipmap level within the texture to target
 *
 * Creates a new offscreen buffer which will target the given
 * texture. By default the buffer will have a depth and stencil
 * buffer. This can be disabled by passing
 * %COGL_OFFSCREEN_DISABLE_DEPTH_AND_STENCIL in @create_flags.
 *
 * Return value: the new CoglOffscreen object.
 */
CoglOffscreen *
_cogl_offscreen_new_with_texture_full (CoglTexture *texture,
                                       CoglOffscreenFlags create_flags,
                                       int level);

void
_cogl_framebuffer_save_clip_stack (CoglFramebuffer *framebuffer);

void
_cogl_framebuffer_restore_clip_stack (CoglFramebuffer *framebuffer);

void
_cogl_framebuffer_unref (CoglFramebuffer *framebuffer);

/* This can be called directly by the CoglJournal to draw attributes
 * skipping the implicit journal flush, the framebuffer flush and
 * pipeline validation. */
void
_cogl_framebuffer_draw_attributes (CoglFramebuffer *framebuffer,
                                   CoglPipeline *pipeline,
                                   CoglVerticesMode mode,
                                   int first_vertex,
                                   int n_vertices,
                                   CoglAttribute **attributes,
                                   int n_attributes,
                                   CoglDrawFlags flags);

void
_cogl_framebuffer_draw_indexed_attributes (CoglFramebuffer *framebuffer,
                                           CoglPipeline *pipeline,
                                           CoglVerticesMode mode,
                                           int first_vertex,
                                           int n_vertices,
                                           CoglIndices *indices,
                                           CoglAttribute **attributes,
                                           int n_attributes,
                                           CoglDrawFlags flags);

void
cogl_framebuffer_set_viewport4fv (CoglFramebuffer *framebuffer,
                                  float *viewport);

unsigned long
_cogl_framebuffer_compare (CoglFramebuffer *a,
                           CoglFramebuffer *b,
                           unsigned long state);

static inline CoglMatrixEntry *
_cogl_framebuffer_get_modelview_entry (CoglFramebuffer *framebuffer)
{
  CoglMatrixStack *modelview_stack =
    _cogl_framebuffer_get_modelview_stack (framebuffer);
  return modelview_stack->last_entry;
}

static inline CoglMatrixEntry *
_cogl_framebuffer_get_projection_entry (CoglFramebuffer *framebuffer)
{
  CoglMatrixStack *projection_stack =
    _cogl_framebuffer_get_projection_stack (framebuffer);
  return projection_stack->last_entry;
}

gboolean
_cogl_framebuffer_read_pixels_into_bitmap (CoglFramebuffer *framebuffer,
                                           int x,
                                           int y,
                                           CoglReadPixelsFlags source,
                                           CoglBitmap *bitmap,
                                           GError **error);

/*
 * _cogl_framebuffer_get_stencil_bits:
 * @framebuffer: a pointer to a #CoglFramebuffer
 *
 * Retrieves the number of stencil bits of @framebuffer
 *
 * Return value: the number of bits
 *
 * Since: 2.0
 * Stability: unstable
 */
COGL_EXPORT int
_cogl_framebuffer_get_stencil_bits (CoglFramebuffer *framebuffer);

#endif /* __COGL_FRAMEBUFFER_PRIVATE_H */
