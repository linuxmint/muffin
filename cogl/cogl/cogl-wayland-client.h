/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2024 Linux Mint
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
 *   Michael Webster <miketwebster@gmail.com>
 */

#ifndef __COGL_WAYLAND_CLIENT_H__
#define __COGL_WAYLAND_CLIENT_H__

/* NB: this is a top-level header that can be included directly but we
 * want to be careful not to define __COGL_H_INSIDE__ when this is
 * included internally while building Cogl itself since
 * __COGL_H_INSIDE__ is used in headers to guard public vs private api
 * definitions
 */
#ifndef COGL_COMPILATION

#ifndef __COGL_H_INSIDE__
#define __COGL_H_INSIDE__
#define __COGL_WAYLAND_CLIENT_H_MUST_UNDEF_COGL_H_INSIDE__
#endif

#endif /* COGL_COMPILATION */

#include <cogl/cogl-renderer.h>
#include <cogl/cogl-onscreen.h>

#include <wayland-client.h>

G_BEGIN_DECLS

/**
 * cogl_wayland_renderer_set_foreign_display:
 * @renderer: A #CoglRenderer
 * @display: A Wayland display
 *
 * Allows you to specify a foreign Wayland display for Cogl to use
 * as a Wayland client. This must be called before the renderer is
 * connected.
 *
 * Since: 1.20
 * Stability: Unstable
 */
COGL_EXPORT void
cogl_wayland_renderer_set_foreign_display (CoglRenderer *renderer,
                                           struct wl_display *display);

/**
 * cogl_wayland_renderer_get_display:
 * @renderer: A #CoglRenderer
 *
 * Retrieves the Wayland display that Cogl is using as a client. This
 * may be a foreign display that was set with
 * cogl_wayland_renderer_set_foreign_display() or it will be the
 * display that Cogl created internally.
 *
 * Return value: The Wayland display currently used by Cogl
 *
 * Since: 1.20
 * Stability: Unstable
 */
COGL_EXPORT struct wl_display *
cogl_wayland_renderer_get_display (CoglRenderer *renderer);

/**
 * cogl_wayland_onscreen_get_wl_surface:
 * @onscreen: A #CoglOnscreen
 *
 * Gets the underlying wl_surface for an onscreen framebuffer.
 *
 * Return value: The wl_surface for this onscreen
 *
 * Since: 1.20
 * Stability: Unstable
 */
COGL_EXPORT struct wl_surface *
cogl_wayland_onscreen_get_wl_surface (CoglOnscreen *onscreen);

/**
 * cogl_wayland_onscreen_resize:
 * @onscreen: A #CoglOnscreen
 * @width: New width
 * @height: New height
 * @offset_x: X offset for content
 * @offset_y: Y offset for content
 *
 * Resizes the underlying wl_egl_window for an onscreen framebuffer.
 * This should be called when the Wayland surface is resized.
 *
 * Since: 1.20
 * Stability: Unstable
 */
COGL_EXPORT void
cogl_wayland_onscreen_resize (CoglOnscreen *onscreen,
                              int width,
                              int height,
                              int offset_x,
                              int offset_y);

G_END_DECLS

#ifdef __COGL_WAYLAND_CLIENT_H_MUST_UNDEF_COGL_H_INSIDE__
#undef __COGL_H_INSIDE__
#undef __COGL_WAYLAND_CLIENT_H_MUST_UNDEF_COGL_H_INSIDE__
#endif

#endif /* __COGL_WAYLAND_CLIENT_H__ */
