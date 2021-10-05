/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2014 Intel Corporation.
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

#ifndef __COGL_FRAMEBUFFER_DEPRECATED_H__
#define __COGL_FRAMEBUFFER_DEPRECATED_H__

#include <cogl/cogl-framebuffer.h>
#include <cogl/cogl-macros.h>

G_BEGIN_DECLS

/**
 * cogl_set_framebuffer: (skip)
 * @buffer: A #CoglFramebuffer object, either onscreen or offscreen.
 *
 * This redirects all subsequent drawing to the specified framebuffer. This can
 * either be an offscreen buffer created with cogl_offscreen_new_to_texture ()
 * or in the future it may be an onscreen framebuffers too.
 *
 * Since: 1.2
 * Deprecated: 1.16: The latest drawing apis take explicit
 *                   #CoglFramebuffer arguments so this stack of
 *                   framebuffers shouldn't be used anymore.
 */
COGL_DEPRECATED
void
cogl_set_framebuffer (CoglFramebuffer *buffer);

/**
 * cogl_push_framebuffer: (skip)
 * @buffer: A #CoglFramebuffer object, either onscreen or offscreen.
 *
 * Redirects all subsequent drawing to the specified framebuffer. This can
 * either be an offscreen buffer created with cogl_offscreen_new_to_texture ()
 * or in the future it may be an onscreen framebuffer too.
 *
 * You should understand that a framebuffer owns the following state:
 * <itemizedlist>
 *  <listitem><simpara>The projection matrix</simpara></listitem>
 *  <listitem><simpara>The modelview matrix stack</simpara></listitem>
 *  <listitem><simpara>The viewport</simpara></listitem>
 *  <listitem><simpara>The clip stack</simpara></listitem>
 * </itemizedlist>
 * So these items will automatically be saved and restored when you
 * push and pop between different framebuffers.
 *
 * Also remember a newly allocated framebuffer will have an identity matrix for
 * the projection and modelview matrices which gives you a coordinate space
 * like OpenGL with (-1, -1) corresponding to the top left of the viewport,
 * (1, 1) corresponding to the bottom right and +z coming out towards the
 * viewer.
 *
 * If you want to set up a coordinate space like Clutter does with (0, 0)
 * corresponding to the top left and (framebuffer_width, framebuffer_height)
 * corresponding to the bottom right you can do so like this:
 *
 * |[
 * static void
 * setup_viewport (unsigned int width,
 *                 unsigned int height,
 *                 float fovy,
 *                 float aspect,
 *                 float z_near,
 *                 float z_far)
 * {
 *   float z_camera;
 *   CoglMatrix projection_matrix;
 *   CoglMatrix mv_matrix;
 *
 *   cogl_set_viewport (0, 0, width, height);
 *   cogl_perspective (fovy, aspect, z_near, z_far);
 *
 *   cogl_get_projection_matrix (&amp;projection_matrix);
 *   z_camera = 0.5 * projection_matrix.xx;
 *
 *   cogl_matrix_init_identity (&amp;mv_matrix);
 *   cogl_matrix_translate (&amp;mv_matrix, -0.5f, -0.5f, -z_camera);
 *   cogl_matrix_scale (&amp;mv_matrix, 1.0f / width, -1.0f / height, 1.0f / width);
 *   cogl_matrix_translate (&amp;mv_matrix, 0.0f, -1.0 * height, 0.0f);
 *   cogl_set_modelview_matrix (&amp;mv_matrix);
 * }
 *
 * static void
 * my_init_framebuffer (ClutterStage *stage,
 *                      CoglFramebuffer *framebuffer,
 *                      unsigned int framebuffer_width,
 *                      unsigned int framebuffer_height)
 * {
 *   ClutterPerspective perspective;
 *
 *   clutter_stage_get_perspective (stage, &perspective);
 *
 *   cogl_push_framebuffer (framebuffer);
 *   setup_viewport (framebuffer_width,
 *                   framebuffer_height,
 *                   perspective.fovy,
 *                   perspective.aspect,
 *                   perspective.z_near,
 *                   perspective.z_far);
 * }
 * ]|
 *
 * The previous framebuffer can be restored by calling cogl_pop_framebuffer()
 *
 * Since: 1.2
 * Deprecated: 1.16: The latest drawing apis take explicit
 *                   #CoglFramebuffer arguments so this stack of
 *                   framebuffers shouldn't be used anymore.
 */
COGL_DEPRECATED
void
cogl_push_framebuffer (CoglFramebuffer *buffer);

/**
 * cogl_pop_framebuffer: (skip)
 *
 * Restores the framebuffer that was previously at the top of the stack.
 * All subsequent drawing will be redirected to this framebuffer.
 *
 * Since: 1.2
 * Deprecated: 1.16: The latest drawing apis take explicit
 *                   #CoglFramebuffer arguments so this stack of
 *                   framebuffers shouldn't be used anymore.
 */
COGL_DEPRECATED
void
cogl_pop_framebuffer (void);

G_END_DECLS

#endif /* __COGL_FRAMEBUFFER_DEPRECATED_H__ */
