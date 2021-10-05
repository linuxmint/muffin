/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009,2010,2011,2013 Intel Corporation.
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
 * Authors:
 *   Robert Bragg <robert@linux.intel.com>
 */

#include "cogl-config.h"

#include "cogl-i18n-private.h"
#include "cogl-util.h"
#include "cogl-feature-private.h"
#include "cogl-context-private.h"
#include "cogl-framebuffer.h"
#include "cogl-onscreen-private.h"
#include "cogl-swap-chain-private.h"
#include "cogl-renderer-private.h"
#include "cogl-onscreen-template-private.h"
#include "cogl-egl.h"
#include "cogl-private.h"
#include "cogl-trace.h"
#include "winsys/cogl-winsys-egl-private.h"
#include "winsys/cogl-winsys-private.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#ifndef EGL_KHR_create_context
#define EGL_CONTEXT_MAJOR_VERSION_KHR           0x3098
#define EGL_CONTEXT_MINOR_VERSION_KHR           0x30FB
#define EGL_CONTEXT_FLAGS_KHR                   0x30FC
#define EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR     0x30FD
#define EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR  0x31BD
#define EGL_OPENGL_ES3_BIT_KHR                  0x0040
#define EGL_NO_RESET_NOTIFICATION_KHR           0x31BE
#define EGL_LOSE_CONTEXT_ON_RESET_KHR           0x31BF
#define EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR                 0x00000001
#define EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR    0x00000002
#define EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR         0x00000004
#define EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR          0x00000001
#define EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR 0x00000002
#endif

#ifndef EGL_IMG_context_priority
#define EGL_CONTEXT_PRIORITY_LEVEL_IMG          0x3100
#define EGL_CONTEXT_PRIORITY_HIGH_IMG           0x3101
#define EGL_CONTEXT_PRIORITY_MEDIUM_IMG         0x3102
#define EGL_CONTEXT_PRIORITY_LOW_IMG            0x3103
#endif

#define MAX_EGL_CONFIG_ATTRIBS 30

/* Define a set of arrays containing the functions required from GL
   for each winsys feature */
#define COGL_WINSYS_FEATURE_BEGIN(name, namespaces, extension_names,    \
                                  egl_private_flags)                    \
  static const CoglFeatureFunction                                      \
  cogl_egl_feature_ ## name ## _funcs[] = {
