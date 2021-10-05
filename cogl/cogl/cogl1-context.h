/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2010 Intel Corporation.
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
 *  Robert Bragg <robert@linux.intel.com>
 *
 */

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#ifndef __COGL_1_CONTEXT_H__
#define __COGL_1_CONTEXT_H__

#include <cogl/cogl-types.h>
#include <cogl/cogl-texture.h>
#include <cogl/cogl-framebuffer.h>
#include <cogl/cogl-macros.h>

G_BEGIN_DECLS

/**
 * cogl_get_option_group:
 *
 * Retrieves the #GOptionGroup used by Cogl to parse the command
 * line options. Clutter uses this to handle the Cogl command line
 * options during its initialization process.
 *
 * Return value: a #GOptionGroup
 *
 * Since: 1.0
 * Deprecated: 1.16: Not replaced
 */
COGL_DEPRECATED
COGL_EXPORT GOptionGroup *
cogl_get_option_group (void);

/* Misc */
/**
 * cogl_get_proc_address: (skip)
 * @name: the name of the function.
 *
 * Gets a pointer to a given GL or GL ES extension function. This acts
 * as a wrapper around glXGetProcAddress() or whatever is the
 * appropriate function for the current backend.
 *
 * <note>This function should not be used to query core opengl API
 * symbols since eglGetProcAddress for example doesn't allow this and
 * and may return a junk pointer if you do.</note>
 *
 * Return value: a pointer to the requested function or %NULL if the
 *   function is not available.
 */
COGL_EXPORT GCallback
cogl_get_proc_address (const char *name);

/**
 * cogl_set_depth_test_enabled:
 * @setting: %TRUE to enable depth testing or %FALSE to disable.
 *
 * Sets whether depth testing is enabled. If it is disabled then the
 * order that actors are layered on the screen depends solely on the
 * order specified using clutter_actor_raise() and
 * clutter_actor_lower(), otherwise it will also take into account the
 * actor's depth. Depth testing is disabled by default.
 *
 * Deprecated: 1.16: Use cogl_pipeline_set_depth_state() instead
 */
COGL_DEPRECATED_FOR (cogl_pipeline_set_depth_state)
COGL_EXPORT void
cogl_set_depth_test_enabled (gboolean setting);

/**
 * cogl_get_depth_test_enabled:
 *
 * Queries if depth testing has been enabled via cogl_set_depth_test_enable()
 *
 * Return value: %TRUE if depth testing is enabled, and %FALSE otherwise
 *
 * Deprecated: 1.16: Use cogl_pipeline_set_depth_state() instead
 */
COGL_DEPRECATED_FOR (cogl_pipeline_set_depth_state)
COGL_EXPORT gboolean
cogl_get_depth_test_enabled (void);

/**
 * cogl_set_backface_culling_enabled:
 * @setting: %TRUE to enable backface culling or %FALSE to disable.
 *
 * Sets whether textures positioned so that their backface is showing
 * should be hidden. This can be used to efficiently draw two-sided
 * textures or fully closed cubes without enabling depth testing. This
 * only affects calls to the cogl_rectangle* family of functions and
 * cogl_vertex_buffer_draw*. Backface culling is disabled by default.
 *
 * Deprecated: 1.16: Use cogl_pipeline_set_cull_face_mode() instead
 */
COGL_DEPRECATED_FOR (cogl_pipeline_set_cull_face_mode)
COGL_EXPORT void
cogl_set_backface_culling_enabled (gboolean setting);

/**
 * cogl_get_backface_culling_enabled:
 *
 * Queries if backface culling has been enabled via
 * cogl_set_backface_culling_enabled()
 *
 * Return value: %TRUE if backface culling is enabled, and %FALSE otherwise
 *
 * Deprecated: 1.16: Use cogl_pipeline_get_cull_face_mode() instead
 */
COGL_DEPRECATED_FOR (cogl_pipeline_get_cull_face_mode)
COGL_EXPORT gboolean
cogl_get_backface_culling_enabled (void);

/**
 * cogl_flush:
 *
 * This function should only need to be called in exceptional circumstances.
 *
 * As an optimization Cogl drawing functions may batch up primitives
 * internally, so if you are trying to use raw GL outside of Cogl you stand a
 * better chance of being successful if you ask Cogl to flush any batched
 * geometry before making your state changes.
 *
 * It only ensure that the underlying driver is issued all the commands
 * necessary to draw the batched primitives. It provides no guarantees about
 * when the driver will complete the rendering.
 *
 * This provides no guarantees about the GL state upon returning and to avoid
 * confusing Cogl you should aim to restore any changes you make before
 * resuming use of Cogl.
 *
 * If you are making state changes with the intention of affecting Cogl drawing
 * primitives you are 100% on your own since you stand a good chance of
 * conflicting with Cogl internals. For example clutter-gst which currently
 * uses direct GL calls to bind ARBfp programs will very likely break when Cogl
 * starts to use ARBfb programs itself for the material API.
 *
 * Since: 1.0
 */
COGL_EXPORT void
cogl_flush (void);

G_END_DECLS

#endif /* __COGL_1_CONTEXT_H__ */
