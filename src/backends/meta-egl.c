/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016, 2017 Red Hat Inc.
 * Copyright (C) 2018, 2019 DisplayLink (UK) Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Jonas Ådahl <jadahl@gmail.com>
 */

#include "config.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglmesaext.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib-object.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-egl.h"
#include "backends/meta-egl-ext.h"
#include "meta/util.h"

struct _MetaEgl
{
  GObject parent;

  PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT;

  PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
  PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;

  PFNEGLQUERYWAYLANDBUFFERWL eglQueryWaylandBufferWL;

  PFNEGLQUERYDEVICESEXTPROC eglQueryDevicesEXT;
  PFNEGLQUERYDEVICESTRINGEXTPROC eglQueryDeviceStringEXT;

  PFNEGLGETOUTPUTLAYERSEXTPROC eglGetOutputLayersEXT;
  PFNEGLQUERYOUTPUTLAYERATTRIBEXTPROC eglQueryOutputLayerAttribEXT;

  PFNEGLCREATESTREAMKHRPROC eglCreateStreamKHR;
  PFNEGLDESTROYSTREAMKHRPROC eglDestroyStreamKHR;
  PFNEGLQUERYSTREAMKHRPROC eglQueryStreamKHR;

  PFNEGLCREATESTREAMATTRIBNVPROC eglCreateStreamAttribNV;

  PFNEGLCREATESTREAMPRODUCERSURFACEKHRPROC eglCreateStreamProducerSurfaceKHR;

  PFNEGLSTREAMCONSUMEROUTPUTEXTPROC eglStreamConsumerOutputEXT;

  PFNEGLSTREAMCONSUMERGLTEXTUREEXTERNALKHRPROC eglStreamConsumerGLTextureExternalKHR;

  PFNEGLSTREAMCONSUMERACQUIREKHRPROC eglStreamConsumerAcquireKHR;
  PFNEGLSTREAMCONSUMERACQUIREATTRIBNVPROC eglStreamConsumerAcquireAttribNV;

  PFNEGLQUERYDMABUFFORMATSEXTPROC eglQueryDmaBufFormatsEXT;
  PFNEGLQUERYDMABUFMODIFIERSEXTPROC eglQueryDmaBufModifiersEXT;
};

G_DEFINE_TYPE (MetaEgl, meta_egl, G_TYPE_OBJECT)

G_DEFINE_QUARK (-meta-egl-error-quark, meta_egl_error)

static const char *
get_egl_error_str (EGLint error_number)
{
  switch (error_number)
    {
    case EGL_SUCCESS:
      return "The last function succeeded without error.";
      break;
    case EGL_NOT_INITIALIZED:
      return "EGL is not initialized, or could not be initialized, for the specified EGL display connection.";
      break;
    case EGL_BAD_ACCESS:
      return "EGL cannot access a requested resource (for example a context is bound in another thread).";
      break;
    case EGL_BAD_ALLOC:
      return "EGL failed to allocate resources for the requested operation.";
      break;
    case EGL_BAD_ATTRIBUTE:
      return "An unrecognized attribute or attribute value was passed in the attribute list.";
      break;
    case EGL_BAD_CONTEXT:
      return "An EGLContext argument does not name a valid EGL rendering context.";
      break;
    case EGL_BAD_CONFIG:
      return "An EGLConfig argument does not name a valid EGL frame buffer configuration.";
      break;
    case EGL_BAD_CURRENT_SURFACE:
      return "The current surface of the calling thread is a window, pixel buffer or pixmap that is no longer valid.";
      break;
    case EGL_BAD_DISPLAY:
      return "An EGLDisplay argument does not name a valid EGL display connection.";
      break;
    case EGL_BAD_SURFACE:
      return "An EGLSurface argument does not name a valid surface (window, pixel buffer or pixmap) configured for GL rendering.";
      break;
    case EGL_BAD_MATCH:
      return "Arguments are inconsistent (for example, a valid context requires buffers not supplied by a valid surface).";
      break;
    case EGL_BAD_PARAMETER:
      return "One or more argument values are invalid.";
      break;
    case EGL_BAD_NATIVE_PIXMAP:
      return "A NativePixmapType argument does not refer to a valid native pixmap.";
      break;
    case EGL_BAD_NATIVE_WINDOW:
      return "A NativeWindowType argument does not refer to a valid native window.";
      break;
    case EGL_CONTEXT_LOST:
      return "A power management event has occurred. The application must destroy all contexts and reinitialise OpenGL ES state and objects to continue rendering. ";
      break;
    case EGL_BAD_STREAM_KHR:
      return "An EGLStreamKHR argument does not name a valid EGL stream.";
      break;
    case EGL_BAD_STATE_KHR:
      return "An EGLStreamKHR argument is not in a valid state";
      break;
    case EGL_BAD_DEVICE_EXT:
      return "An EGLDeviceEXT argument does not name a valid EGL device.";
      break;
    case EGL_BAD_OUTPUT_LAYER_EXT:
      return "An EGLOutputLayerEXT argument does not name a valid EGL output layer.";
    case EGL_RESOURCE_BUSY_EXT:
      return "The operation could not be completed on the requested resource because it is temporary unavailable.";
    default:
      return "Unknown error";
      break;
    }
}