#define COGL_WINSYS_FEATURE_FUNCTION(ret, name, args)                   \
  { G_STRINGIFY (name), G_STRUCT_OFFSET (CoglRendererEGL, pf_ ## name) },
#define COGL_WINSYS_FEATURE_END()               \
  { NULL, 0 },                                  \
    };
#include "winsys/cogl-winsys-egl-feature-functions.h"

/* Define an array of features */
#undef COGL_WINSYS_FEATURE_BEGIN
#define COGL_WINSYS_FEATURE_BEGIN(name, namespaces, extension_names,    \
                                  egl_private_flags)                    \
  { 255, 255, 0, namespaces, extension_names,                           \
      egl_private_flags,                                                \
      0,                                                                \
      cogl_egl_feature_ ## name ## _funcs },
#undef COGL_WINSYS_FEATURE_FUNCTION
#define COGL_WINSYS_FEATURE_FUNCTION(ret, name, args)
#undef COGL_WINSYS_FEATURE_END
#define COGL_WINSYS_FEATURE_END()

static const CoglFeatureData winsys_feature_data[] =
  {
#include "winsys/cogl-winsys-egl-feature-functions.h"
  };

static GCallback
_cogl_winsys_renderer_get_proc_address (CoglRenderer *renderer,
                                        const char *name,
                                        gboolean in_core)
{
  void *ptr = NULL;

  if (!in_core)
    ptr = eglGetProcAddress (name);

  /* eglGetProcAddress doesn't support fetching core API so we need to
     get that separately with GModule */
  if (ptr == NULL)
    g_module_symbol (renderer->libgl_module, name, &ptr);

  return ptr;
}

static void
_cogl_winsys_renderer_disconnect (CoglRenderer *renderer)
{
  /* This function must be overridden by a platform winsys */
  g_assert_not_reached ();
}

/* Updates all the function pointers */
static void
check_egl_extensions (CoglRenderer *renderer)
{
  CoglRendererEGL *egl_renderer = renderer->winsys;
  const char *egl_extensions;
  char **split_extensions;
  int i;

  egl_extensions = eglQueryString (egl_renderer->edpy, EGL_EXTENSIONS);
  split_extensions = g_strsplit (egl_extensions, " ", 0 /* max_tokens */);

  COGL_NOTE (WINSYS, "  EGL Extensions: %s", egl_extensions);

  egl_renderer->private_features = 0;
  for (i = 0; i < G_N_ELEMENTS (winsys_feature_data); i++)
    if (_cogl_feature_check (renderer,
                             "EGL", winsys_feature_data + i, 0, 0,
                             COGL_DRIVER_GL, /* the driver isn't used */
                             split_extensions,
                             egl_renderer))
      {
        egl_renderer->private_features |=
          winsys_feature_data[i].feature_flags_private;
      }

  g_strfreev (split_extensions);
}

gboolean
_cogl_winsys_egl_renderer_connect_common (CoglRenderer *renderer,
                                          GError **error)
{
  CoglRendererEGL *egl_renderer = renderer->winsys;

  if (!eglInitialize (egl_renderer->edpy,
                      &egl_renderer->egl_version_major,
                      &egl_renderer->egl_version_minor))
    {
      g_set_error (error, COGL_WINSYS_ERROR,
                   COGL_WINSYS_ERROR_INIT,
                   "Couldn't initialize EGL");
      return FALSE;
    }

  check_egl_extensions (renderer);

  return TRUE;
}

static gboolean
_cogl_winsys_renderer_connect (CoglRenderer *renderer,
                               GError **error)
{
  /* This function must be overridden by a platform winsys */
  g_assert_not_reached ();
  return FALSE;
}

static void
egl_attributes_from_framebuffer_config (CoglDisplay *display,
                                        CoglFramebufferConfig *config,
                                        EGLint *attributes)
{
  CoglRenderer *renderer = display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;
  int i = 0;

  /* Let the platform add attributes first, including setting the
   * EGL_SURFACE_TYPE */
  i = egl_renderer->platform_vtable->add_config_attributes (display,
                                                            config,
                                                            attributes);

  if (config->need_stencil)
    {
      attributes[i++] = EGL_STENCIL_SIZE;
      attributes[i++] = 2;
    }

  attributes[i++] = EGL_RED_SIZE;
  attributes[i++] = 1;
  attributes[i++] = EGL_GREEN_SIZE;
  attributes[i++] = 1;
  attributes[i++] = EGL_BLUE_SIZE;
  attributes[i++] = 1;

  attributes[i++] = EGL_ALPHA_SIZE;
  attributes[i++] = config->swap_chain->has_alpha ? 1 : EGL_DONT_CARE;

  attributes[i++] = EGL_DEPTH_SIZE;
  attributes[i++] = 1;

  attributes[i++] = EGL_BUFFER_SIZE;
  attributes[i++] = EGL_DONT_CARE;

  attributes[i++] = EGL_RENDERABLE_TYPE;
  attributes[i++] = ((renderer->driver == COGL_DRIVER_GL ||
                      renderer->driver == COGL_DRIVER_GL3) ?
                     EGL_OPENGL_BIT :
                     EGL_OPENGL_ES2_BIT);

  if (config->samples_per_pixel)
    {
       attributes[i++] = EGL_SAMPLE_BUFFERS;
       attributes[i++] = 1;
       attributes[i++] = EGL_SAMPLES;
       attributes[i++] = config->samples_per_pixel;
    }

  attributes[i++] = EGL_NONE;

  g_assert (i < MAX_EGL_CONFIG_ATTRIBS);
}

EGLBoolean
_cogl_winsys_egl_make_current (CoglDisplay *display,
                               EGLSurface draw,
                               EGLSurface read,
                               EGLContext context)
{
  CoglDisplayEGL *egl_display = display->winsys;
  CoglRendererEGL *egl_renderer = display->renderer->winsys;
  EGLBoolean ret;

  if (egl_display->current_draw_surface == draw &&
      egl_display->current_read_surface == read &&
      egl_display->current_context == context)
    return EGL_TRUE;

  ret = eglMakeCurrent (egl_renderer->edpy,
                        draw,
                        read,
                        context);

  egl_display->current_draw_surface = draw;
  egl_display->current_read_surface = read;
  egl_display->current_context = context;

  return ret;
}

EGLBoolean
_cogl_winsys_egl_ensure_current (CoglDisplay *display)
{
  CoglDisplayEGL *egl_display = display->winsys;
  CoglRendererEGL *egl_renderer = display->renderer->winsys;

  return eglMakeCurrent (egl_renderer->edpy,
                         egl_display->current_draw_surface,
                         egl_display->current_read_surface,
                         egl_display->current_context);
}

static void
cleanup_context (CoglDisplay *display)
{
  CoglRenderer *renderer = display->renderer;
  CoglDisplayEGL *egl_display = display->winsys;
  CoglRendererEGL *egl_renderer = renderer->winsys;

  if (egl_display->egl_context != EGL_NO_CONTEXT)
    {
      _cogl_winsys_egl_make_current (display,
                                     EGL_NO_SURFACE, EGL_NO_SURFACE,
                                     EGL_NO_CONTEXT);
      eglDestroyContext (egl_renderer->edpy, egl_display->egl_context);
      egl_display->egl_context = EGL_NO_CONTEXT;
    }

  if (egl_renderer->platform_vtable->cleanup_context)
    egl_renderer->platform_vtable->cleanup_context (display);
}

static gboolean
try_create_context (CoglDisplay *display,
                    GError **error)
{
  CoglRenderer *renderer = display->renderer;
  CoglDisplayEGL *egl_display = display->winsys;
  CoglRendererEGL *egl_renderer = renderer->winsys;
  EGLDisplay edpy;
  EGLConfig config;
  EGLint attribs[11];
  EGLint cfg_attribs[MAX_EGL_CONFIG_ATTRIBS];
  GError *config_error = NULL;
  const char *error_message;
  int i = 0;

  g_return_val_if_fail (egl_display->egl_context == NULL, TRUE);

  if (renderer->driver == COGL_DRIVER_GL ||
      renderer->driver == COGL_DRIVER_GL3)
    eglBindAPI (EGL_OPENGL_API);
  else if (renderer->driver == COGL_DRIVER_GLES2)
    eglBindAPI (EGL_OPENGL_ES_API);

  egl_attributes_from_framebuffer_config (display,
                                          &display->onscreen_template->config,
                                          cfg_attribs);

  edpy = egl_renderer->edpy;

  if (!egl_renderer->platform_vtable->choose_config (display,
                                                     cfg_attribs,
                                                     &config,
                                                     &config_error))
    {
      g_set_error (error, COGL_WINSYS_ERROR,
                   COGL_WINSYS_ERROR_CREATE_CONTEXT,
                   "Couldn't choose config: %s", config_error->message);
      g_error_free (config_error);
      goto err;
    }

  egl_display->egl_config = config;

  if (display->renderer->driver == COGL_DRIVER_GL3)
    {
      if (!(egl_renderer->private_features &
            COGL_EGL_WINSYS_FEATURE_CREATE_CONTEXT))
        {
          error_message = "Driver does not support GL 3 contexts";
          goto fail;
        }

      /* Try to get a core profile 3.1 context with no deprecated features */
      attribs[i++] = EGL_CONTEXT_MAJOR_VERSION_KHR;
      attribs[i++] = 3;
      attribs[i++] = EGL_CONTEXT_MINOR_VERSION_KHR;
      attribs[i++] = 1;
      attribs[i++] = EGL_CONTEXT_FLAGS_KHR;
      attribs[i++] = EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR;
      attribs[i++] = EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR;
      attribs[i++] = EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR;
    }
  else if (display->renderer->driver == COGL_DRIVER_GLES2)
    {
      attribs[i++] = EGL_CONTEXT_CLIENT_VERSION;
      attribs[i++] = 2;
    }

  if (egl_renderer->private_features &
      COGL_EGL_WINSYS_FEATURE_CONTEXT_PRIORITY)
    {
      attribs[i++] = EGL_CONTEXT_PRIORITY_LEVEL_IMG;
      attribs[i++] = EGL_CONTEXT_PRIORITY_HIGH_IMG;
    }

  attribs[i++] = EGL_NONE;

  egl_display->egl_context = eglCreateContext (edpy,
                                               config,
                                               EGL_NO_CONTEXT,
                                               attribs);

  if (egl_display->egl_context == EGL_NO_CONTEXT)
    {
      error_message = "Unable to create a suitable EGL context";
      goto fail;
    }

  if (egl_renderer->private_features &
      COGL_EGL_WINSYS_FEATURE_CONTEXT_PRIORITY)
    {
      EGLint value = EGL_CONTEXT_PRIORITY_MEDIUM_IMG;

      eglQueryContext (egl_renderer->edpy,
                       egl_display->egl_context,
                       EGL_CONTEXT_PRIORITY_LEVEL_IMG,
                       &value);

      if (value != EGL_CONTEXT_PRIORITY_HIGH_IMG)
        g_message ("Failed to obtain high priority context");
    }

  if (egl_renderer->platform_vtable->context_created &&
      !egl_renderer->platform_vtable->context_created (display, error))
    return FALSE;

  return TRUE;

fail:
  g_set_error (error, COGL_WINSYS_ERROR,
               COGL_WINSYS_ERROR_CREATE_CONTEXT,
               "%s", error_message);

err:
  cleanup_context (display);

  return FALSE;
}

static void
_cogl_winsys_display_destroy (CoglDisplay *display)
{
  CoglRendererEGL *egl_renderer = display->renderer->winsys;
  CoglDisplayEGL *egl_display = display->winsys;

  g_return_if_fail (egl_display != NULL);

  cleanup_context (display);

  if (egl_renderer->platform_vtable->display_destroy)
    egl_renderer->platform_vtable->display_destroy (display);

  g_slice_free (CoglDisplayEGL, display->winsys);
  display->winsys = NULL;
}

static gboolean
_cogl_winsys_display_setup (CoglDisplay *display,
                            GError **error)
{
  CoglDisplayEGL *egl_display;
  CoglRenderer *renderer = display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;

  g_return_val_if_fail (display->winsys == NULL, FALSE);

  egl_display = g_slice_new0 (CoglDisplayEGL);
  display->winsys = egl_display;

#ifdef COGL_HAS_WAYLAND_EGL_SERVER_SUPPORT
  if (display->wayland_compositor_display)
    {
      struct wl_display *wayland_display = display->wayland_compositor_display;
      CoglRendererEGL *egl_renderer = display->renderer->winsys;

      if (egl_renderer->pf_eglBindWaylandDisplay)
	egl_renderer->pf_eglBindWaylandDisplay (egl_renderer->edpy,
						wayland_display);
    }
#endif

  if (egl_renderer->platform_vtable->display_setup &&
      !egl_renderer->platform_vtable->display_setup (display, error))
    goto error;

  if (!try_create_context (display, error))
    goto error;

  egl_display->found_egl_config = TRUE;

  return TRUE;

error:
  _cogl_winsys_display_destroy (display);
  return FALSE;
}

static gboolean
_cogl_winsys_context_init (CoglContext *context, GError **error)
{
  CoglRenderer *renderer = context->display->renderer;
  CoglDisplayEGL *egl_display = context->display->winsys;
  CoglRendererEGL *egl_renderer = renderer->winsys;

  context->winsys = g_new0 (CoglContextEGL, 1);

  g_return_val_if_fail (egl_display->egl_context, FALSE);

  memset (context->winsys_features, 0, sizeof (context->winsys_features));

  check_egl_extensions (renderer);

  if (!_cogl_context_update_features (context, error))
    return FALSE;

  if (egl_renderer->private_features & COGL_EGL_WINSYS_FEATURE_SWAP_REGION)
    {
      COGL_FLAGS_SET (context->winsys_features,
                      COGL_WINSYS_FEATURE_SWAP_REGION, TRUE);
      COGL_FLAGS_SET (context->winsys_features,
                      COGL_WINSYS_FEATURE_SWAP_REGION_THROTTLE, TRUE);
    }

  if ((egl_renderer->private_features & COGL_EGL_WINSYS_FEATURE_FENCE_SYNC) &&
      _cogl_has_private_feature (context, COGL_PRIVATE_FEATURE_OES_EGL_SYNC))
    COGL_FLAGS_SET (context->features, COGL_FEATURE_ID_FENCE, TRUE);

  if (egl_renderer->private_features & COGL_EGL_WINSYS_FEATURE_BUFFER_AGE)
    {
      COGL_FLAGS_SET (context->winsys_features,
                      COGL_WINSYS_FEATURE_BUFFER_AGE,
                      TRUE);
      COGL_FLAGS_SET (context->features, COGL_FEATURE_ID_BUFFER_AGE, TRUE);
    }

  if (egl_renderer->platform_vtable->context_init &&
      !egl_renderer->platform_vtable->context_init (context, error))
    return FALSE;

  return TRUE;
}

static void
_cogl_winsys_context_deinit (CoglContext *context)
{
  CoglRenderer *renderer = context->display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;

  if (egl_renderer->platform_vtable->context_deinit)
    egl_renderer->platform_vtable->context_deinit (context);

  g_free (context->winsys);
}

static gboolean
_cogl_winsys_onscreen_init (CoglOnscreen *onscreen,
                            GError **error)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = framebuffer->context;
  CoglDisplay *display = context->display;
  CoglDisplayEGL *egl_display = display->winsys;
  CoglRenderer *renderer = display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;
  EGLint attributes[MAX_EGL_CONFIG_ATTRIBS];
  EGLConfig egl_config;
  EGLint config_count = 0;
  EGLBoolean status;

  g_return_val_if_fail (egl_display->egl_context, FALSE);

  egl_attributes_from_framebuffer_config (display,
                                          &framebuffer->config,
                                          attributes);

  status = eglChooseConfig (egl_renderer->edpy,
                            attributes,
                            &egl_config, 1,
                            &config_count);
  if (status != EGL_TRUE || config_count == 0)
    {
      g_set_error (error, COGL_WINSYS_ERROR,
                   COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                   "Failed to find a suitable EGL configuration");
      return FALSE;
    }

  /* Update the real number of samples_per_pixel now that we have
   * found an egl_config... */
  if (framebuffer->config.samples_per_pixel)
    {
      EGLint samples;
      status = eglGetConfigAttrib (egl_renderer->edpy,
                                   egl_config,
                                   EGL_SAMPLES, &samples);
      g_return_val_if_fail (status == EGL_TRUE, TRUE);
      framebuffer->samples_per_pixel = samples;
    }

  onscreen->winsys = g_slice_new0 (CoglOnscreenEGL);

  if (egl_renderer->platform_vtable->onscreen_init &&
      !egl_renderer->platform_vtable->onscreen_init (onscreen,
                                                     egl_config,
                                                     error))
    {
      g_slice_free (CoglOnscreenEGL, onscreen->winsys);
      return FALSE;
    }

  return TRUE;
}

