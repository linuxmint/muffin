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
 *
 * Wayland client winsys for Cogl - allows Cogl to run as a Wayland client
 */

#include "cogl-config.h"

#include <wayland-client.h>
#include <wayland-egl.h>
#include <string.h>

/* EGL Wayland platform defines - may not be in all EGL headers */
#ifndef EGL_PLATFORM_WAYLAND_KHR
#define EGL_PLATFORM_WAYLAND_KHR 0x31D8
#endif
#ifndef EGL_PLATFORM_WAYLAND_EXT
#define EGL_PLATFORM_WAYLAND_EXT 0x31D8
#endif

#include "cogl-wayland-client.h"
#include "cogl-macros.h"
#include "winsys/cogl-winsys-egl-wayland-private.h"
#include "winsys/cogl-winsys-egl-private.h"
#include "cogl-renderer-private.h"
#include "cogl-onscreen-private.h"
#include "cogl-framebuffer-private.h"
#include "cogl-display-private.h"
#include "cogl-private.h"

static const CoglWinsysEGLVtable _cogl_winsys_egl_vtable;

typedef struct _CoglRendererWayland
{
    struct wl_display *wl_display;
    struct wl_registry *wl_registry;
    struct wl_compositor *wl_compositor;
    gboolean own_display;
} CoglRendererWayland;

typedef struct _CoglDisplayWayland
{
    struct wl_surface *dummy_surface;
    struct wl_egl_window *dummy_egl_window;
} CoglDisplayWayland;

typedef struct _CoglOnscreenWayland
{
    struct wl_surface *wl_surface;
    struct wl_egl_window *wl_egl_window;
    int pending_width;
    int pending_height;
} CoglOnscreenWayland;

/* Registry listener */
static void
registry_global (void *data,
                 struct wl_registry *registry,
                 uint32_t name,
                 const char *interface,
                 uint32_t version)
{
    CoglRendererWayland *wayland_renderer = data;

    if (strcmp (interface, "wl_compositor") == 0)
    {
        wayland_renderer->wl_compositor =
            wl_registry_bind (registry, name, &wl_compositor_interface,
                              MIN ((uint32_t) version, 4));
    }
}

static void
registry_global_remove (void *data,
                        struct wl_registry *registry,
                        uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    registry_global_remove
};

static void
_cogl_winsys_renderer_disconnect (CoglRenderer *renderer)
{
    CoglRendererEGL *egl_renderer = renderer->winsys;
    CoglRendererWayland *wayland_renderer = egl_renderer->platform;

    eglTerminate (egl_renderer->edpy);

    if (wayland_renderer->wl_compositor)
        wl_compositor_destroy (wayland_renderer->wl_compositor);

    if (wayland_renderer->wl_registry)
        wl_registry_destroy (wayland_renderer->wl_registry);

    if (wayland_renderer->own_display && wayland_renderer->wl_display)
        wl_display_disconnect (wayland_renderer->wl_display);

    g_free (wayland_renderer);

    g_slice_free (CoglRendererEGL, egl_renderer);
}

static EGLDisplay
_cogl_winsys_egl_get_display (void *native)
{
    EGLDisplay dpy = NULL;
    const char *client_exts = eglQueryString (NULL, EGL_EXTENSIONS);

    if (client_exts && g_strstr_len (client_exts, -1, "EGL_KHR_platform_base"))
    {
        PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
            (void *) eglGetProcAddress ("eglGetPlatformDisplay");

        if (get_platform_display)
            dpy = get_platform_display (EGL_PLATFORM_WAYLAND_KHR, native, NULL);

        if (dpy)
            return dpy;
    }

    if (client_exts && g_strstr_len (client_exts, -1, "EGL_EXT_platform_base"))
    {
        PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
            (void *) eglGetProcAddress ("eglGetPlatformDisplayEXT");

        if (get_platform_display)
            dpy = get_platform_display (EGL_PLATFORM_WAYLAND_EXT, native, NULL);

        if (dpy)
            return dpy;
    }

    return eglGetDisplay ((EGLNativeDisplayType) native);
}