static void
set_egl_error (GError **error)
{
  EGLint error_number;
  const char *error_str;

  if (!error)
    return;

  error_number = eglGetError ();
  error_str = get_egl_error_str (error_number);
  g_set_error_literal (error, META_EGL_ERROR,
                       error_number,
                       error_str);
}

gboolean
meta_extensions_string_has_extensions_valist (const char   *extensions_str,
                                              const char ***missing_extensions,
                                              const char   *first_extension,
                                              va_list       var_args)
{
  char **extensions;
  const char *extension;
  size_t num_missing_extensions = 0;

  if (missing_extensions)
    *missing_extensions = NULL;

  extensions = g_strsplit (extensions_str, " ", -1);

  extension = first_extension;
  while (extension)
    {
      if (!g_strv_contains ((const char * const *) extensions, extension))
        {
          num_missing_extensions++;
          if (missing_extensions)
            {
              *missing_extensions = g_realloc_n (*missing_extensions,
                                                 num_missing_extensions + 1,
                                                 sizeof (const char *));
              (*missing_extensions)[num_missing_extensions - 1] = extension;
              (*missing_extensions)[num_missing_extensions] = NULL;
            }
          else
            {
              break;
            }
        }
      extension = va_arg (var_args, char *);
    }

  g_strfreev (extensions);

  return num_missing_extensions == 0;
}

gboolean
meta_egl_has_extensions (MetaEgl      *egl,
                         EGLDisplay    display,
                         const char ***missing_extensions,
                         const char   *first_extension,
                         ...)
{
  va_list var_args;
  const char *extensions_str;
  gboolean has_extensions;

  extensions_str = (const char *) eglQueryString (display, EGL_EXTENSIONS);
  if (!extensions_str)
    {
      g_warning ("Failed to query string: %s",
                 get_egl_error_str (eglGetError ()));
      return FALSE;
    }

  va_start (var_args, first_extension);
  has_extensions =
    meta_extensions_string_has_extensions_valist (extensions_str,
                                                  missing_extensions,
                                                  first_extension,
                                                  var_args);
  va_end (var_args);

  return has_extensions;
}

gboolean
meta_egl_initialize (MetaEgl   *egl,
                     EGLDisplay display,
                     GError   **error)
{
  if (!eglInitialize (display, NULL, NULL))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

gpointer
meta_egl_get_proc_address (MetaEgl    *egl,
                           const char *procname,
                           GError    **error)
{
  gpointer func;

  func = (gpointer) eglGetProcAddress (procname);
  if (!func)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Could not load symbol '%s': Not found",
                   procname);
      return NULL;
    }

  return func;
}