static void
_cogl_winsys_onscreen_deinit (CoglOnscreen *onscreen)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = framebuffer->context;
  CoglDisplayEGL *egl_display = context->display->winsys;
  CoglRenderer *renderer = context->display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;
  CoglOnscreenEGL *egl_onscreen = onscreen->winsys;

  /* If we never successfully allocated then there's nothing to do */
  if (egl_onscreen == NULL)
    return;

  if (egl_onscreen->egl_surface != EGL_NO_SURFACE)
    {
      /* Cogl always needs a valid context bound to something so if we
       * are destroying the onscreen that is currently bound we'll
       * switch back to the dummy drawable. */
      if ((egl_display->dummy_surface != EGL_NO_SURFACE ||
           (egl_renderer->private_features &
            COGL_EGL_WINSYS_FEATURE_SURFACELESS_CONTEXT) != 0) &&
          (egl_display->current_draw_surface == egl_onscreen->egl_surface ||
           egl_display->current_read_surface == egl_onscreen->egl_surface))
        {
          _cogl_winsys_egl_make_current (context->display,
                                         egl_display->dummy_surface,
                                         egl_display->dummy_surface,
                                         egl_display->current_context);
        }

      if (eglDestroySurface (egl_renderer->edpy, egl_onscreen->egl_surface)
          == EGL_FALSE)
        g_warning ("Failed to destroy EGL surface");
      egl_onscreen->egl_surface = EGL_NO_SURFACE;
    }

  if (egl_renderer->platform_vtable->onscreen_deinit)
    egl_renderer->platform_vtable->onscreen_deinit (onscreen);

  g_slice_free (CoglOnscreenEGL, onscreen->winsys);
  onscreen->winsys = NULL;
}