static gboolean
_cogl_winsys_renderer_connect (CoglRenderer *renderer,
                               GError **error)
{
    CoglRendererEGL *egl_renderer;
    CoglRendererWayland *wayland_renderer;

    renderer->winsys = g_slice_new0 (CoglRendererEGL);
    egl_renderer = renderer->winsys;
    wayland_renderer = g_new0 (CoglRendererWayland, 1);
    egl_renderer->platform = wayland_renderer;
    egl_renderer->platform_vtable = &_cogl_winsys_egl_vtable;

    /* Check for foreign display */
    wayland_renderer->wl_display = renderer->foreign_wayland_display;
    if (wayland_renderer->wl_display)
    {
        wayland_renderer->own_display = FALSE;
    }
    else
    {
        wayland_renderer->wl_display = wl_display_connect (NULL);
        wayland_renderer->own_display = TRUE;
    }

    if (!wayland_renderer->wl_display)
    {
        g_set_error (error, COGL_WINSYS_ERROR,
                     COGL_WINSYS_ERROR_INIT,
                     "Failed to connect to Wayland display");
        goto error;
    }

    /* Get registry and bind compositor */
    wayland_renderer->wl_registry =
        wl_display_get_registry (wayland_renderer->wl_display);
    wl_registry_add_listener (wayland_renderer->wl_registry,
                              &registry_listener,
                              wayland_renderer);
    wl_display_roundtrip (wayland_renderer->wl_display);

    if (!wayland_renderer->wl_compositor)
    {
        g_set_error (error, COGL_WINSYS_ERROR,
                     COGL_WINSYS_ERROR_INIT,
                     "wl_compositor not available");
        goto error;
    }

    egl_renderer->edpy = _cogl_winsys_egl_get_display (wayland_renderer->wl_display);

    if (!_cogl_winsys_egl_renderer_connect_common (renderer, error))
        goto error;

    return TRUE;

error:
    _cogl_winsys_renderer_disconnect (renderer);
    return FALSE;
}

static int
_cogl_winsys_egl_add_config_attributes (CoglDisplay *display,
                                        CoglFramebufferConfig *config,
                                        EGLint *attributes)
{
    int i = 0;

    attributes[i++] = EGL_SURFACE_TYPE;
    attributes[i++] = EGL_WINDOW_BIT;

    return i;
}

static gboolean
_cogl_winsys_egl_choose_config (CoglDisplay *display,
                                EGLint *attributes,
                                EGLConfig *out_config,
                                GError **error)
{
    CoglRenderer *renderer = display->renderer;
    CoglRendererEGL *egl_renderer = renderer->winsys;
    EGLint config_count = 0;
    EGLBoolean status;

    status = eglChooseConfig (egl_renderer->edpy,
                              attributes,
                              out_config, 1,
                              &config_count);
    if (status != EGL_TRUE || config_count == 0)
    {
        g_set_error (error, COGL_WINSYS_ERROR,
                     COGL_WINSYS_ERROR_CREATE_CONTEXT,
                     "No compatible EGL configs found");
        return FALSE;
    }

    return TRUE;
}

static gboolean
_cogl_winsys_egl_display_setup (CoglDisplay *display,
                                GError **error)
{
    CoglDisplayEGL *egl_display = display->winsys;
    CoglDisplayWayland *wayland_display;

    wayland_display = g_slice_new0 (CoglDisplayWayland);
    egl_display->platform = wayland_display;

    return TRUE;
}

static void
_cogl_winsys_egl_display_destroy (CoglDisplay *display)
{
    CoglDisplayEGL *egl_display = display->winsys;
    CoglDisplayWayland *wayland_display = egl_display->platform;

    g_slice_free (CoglDisplayWayland, wayland_display);
}

