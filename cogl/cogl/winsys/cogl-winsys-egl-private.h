/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2011 Intel Corporation.
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

#ifndef __COGL_WINSYS_EGL_PRIVATE_H
#define __COGL_WINSYS_EGL_PRIVATE_H

#include "cogl-defines.h"
#include "cogl-context.h"
#include "cogl-context-private.h"
#include "cogl-framebuffer-private.h"
#include "winsys/cogl-winsys-private.h"

/* XXX: depending on what version of Mesa you have then
 * eglQueryWaylandBuffer may take a wl_buffer or wl_resource argument
 * and the EGL header will only forward declare the corresponding
 * type.
 *
 * The use of wl_buffer has been deprecated and so internally we
 * assume that eglQueryWaylandBuffer takes a wl_resource but for
 * compatibility we forward declare wl_resource in case we are
 * building with EGL headers that still use wl_buffer.
 *
 * Placing the forward declaration here means it comes before we
 * #include cogl-winsys-egl-feature-functions.h bellow which
 * declares lots of function pointers for accessing EGL extensions
 * and cogl-winsys-egl.c will include this header before it also
 * includes cogl-winsys-egl-feature-functions.h that may depend
 * on this type.
 */
#ifdef EGL_WL_bind_wayland_display
struct wl_resource;
#endif

typedef struct _CoglWinsysEGLVtable
{
  gboolean
  (* display_setup) (CoglDisplay *display,
                     GError **error);
  void
  (* display_destroy) (CoglDisplay *display);

  gboolean
  (* context_created) (CoglDisplay *display,
                       GError **error);

  void
  (* cleanup_context) (CoglDisplay *display);

  gboolean
  (* context_init) (CoglContext *context, GError **error);

  void
  (* context_deinit) (CoglContext *context);

  gboolean
  (* onscreen_init) (CoglOnscreen *onscreen,
                     EGLConfig config,
                     GError **error);
  void
  (* onscreen_deinit) (CoglOnscreen *onscreen);

  int
  (* add_config_attributes) (CoglDisplay *display,
                             CoglFramebufferConfig *config,
                             EGLint *attributes);
  gboolean
  (* choose_config) (CoglDisplay *display,
                     EGLint *attributes,
                     EGLConfig *out_config,
                     GError **error);
} CoglWinsysEGLVtable;

typedef enum _CoglEGLWinsysFeature
{
  COGL_EGL_WINSYS_FEATURE_SWAP_REGION                   =1L<<0,
  COGL_EGL_WINSYS_FEATURE_EGL_IMAGE_FROM_X11_PIXMAP     =1L<<1,
  COGL_EGL_WINSYS_FEATURE_EGL_IMAGE_FROM_WAYLAND_BUFFER =1L<<2,
  COGL_EGL_WINSYS_FEATURE_CREATE_CONTEXT                =1L<<3,
  COGL_EGL_WINSYS_FEATURE_BUFFER_AGE                    =1L<<4,
  COGL_EGL_WINSYS_FEATURE_FENCE_SYNC                    =1L<<5,
  COGL_EGL_WINSYS_FEATURE_SURFACELESS_CONTEXT           =1L<<6,
  COGL_EGL_WINSYS_FEATURE_CONTEXT_PRIORITY              =1L<<7,
} CoglEGLWinsysFeature;

typedef struct _CoglRendererEGL
{
  CoglEGLWinsysFeature private_features;

  EGLDisplay edpy;

  EGLint egl_version_major;
  EGLint egl_version_minor;

  CoglClosure *resize_notify_idle;

  /* Data specific to the EGL platform */
  void *platform;
  /* vtable for platform specific parts */
  const CoglWinsysEGLVtable *platform_vtable;

  /* Function pointers for EGL specific extensions */
#define COGL_WINSYS_FEATURE_BEGIN(a, b, c, d)

#define COGL_WINSYS_FEATURE_FUNCTION(ret, name, args) \
  ret (APIENTRY * pf_ ## name) args;

#define COGL_WINSYS_FEATURE_END()

#include "winsys/cogl-winsys-egl-feature-functions.h"

#undef COGL_WINSYS_FEATURE_BEGIN
#undef COGL_WINSYS_FEATURE_FUNCTION
#undef COGL_WINSYS_FEATURE_END
} CoglRendererEGL;

typedef struct _CoglDisplayEGL
{
  EGLContext egl_context;
  EGLSurface dummy_surface;
  EGLSurface egl_surface;

  EGLConfig egl_config;
  gboolean found_egl_config;

  EGLSurface current_read_surface;
  EGLSurface current_draw_surface;
  EGLContext current_context;

  /* Platform specific display data */
  void *platform;
} CoglDisplayEGL;

typedef struct _CoglContextEGL
{
  EGLSurface saved_draw_surface;
  EGLSurface saved_read_surface;
} CoglContextEGL;

typedef struct _CoglOnscreenEGL
{
  EGLSurface egl_surface;

  gboolean pending_resize_notify;

  /* Platform specific data */
  void *platform;
} CoglOnscreenEGL;

COGL_EXPORT const CoglWinsysVtable *
_cogl_winsys_egl_get_vtable (void);

COGL_EXPORT EGLBoolean
_cogl_winsys_egl_make_current (CoglDisplay *display,
                               EGLSurface draw,
                               EGLSurface read,
                               EGLContext context);

COGL_EXPORT EGLBoolean
_cogl_winsys_egl_ensure_current (CoglDisplay *display);

#ifdef EGL_KHR_image_base
EGLImageKHR
_cogl_egl_create_image (CoglContext *ctx,
                        EGLenum target,
                        EGLClientBuffer buffer,
                        const EGLint *attribs);

void
_cogl_egl_destroy_image (CoglContext *ctx,
                         EGLImageKHR image);
#endif

#ifdef EGL_WL_bind_wayland_display
gboolean
_cogl_egl_query_wayland_buffer (CoglContext *ctx,
                                struct wl_resource *buffer,
                                int attribute,
                                int *value);
#endif

COGL_EXPORT gboolean
_cogl_winsys_egl_renderer_connect_common (CoglRenderer *renderer,
                                          GError **error);

#endif /* __COGL_WINSYS_EGL_PRIVATE_H */