static gboolean
bind_onscreen_with_context (CoglOnscreen *onscreen,
                            EGLContext egl_context)
{
  CoglFramebuffer *fb = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = fb->context;
  CoglOnscreenEGL *egl_onscreen = onscreen->winsys;

  gboolean status = _cogl_winsys_egl_make_current (context->display,
                                                   egl_onscreen->egl_surface,
                                                   egl_onscreen->egl_surface,
                                                   egl_context);
  if (status)
    {
      CoglRenderer *renderer = context->display->renderer;
      CoglRendererEGL *egl_renderer = renderer->winsys;

      eglSwapInterval (egl_renderer->edpy, 1);
    }

  return status;
}

static gboolean
bind_onscreen (CoglOnscreen *onscreen)
{
  CoglFramebuffer *fb = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = fb->context;
  CoglDisplayEGL *egl_display = context->display->winsys;

  return bind_onscreen_with_context (onscreen, egl_display->egl_context);
}

static void
_cogl_winsys_onscreen_bind (CoglOnscreen *onscreen)
{
  bind_onscreen (onscreen);
}

#ifndef EGL_BUFFER_AGE_EXT
#define EGL_BUFFER_AGE_EXT 0x313D
#endif

static int
_cogl_winsys_onscreen_get_buffer_age (CoglOnscreen *onscreen)
{
  CoglContext *context = COGL_FRAMEBUFFER (onscreen)->context;
  CoglRenderer *renderer = context->display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;
  CoglOnscreenEGL *egl_onscreen = onscreen->winsys;
  CoglDisplayEGL *egl_display = context->display->winsys;
  EGLSurface surface = egl_onscreen->egl_surface;
  static gboolean warned = FALSE;
  int age = 0;

  if (!(egl_renderer->private_features & COGL_EGL_WINSYS_FEATURE_BUFFER_AGE))
    return 0;

  if (!_cogl_winsys_egl_make_current (context->display,
				      surface, surface,
                                      egl_display->egl_context))
    return 0;

  if (!eglQuerySurface (egl_renderer->edpy, surface, EGL_BUFFER_AGE_EXT, &age))
    {
      if (!warned)
        g_critical ("Failed to query buffer age, got error %x", eglGetError ());
      warned = TRUE;
    }
  else
    {
      warned = FALSE;
    }

  return age;
}