static gboolean
_cogl_winsys_egl_context_created (CoglDisplay *display,
                                  GError **error)
{
    CoglRenderer *renderer = display->renderer;
    CoglRendererEGL *egl_renderer = renderer->winsys;
    CoglRendererWayland *wayland_renderer = egl_renderer->platform;
    CoglDisplayEGL *egl_display = display->winsys;
    CoglDisplayWayland *wayland_display = egl_display->platform;

    /* Create dummy surface for context */
    wayland_display->dummy_surface =
        wl_compositor_create_surface (wayland_renderer->wl_compositor);
    if (!wayland_display->dummy_surface)
    {
        g_set_error (error, COGL_WINSYS_ERROR,
                     COGL_WINSYS_ERROR_CREATE_CONTEXT,
                     "Failed to create dummy wl_surface");
        return FALSE;
    }

    wayland_display->dummy_egl_window =
        wl_egl_window_create (wayland_display->dummy_surface, 1, 1);
    if (!wayland_display->dummy_egl_window)
    {
        g_set_error (error, COGL_WINSYS_ERROR,
                     COGL_WINSYS_ERROR_CREATE_CONTEXT,
                     "Failed to create dummy wl_egl_window");
        return FALSE;
    }

    egl_display->dummy_surface =
        eglCreateWindowSurface (egl_renderer->edpy,
                                egl_display->egl_config,
                                (EGLNativeWindowType) wayland_display->dummy_egl_window,
                                NULL);
    if (egl_display->dummy_surface == EGL_NO_SURFACE)
    {
        g_set_error (error, COGL_WINSYS_ERROR,
                     COGL_WINSYS_ERROR_CREATE_CONTEXT,
                     "Failed to create dummy EGL surface");
        return FALSE;
    }

    if (!_cogl_winsys_egl_make_current (display,
                                        egl_display->dummy_surface,
                                        egl_display->dummy_surface,
                                        egl_display->egl_context))
    {
        g_set_error (error, COGL_WINSYS_ERROR,
                     COGL_WINSYS_ERROR_CREATE_CONTEXT,
                     "Failed to make context current");
        return FALSE;
    }

    return TRUE;
}

static void
_cogl_winsys_egl_cleanup_context (CoglDisplay *display)
{
    CoglRenderer *renderer = display->renderer;
    CoglRendererEGL *egl_renderer = renderer->winsys;
    CoglDisplayEGL *egl_display = display->winsys;
    CoglDisplayWayland *wayland_display = egl_display->platform;

    if (egl_display->dummy_surface != EGL_NO_SURFACE)
    {
        eglDestroySurface (egl_renderer->edpy, egl_display->dummy_surface);
        egl_display->dummy_surface = EGL_NO_SURFACE;
    }

    if (wayland_display->dummy_egl_window)
    {
        wl_egl_window_destroy (wayland_display->dummy_egl_window);
        wayland_display->dummy_egl_window = NULL;
    }

    if (wayland_display->dummy_surface)
    {
        wl_surface_destroy (wayland_display->dummy_surface);
        wayland_display->dummy_surface = NULL;
    }
}

static gboolean
_cogl_winsys_egl_onscreen_init (CoglOnscreen *onscreen,
                                EGLConfig egl_config,
                                GError **error)
{
    CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
    CoglContext *context = framebuffer->context;
    CoglRenderer *renderer = context->display->renderer;
    CoglRendererEGL *egl_renderer = renderer->winsys;
    CoglRendererWayland *wayland_renderer = egl_renderer->platform;
    CoglOnscreenEGL *egl_onscreen = onscreen->winsys;
    CoglOnscreenWayland *wayland_onscreen;
    int width, height;

    wayland_onscreen = g_slice_new0 (CoglOnscreenWayland);
    egl_onscreen->platform = wayland_onscreen;

    width = cogl_framebuffer_get_width (framebuffer);
    height = cogl_framebuffer_get_height (framebuffer);

    /* Create wl_surface */
    wayland_onscreen->wl_surface =
        wl_compositor_create_surface (wayland_renderer->wl_compositor);
    if (!wayland_onscreen->wl_surface)
    {
        g_set_error (error, COGL_WINSYS_ERROR,
                     COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                     "Failed to create wl_surface");
        return FALSE;
    }

    wayland_onscreen->wl_egl_window =
        wl_egl_window_create (wayland_onscreen->wl_surface, width, height);
    if (!wayland_onscreen->wl_egl_window)
    {
        g_set_error (error, COGL_WINSYS_ERROR,
                     COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                     "Failed to create wl_egl_window");
        return FALSE;
    }

    egl_onscreen->egl_surface =
        eglCreateWindowSurface (egl_renderer->edpy,
                                egl_config,
                                (EGLNativeWindowType) wayland_onscreen->wl_egl_window,
                                NULL);
    if (egl_onscreen->egl_surface == EGL_NO_SURFACE)
    {
        g_set_error (error, COGL_WINSYS_ERROR,
                     COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                     "Failed to create EGL surface");
        return FALSE;
    }

    return TRUE;
}