gboolean
meta_egl_get_config_attrib (MetaEgl     *egl,
                            EGLDisplay   display,
                            EGLConfig    config,
                            EGLint       attribute,
                            EGLint      *value,
                            GError     **error)
{
  if (!eglGetConfigAttrib (display,
                           config,
                           attribute,
                           value))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

EGLConfig *
meta_egl_choose_all_configs (MetaEgl       *egl,
                             EGLDisplay     display,
                             const EGLint  *attrib_list,
                             EGLint        *out_num_configs,
                             GError       **error)
{
  EGLint num_configs;
  EGLConfig *configs;
  EGLint num_matches;

  if (!eglGetConfigs (display, NULL, 0, &num_configs))
    {
      set_egl_error (error);
      return FALSE;
    }

  if (num_configs < 1)
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "No EGL configurations available");
      return FALSE;
    }

  configs = g_new0 (EGLConfig, num_configs);

  if (!eglChooseConfig (display, attrib_list, configs, num_configs, &num_matches))
    {
      g_free (configs);
      set_egl_error (error);
      return FALSE;
    }

  if (num_matches == 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No matching EGL configs");
      g_free (configs);
      return NULL;
    }

  *out_num_configs = num_configs;
  return configs;
}

gboolean
meta_egl_choose_first_config (MetaEgl       *egl,
                              EGLDisplay     display,
                              const EGLint  *attrib_list,
                              EGLConfig     *chosen_config,
                              GError       **error)
{
  EGLint num_configs;
  EGLConfig *configs;
  EGLint num_matches;

  if (!eglGetConfigs (display, NULL, 0, &num_configs))
    {
      set_egl_error (error);
      return FALSE;
    }

  if (num_configs < 1)
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "No EGL configurations available");
      return FALSE;
    }

  configs = g_new0 (EGLConfig, num_configs);

  if (!eglChooseConfig (display, attrib_list, configs, num_configs, &num_matches))
    {
      g_free (configs);
      set_egl_error (error);
      return FALSE;
    }

  if (num_matches == 0)
    {
      g_free (configs);
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No matching EGLConfig found");
      return FALSE;
    }

  /*
   * We don't have any preference specified yet, so lets choose the first one.
   */
  *chosen_config = configs[0];

  g_free (configs);

  return TRUE;
}

EGLSurface
meta_egl_create_window_surface (MetaEgl            *egl,
                                EGLDisplay          display,
                                EGLConfig           config,
                                EGLNativeWindowType native_window_type,
                                const EGLint       *attrib_list,
                                GError            **error)
{
  EGLSurface surface;

  surface = eglCreateWindowSurface (display, config,
                                    native_window_type, attrib_list);
  if (surface == EGL_NO_SURFACE)
    {
      set_egl_error (error);
      return EGL_NO_SURFACE;
    }

  return surface;
}

EGLSurface
meta_egl_create_pbuffer_surface (MetaEgl      *egl,
                                 EGLDisplay    display,
                                 EGLConfig     config,
                                 const EGLint *attrib_list,
                                 GError      **error)
{
  EGLSurface surface;

  surface = eglCreatePbufferSurface (display, config, attrib_list);
  if (surface == EGL_NO_SURFACE)
    {
      set_egl_error (error);
      return EGL_NO_SURFACE;
    }

  return surface;
}

gboolean
meta_egl_destroy_surface (MetaEgl   *egl,
                          EGLDisplay display,
                          EGLSurface surface,
                          GError   **error)
{
  if (!eglDestroySurface (display, surface))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

static gboolean
is_egl_proc_valid_real (void       *proc,
                        const char *proc_name,
                        GError    **error)
{
  if (!proc)
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "EGL proc '%s' not resolved",
                   proc_name);
      return FALSE;
    }

  return TRUE;
}