static void
_cogl_winsys_onscreen_swap_region (CoglOnscreen *onscreen,
                                   const int *user_rectangles,
                                   int n_rectangles)
{
  CoglContext *context = COGL_FRAMEBUFFER (onscreen)->context;
  CoglRenderer *renderer = context->display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;
  CoglOnscreenEGL *egl_onscreen = onscreen->winsys;
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  int framebuffer_height  = cogl_framebuffer_get_height (framebuffer);
  int *rectangles = g_alloca (sizeof (int) * n_rectangles * 4);
  int i;

  /* eglSwapBuffersRegion expects rectangles relative to the
   * bottom left corner but we are given rectangles relative to
   * the top left so we need to flip them... */
  memcpy (rectangles, user_rectangles, sizeof (int) * n_rectangles * 4);
  for (i = 0; i < n_rectangles; i++)
    {
      int *rect = &rectangles[4 * i];
      rect[1] = framebuffer_height - rect[1] - rect[3];
    }

  /* At least for eglSwapBuffers the EGL spec says that the surface to
     swap must be bound to the current context. It looks like Mesa
     also validates that this is the case for eglSwapBuffersRegion so
     we must bind here too */
  _cogl_framebuffer_flush_state (COGL_FRAMEBUFFER (onscreen),
                                 COGL_FRAMEBUFFER (onscreen),
                                 COGL_FRAMEBUFFER_STATE_BIND);

  if (egl_renderer->pf_eglSwapBuffersRegion (egl_renderer->edpy,
                                             egl_onscreen->egl_surface,
                                             n_rectangles,
                                             rectangles) == EGL_FALSE)
    g_warning ("Error reported by eglSwapBuffersRegion");
}