static void
_cogl_winsys_egl_onscreen_deinit (CoglOnscreen *onscreen)
{
    CoglOnscreenEGL *egl_onscreen = onscreen->winsys;
    CoglOnscreenWayland *wayland_onscreen = egl_onscreen->platform;

    if (wayland_onscreen->wl_egl_window)
    {
        wl_egl_window_destroy (wayland_onscreen->wl_egl_window);
        wayland_onscreen->wl_egl_window = NULL;
    }

    if (wayland_onscreen->wl_surface)
    {
        wl_surface_destroy (wayland_onscreen->wl_surface);
        wayland_onscreen->wl_surface = NULL;
    }

    g_slice_free (CoglOnscreenWayland, wayland_onscreen);
}

static void
_cogl_winsys_onscreen_set_visibility (CoglOnscreen *onscreen,
                                      gboolean visibility)
{
    /* For Wayland, visibility is controlled by buffer attachment */
}

/* Get the wl_surface for an onscreen */
COGL_EXPORT struct wl_surface *
cogl_wayland_onscreen_get_wl_surface (CoglOnscreen *onscreen)
{
    CoglOnscreenEGL *egl_onscreen;
    CoglOnscreenWayland *wayland_onscreen;

    g_return_val_if_fail (onscreen != NULL, NULL);
    g_return_val_if_fail (onscreen->winsys != NULL, NULL);

    egl_onscreen = onscreen->winsys;
    wayland_onscreen = egl_onscreen->platform;

    return wayland_onscreen->wl_surface;
}

/* Resize an onscreen's wl_egl_window */
COGL_EXPORT void
cogl_wayland_onscreen_resize (CoglOnscreen *onscreen,
                              int width,
                              int height,
                              int offset_x,
                              int offset_y)
{
    CoglOnscreenEGL *egl_onscreen;
    CoglOnscreenWayland *wayland_onscreen;

    g_return_if_fail (onscreen != NULL);
    g_return_if_fail (onscreen->winsys != NULL);

    egl_onscreen = onscreen->winsys;
    wayland_onscreen = egl_onscreen->platform;

    if (wayland_onscreen->wl_egl_window)
    {
        wl_egl_window_resize (wayland_onscreen->wl_egl_window,
                              width, height, offset_x, offset_y);
    }

    _cogl_framebuffer_winsys_update_size (COGL_FRAMEBUFFER (onscreen),
                                          width, height);
}

static const CoglWinsysEGLVtable
_cogl_winsys_egl_vtable =
{
    .add_config_attributes = _cogl_winsys_egl_add_config_attributes,
    .choose_config = _cogl_winsys_egl_choose_config,
    .display_setup = _cogl_winsys_egl_display_setup,
    .display_destroy = _cogl_winsys_egl_display_destroy,
    .context_created = _cogl_winsys_egl_context_created,
    .cleanup_context = _cogl_winsys_egl_cleanup_context,
    .onscreen_init = _cogl_winsys_egl_onscreen_init,
    .onscreen_deinit = _cogl_winsys_egl_onscreen_deinit,
};

COGL_EXPORT const CoglWinsysVtable *
_cogl_winsys_egl_wayland_get_vtable (void)
{
    static gboolean vtable_inited = FALSE;
    static CoglWinsysVtable vtable;

    if (!vtable_inited)
    {
        /* The EGL_WAYLAND winsys is a subclass of the EGL winsys so we
           start by copying its vtable */
        vtable = *_cogl_winsys_egl_get_vtable ();

        vtable.id = COGL_WINSYS_ID_EGL_WAYLAND;
        vtable.name = "EGL_WAYLAND";
        vtable.constraints |= COGL_RENDERER_CONSTRAINT_USES_EGL;

        vtable.renderer_connect = _cogl_winsys_renderer_connect;
        vtable.renderer_disconnect = _cogl_winsys_renderer_disconnect;

        vtable.onscreen_set_visibility = _cogl_winsys_onscreen_set_visibility;

        vtable_inited = TRUE;
    }

    return &vtable;
}