#define is_egl_proc_valid(proc, error) \
  is_egl_proc_valid_real (proc, #proc, error)

EGLDisplay
meta_egl_get_platform_display (MetaEgl      *egl,
                               EGLenum       platform,
                               void         *native_display,
                               const EGLint *attrib_list,
                               GError      **error)
{
  EGLDisplay display;

  if (!is_egl_proc_valid (egl->eglGetPlatformDisplayEXT, error))
    return EGL_NO_DISPLAY;

  display = egl->eglGetPlatformDisplayEXT (platform,
                                           native_display,
                                           attrib_list);
  if (display == EGL_NO_DISPLAY)
    {
      set_egl_error (error);
      return EGL_NO_DISPLAY;
    }

  return display;
}

gboolean
meta_egl_terminate (MetaEgl   *egl,
                    EGLDisplay display,
                    GError   **error)
{
  if (!eglTerminate (display))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

EGLContext
meta_egl_create_context (MetaEgl      *egl,
                         EGLDisplay    display,
                         EGLConfig     config,
                         EGLContext    share_context,
                         const EGLint *attrib_list,
                         GError      **error)
{
  EGLContext context;

  context = eglCreateContext (display, config, share_context, attrib_list);
  if (context == EGL_NO_CONTEXT)
    {
      set_egl_error (error);
      return EGL_NO_CONTEXT;
    }

  return context;
}

gboolean
meta_egl_destroy_context (MetaEgl   *egl,
                          EGLDisplay display,
                          EGLContext context,
                          GError   **error)
{
  if (!eglDestroyContext (display, context))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

EGLImageKHR
meta_egl_create_image (MetaEgl        *egl,
                       EGLDisplay      display,
                       EGLContext      context,
                       EGLenum         target,
                       EGLClientBuffer buffer,
                       const EGLint   *attrib_list,
                       GError        **error)
{
  EGLImageKHR image;

  if (!is_egl_proc_valid (egl->eglCreateImageKHR, error))
    return EGL_NO_IMAGE_KHR;

  image = egl->eglCreateImageKHR (display, context,
                                  target, buffer, attrib_list);
  if (image == EGL_NO_IMAGE_KHR)
    {
      set_egl_error (error);
      return EGL_NO_IMAGE_KHR;
    }

  return image;
}

gboolean
meta_egl_destroy_image (MetaEgl    *egl,
                        EGLDisplay  display,
                        EGLImageKHR image,
                        GError    **error)
{
  if (!is_egl_proc_valid (egl->eglDestroyImageKHR, error))
    return FALSE;

  if (!egl->eglDestroyImageKHR (display, image))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

EGLImageKHR
meta_egl_create_dmabuf_image (MetaEgl         *egl,
                              EGLDisplay       egl_display,
                              unsigned int     width,
                              unsigned int     height,
                              uint32_t         drm_format,
                              uint32_t         n_planes,
                              const int       *fds,
                              const uint32_t  *strides,
                              const uint32_t  *offsets,
                              const uint64_t  *modifiers,
                              GError         **error)
{
  EGLint attribs[37];
  int atti = 0;

  /* This requires the Mesa commit in
   * Mesa 10.3 (08264e5dad4df448e7718e782ad9077902089a07) or
   * Mesa 10.2.7 (55d28925e6109a4afd61f109e845a8a51bd17652).
   * Otherwise Mesa closes the fd behind our back and re-importing
   * will fail.
   * https://bugs.freedesktop.org/show_bug.cgi?id=76188
   */

  attribs[atti++] = EGL_WIDTH;
  attribs[atti++] = width;
  attribs[atti++] = EGL_HEIGHT;
  attribs[atti++] = height;
  attribs[atti++] = EGL_LINUX_DRM_FOURCC_EXT;
  attribs[atti++] = drm_format;

  if (n_planes > 0)
    {
      attribs[atti++] = EGL_DMA_BUF_PLANE0_FD_EXT;
      attribs[atti++] = fds[0];
      attribs[atti++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
      attribs[atti++] = offsets[0];
      attribs[atti++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
      attribs[atti++] = strides[0];
      if (modifiers)
        {
          attribs[atti++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
          attribs[atti++] = modifiers[0] & 0xFFFFFFFF;
          attribs[atti++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
          attribs[atti++] = modifiers[0] >> 32;
        }
    }

  if (n_planes > 1)
    {
      attribs[atti++] = EGL_DMA_BUF_PLANE1_FD_EXT;
      attribs[atti++] = fds[1];
      attribs[atti++] = EGL_DMA_BUF_PLANE1_OFFSET_EXT;
      attribs[atti++] = offsets[1];
      attribs[atti++] = EGL_DMA_BUF_PLANE1_PITCH_EXT;
      attribs[atti++] = strides[1];
      if (modifiers)
        {
          attribs[atti++] = EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT;
          attribs[atti++] = modifiers[1] & 0xFFFFFFFF;
          attribs[atti++] = EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT;
          attribs[atti++] = modifiers[1] >> 32;
        }
    }

  if (n_planes > 2)
    {
      attribs[atti++] = EGL_DMA_BUF_PLANE2_FD_EXT;
      attribs[atti++] = fds[2];
      attribs[atti++] = EGL_DMA_BUF_PLANE2_OFFSET_EXT;
      attribs[atti++] = offsets[2];
      attribs[atti++] = EGL_DMA_BUF_PLANE2_PITCH_EXT;
      attribs[atti++] = strides[2];
      if (modifiers)
        {
          attribs[atti++] = EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT;
          attribs[atti++] = modifiers[2] & 0xFFFFFFFF;
          attribs[atti++] = EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT;
          attribs[atti++] = modifiers[2] >> 32;
        }
    }

  attribs[atti++] = EGL_NONE;
  g_assert (atti <= G_N_ELEMENTS (attribs));

  return meta_egl_create_image (egl, egl_display, EGL_NO_CONTEXT,
                                EGL_LINUX_DMA_BUF_EXT, NULL,
                                attribs,
                                error);
}

gboolean
meta_egl_make_current (MetaEgl   *egl,
                       EGLDisplay display,
                       EGLSurface draw,
                       EGLSurface read,
                       EGLContext context,
                       GError   **error)
{
  if (!eglMakeCurrent (display, draw, read, context))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

gboolean
meta_egl_swap_buffers (MetaEgl   *egl,
                       EGLDisplay display,
                       EGLSurface surface,
                       GError   **error)
{
  if (!eglSwapBuffers (display, surface))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

gboolean
meta_egl_query_wayland_buffer (MetaEgl            *egl,
                               EGLDisplay          display,
                               struct wl_resource *buffer,
                               EGLint              attribute,
                               EGLint             *value,
                               GError            **error)
{
  if (!is_egl_proc_valid (egl->eglQueryWaylandBufferWL, error))
   return FALSE;

  if (!egl->eglQueryWaylandBufferWL (display, buffer, attribute, value))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

gboolean
meta_egl_query_devices (MetaEgl      *egl,
                        EGLint        max_devices,
                        EGLDeviceEXT *devices,
                        EGLint       *num_devices,
                        GError      **error)
{
  if (!is_egl_proc_valid (egl->eglQueryDevicesEXT, error))
    return FALSE;

  if (!egl->eglQueryDevicesEXT (max_devices,
                                devices,
                                num_devices))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

const char *
meta_egl_query_device_string (MetaEgl     *egl,
                              EGLDeviceEXT device,
                              EGLint       name,
                              GError     **error)
{
  const char *device_string;

  if (!is_egl_proc_valid (egl->eglQueryDeviceStringEXT, error))
    return NULL;

  device_string = egl->eglQueryDeviceStringEXT (device, name);
  if (!device_string)
    {
      set_egl_error (error);
      return NULL;
    }

  return device_string;
}

gboolean
meta_egl_egl_device_has_extensions (MetaEgl        *egl,
                                    EGLDeviceEXT    device,
                                    const char   ***missing_extensions,
                                    const char     *first_extension,
                                    ...)
{
  va_list var_args;
  const char *extensions_str;
  gboolean has_extensions;
  GError *error = NULL;

  extensions_str = meta_egl_query_device_string (egl, device, EGL_EXTENSIONS,
                                                 &error);
  if (!extensions_str)
    {
      g_warning ("Failed to query device string: %s", error->message);
      g_error_free (error);
      return FALSE;
    }

  va_start (var_args, first_extension);
  has_extensions =
    meta_extensions_string_has_extensions_valist (extensions_str,
                                                  missing_extensions,
                                                  first_extension,
                                                  var_args);
  va_end (var_args);

  return has_extensions;
}

gboolean
meta_egl_get_output_layers (MetaEgl           *egl,
                            EGLDisplay         display,
                            const EGLAttrib   *attrib_list,
                            EGLOutputLayerEXT *layers,
                            EGLint             max_layers,
                            EGLint            *num_layers,
                            GError           **error)
{
  if (!is_egl_proc_valid (egl->eglGetOutputLayersEXT, error))
    return FALSE;

  if (!egl->eglGetOutputLayersEXT (display,
                                   attrib_list,
                                   layers,
                                   max_layers,
                                   num_layers))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

gboolean
meta_egl_query_output_layer_attrib (MetaEgl          *egl,
                                    EGLDisplay        display,
                                    EGLOutputLayerEXT layer,
                                    EGLint            attribute,
                                    EGLAttrib        *value,
                                    GError          **error)
{
  if (!is_egl_proc_valid (egl->eglQueryOutputLayerAttribEXT, error))
    return FALSE;

  if (!egl->eglQueryOutputLayerAttribEXT (display, layer,
                                          attribute, value))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

EGLStreamKHR
meta_egl_create_stream (MetaEgl      *egl,
                        EGLDisplay    display,
                        const EGLint *attrib_list,
                        GError      **error)
{
  EGLStreamKHR stream;

  if (!is_egl_proc_valid (egl->eglCreateStreamKHR, error))
    return EGL_NO_STREAM_KHR;

  stream = egl->eglCreateStreamKHR (display, attrib_list);
  if (stream == EGL_NO_STREAM_KHR)
    {
      set_egl_error (error);
      return EGL_NO_STREAM_KHR;
    }

  return stream;
}

gboolean
meta_egl_destroy_stream (MetaEgl     *egl,
                         EGLDisplay   display,
                         EGLStreamKHR stream,
                         GError     **error)
{
  if (!is_egl_proc_valid (egl->eglDestroyStreamKHR, error))
    return FALSE;

  if (!egl->eglDestroyStreamKHR (display, stream))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

gboolean
meta_egl_query_stream (MetaEgl     *egl,
                       EGLDisplay   display,
                       EGLStreamKHR stream,
                       EGLenum      attribute,
                       EGLint      *value,
                       GError     **error)
{
  if (!is_egl_proc_valid (egl->eglQueryStreamKHR, error))
    return FALSE;

  if (!egl->eglQueryStreamKHR (display, stream, attribute, value))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

EGLStreamKHR
meta_egl_create_stream_attrib (MetaEgl         *egl,
                               EGLDisplay       display,
                               const EGLAttrib *attrib_list,
                               GError         **error)
{
  EGLStreamKHR stream;

  if (!is_egl_proc_valid (egl->eglCreateStreamAttribNV, error))
    return FALSE;

  stream = egl->eglCreateStreamAttribNV (display, attrib_list);
  if (stream == EGL_NO_STREAM_KHR)
    {
      set_egl_error (error);
      return EGL_NO_STREAM_KHR;
    }

  return stream;
}

EGLSurface
meta_egl_create_stream_producer_surface (MetaEgl     *egl,
                                         EGLDisplay   display,
                                         EGLConfig    config,
                                         EGLStreamKHR stream,
                                         const EGLint *attrib_list,
                                         GError      **error)
{
  EGLSurface surface;

  if (!is_egl_proc_valid (egl->eglCreateStreamProducerSurfaceKHR, error))
    return EGL_NO_SURFACE;

  surface = egl->eglCreateStreamProducerSurfaceKHR (display,
                                                    config,
                                                    stream,
                                                    attrib_list);
  if (surface == EGL_NO_SURFACE)
    {
      set_egl_error (error);
      return EGL_NO_SURFACE;
    }

  return surface;
}

gboolean
meta_egl_stream_consumer_output (MetaEgl          *egl,
                                 EGLDisplay        display,
                                 EGLStreamKHR      stream,
                                 EGLOutputLayerEXT layer,
                                 GError          **error)
{
  if (!is_egl_proc_valid (egl->eglStreamConsumerOutputEXT, error))
    return FALSE;

  if (!egl->eglStreamConsumerOutputEXT (display, stream, layer))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

gboolean
meta_egl_stream_consumer_acquire_attrib (MetaEgl     *egl,
                                         EGLDisplay   display,
                                         EGLStreamKHR stream,
                                         EGLAttrib   *attrib_list,
                                         GError     **error)
{
  if (!is_egl_proc_valid (egl->eglStreamConsumerAcquireAttribNV, error))
    return FALSE;

  if (!egl->eglStreamConsumerAcquireAttribNV (display, stream, attrib_list))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

gboolean
meta_egl_stream_consumer_gl_texture_external (MetaEgl     *egl,
                                              EGLDisplay   display,
                                              EGLStreamKHR stream,
                                              GError     **error)
{
  if (!is_egl_proc_valid (egl->eglStreamConsumerGLTextureExternalKHR, error))
    return FALSE;

  if (!egl->eglStreamConsumerGLTextureExternalKHR (display, stream))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

gboolean
meta_egl_stream_consumer_acquire (MetaEgl     *egl,
                                  EGLDisplay   display,
                                  EGLStreamKHR stream,
                                  GError     **error)
{
  if (!is_egl_proc_valid (egl->eglStreamConsumerAcquireKHR, error))
    return FALSE;

  if (!egl->eglStreamConsumerAcquireKHR (display, stream))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

gboolean
meta_egl_query_dma_buf_formats (MetaEgl   *egl,
                                EGLDisplay display,
                                EGLint     max_formats,
                                EGLint    *formats,
                                EGLint    *num_formats,
                                GError   **error)
{
  if (!is_egl_proc_valid (egl->eglQueryDmaBufFormatsEXT, error))
    return FALSE;

  if (!egl->eglQueryDmaBufFormatsEXT (display, max_formats, formats,
                                      num_formats))
    {
      set_egl_error (error);
      return FALSE;
    }

    return TRUE;
}

gboolean
meta_egl_query_dma_buf_modifiers (MetaEgl      *egl,
                                  EGLDisplay    display,
                                  EGLint        format,
                                  EGLint        max_modifiers,
                                  EGLuint64KHR *modifiers,
                                  EGLBoolean   *external_only,
                                  EGLint       *num_modifiers,
                                  GError      **error)
{
  if (!is_egl_proc_valid (egl->eglQueryDmaBufModifiersEXT, error))
    return FALSE;

  if (!egl->eglQueryDmaBufModifiersEXT (display, format, max_modifiers,
                                        modifiers, external_only,
                                        num_modifiers))
    {
      set_egl_error (error);
      return FALSE;
    }

    return TRUE;
}

#define GET_EGL_PROC_ADDR(proc) \
  egl->proc = (void *) eglGetProcAddress (#proc);

static void
meta_egl_constructed (GObject *object)
{
  MetaEgl *egl = META_EGL (object);

  GET_EGL_PROC_ADDR (eglGetPlatformDisplayEXT);

  GET_EGL_PROC_ADDR (eglCreateImageKHR);
  GET_EGL_PROC_ADDR (eglDestroyImageKHR);

  GET_EGL_PROC_ADDR (eglQueryWaylandBufferWL);

  GET_EGL_PROC_ADDR (eglQueryDevicesEXT);
  GET_EGL_PROC_ADDR (eglQueryDeviceStringEXT);

  GET_EGL_PROC_ADDR (eglGetOutputLayersEXT);
  GET_EGL_PROC_ADDR (eglQueryOutputLayerAttribEXT);

  GET_EGL_PROC_ADDR (eglCreateStreamKHR);
  GET_EGL_PROC_ADDR (eglDestroyStreamKHR);
  GET_EGL_PROC_ADDR (eglQueryStreamKHR);

  GET_EGL_PROC_ADDR (eglCreateStreamAttribNV);

  GET_EGL_PROC_ADDR (eglCreateStreamProducerSurfaceKHR);

  GET_EGL_PROC_ADDR (eglStreamConsumerOutputEXT);

  GET_EGL_PROC_ADDR (eglStreamConsumerGLTextureExternalKHR);

  GET_EGL_PROC_ADDR (eglStreamConsumerAcquireKHR);
  GET_EGL_PROC_ADDR (eglStreamConsumerAcquireAttribNV);

  GET_EGL_PROC_ADDR (eglQueryDmaBufFormatsEXT);
  GET_EGL_PROC_ADDR (eglQueryDmaBufModifiersEXT);
}

#undef GET_EGL_PROC_ADDR

static void
meta_egl_init (MetaEgl *egl)
{
}

static void
meta_egl_class_init (MetaEglClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_egl_constructed;
}