static void
_cogl_winsys_onscreen_swap_buffers_with_damage (CoglOnscreen *onscreen,
                                                const int *rectangles,
                                                int n_rectangles)
{
  CoglContext *context = COGL_FRAMEBUFFER (onscreen)->context;
  CoglRenderer *renderer = context->display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;
  CoglOnscreenEGL *egl_onscreen = onscreen->winsys;

  COGL_TRACE_BEGIN_SCOPED (CoglOnscreenEGLSwapBuffersWithDamage,
                           "Onscreen (eglSwapBuffers)");

  /* The specification for EGL (at least in 1.4) says that the surface
     needs to be bound to the current context for the swap to work
     although it may change in future. Mesa explicitly checks for this
     and just returns an error if this is not the case so we can't
     just pretend this isn't in the spec. */
  _cogl_framebuffer_flush_state (COGL_FRAMEBUFFER (onscreen),
                                 COGL_FRAMEBUFFER (onscreen),
                                 COGL_FRAMEBUFFER_STATE_BIND);

  if (n_rectangles && egl_renderer->pf_eglSwapBuffersWithDamage)
    {
      CoglFramebuffer *fb = COGL_FRAMEBUFFER (onscreen);
      size_t size = n_rectangles * sizeof (int) * 4;
      int *flipped = alloca (size);
      int i;

      memcpy (flipped, rectangles, size);
      for (i = 0; i < n_rectangles; i++)
        {
          const int *rect = rectangles + 4 * i;
          int *flip_rect = flipped + 4 * i;
          flip_rect[1] = fb->height - rect[1] - rect[3];
        }

      if (egl_renderer->pf_eglSwapBuffersWithDamage (egl_renderer->edpy,
                                                     egl_onscreen->egl_surface,
                                                     flipped,
                                                     n_rectangles) == EGL_FALSE)
        g_warning ("Error reported by eglSwapBuffersWithDamage");
    }
  else
    eglSwapBuffers (egl_renderer->edpy, egl_onscreen->egl_surface);
}

#if defined(EGL_KHR_fence_sync) || defined(EGL_KHR_reusable_sync)
static void *
_cogl_winsys_fence_add (CoglContext *context)
{
  CoglRendererEGL *renderer = context->display->renderer->winsys;
  void *ret;

  if (renderer->pf_eglCreateSync)
    ret = renderer->pf_eglCreateSync (renderer->edpy,
                                      EGL_SYNC_FENCE_KHR,
                                      NULL);
  else
    ret = NULL;

  return ret;
}

static gboolean
_cogl_winsys_fence_is_complete (CoglContext *context, void *fence)
{
  CoglRendererEGL *renderer = context->display->renderer->winsys;
  EGLint ret;

  ret = renderer->pf_eglClientWaitSync (renderer->edpy,
                                        fence,
                                        EGL_SYNC_FLUSH_COMMANDS_BIT_KHR,
                                        0);
  return (ret == EGL_CONDITION_SATISFIED_KHR);
}

static void
_cogl_winsys_fence_destroy (CoglContext *context, void *fence)
{
  CoglRendererEGL *renderer = context->display->renderer->winsys;

  renderer->pf_eglDestroySync (renderer->edpy, fence);
}
#endif

static CoglWinsysVtable _cogl_winsys_vtable =
  {
    .constraints = COGL_RENDERER_CONSTRAINT_USES_EGL,

    /* This winsys is only used as a base for the EGL-platform
       winsys's so it does not have an ID or a name */

    .renderer_get_proc_address = _cogl_winsys_renderer_get_proc_address,
    .renderer_connect = _cogl_winsys_renderer_connect,
    .renderer_disconnect = _cogl_winsys_renderer_disconnect,
    .display_setup = _cogl_winsys_display_setup,
    .display_destroy = _cogl_winsys_display_destroy,
    .context_init = _cogl_winsys_context_init,
    .context_deinit = _cogl_winsys_context_deinit,
    .onscreen_init = _cogl_winsys_onscreen_init,
    .onscreen_deinit = _cogl_winsys_onscreen_deinit,
    .onscreen_bind = _cogl_winsys_onscreen_bind,
    .onscreen_swap_buffers_with_damage =
      _cogl_winsys_onscreen_swap_buffers_with_damage,
    .onscreen_swap_region = _cogl_winsys_onscreen_swap_region,
    .onscreen_get_buffer_age = _cogl_winsys_onscreen_get_buffer_age,

#if defined(EGL_KHR_fence_sync) || defined(EGL_KHR_reusable_sync)
    .fence_add = _cogl_winsys_fence_add,
    .fence_is_complete = _cogl_winsys_fence_is_complete,
    .fence_destroy = _cogl_winsys_fence_destroy,
#endif
  };

/* XXX: we use a function because no doubt someone will complain
 * about using c99 member initializers because they aren't portable
 * to windows. We want to avoid having to rigidly follow the real
 * order of members since some members are #ifdefd and we'd have
 * to mirror the #ifdefing to add padding etc. For any winsys that
 * can assume the platform has a sane compiler then we can just use
 * c99 initializers for insane platforms they can initialize
 * the members by name in a function.
 */
const CoglWinsysVtable *
_cogl_winsys_egl_get_vtable (void)
{
  return &_cogl_winsys_vtable;
}

#ifdef EGL_KHR_image_base
EGLImageKHR
_cogl_egl_create_image (CoglContext *ctx,
                        EGLenum target,
                        EGLClientBuffer buffer,
                        const EGLint *attribs)
{
  CoglDisplayEGL *egl_display = ctx->display->winsys;
  CoglRendererEGL *egl_renderer = ctx->display->renderer->winsys;
  EGLContext egl_ctx;

  g_return_val_if_fail (egl_renderer->pf_eglCreateImage, EGL_NO_IMAGE_KHR);

  /* The EGL_KHR_image_pixmap spec explicitly states that EGL_NO_CONTEXT must
   * always be used in conjunction with the EGL_NATIVE_PIXMAP_KHR target */
#ifdef EGL_KHR_image_pixmap
  if (target == EGL_NATIVE_PIXMAP_KHR)
    egl_ctx = EGL_NO_CONTEXT;
  else
#endif
#ifdef COGL_HAS_WAYLAND_EGL_SERVER_SUPPORT
  /* The WL_bind_wayland_display spec states that EGL_NO_CONTEXT is to be used
   * in conjunction with the EGL_WAYLAND_BUFFER_WL target */
  if (target == EGL_WAYLAND_BUFFER_WL)
    egl_ctx = EGL_NO_CONTEXT;
  else
#endif
    egl_ctx = egl_display->egl_context;

  return egl_renderer->pf_eglCreateImage (egl_renderer->edpy,
                                          egl_ctx,
                                          target,
                                          buffer,
                                          attribs);
}

void
_cogl_egl_destroy_image (CoglContext *ctx,
                         EGLImageKHR image)
{
  CoglRendererEGL *egl_renderer = ctx->display->renderer->winsys;

  g_return_if_fail (egl_renderer->pf_eglDestroyImage);

  egl_renderer->pf_eglDestroyImage (egl_renderer->edpy, image);
}
#endif

#ifdef EGL_WL_bind_wayland_display
gboolean
_cogl_egl_query_wayland_buffer (CoglContext *ctx,
                                struct wl_resource *buffer,
                                int attribute,
                                int *value)
{
  CoglRendererEGL *egl_renderer = ctx->display->renderer->winsys;

  g_return_val_if_fail (egl_renderer->pf_eglQueryWaylandBuffer, FALSE);

  return egl_renderer->pf_eglQueryWaylandBuffer (egl_renderer->edpy,
                                                 buffer,
                                                 attribute,
                                                 value);
}
#endif

EGLDisplay
cogl_egl_context_get_egl_display (CoglContext *context)
{
  CoglRendererEGL *egl_renderer = context->display->renderer->winsys;

  return egl_renderer->edpy;
}
