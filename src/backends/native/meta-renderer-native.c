/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2011 Intel Corporation.
 * Copyright (C) 2016 Red Hat
 * Copyright (c) 2018,2019 DisplayLink (UK) Ltd.
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
 *   Rob Bradford <rob@linux.intel.com> (from cogl-winsys-egl-kms.c)
 *   Kristian Høgsberg (from eglkms.c)
 *   Benjamin Franzke (from eglkms.c)
 *   Robert Bragg <robert@linux.intel.com> (from cogl-winsys-egl-kms.c)
 *   Neil Roberts <neil@linux.intel.com> (from cogl-winsys-egl-kms.c)
 *   Jonas Ådahl <jadahl@redhat.com>
 *
 */

#include "config.h"

#include <drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <gbm.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-crtc.h"
#include "backends/meta-egl-ext.h"
#include "backends/meta-egl.h"
#include "backends/meta-gles3.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-output.h"
#include "backends/meta-renderer-view.h"
#include "backends/native/meta-crtc-kms.h"
#include "backends/native/meta-drm-buffer-dumb.h"
#include "backends/native/meta-drm-buffer-gbm.h"
#include "backends/native/meta-drm-buffer-import.h"
#include "backends/native/meta-drm-buffer.h"
#include "backends/native/meta-gpu-kms.h"
#include "backends/native/meta-kms-update.h"
#include "backends/native/meta-kms-utils.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-output-kms.h"
#include "backends/native/meta-renderer-native-gles3.h"
#include "backends/native/meta-renderer-native.h"
//#include "cogl/cogl-framebuffer.h"
#include "cogl/cogl.h"
#include "core/boxes-private.h"

#ifndef EGL_DRM_MASTER_FD_EXT
#define EGL_DRM_MASTER_FD_EXT 0x333C
#endif

/* added in libdrm 2.4.95 */
#ifndef DRM_FORMAT_INVALID
#define DRM_FORMAT_INVALID 0
#endif

typedef enum _MetaSharedFramebufferCopyMode
{
  /* Zero-copy: primary GPU exports, secondary GPU imports as KMS FB */
  META_SHARED_FRAMEBUFFER_COPY_MODE_ZERO,
  /* the secondary GPU will make the copy */
  META_SHARED_FRAMEBUFFER_COPY_MODE_SECONDARY_GPU,
  /*
   * The copy is made in the primary GPU rendering context, either
   * as a CPU copy through Cogl read-pixels or as primary GPU copy
   * using glBlitFramebuffer.
   */
  META_SHARED_FRAMEBUFFER_COPY_MODE_PRIMARY
} MetaSharedFramebufferCopyMode;

typedef struct _MetaRendererNativeGpuData
{
  MetaRendererNative *renderer_native;

  struct {
    struct gbm_device *device;
  } gbm;

#ifdef HAVE_EGL_DEVICE
  struct {
    EGLDeviceEXT device;
  } egl;
#endif

  MetaRendererNativeMode mode;

  EGLDisplay egl_display;

  /*
   * Fields used for blitting iGPU framebuffer content onto dGPU framebuffers.
   */
  struct {
    MetaSharedFramebufferCopyMode copy_mode;
    gboolean is_hardware_rendering;
    gboolean has_EGL_EXT_image_dma_buf_import_modifiers;

    /* For GPU blit mode */
    EGLContext egl_context;
    EGLConfig egl_config;
  } secondary;
} MetaRendererNativeGpuData;

typedef struct _MetaDumbBuffer
{
  uint32_t fb_id;
  uint32_t handle;
  void *map;
  uint64_t map_size;
  int width;
  int height;
  int stride_bytes;
  uint32_t drm_format;
  int dmabuf_fd;
} MetaDumbBuffer;

typedef enum _MetaSharedFramebufferImportStatus
{
  /* Not tried importing yet. */
  META_SHARED_FRAMEBUFFER_IMPORT_STATUS_NONE,
  /* Tried before and failed. */
  META_SHARED_FRAMEBUFFER_IMPORT_STATUS_FAILED,
  /* Tried before and succeeded. */
  META_SHARED_FRAMEBUFFER_IMPORT_STATUS_OK
} MetaSharedFramebufferImportStatus;

typedef struct _MetaOnscreenNativeSecondaryGpuState
{
  MetaGpuKms *gpu_kms;
  MetaRendererNativeGpuData *renderer_gpu_data;

  EGLSurface egl_surface;

  struct {
    struct gbm_surface *surface;
    MetaDrmBuffer *current_fb;
    MetaDrmBuffer *next_fb;
  } gbm;

  struct {
    MetaDumbBuffer *dumb_fb;
    MetaDumbBuffer dumb_fbs[2];
  } cpu;

  int pending_flips;

  gboolean noted_primary_gpu_copy_ok;
  gboolean noted_primary_gpu_copy_failed;
  MetaSharedFramebufferImportStatus import_status;
} MetaOnscreenNativeSecondaryGpuState;

typedef struct _MetaOnscreenNative
{
  MetaRendererNative *renderer_native;
  MetaGpuKms *render_gpu;
  MetaOutput *output;
  MetaCrtc *crtc;

  MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state;

  struct {
    struct gbm_surface *surface;
    MetaDrmBuffer *current_fb;
    MetaDrmBuffer *next_fb;
  } gbm;

#ifdef HAVE_EGL_DEVICE
  struct {
    EGLStreamKHR stream;

    MetaDumbBuffer dumb_fb;
  } egl;
#endif

  gboolean pending_swap_notify;

  gboolean pending_set_crtc;

  int64_t pending_queue_swap_notify_frame_count;
  int64_t pending_swap_notify_frame_count;

  MetaRendererView *view;
  int total_pending_flips;
} MetaOnscreenNative;

struct _MetaRendererNative
{
  MetaRenderer parent;

  MetaGpuKms *primary_gpu_kms;

  MetaGles3 *gles3;

  gboolean use_modifiers;

  GHashTable *gpu_datas;

  CoglClosure *swap_notify_idle;

  int64_t frame_counter;
  gboolean pending_unset_disabled_crtcs;

  GList *power_save_page_flip_onscreens;
  guint power_save_page_flip_source_id;
};

static void
initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (MetaRendererNative,
                         meta_renderer_native,
                         META_TYPE_RENDERER,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_iface_init))

static const CoglWinsysEGLVtable _cogl_winsys_egl_vtable;
static const CoglWinsysVtable *parent_vtable;

static void
release_dumb_fb (MetaDumbBuffer *dumb_fb,
                 MetaGpuKms     *gpu_kms);

static gboolean
init_dumb_fb (MetaDumbBuffer *dumb_fb,
              MetaGpuKms     *gpu_kms,
              int             width,
              int             height,
              uint32_t        format,
              GError        **error);

static int
meta_dumb_buffer_ensure_dmabuf_fd (MetaDumbBuffer *dumb_fb,
                                   MetaGpuKms     *gpu_kms);

static MetaEgl *
meta_renderer_native_get_egl (MetaRendererNative *renderer_native);

static void
free_current_secondary_bo (CoglOnscreen *onscreen);

static gboolean
cogl_pixel_format_from_drm_format (uint32_t               drm_format,
                                   CoglPixelFormat       *out_format,
                                   CoglTextureComponents *out_components);

static void
meta_renderer_native_queue_modes_reset (MetaRendererNative *renderer_native);

static void
meta_renderer_native_gpu_data_free (MetaRendererNativeGpuData *renderer_gpu_data)
{
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);

  if (renderer_gpu_data->egl_display != EGL_NO_DISPLAY)
    meta_egl_terminate (egl, renderer_gpu_data->egl_display, NULL);

  g_clear_pointer (&renderer_gpu_data->gbm.device, gbm_device_destroy);
  g_free (renderer_gpu_data);
}

static MetaRendererNativeGpuData *
meta_renderer_native_get_gpu_data (MetaRendererNative *renderer_native,
                                   MetaGpuKms         *gpu_kms)
{
  return g_hash_table_lookup (renderer_native->gpu_datas, gpu_kms);
}

static MetaRendererNative *
meta_renderer_native_from_gpu (MetaGpuKms *gpu_kms)
{
  MetaBackend *backend = meta_gpu_get_backend (META_GPU (gpu_kms));

  return META_RENDERER_NATIVE (meta_backend_get_renderer (backend));
}

struct gbm_device *
meta_gbm_device_from_gpu (MetaGpuKms *gpu_kms)
{
  MetaRendererNative *renderer_native = meta_renderer_native_from_gpu (gpu_kms);
  MetaRendererNativeGpuData *renderer_gpu_data;

  renderer_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                         gpu_kms);

  return renderer_gpu_data->gbm.device;
}

static MetaRendererNativeGpuData *
meta_create_renderer_native_gpu_data (MetaGpuKms *gpu_kms)
{
  return g_new0 (MetaRendererNativeGpuData, 1);
}

static MetaEgl *
meta_renderer_native_get_egl (MetaRendererNative *renderer_native)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);

  return meta_backend_get_egl (meta_renderer_get_backend (renderer));
}

static MetaEgl *
meta_onscreen_native_get_egl (MetaOnscreenNative *onscreen_native)
{
  return meta_renderer_native_get_egl (onscreen_native->renderer_native);
}

static GArray *
get_supported_kms_modifiers (MetaCrtc *crtc,
                             uint32_t  format)
{
  GArray *modifiers;
  GArray *crtc_mods;
  unsigned int i;

  crtc_mods = meta_crtc_kms_get_modifiers (crtc, format);
  if (!crtc_mods)
    return NULL;

  modifiers = g_array_new (FALSE, FALSE, sizeof (uint64_t));

  /*
   * For each modifier from base_crtc, check if it's available on all other
   * CRTCs.
   */
  for (i = 0; i < crtc_mods->len; i++)
    {
      uint64_t modifier = g_array_index (crtc_mods, uint64_t, i);

      g_array_append_val (modifiers, modifier);
    }

  if (modifiers->len == 0)
    {
      g_array_free (modifiers, TRUE);
      return NULL;
    }

  return modifiers;
}

static GArray *
get_supported_egl_modifiers (CoglOnscreen *onscreen,
                             MetaCrtc     *crtc,
                             uint32_t      format)
{
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaRendererNative *renderer_native = onscreen_native->renderer_native;
  MetaEgl *egl = meta_onscreen_native_get_egl (onscreen_native);
  MetaGpu *gpu;
  MetaRendererNativeGpuData *renderer_gpu_data;
  EGLint num_modifiers;
  GArray *modifiers;
  GError *error = NULL;
  gboolean ret;

  gpu = meta_crtc_get_gpu (crtc);
  renderer_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                         META_GPU_KMS (gpu));

  if (!meta_egl_has_extensions (egl, renderer_gpu_data->egl_display, NULL,
                                "EGL_EXT_image_dma_buf_import_modifiers",
                                NULL))
    return NULL;

  ret = meta_egl_query_dma_buf_modifiers (egl, renderer_gpu_data->egl_display,
                                          format, 0, NULL, NULL,
                                          &num_modifiers, NULL);
  if (!ret || num_modifiers == 0)
    return NULL;

  modifiers = g_array_sized_new (FALSE, FALSE, sizeof (uint64_t),
                                 num_modifiers);
  ret = meta_egl_query_dma_buf_modifiers (egl, renderer_gpu_data->egl_display,
                                          format, num_modifiers,
                                          (EGLuint64KHR *) modifiers->data, NULL,
                                          &num_modifiers, &error);

  if (!ret)
    {
      g_warning ("Failed to query DMABUF modifiers: %s", error->message);
      g_error_free (error);
      g_array_free (modifiers, TRUE);
      return NULL;
    }

  return modifiers;
}

static GArray *
get_supported_modifiers (CoglOnscreen *onscreen,
                         uint32_t      format)
{
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaCrtc *crtc = onscreen_native->crtc;
  MetaGpu *gpu;
  g_autoptr (GArray) modifiers = NULL;

  gpu = meta_crtc_get_gpu (crtc);
  if (gpu == META_GPU (onscreen_native->render_gpu))
    modifiers = get_supported_kms_modifiers (crtc, format);
  else
    modifiers = get_supported_egl_modifiers (onscreen, crtc, format);

  return g_steal_pointer (&modifiers);
}

static GArray *
get_supported_kms_formats (CoglOnscreen *onscreen)
{
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaCrtc *crtc = onscreen_native->crtc;

  return meta_crtc_kms_copy_drm_format_list (crtc);
}

static gboolean
init_secondary_gpu_state_gpu_copy_mode (MetaRendererNative         *renderer_native,
                                        CoglOnscreen               *onscreen,
                                        MetaRendererNativeGpuData  *renderer_gpu_data,
                                        GError                    **error)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaEgl *egl = meta_onscreen_native_get_egl (onscreen_native);
  int width, height;
  EGLNativeWindowType egl_native_window;
  struct gbm_surface *gbm_surface;
  EGLSurface egl_surface;
  MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state;
  MetaGpuKms *gpu_kms;

  width = cogl_framebuffer_get_width (framebuffer);
  height = cogl_framebuffer_get_height (framebuffer);

  gbm_surface = gbm_surface_create (renderer_gpu_data->gbm.device,
                                    width, height,
                                    GBM_FORMAT_XRGB8888,
                                    GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
  if (!gbm_surface)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create gbm_surface: %s", strerror (errno));
      return FALSE;
    }

  egl_native_window = (EGLNativeWindowType) gbm_surface;
  egl_surface =
    meta_egl_create_window_surface (egl,
                                    renderer_gpu_data->egl_display,
                                    renderer_gpu_data->secondary.egl_config,
                                    egl_native_window,
                                    NULL,
                                    error);
  if (egl_surface == EGL_NO_SURFACE)
    {
      gbm_surface_destroy (gbm_surface);
      return FALSE;
    }

  secondary_gpu_state = g_new0 (MetaOnscreenNativeSecondaryGpuState, 1);

  gpu_kms = META_GPU_KMS (meta_crtc_get_gpu (onscreen_native->crtc));
  secondary_gpu_state->gpu_kms = gpu_kms;
  secondary_gpu_state->renderer_gpu_data = renderer_gpu_data;
  secondary_gpu_state->gbm.surface = gbm_surface;
  secondary_gpu_state->egl_surface = egl_surface;

  onscreen_native->secondary_gpu_state = secondary_gpu_state;

  return TRUE;
}

static void
secondary_gpu_release_dumb (MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state)
{
  MetaGpuKms *gpu_kms = secondary_gpu_state->gpu_kms;
  unsigned i;

  for (i = 0; i < G_N_ELEMENTS (secondary_gpu_state->cpu.dumb_fbs); i++)
    release_dumb_fb (&secondary_gpu_state->cpu.dumb_fbs[i], gpu_kms);
}

static void
secondary_gpu_state_free (MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state)
{
  MetaBackend *backend = meta_get_backend ();
  MetaEgl *egl = meta_backend_get_egl (backend);

  if (secondary_gpu_state->egl_surface != EGL_NO_SURFACE)
    {
      MetaRendererNativeGpuData *renderer_gpu_data;

      renderer_gpu_data = secondary_gpu_state->renderer_gpu_data;
      meta_egl_destroy_surface (egl,
                                renderer_gpu_data->egl_display,
                                secondary_gpu_state->egl_surface,
                                NULL);
    }

  g_clear_object (&secondary_gpu_state->gbm.current_fb);
  g_clear_object (&secondary_gpu_state->gbm.next_fb);
  g_clear_pointer (&secondary_gpu_state->gbm.surface, gbm_surface_destroy);

  secondary_gpu_release_dumb (secondary_gpu_state);

  g_free (secondary_gpu_state);
}

static uint32_t
pick_secondary_gpu_framebuffer_format_for_cpu (CoglOnscreen *onscreen)
{
  /*
   * cogl_framebuffer_read_pixels_into_bitmap () supported formats in
   * preference order. Ideally these should depend on the render buffer
   * format copy_shared_framebuffer_cpu () will be reading from but
   * alpha channel ignored.
   */
  static const uint32_t preferred_formats[] =
    {
      /*
       * DRM_FORMAT_XBGR8888 a.k.a GL_RGBA, GL_UNSIGNED_BYTE on
       * little-endian is possibly the most optimized glReadPixels
       * output format. glReadPixels cannot avoid manufacturing an alpha
       * channel if the render buffer does not have one and converting
       * to ABGR8888 may be more optimized than ARGB8888.
       */
      DRM_FORMAT_XBGR8888,
      /* The rest are other fairly commonly used formats in OpenGL. */
      DRM_FORMAT_XRGB8888,
    };
  g_autoptr (GArray) formats = NULL;
  size_t k;
  unsigned int i;
  uint32_t drm_format;

  formats = get_supported_kms_formats (onscreen);

  /* Check if any of our preferred formats are supported. */
  for (k = 0; k < G_N_ELEMENTS (preferred_formats); k++)
    {
      g_assert (cogl_pixel_format_from_drm_format (preferred_formats[k],
                                                   NULL,
                                                   NULL));

      for (i = 0; i < formats->len; i++)
        {
          drm_format = g_array_index (formats, uint32_t, i);

          if (drm_format == preferred_formats[k])
            return drm_format;
        }
    }

  /*
   * Otherwise just pick an arbitrary format we recognize. The formats
   * list is not in any specific order and we don't know any better
   * either.
   */
  for (i = 0; i < formats->len; i++)
    {
      drm_format = g_array_index (formats, uint32_t, i);

      if (cogl_pixel_format_from_drm_format (drm_format, NULL, NULL))
        return drm_format;
    }

  return DRM_FORMAT_INVALID;
}

static gboolean
init_secondary_gpu_state_cpu_copy_mode (MetaRendererNative         *renderer_native,
                                        CoglOnscreen               *onscreen,
                                        MetaRendererNativeGpuData  *renderer_gpu_data,
                                        GError                    **error)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state;
  MetaGpuKms *gpu_kms;
  int width, height;
  unsigned int i;
  uint32_t drm_format;
  MetaDrmFormatBuf tmp;

  drm_format = pick_secondary_gpu_framebuffer_format_for_cpu (onscreen);
  if (drm_format == DRM_FORMAT_INVALID)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Could not find a suitable pixel format in CPU copy mode");
      return FALSE;
    }

  width = cogl_framebuffer_get_width (framebuffer);
  height = cogl_framebuffer_get_height (framebuffer);

  gpu_kms = META_GPU_KMS (meta_crtc_get_gpu (onscreen_native->crtc));
  g_debug ("Secondary GPU %s using DRM format '%s' (0x%x) for a %dx%d output.",
           meta_gpu_kms_get_file_path (gpu_kms),
           meta_drm_format_to_string (&tmp, drm_format),
           drm_format,
           width, height);

  secondary_gpu_state = g_new0 (MetaOnscreenNativeSecondaryGpuState, 1);
  secondary_gpu_state->renderer_gpu_data = renderer_gpu_data;
  secondary_gpu_state->gpu_kms = gpu_kms;
  secondary_gpu_state->egl_surface = EGL_NO_SURFACE;

  for (i = 0; i < G_N_ELEMENTS (secondary_gpu_state->cpu.dumb_fbs); i++)
    {
      MetaDumbBuffer *dumb_fb = &secondary_gpu_state->cpu.dumb_fbs[i];

      if (!init_dumb_fb (dumb_fb,
                         gpu_kms,
                         width, height,
                         drm_format,
                         error))
        {
          secondary_gpu_state_free (secondary_gpu_state);
          return FALSE;
        }
    }

  /*
   * This function initializes everything needed for
   * META_SHARED_FRAMEBUFFER_COPY_MODE_ZERO as well.
   */
  secondary_gpu_state->import_status =
    META_SHARED_FRAMEBUFFER_IMPORT_STATUS_NONE;

  onscreen_native->secondary_gpu_state = secondary_gpu_state;

  return TRUE;
}

static gboolean
init_secondary_gpu_state (MetaRendererNative  *renderer_native,
                          CoglOnscreen        *onscreen,
                          GError             **error)
{
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaGpu *gpu = meta_crtc_get_gpu (onscreen_native->crtc);
  MetaRendererNativeGpuData *renderer_gpu_data;

  renderer_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                         META_GPU_KMS (gpu));

  switch (renderer_gpu_data->secondary.copy_mode)
    {
    case META_SHARED_FRAMEBUFFER_COPY_MODE_SECONDARY_GPU:
      if (!init_secondary_gpu_state_gpu_copy_mode (renderer_native,
                                                   onscreen,
                                                   renderer_gpu_data,
                                                   error))
        return FALSE;
      break;
    case META_SHARED_FRAMEBUFFER_COPY_MODE_ZERO:
      /*
       * Initialize also the primary copy mode, so that if zero-copy
       * path fails, which is quite likely, we can simply continue
       * with the primary copy path on the very first frame.
       */
      G_GNUC_FALLTHROUGH;
    case META_SHARED_FRAMEBUFFER_COPY_MODE_PRIMARY:
      if (!init_secondary_gpu_state_cpu_copy_mode (renderer_native,
                                                   onscreen,
                                                   renderer_gpu_data,
                                                   error))
        return FALSE;
      break;
    }

  return TRUE;
}

static void
meta_renderer_native_disconnect (CoglRenderer *cogl_renderer)
{
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;

  g_slice_free (CoglRendererEGL, cogl_renderer_egl);
}

static void
flush_pending_swap_notify (CoglFramebuffer *framebuffer)
{
  if (framebuffer->type == COGL_FRAMEBUFFER_TYPE_ONSCREEN)
    {
      CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);
      CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
      MetaOnscreenNative *onscreen_native = onscreen_egl->platform;

      if (onscreen_native->pending_swap_notify)
        {
          CoglFrameInfo *info;

          while ((info = g_queue_peek_head (&onscreen->pending_frame_infos)) &&
                 info->global_frame_counter <= onscreen_native->pending_swap_notify_frame_count)
            {
              _cogl_onscreen_notify_frame_sync (onscreen, info);
              _cogl_onscreen_notify_complete (onscreen, info);
              cogl_object_unref (info);
              g_queue_pop_head (&onscreen->pending_frame_infos);
            }

          onscreen_native->pending_swap_notify = FALSE;
          cogl_object_unref (onscreen);
        }
    }
}

static void
flush_pending_swap_notify_idle (void *user_data)
{
  CoglContext *cogl_context = user_data;
  CoglRendererEGL *cogl_renderer_egl = cogl_context->display->renderer->winsys;
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;
  GList *l;

  /* This needs to be disconnected before invoking the callbacks in
   * case the callbacks cause it to be queued again */
  _cogl_closure_disconnect (renderer_native->swap_notify_idle);
  renderer_native->swap_notify_idle = NULL;

  l = cogl_context->framebuffers;
  while (l)
    {
      GList *next = l->next;
      CoglFramebuffer *framebuffer = l->data;

      flush_pending_swap_notify (framebuffer);

      l = next;
    }
}

static void
free_current_secondary_bo (CoglOnscreen *onscreen)
{
  CoglOnscreenEGL *onscreen_egl =  onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state;

  secondary_gpu_state = onscreen_native->secondary_gpu_state;
  if (!secondary_gpu_state)
    return;

  g_clear_object (&secondary_gpu_state->gbm.current_fb);
}

static void
free_current_bo (CoglOnscreen *onscreen)
{
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;

  g_clear_object (&onscreen_native->gbm.current_fb);
  free_current_secondary_bo (onscreen);
}

static void
meta_onscreen_native_queue_swap_notify (CoglOnscreen *onscreen)
{
  CoglOnscreenEGL *onscreen_egl =  onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaRendererNative *renderer_native = onscreen_native->renderer_native;

  onscreen_native->pending_swap_notify_frame_count =
    onscreen_native->pending_queue_swap_notify_frame_count;

  if (onscreen_native->pending_swap_notify)
    return;

  /* We only want to notify that the swap is complete when the
   * application calls cogl_context_dispatch so instead of
   * immediately notifying we queue an idle callback */
  if (!renderer_native->swap_notify_idle)
    {
      CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
      CoglContext *cogl_context = framebuffer->context;
      CoglRenderer *cogl_renderer = cogl_context->display->renderer;

      renderer_native->swap_notify_idle =
        _cogl_poll_renderer_add_idle (cogl_renderer,
                                      flush_pending_swap_notify_idle,
                                      cogl_context,
                                      NULL);
    }

  /*
   * The framebuffer will have its own referenc while the swap notify is
   * pending. Otherwise when destroying the view would drop the pending
   * notification with if the destruction happens before the idle callback
   * is invoked.
   */
  cogl_object_ref (onscreen);
  onscreen_native->pending_swap_notify = TRUE;
}

static gboolean
meta_renderer_native_connect (CoglRenderer *cogl_renderer,
                              GError      **error)
{
  CoglRendererEGL *cogl_renderer_egl;
  MetaGpuKms *gpu_kms = cogl_renderer->custom_winsys_user_data;
  MetaRendererNative *renderer_native = meta_renderer_native_from_gpu (gpu_kms);
  MetaRendererNativeGpuData *renderer_gpu_data;

  cogl_renderer->winsys = g_slice_new0 (CoglRendererEGL);
  cogl_renderer_egl = cogl_renderer->winsys;

  renderer_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                         gpu_kms);

  cogl_renderer_egl->platform_vtable = &_cogl_winsys_egl_vtable;
  cogl_renderer_egl->platform = renderer_gpu_data;
  cogl_renderer_egl->edpy = renderer_gpu_data->egl_display;

  if (!_cogl_winsys_egl_renderer_connect_common (cogl_renderer, error))
    goto fail;

  return TRUE;

fail:
  meta_renderer_native_disconnect (cogl_renderer);

  return FALSE;
}

static int
meta_renderer_native_add_egl_config_attributes (CoglDisplay           *cogl_display,
                                                CoglFramebufferConfig *config,
                                                EGLint                *attributes)
{
  CoglRendererEGL *cogl_renderer_egl = cogl_display->renderer->winsys;
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  int i = 0;

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      attributes[i++] = EGL_SURFACE_TYPE;
      attributes[i++] = EGL_WINDOW_BIT;
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      attributes[i++] = EGL_SURFACE_TYPE;
      attributes[i++] = EGL_STREAM_BIT_KHR;
      break;
#endif
    }

  return i;
}

static gboolean
choose_egl_config_from_gbm_format (MetaEgl       *egl,
                                   EGLDisplay     egl_display,
                                   const EGLint  *attributes,
                                   uint32_t       gbm_format,
                                   EGLConfig     *out_config,
                                   GError       **error)
{
  EGLConfig *egl_configs;
  EGLint n_configs;
  EGLint i;

  egl_configs = meta_egl_choose_all_configs (egl, egl_display,
                                             attributes,
                                             &n_configs,
                                             error);
  if (!egl_configs)
    return FALSE;

  for (i = 0; i < n_configs; i++)
    {
      EGLint visual_id;

      if (!meta_egl_get_config_attrib (egl, egl_display,
                                       egl_configs[i],
                                       EGL_NATIVE_VISUAL_ID,
                                       &visual_id,
                                       error))
        {
          g_free (egl_configs);
          return FALSE;
        }

      if ((uint32_t) visual_id == gbm_format)
        {
          *out_config = egl_configs[i];
          g_free (egl_configs);
          return TRUE;
        }
    }

  g_free (egl_configs);
  g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
               "No EGL config matching supported GBM format found");
  return FALSE;
}

static gboolean
meta_renderer_native_choose_egl_config (CoglDisplay  *cogl_display,
                                        EGLint       *attributes,
                                        EGLConfig    *out_config,
                                        GError      **error)
{
  CoglRenderer *cogl_renderer = cogl_display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaBackend *backend = meta_get_backend ();
  MetaEgl *egl = meta_backend_get_egl (backend);
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  EGLDisplay egl_display = cogl_renderer_egl->edpy;

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      return choose_egl_config_from_gbm_format (egl,
                                                egl_display,
                                                attributes,
                                                GBM_FORMAT_XRGB8888,
                                                out_config,
                                                error);
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      return meta_egl_choose_first_config (egl,
                                           egl_display,
                                           attributes,
                                           out_config,
                                           error);
#endif
    }

  return FALSE;
}

static gboolean
meta_renderer_native_setup_egl_display (CoglDisplay *cogl_display,
                                        GError     **error)
{
  CoglDisplayEGL *cogl_display_egl = cogl_display->winsys;
  CoglRendererEGL *cogl_renderer_egl = cogl_display->renderer->winsys;
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;

  cogl_display_egl->platform = renderer_native;

  /* Force a full modeset / drmModeSetCrtc on
   * the first swap buffers call.
   */
  meta_renderer_native_queue_modes_reset (renderer_native);

  return TRUE;
}

static void
meta_renderer_native_destroy_egl_display (CoglDisplay *cogl_display)
{
}

static EGLSurface
create_dummy_pbuffer_surface (EGLDisplay egl_display,
                              GError   **error)
{
  MetaBackend *backend = meta_get_backend ();
  MetaEgl *egl = meta_backend_get_egl (backend);
  EGLConfig pbuffer_config;
  static const EGLint pbuffer_config_attribs[] = {
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_RED_SIZE, 1,
    EGL_GREEN_SIZE, 1,
    EGL_BLUE_SIZE, 1,
    EGL_ALPHA_SIZE, 0,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_NONE
  };
  static const EGLint pbuffer_attribs[] = {
    EGL_WIDTH, 16,
    EGL_HEIGHT, 16,
    EGL_NONE
  };

  if (!meta_egl_choose_first_config (egl, egl_display, pbuffer_config_attribs,
                                     &pbuffer_config, error))
    return EGL_NO_SURFACE;

  return meta_egl_create_pbuffer_surface (egl, egl_display,
                                          pbuffer_config, pbuffer_attribs,
                                          error);
}

static gboolean
meta_renderer_native_egl_context_created (CoglDisplay *cogl_display,
                                          GError     **error)
{
  CoglDisplayEGL *cogl_display_egl = cogl_display->winsys;
  CoglRenderer *cogl_renderer = cogl_display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;

  if ((cogl_renderer_egl->private_features &
       COGL_EGL_WINSYS_FEATURE_SURFACELESS_CONTEXT) == 0)
    {
      cogl_display_egl->dummy_surface =
        create_dummy_pbuffer_surface (cogl_renderer_egl->edpy, error);
      if (cogl_display_egl->dummy_surface == EGL_NO_SURFACE)
        return FALSE;
    }

  if (!_cogl_winsys_egl_make_current (cogl_display,
                                      cogl_display_egl->dummy_surface,
                                      cogl_display_egl->dummy_surface,
                                      cogl_display_egl->egl_context))
    {
      g_set_error (error, COGL_WINSYS_ERROR,
                   COGL_WINSYS_ERROR_CREATE_CONTEXT,
                   "Failed to make context current");
      return FALSE;
    }

  return TRUE;
}

static void
meta_renderer_native_egl_cleanup_context (CoglDisplay *cogl_display)
{
  CoglDisplayEGL *cogl_display_egl = cogl_display->winsys;
  CoglRenderer *cogl_renderer = cogl_display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);

  if (cogl_display_egl->dummy_surface != EGL_NO_SURFACE)
    {
      meta_egl_destroy_surface (egl,
                                cogl_renderer_egl->edpy,
                                cogl_display_egl->dummy_surface,
                                NULL);
      cogl_display_egl->dummy_surface = EGL_NO_SURFACE;
    }
}

static void
swap_secondary_drm_fb (CoglOnscreen *onscreen)
{
  CoglOnscreenEGL *onscreen_egl =  onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state;

  secondary_gpu_state = onscreen_native->secondary_gpu_state;
  if (!secondary_gpu_state)
    return;

  g_set_object (&secondary_gpu_state->gbm.current_fb,
                secondary_gpu_state->gbm.next_fb);
  g_clear_object (&secondary_gpu_state->gbm.next_fb);
}

static void
meta_onscreen_native_swap_drm_fb (CoglOnscreen *onscreen)
{
  CoglOnscreenEGL *onscreen_egl =  onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;

  free_current_bo (onscreen);

  g_set_object (&onscreen_native->gbm.current_fb, onscreen_native->gbm.next_fb);
  g_clear_object (&onscreen_native->gbm.next_fb);

  swap_secondary_drm_fb (onscreen);
}

static void
notify_view_crtc_presented (MetaRendererView *view,
                            MetaKmsCrtc      *kms_crtc,
                            int64_t           time_ns)
{
  ClutterStageView *stage_view = CLUTTER_STAGE_VIEW (view);
  CoglFramebuffer *framebuffer =
    clutter_stage_view_get_onscreen (stage_view);
  CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);
  CoglOnscreenEGL *onscreen_egl =  onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaRendererNative *renderer_native = onscreen_native->renderer_native;
  MetaGpuKms *render_gpu = onscreen_native->render_gpu;
  CoglFrameInfo *frame_info;
  MetaCrtc *crtc;
  float refresh_rate;
  MetaGpuKms *gpu_kms;

  /* Only keep the frame info for the fastest CRTC in use, which may not be
   * the first one to complete a flip. By only telling the compositor about the
   * fastest monitor(s) we direct it to produce new frames fast enough to
   * satisfy all monitors.
   */
  frame_info = g_queue_peek_tail (&onscreen->pending_frame_infos);

  crtc = meta_crtc_kms_from_kms_crtc (kms_crtc);
  refresh_rate = crtc && crtc->config ?
                 crtc->config->mode->refresh_rate :
                 0.0f;
  if (refresh_rate >= frame_info->refresh_rate)
    {
      frame_info->presentation_time = time_ns;
      frame_info->refresh_rate = refresh_rate;
    }

  gpu_kms = META_GPU_KMS (meta_crtc_get_gpu (crtc));
  if (gpu_kms != render_gpu)
    {
      MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state =
        onscreen_native->secondary_gpu_state;

      secondary_gpu_state->pending_flips--;
    }

  onscreen_native->total_pending_flips--;
  if (onscreen_native->total_pending_flips == 0)
    {
      MetaRendererNativeGpuData *renderer_gpu_data;

      meta_onscreen_native_queue_swap_notify (onscreen);

      renderer_gpu_data =
        meta_renderer_native_get_gpu_data (renderer_native,
                                           onscreen_native->render_gpu);
      switch (renderer_gpu_data->mode)
        {
        case META_RENDERER_NATIVE_MODE_GBM:
          meta_onscreen_native_swap_drm_fb (onscreen);
          break;
#ifdef HAVE_EGL_DEVICE
        case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
          break;
#endif
        }
    }
}

static int64_t
timeval_to_nanoseconds (const struct timeval *tv)
{
  int64_t usec = ((int64_t) tv->tv_sec) * G_USEC_PER_SEC + tv->tv_usec;
  int64_t nsec = usec * 1000;

  return nsec;
}

static void
page_flip_feedback_flipped (MetaKmsCrtc  *kms_crtc,
                            unsigned int  sequence,
                            unsigned int  tv_sec,
                            unsigned int  tv_usec,
                            gpointer      user_data)
{
  MetaRendererView *view = user_data;
  struct timeval page_flip_time;

  page_flip_time = (struct timeval) {
    .tv_sec = tv_sec,
    .tv_usec = tv_usec,
  };

  notify_view_crtc_presented (view, kms_crtc,
                              timeval_to_nanoseconds (&page_flip_time));

  g_object_unref (view);
}

static void
page_flip_feedback_mode_set_fallback (MetaKmsCrtc *kms_crtc,
                                      gpointer     user_data)
{
  MetaRendererView *view = user_data;
  MetaCrtc *crtc;
  MetaGpuKms *gpu_kms;
  int64_t now_ns;

  /*
   * We ended up not page flipping, thus we don't have a presentation time to
   * use. Lets use the next best thing: the current time.
   */

  crtc = meta_crtc_kms_from_kms_crtc (kms_crtc);
  gpu_kms = META_GPU_KMS (meta_crtc_get_gpu (crtc));
  now_ns = meta_gpu_kms_get_current_time_ns (gpu_kms);

  notify_view_crtc_presented (view, kms_crtc, now_ns);

  g_object_unref (view);
}

static void
page_flip_feedback_discarded (MetaKmsCrtc  *kms_crtc,
                              gpointer      user_data,
                              const GError *error)
{
  MetaRendererView *view = user_data;
  MetaCrtc *crtc;
  MetaGpuKms *gpu_kms;
  int64_t now_ns;

  /*
   * Page flipping failed, but we want to fail gracefully, so to avoid freezing
   * the frame clack, pretend we flipped.
   */

  if (error)
    g_warning ("Page flip discarded: %s", error->message);

  crtc = meta_crtc_kms_from_kms_crtc (kms_crtc);
  gpu_kms = META_GPU_KMS (meta_crtc_get_gpu (crtc));
  now_ns = meta_gpu_kms_get_current_time_ns (gpu_kms);

  notify_view_crtc_presented (view, kms_crtc, now_ns);

  g_object_unref (view);
}

static const MetaKmsPageFlipFeedback page_flip_feedback = {
  .flipped = page_flip_feedback_flipped,
  .mode_set_fallback = page_flip_feedback_mode_set_fallback,
  .discarded = page_flip_feedback_discarded,
};

#ifdef HAVE_EGL_DEVICE
static int
custom_egl_stream_page_flip (gpointer custom_page_flip_data,
                             gpointer user_data)
{
  MetaOnscreenNative *onscreen_native = custom_page_flip_data;
  MetaRendererView *view = user_data;
  MetaEgl *egl = meta_onscreen_native_get_egl (onscreen_native);
  MetaRendererNativeGpuData *renderer_gpu_data;
  EGLDisplay *egl_display;
  EGLAttrib *acquire_attribs;
  g_autoptr (GError) error = NULL;

  acquire_attribs = (EGLAttrib[]) {
    EGL_DRM_FLIP_EVENT_DATA_NV,
    (EGLAttrib) view,
    EGL_NONE
  };

  renderer_gpu_data =
    meta_renderer_native_get_gpu_data (onscreen_native->renderer_native,
                                       onscreen_native->render_gpu);

  egl_display = renderer_gpu_data->egl_display;
  if (!meta_egl_stream_consumer_acquire_attrib (egl,
                                                egl_display,
                                                onscreen_native->egl.stream,
                                                acquire_attribs,
                                                &error))
    {
      if (g_error_matches (error, META_EGL_ERROR, EGL_RESOURCE_BUSY_EXT))
        return -EBUSY;
      else
        return -EINVAL;
    }

  return 0;
}
#endif /* HAVE_EGL_DEVICE */

static void
dummy_power_save_page_flip (CoglOnscreen *onscreen)
{
  meta_onscreen_native_swap_drm_fb (onscreen);
  meta_onscreen_native_queue_swap_notify (onscreen);
}

static gboolean
dummy_power_save_page_flip_cb (gpointer user_data)
{
  MetaRendererNative *renderer_native = user_data;

  g_list_foreach (renderer_native->power_save_page_flip_onscreens,
                  (GFunc) dummy_power_save_page_flip, NULL);
  g_list_free_full (renderer_native->power_save_page_flip_onscreens,
                    (GDestroyNotify) cogl_object_unref);
  renderer_native->power_save_page_flip_onscreens = NULL;
  renderer_native->power_save_page_flip_source_id = 0;

  return G_SOURCE_REMOVE;
}

static void
queue_dummy_power_save_page_flip (CoglOnscreen *onscreen)
{
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaRendererNative *renderer_native = onscreen_native->renderer_native;
  const unsigned int timeout_ms = 100;

  if (!renderer_native->power_save_page_flip_source_id)
    {
      renderer_native->power_save_page_flip_source_id =
        g_timeout_add (timeout_ms,
                       dummy_power_save_page_flip_cb,
                       renderer_native);
    }

  renderer_native->power_save_page_flip_onscreens =
    g_list_prepend (renderer_native->power_save_page_flip_onscreens,
                    cogl_object_ref (onscreen));
}

static void
meta_onscreen_native_flip_crtc (CoglOnscreen     *onscreen,
                                MetaRendererView *view,
                                MetaCrtc         *crtc,
                                MetaKmsUpdate    *kms_update)
{
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaRendererNative *renderer_native = onscreen_native->renderer_native;
  MetaGpuKms *render_gpu = onscreen_native->render_gpu;
  MetaRendererNativeGpuData *renderer_gpu_data;
  MetaGpuKms *gpu_kms;
  MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state = NULL;
  uint32_t fb_id;

  gpu_kms = META_GPU_KMS (meta_crtc_get_gpu (crtc));

  g_assert (meta_gpu_kms_is_crtc_active (gpu_kms, crtc));

  renderer_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                         render_gpu);
  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      if (gpu_kms == render_gpu)
        {
          fb_id = meta_drm_buffer_get_fb_id (onscreen_native->gbm.next_fb);
        }
      else
        {
          secondary_gpu_state = onscreen_native->secondary_gpu_state;
          fb_id = meta_drm_buffer_get_fb_id (secondary_gpu_state->gbm.next_fb);
        }

      meta_crtc_kms_assign_primary_plane (crtc, fb_id, kms_update);
      meta_crtc_kms_page_flip (crtc,
                               &page_flip_feedback,
                               g_object_ref (view),
                               kms_update);

      onscreen_native->total_pending_flips++;
      if (secondary_gpu_state)
        secondary_gpu_state->pending_flips++;

      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      meta_kms_update_custom_page_flip (kms_update,
                                        meta_crtc_kms_get_kms_crtc (crtc),
                                        &page_flip_feedback,
                                        g_object_ref (view),
                                        custom_egl_stream_page_flip,
                                        onscreen_native);
      onscreen_native->total_pending_flips++;
      break;
#endif
    }
}

static void
meta_onscreen_native_set_crtc_mode (CoglOnscreen              *onscreen,
                                    MetaRendererNativeGpuData *renderer_gpu_data,
                                    MetaKmsUpdate             *kms_update)
{
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;

  COGL_TRACE_BEGIN_SCOPED (MetaOnscreenNativeSetCrtcModes,
                           "Onscreen (set CRTC modes)");

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      {
        uint32_t fb_id;

        fb_id = onscreen_native->egl.dumb_fb.fb_id;
        meta_crtc_kms_assign_primary_plane (onscreen_native->crtc,
                                            fb_id, kms_update);
        break;
      }
#endif
    }

  meta_crtc_kms_set_mode (onscreen_native->crtc, kms_update);
  meta_output_kms_set_underscan (onscreen_native->output, kms_update);
}

static void
meta_onscreen_native_flip_crtcs (CoglOnscreen  *onscreen,
                                 MetaKmsUpdate *kms_update)
{
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaRendererView *view = onscreen_native->view;
  MetaRendererNative *renderer_native = onscreen_native->renderer_native;
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (meta_renderer_get_backend (renderer));
  MetaPowerSave power_save_mode;

  COGL_TRACE_BEGIN_SCOPED (MetaOnscreenNativeFlipCrtcs,
                           "Onscreen (flip CRTCs)");

  power_save_mode = meta_monitor_manager_get_power_save_mode (monitor_manager);
  if (power_save_mode == META_POWER_SAVE_ON)
    {
      meta_onscreen_native_flip_crtc (onscreen, view, onscreen_native->crtc,
                                      kms_update);
    }
  else
    {
      queue_dummy_power_save_page_flip (onscreen);
    }
}

static void
wait_for_pending_flips (CoglOnscreen *onscreen)
{
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state;
  GError *error = NULL;

  secondary_gpu_state = onscreen_native->secondary_gpu_state;
  if (secondary_gpu_state)
    {
      while (secondary_gpu_state->pending_flips)
        {
          if (!meta_gpu_kms_wait_for_flip (secondary_gpu_state->gpu_kms, &error))
            {
              g_warning ("Failed to wait for flip on secondary GPU: %s",
                         error->message);
              g_clear_error (&error);
              break;
            }
        }
    }

  while (onscreen_native->total_pending_flips)
    {
      if (!meta_gpu_kms_wait_for_flip (onscreen_native->render_gpu, &error))
        {
          g_warning ("Failed to wait for flip: %s", error->message);
          g_clear_error (&error);
          break;
        }
    }
}

static gboolean
import_shared_framebuffer (CoglOnscreen                        *onscreen,
                           MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state)
{
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaDrmBufferGbm *buffer_gbm;
  MetaDrmBufferImport *buffer_import;
  g_autoptr (GError) error = NULL;

  buffer_gbm = META_DRM_BUFFER_GBM (onscreen_native->gbm.next_fb);

  buffer_import = meta_drm_buffer_import_new (secondary_gpu_state->gpu_kms,
                                              buffer_gbm,
                                              &error);
  if (!buffer_import)
    {
      g_debug ("Zero-copy disabled for %s, meta_drm_buffer_import_new failed: %s",
               meta_gpu_kms_get_file_path (secondary_gpu_state->gpu_kms),
               error->message);

      g_warn_if_fail (secondary_gpu_state->import_status ==
                      META_SHARED_FRAMEBUFFER_IMPORT_STATUS_NONE);

      /*
       * Fall back. If META_SHARED_FRAMEBUFFER_IMPORT_STATUS_NONE is
       * in effect, we have COPY_MODE_PRIMARY prepared already, so we
       * simply retry with that path. Import status cannot be FAILED,
       * because we should not retry if failed once.
       *
       * If import status is OK, that is unexpected and we do not
       * have the fallback path prepared which means this output cannot
       * work anymore.
       */
      secondary_gpu_state->renderer_gpu_data->secondary.copy_mode =
        META_SHARED_FRAMEBUFFER_COPY_MODE_PRIMARY;

      secondary_gpu_state->import_status =
        META_SHARED_FRAMEBUFFER_IMPORT_STATUS_FAILED;
      return FALSE;
    }

  /*
   * next_fb may already contain a fallback buffer, so clear it only
   * when we are sure to succeed.
   */
  g_clear_object (&secondary_gpu_state->gbm.next_fb);
  secondary_gpu_state->gbm.next_fb = META_DRM_BUFFER (buffer_import);

  if (secondary_gpu_state->import_status ==
      META_SHARED_FRAMEBUFFER_IMPORT_STATUS_NONE)
    {
      /*
       * Clean up the cpu-copy part of
       * init_secondary_gpu_state_cpu_copy_mode ()
       */
      secondary_gpu_release_dumb (secondary_gpu_state);

      g_debug ("Using zero-copy for %s succeeded once.",
               meta_gpu_kms_get_file_path (secondary_gpu_state->gpu_kms));
    }

  secondary_gpu_state->import_status =
    META_SHARED_FRAMEBUFFER_IMPORT_STATUS_OK;
  return TRUE;
}

static void
copy_shared_framebuffer_gpu (CoglOnscreen                        *onscreen,
                             MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state,
                             MetaRendererNativeGpuData           *renderer_gpu_data,
                             gboolean                            *egl_context_changed)
{
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);
  GError *error = NULL;
  MetaDrmBufferGbm *buffer_gbm;
  struct gbm_bo *bo;

  COGL_TRACE_BEGIN_SCOPED (CopySharedFramebufferSecondaryGpu,
                           "FB Copy (secondary GPU)");

  g_warn_if_fail (secondary_gpu_state->gbm.next_fb == NULL);
  g_clear_object (&secondary_gpu_state->gbm.next_fb);

  if (!meta_egl_make_current (egl,
                              renderer_gpu_data->egl_display,
                              secondary_gpu_state->egl_surface,
                              secondary_gpu_state->egl_surface,
                              renderer_gpu_data->secondary.egl_context,
                              &error))
    {
      g_warning ("Failed to make current: %s", error->message);
      g_error_free (error);
      return;
    }

  *egl_context_changed = TRUE;

  buffer_gbm = META_DRM_BUFFER_GBM (onscreen_native->gbm.next_fb);
  bo =  meta_drm_buffer_gbm_get_bo (buffer_gbm);
  if (!meta_renderer_native_gles3_blit_shared_bo (egl,
                                                  renderer_native->gles3,
                                                  renderer_gpu_data->egl_display,
                                                  renderer_gpu_data->secondary.egl_context,
                                                  secondary_gpu_state->egl_surface,
                                                  bo,
                                                  &error))
    {
      g_warning ("Failed to blit shared framebuffer: %s", error->message);
      g_error_free (error);
      return;
    }

  if (!meta_egl_swap_buffers (egl,
                              renderer_gpu_data->egl_display,
                              secondary_gpu_state->egl_surface,
                              &error))
    {
      g_warning ("Failed to swap buffers: %s", error->message);
      g_error_free (error);
      return;
    }

  buffer_gbm = meta_drm_buffer_gbm_new (secondary_gpu_state->gpu_kms,
                                        secondary_gpu_state->gbm.surface,
                                        renderer_native->use_modifiers,
                                        &error);
  if (!buffer_gbm)
    {
      g_warning ("meta_drm_buffer_gbm_new failed: %s",
                 error->message);
      g_error_free (error);
      return;
    }

  secondary_gpu_state->gbm.next_fb = META_DRM_BUFFER (buffer_gbm);
}

static MetaDumbBuffer *
secondary_gpu_get_next_dumb_buffer (MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state)
{
  MetaDumbBuffer *current_dumb_fb;

  current_dumb_fb = secondary_gpu_state->cpu.dumb_fb;
  if (current_dumb_fb == &secondary_gpu_state->cpu.dumb_fbs[0])
    return &secondary_gpu_state->cpu.dumb_fbs[1];
  else
    return &secondary_gpu_state->cpu.dumb_fbs[0];
}

static CoglContext *
cogl_context_from_renderer_native (MetaRendererNative *renderer_native)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);

  return clutter_backend_get_cogl_context (clutter_backend);
}

static CoglFramebuffer *
create_dma_buf_framebuffer (MetaRendererNative  *renderer_native,
                            int                  dmabuf_fd,
                            uint32_t             width,
                            uint32_t             height,
                            uint32_t             stride,
                            uint32_t             offset,
                            uint64_t             modifier,
                            uint32_t             drm_format,
                            GError             **error)
{
  CoglContext *cogl_context =
    cogl_context_from_renderer_native (renderer_native);
  CoglDisplay *cogl_display = cogl_context->display;
  CoglRenderer *cogl_renderer = cogl_display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  EGLDisplay egl_display = cogl_renderer_egl->edpy;
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);
  EGLImageKHR egl_image;
  uint32_t strides[1];
  uint32_t offsets[1];
  uint64_t modifiers[1];
  CoglPixelFormat cogl_format;
  CoglEglImageFlags flags;
  CoglTexture2D *cogl_tex;
  CoglOffscreen *cogl_fbo;
  int ret;

  ret = cogl_pixel_format_from_drm_format (drm_format, &cogl_format, NULL);
  g_assert (ret);

  strides[0] = stride;
  offsets[0] = offset;
  modifiers[0] = modifier;
  egl_image = meta_egl_create_dmabuf_image (egl,
                                            egl_display,
                                            width,
                                            height,
                                            drm_format,
                                            1 /* n_planes */,
                                            &dmabuf_fd,
                                            strides,
                                            offsets,
                                            modifiers,
                                            error);
  if (egl_image == EGL_NO_IMAGE_KHR)
    return NULL;

  flags = COGL_EGL_IMAGE_FLAG_NO_GET_DATA;
  cogl_tex = cogl_egl_texture_2d_new_from_image (cogl_context,
                                                 width,
                                                 height,
                                                 cogl_format,
                                                 egl_image,
                                                 flags,
                                                 error);

  meta_egl_destroy_image (egl, egl_display, egl_image, NULL);

  if (!cogl_tex)
    return NULL;

  cogl_fbo = cogl_offscreen_new_with_texture (COGL_TEXTURE (cogl_tex));
  cogl_object_unref (cogl_tex);

  if (!cogl_framebuffer_allocate (COGL_FRAMEBUFFER (cogl_fbo), error))
    {
      cogl_object_unref (cogl_fbo);
      return NULL;
    }


  return COGL_FRAMEBUFFER (cogl_fbo);
}

static gboolean
copy_shared_framebuffer_primary_gpu (CoglOnscreen                        *onscreen,
                                     MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaRendererNative *renderer_native = onscreen_native->renderer_native;
  MetaRendererNativeGpuData *primary_gpu_data;
  MetaDrmBufferDumb *buffer_dumb;
  MetaDumbBuffer *dumb_fb;
  CoglFramebuffer *dmabuf_fb;
  int dmabuf_fd;
  g_autoptr (GError) error = NULL;
  CoglPixelFormat cogl_format;
  int ret;

  COGL_TRACE_BEGIN_SCOPED (CopySharedFramebufferPrimaryGpu,
                           "FB Copy (primary GPU)");

  primary_gpu_data =
    meta_renderer_native_get_gpu_data (renderer_native,
                                       renderer_native->primary_gpu_kms);
  if (!primary_gpu_data->secondary.has_EGL_EXT_image_dma_buf_import_modifiers)
    return FALSE;

  dumb_fb = secondary_gpu_get_next_dumb_buffer (secondary_gpu_state);

  g_assert (cogl_framebuffer_get_width (framebuffer) == dumb_fb->width);
  g_assert (cogl_framebuffer_get_height (framebuffer) == dumb_fb->height);

  ret = cogl_pixel_format_from_drm_format (dumb_fb->drm_format,
                                           &cogl_format,
                                           NULL);
  g_assert (ret);

  dmabuf_fd = meta_dumb_buffer_ensure_dmabuf_fd (dumb_fb,
                                                 secondary_gpu_state->gpu_kms);
  if (dmabuf_fd == -1)
    return FALSE;

  dmabuf_fb = create_dma_buf_framebuffer (renderer_native,
                                          dmabuf_fd,
                                          dumb_fb->width,
                                          dumb_fb->height,
                                          dumb_fb->stride_bytes,
                                          0, DRM_FORMAT_MOD_LINEAR,
                                          dumb_fb->drm_format,
                                          &error);

  if (error)
    {
      g_debug ("%s: Failed to blit DMA buffer image: %s",
               G_STRFUNC, error->message);
      return FALSE;
    }

  if (!cogl_blit_framebuffer (framebuffer, COGL_FRAMEBUFFER (dmabuf_fb),
                              0, 0, 0, 0,
                              dumb_fb->width,
                              dumb_fb->height,
                              &error))
    {
      cogl_object_unref (dmabuf_fb);
      return FALSE;
    }

  cogl_object_unref (dmabuf_fb);

  g_clear_object (&secondary_gpu_state->gbm.next_fb);
  buffer_dumb = meta_drm_buffer_dumb_new (dumb_fb->fb_id);
  secondary_gpu_state->gbm.next_fb = META_DRM_BUFFER (buffer_dumb);
  secondary_gpu_state->cpu.dumb_fb = dumb_fb;

  return TRUE;
}

typedef struct _PixelFormatMap {
  uint32_t drm_format;
  CoglPixelFormat cogl_format;
  CoglTextureComponents cogl_components;
} PixelFormatMap;

static const PixelFormatMap pixel_format_map[] = {
/* DRM formats are defined as little-endian, not machine endian. */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  { DRM_FORMAT_RGB565,   COGL_PIXEL_FORMAT_RGB_565,       COGL_TEXTURE_COMPONENTS_RGB  },
  { DRM_FORMAT_ABGR8888, COGL_PIXEL_FORMAT_RGBA_8888_PRE, COGL_TEXTURE_COMPONENTS_RGBA },
  { DRM_FORMAT_XBGR8888, COGL_PIXEL_FORMAT_RGBA_8888_PRE, COGL_TEXTURE_COMPONENTS_RGB  },
  { DRM_FORMAT_ARGB8888, COGL_PIXEL_FORMAT_BGRA_8888_PRE, COGL_TEXTURE_COMPONENTS_RGBA },
  { DRM_FORMAT_XRGB8888, COGL_PIXEL_FORMAT_BGRA_8888_PRE, COGL_TEXTURE_COMPONENTS_RGB  },
  { DRM_FORMAT_BGRA8888, COGL_PIXEL_FORMAT_ARGB_8888_PRE, COGL_TEXTURE_COMPONENTS_RGBA },
  { DRM_FORMAT_BGRX8888, COGL_PIXEL_FORMAT_ARGB_8888_PRE, COGL_TEXTURE_COMPONENTS_RGB  },
  { DRM_FORMAT_RGBA8888, COGL_PIXEL_FORMAT_ABGR_8888_PRE, COGL_TEXTURE_COMPONENTS_RGBA },
  { DRM_FORMAT_RGBX8888, COGL_PIXEL_FORMAT_ABGR_8888_PRE, COGL_TEXTURE_COMPONENTS_RGB  },
#elif G_BYTE_ORDER == G_BIG_ENDIAN
  /* DRM_FORMAT_RGB565 cannot be expressed. */
  { DRM_FORMAT_ABGR8888, COGL_PIXEL_FORMAT_ABGR_8888_PRE, COGL_TEXTURE_COMPONENTS_RGBA },
  { DRM_FORMAT_XBGR8888, COGL_PIXEL_FORMAT_ABGR_8888_PRE, COGL_TEXTURE_COMPONENTS_RGB  },
  { DRM_FORMAT_ARGB8888, COGL_PIXEL_FORMAT_ARGB_8888_PRE, COGL_TEXTURE_COMPONENTS_RGBA },
  { DRM_FORMAT_XRGB8888, COGL_PIXEL_FORMAT_ARGB_8888_PRE, COGL_TEXTURE_COMPONENTS_RGB  },
  { DRM_FORMAT_BGRA8888, COGL_PIXEL_FORMAT_BGRA_8888_PRE, COGL_TEXTURE_COMPONENTS_RGBA },
  { DRM_FORMAT_BGRX8888, COGL_PIXEL_FORMAT_BGRA_8888_PRE, COGL_TEXTURE_COMPONENTS_RGB  },
  { DRM_FORMAT_RGBA8888, COGL_PIXEL_FORMAT_RGBA_8888_PRE, COGL_TEXTURE_COMPONENTS_RGBA },
  { DRM_FORMAT_RGBX8888, COGL_PIXEL_FORMAT_RGBA_8888_PRE, COGL_TEXTURE_COMPONENTS_RGB  },
#else
#error "unexpected G_BYTE_ORDER"
#endif
};

static gboolean
cogl_pixel_format_from_drm_format (uint32_t               drm_format,
                                   CoglPixelFormat       *out_format,
                                   CoglTextureComponents *out_components)
{
  const size_t n = G_N_ELEMENTS (pixel_format_map);
  size_t i;

  for (i = 0; i < n; i++)
    {
      if (pixel_format_map[i].drm_format == drm_format)
        break;
    }

  if (i == n)
    return FALSE;

  if (out_format)
    *out_format = pixel_format_map[i].cogl_format;

  if (out_components)
    *out_components = pixel_format_map[i].cogl_components;

  return TRUE;
}

static void
copy_shared_framebuffer_cpu (CoglOnscreen                        *onscreen,
                             MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state,
                             MetaRendererNativeGpuData           *renderer_gpu_data)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *cogl_context = framebuffer->context;
  MetaDumbBuffer *dumb_fb;
  CoglBitmap *dumb_bitmap;
  CoglPixelFormat cogl_format;
  gboolean ret;
  MetaDrmBufferDumb *buffer_dumb;

  COGL_TRACE_BEGIN_SCOPED (CopySharedFramebufferCpu,
                           "FB Copy (CPU)");

  dumb_fb = secondary_gpu_get_next_dumb_buffer (secondary_gpu_state);

  g_assert (cogl_framebuffer_get_width (framebuffer) == dumb_fb->width);
  g_assert (cogl_framebuffer_get_height (framebuffer) == dumb_fb->height);

  ret = cogl_pixel_format_from_drm_format (dumb_fb->drm_format,
                                           &cogl_format,
                                           NULL);
  g_assert (ret);

  dumb_bitmap = cogl_bitmap_new_for_data (cogl_context,
                                          dumb_fb->width,
                                          dumb_fb->height,
                                          cogl_format,
                                          dumb_fb->stride_bytes,
                                          dumb_fb->map);

  if (!cogl_framebuffer_read_pixels_into_bitmap (framebuffer,
                                                 0 /* x */,
                                                 0 /* y */,
                                                 COGL_READ_PIXELS_COLOR_BUFFER,
                                                 dumb_bitmap))
    g_warning ("Failed to CPU-copy to a secondary GPU output");

  cogl_object_unref (dumb_bitmap);

  g_clear_object (&secondary_gpu_state->gbm.next_fb);
  buffer_dumb = meta_drm_buffer_dumb_new (dumb_fb->fb_id);
  secondary_gpu_state->gbm.next_fb = META_DRM_BUFFER (buffer_dumb);
  secondary_gpu_state->cpu.dumb_fb = dumb_fb;
}

static void
update_secondary_gpu_state_pre_swap_buffers (CoglOnscreen *onscreen)
{
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state;

  COGL_TRACE_BEGIN_SCOPED (MetaRendererNativeGpuStatePreSwapBuffers,
                           "Onscreen (secondary gpu pre-swap-buffers)");

  secondary_gpu_state = onscreen_native->secondary_gpu_state;
  if (secondary_gpu_state)
    {
      MetaRendererNativeGpuData *renderer_gpu_data;

      renderer_gpu_data = secondary_gpu_state->renderer_gpu_data;
      switch (renderer_gpu_data->secondary.copy_mode)
        {
        case META_SHARED_FRAMEBUFFER_COPY_MODE_SECONDARY_GPU:
          /* Done after eglSwapBuffers. */
          break;
        case META_SHARED_FRAMEBUFFER_COPY_MODE_ZERO:
          /* Done after eglSwapBuffers. */
          if (secondary_gpu_state->import_status ==
              META_SHARED_FRAMEBUFFER_IMPORT_STATUS_OK)
            break;
          /* prepare fallback */
          G_GNUC_FALLTHROUGH;
        case META_SHARED_FRAMEBUFFER_COPY_MODE_PRIMARY:
          if (!copy_shared_framebuffer_primary_gpu (onscreen,
                                                    secondary_gpu_state))
            {
              if (!secondary_gpu_state->noted_primary_gpu_copy_failed)
                {
                  g_debug ("Using primary GPU to copy for %s failed once.",
                           meta_gpu_kms_get_file_path (secondary_gpu_state->gpu_kms));
                  secondary_gpu_state->noted_primary_gpu_copy_failed = TRUE;
                }

              copy_shared_framebuffer_cpu (onscreen,
                                           secondary_gpu_state,
                                           renderer_gpu_data);
            }
          else if (!secondary_gpu_state->noted_primary_gpu_copy_ok)
            {
              g_debug ("Using primary GPU to copy for %s succeeded once.",
                       meta_gpu_kms_get_file_path (secondary_gpu_state->gpu_kms));
              secondary_gpu_state->noted_primary_gpu_copy_ok = TRUE;
            }
          break;
        }
    }
}

static void
update_secondary_gpu_state_post_swap_buffers (CoglOnscreen *onscreen,
                                              gboolean     *egl_context_changed)
{
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaRendererNative *renderer_native = onscreen_native->renderer_native;
  MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state;

  COGL_TRACE_BEGIN_SCOPED (MetaRendererNativeGpuStatePostSwapBuffers,
                           "Onscreen (secondary gpu post-swap-buffers)");

  secondary_gpu_state = onscreen_native->secondary_gpu_state;
  if (secondary_gpu_state)
    {
      MetaRendererNativeGpuData *renderer_gpu_data;

      renderer_gpu_data =
        meta_renderer_native_get_gpu_data (renderer_native,
                                           secondary_gpu_state->gpu_kms);
retry:
      switch (renderer_gpu_data->secondary.copy_mode)
        {
        case META_SHARED_FRAMEBUFFER_COPY_MODE_ZERO:
          if (!import_shared_framebuffer (onscreen,
                                          secondary_gpu_state))
            goto retry;
          break;
        case META_SHARED_FRAMEBUFFER_COPY_MODE_SECONDARY_GPU:
          copy_shared_framebuffer_gpu (onscreen,
                                       secondary_gpu_state,
                                       renderer_gpu_data,
                                       egl_context_changed);
          break;
        case META_SHARED_FRAMEBUFFER_COPY_MODE_PRIMARY:
          /* Done before eglSwapBuffers. */
          break;
        }
    }
}

static MetaKmsUpdate *
unset_disabled_crtcs (MetaBackend *backend,
                      MetaKms     *kms)
{
  MetaKmsUpdate *kms_update = NULL;
  GList *l;

  for (l = meta_backend_get_gpus (backend); l; l = l->next)
    {
      MetaGpu *gpu = l->data;
      GList *k;

      for (k = meta_gpu_get_crtcs (gpu); k; k = k->next)
        {
          MetaCrtc *crtc = k->data;

          if (crtc->config)
            continue;

          kms_update = meta_kms_ensure_pending_update (kms);
          meta_crtc_kms_set_mode (crtc, kms_update);
        }
    }

  return kms_update;
}

static void
post_pending_update (MetaKms *kms)
{
  g_autoptr (MetaKmsFeedback) kms_feedback = NULL;

  kms_feedback = meta_kms_post_pending_update_sync (kms);
  if (meta_kms_feedback_get_result (kms_feedback) != META_KMS_FEEDBACK_PASSED)
    {
      const GError *error = meta_kms_feedback_get_error (kms_feedback);

      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED))
        g_warning ("Failed to post KMS update: %s", error->message);
    }
}

static void
meta_onscreen_native_swap_buffers_with_damage (CoglOnscreen *onscreen,
                                               const int    *rectangles,
                                               int           n_rectangles)
{
  CoglContext *cogl_context = COGL_FRAMEBUFFER (onscreen)->context;
  CoglDisplay *cogl_display = cogl_context_get_display (cogl_context);
  CoglRenderer *cogl_renderer = cogl_context->display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaGpuKms *render_gpu = onscreen_native->render_gpu;
  CoglFrameInfo *frame_info;
  gboolean egl_context_changed = FALSE;
  MetaKmsUpdate *kms_update;
  MetaPowerSave power_save_mode;
  g_autoptr (GError) error = NULL;
  MetaDrmBufferGbm *buffer_gbm;
  g_autoptr (MetaKmsFeedback) kms_feedback = NULL;

  COGL_TRACE_BEGIN_SCOPED (MetaRendererNativeSwapBuffers,
                           "Onscreen (swap-buffers)");

  kms_update = meta_kms_ensure_pending_update (kms);

  /*
   * Wait for the flip callback before continuing, as we might have started the
   * animation earlier due to the animation being driven by some other monitor.
   */
  COGL_TRACE_BEGIN (MetaRendererNativeSwapBuffersWait,
                    "Onscreen (waiting for page flips)");
  wait_for_pending_flips (onscreen);
  COGL_TRACE_END (MetaRendererNativeSwapBuffersWait);

  frame_info = g_queue_peek_tail (&onscreen->pending_frame_infos);
  frame_info->global_frame_counter = renderer_native->frame_counter;

  update_secondary_gpu_state_pre_swap_buffers (onscreen);

  parent_vtable->onscreen_swap_buffers_with_damage (onscreen,
                                                    rectangles,
                                                    n_rectangles);

  renderer_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                         render_gpu);
  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      g_warn_if_fail (onscreen_native->gbm.next_fb == NULL);
      g_clear_object (&onscreen_native->gbm.next_fb);

      buffer_gbm = meta_drm_buffer_gbm_new (render_gpu,
                                            onscreen_native->gbm.surface,
                                            renderer_native->use_modifiers,
                                            &error);
      if (!buffer_gbm)
        {
          g_warning ("meta_drm_buffer_gbm_new failed: %s",
                     error->message);
          return;
        }

      onscreen_native->gbm.next_fb = META_DRM_BUFFER (buffer_gbm);

      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      break;
#endif
    }

  update_secondary_gpu_state_post_swap_buffers (onscreen, &egl_context_changed);

  /* If this is the first framebuffer to be presented then we now setup the
   * crtc modes, else we flip from the previous buffer */

  power_save_mode = meta_monitor_manager_get_power_save_mode (monitor_manager);
  if (onscreen_native->pending_set_crtc &&
      power_save_mode == META_POWER_SAVE_ON)
    {
      meta_onscreen_native_set_crtc_mode (onscreen,
                                          renderer_gpu_data,
                                          kms_update);
      onscreen_native->pending_set_crtc = FALSE;
    }

  onscreen_native->pending_queue_swap_notify_frame_count = renderer_native->frame_counter;
  meta_onscreen_native_flip_crtcs (onscreen, kms_update);

  /*
   * If we changed EGL context, cogl will have the wrong idea about what is
   * current, making it fail to set it when it needs to. Avoid that by making
   * EGL_NO_CONTEXT current now, making cogl eventually set the correct
   * context.
   */
  if (egl_context_changed)
    _cogl_winsys_egl_ensure_current (cogl_display);

  COGL_TRACE_BEGIN (MetaRendererNativePostKmsUpdate,
                    "Onscreen (post pending update)");
  post_pending_update (kms);
  COGL_TRACE_END (MetaRendererNativePostKmsUpdate);
}

static CoglDmaBufHandle *
meta_renderer_native_create_dma_buf (CoglRenderer  *cogl_renderer,
                                     int            width,
                                     int            height,
                                     GError       **error)
{
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      {
        CoglFramebuffer *dmabuf_fb;
        CoglDmaBufHandle *dmabuf_handle;
        struct gbm_bo *new_bo;
        int dmabuf_fd = -1;

        new_bo = gbm_bo_create (renderer_gpu_data->gbm.device,
                                width, height, DRM_FORMAT_XRGB8888,
                                GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);

        if (!new_bo)
          {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                         "Failed to allocate buffer");
            return NULL;
          }

        dmabuf_fd = gbm_bo_get_fd (new_bo);

        if (dmabuf_fd == -1)
          {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                         "Failed to export buffer's DMA fd: %s",
                         g_strerror (errno));
            return NULL;
          }

        dmabuf_fb = create_dma_buf_framebuffer (renderer_native,
                                                dmabuf_fd,
                                                width, height,
                                                gbm_bo_get_stride (new_bo),
                                                gbm_bo_get_offset (new_bo, 0),
                                                DRM_FORMAT_MOD_LINEAR,
                                                DRM_FORMAT_XRGB8888,
                                                error);

        if (!dmabuf_fb)
          return NULL;

        dmabuf_handle =
          cogl_dma_buf_handle_new (dmabuf_fb, dmabuf_fd, new_bo,
                                   (GDestroyNotify) gbm_bo_destroy);
        cogl_object_unref (dmabuf_fb);
        return dmabuf_handle;
      }
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      break;
#endif
    }

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
               "Current mode does not support exporting DMA buffers");

  return NULL;
}

static gboolean
meta_renderer_native_init_egl_context (CoglContext *cogl_context,
                                       GError     **error)
{
#ifdef HAVE_EGL_DEVICE
  CoglRenderer *cogl_renderer = cogl_context->display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
#endif

  COGL_FLAGS_SET (cogl_context->features,
                  COGL_FEATURE_ID_PRESENTATION_TIME, TRUE);
  COGL_FLAGS_SET (cogl_context->features,
                  COGL_FEATURE_ID_SWAP_BUFFERS_EVENT, TRUE);
  /* TODO: remove this deprecated feature */
  COGL_FLAGS_SET (cogl_context->winsys_features,
                  COGL_WINSYS_FEATURE_SWAP_BUFFERS_EVENT,
                  TRUE);
  COGL_FLAGS_SET (cogl_context->winsys_features,
                  COGL_WINSYS_FEATURE_SYNC_AND_COMPLETE_EVENT,
                  TRUE);
  COGL_FLAGS_SET (cogl_context->winsys_features,
                  COGL_WINSYS_FEATURE_MULTIPLE_ONSCREEN,
                  TRUE);

  /* COGL_WINSYS_FEATURE_SWAP_THROTTLE is always true for this renderer
   * because we have the call to wait_for_pending_flips on every frame.
   */
  COGL_FLAGS_SET (cogl_context->winsys_features,
                  COGL_WINSYS_FEATURE_SWAP_THROTTLE,
                  TRUE);

#ifdef HAVE_EGL_DEVICE
  if (renderer_gpu_data->mode == META_RENDERER_NATIVE_MODE_EGL_DEVICE)
    COGL_FLAGS_SET (cogl_context->features,
                    COGL_FEATURE_ID_TEXTURE_EGL_IMAGE_EXTERNAL, TRUE);
#endif

  return TRUE;
}

static gboolean
should_surface_be_sharable (CoglOnscreen *onscreen)
{
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;

  if (META_GPU_KMS (meta_crtc_get_gpu (onscreen_native->crtc)) ==
      onscreen_native->render_gpu)
    return FALSE;
  else
    return TRUE;
}

static gboolean
meta_renderer_native_create_surface_gbm (CoglOnscreen        *onscreen,
                                         int                  width,
                                         int                  height,
                                         struct gbm_surface **gbm_surface,
                                         EGLSurface          *egl_surface,
                                         GError             **error)
{
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaRendererNative *renderer_native = onscreen_native->renderer_native;
  MetaEgl *egl = meta_onscreen_native_get_egl (onscreen_native);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *cogl_context = framebuffer->context;
  CoglDisplay *cogl_display = cogl_context->display;
  CoglDisplayEGL *cogl_display_egl = cogl_display->winsys;
  CoglRenderer *cogl_renderer = cogl_display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  struct gbm_surface *new_gbm_surface = NULL;
  EGLNativeWindowType egl_native_window;
  EGLSurface new_egl_surface;
  uint32_t format = GBM_FORMAT_XRGB8888;
  GArray *modifiers;

  renderer_gpu_data =
    meta_renderer_native_get_gpu_data (renderer_native,
                                       onscreen_native->render_gpu);

  if (renderer_native->use_modifiers)
    modifiers = get_supported_modifiers (onscreen, format);
  else
    modifiers = NULL;

  if (modifiers)
    {
      new_gbm_surface =
        gbm_surface_create_with_modifiers (renderer_gpu_data->gbm.device,
                                           width, height, format,
                                           (uint64_t *) modifiers->data,
                                           modifiers->len);
      g_array_free (modifiers, TRUE);
    }

  if (!new_gbm_surface)
    {
      uint32_t flags = GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING;

      if (should_surface_be_sharable (onscreen))
        flags |= GBM_BO_USE_LINEAR;

      new_gbm_surface = gbm_surface_create (renderer_gpu_data->gbm.device,
                                            width, height,
                                            format,
                                            flags);
    }

  if (!new_gbm_surface)
    {
      g_set_error (error, COGL_WINSYS_ERROR,
                   COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                   "Failed to allocate surface");
      return FALSE;
    }

  egl_native_window = (EGLNativeWindowType) new_gbm_surface;
  new_egl_surface =
    meta_egl_create_window_surface (egl,
                                    cogl_renderer_egl->edpy,
                                    cogl_display_egl->egl_config,
                                    egl_native_window,
                                    NULL,
                                    error);
  if (new_egl_surface == EGL_NO_SURFACE)
    {
      gbm_surface_destroy (new_gbm_surface);
      return FALSE;
    }

  *gbm_surface = new_gbm_surface;
  *egl_surface = new_egl_surface;

  return TRUE;
}

#ifdef HAVE_EGL_DEVICE
static gboolean
meta_renderer_native_create_surface_egl_device (CoglOnscreen  *onscreen,
                                                int            width,
                                                int            height,
                                                EGLStreamKHR  *out_egl_stream,
                                                EGLSurface    *out_egl_surface,
                                                GError       **error)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  CoglContext *cogl_context = framebuffer->context;
  CoglDisplay *cogl_display = cogl_context->display;
  CoglDisplayEGL *cogl_display_egl = cogl_display->winsys;
  CoglRenderer *cogl_renderer = cogl_display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  MetaEgl *egl =
    meta_renderer_native_get_egl (renderer_gpu_data->renderer_native);
  EGLDisplay egl_display = renderer_gpu_data->egl_display;
  EGLConfig egl_config;
  EGLStreamKHR egl_stream;
  EGLSurface egl_surface;
  EGLint num_layers;
  EGLOutputLayerEXT output_layer;
  EGLAttrib output_attribs[3];
  EGLint stream_attribs[] = {
    EGL_STREAM_FIFO_LENGTH_KHR, 0,
    EGL_CONSUMER_AUTO_ACQUIRE_EXT, EGL_FALSE,
    EGL_NONE
  };
  EGLint stream_producer_attribs[] = {
    EGL_WIDTH, width,
    EGL_HEIGHT, height,
    EGL_NONE
  };

  egl_stream = meta_egl_create_stream (egl, egl_display, stream_attribs, error);
  if (egl_stream == EGL_NO_STREAM_KHR)
    return FALSE;

  output_attribs[0] = EGL_DRM_CRTC_EXT;
  output_attribs[1] = onscreen_native->crtc->crtc_id;
  output_attribs[2] = EGL_NONE;

  if (!meta_egl_get_output_layers (egl, egl_display,
                                   output_attribs,
                                   &output_layer, 1, &num_layers,
                                   error))
    {
      meta_egl_destroy_stream (egl, egl_display, egl_stream, NULL);
      return FALSE;
    }

  if (num_layers < 1)
    {
      meta_egl_destroy_stream (egl, egl_display, egl_stream, NULL);
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Unable to find output layers.");
      return FALSE;
    }

  if (!meta_egl_stream_consumer_output (egl, egl_display,
                                        egl_stream, output_layer,
                                        error))
    {
      meta_egl_destroy_stream (egl, egl_display, egl_stream, NULL);
      return FALSE;
    }

  egl_config = cogl_display_egl->egl_config;
  egl_surface = meta_egl_create_stream_producer_surface (egl,
                                                         egl_display,
                                                         egl_config,
                                                         egl_stream,
                                                         stream_producer_attribs,
                                                         error);
  if (egl_surface == EGL_NO_SURFACE)
    {
      meta_egl_destroy_stream (egl, egl_display, egl_stream, NULL);
      return FALSE;
    }

  *out_egl_stream = egl_stream;
  *out_egl_surface = egl_surface;

  return TRUE;
}
#endif /* HAVE_EGL_DEVICE */

static gboolean
init_dumb_fb (MetaDumbBuffer  *dumb_fb,
              MetaGpuKms      *gpu_kms,
              int              width,
              int              height,
              uint32_t         format,
              GError         **error)
{
  struct drm_mode_create_dumb create_arg;
  struct drm_mode_destroy_dumb destroy_arg;
  struct drm_mode_map_dumb map_arg;
  uint32_t fb_id = 0;
  void *map;
  int kms_fd;
  MetaGpuKmsFBArgs fb_args = {
    .width = width,
    .height = height,
    .format = format,
  };

  kms_fd = meta_gpu_kms_get_fd (gpu_kms);

  create_arg = (struct drm_mode_create_dumb) {
    .bpp = 32, /* RGBX8888 */
    .width = width,
    .height = height
  };
  if (drmIoctl (kms_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_arg) != 0)
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Failed to create dumb drm buffer: %s",
                   g_strerror (errno));
      goto err_ioctl;
    }

  fb_args.handles[0] = create_arg.handle;
  fb_args.strides[0] = create_arg.pitch;

  if (!meta_gpu_kms_add_fb (gpu_kms, FALSE, &fb_args, &fb_id, error))
    goto err_add_fb;

  map_arg = (struct drm_mode_map_dumb) {
    .handle = create_arg.handle
  };
  if (drmIoctl (kms_fd, DRM_IOCTL_MODE_MAP_DUMB,
                &map_arg) != 0)
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Failed to map dumb drm buffer: %s",
                   g_strerror (errno));
      goto err_map_dumb;
    }

  map = mmap (NULL, create_arg.size, PROT_WRITE, MAP_SHARED,
              kms_fd, map_arg.offset);
  if (map == MAP_FAILED)
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Failed to mmap dumb drm buffer memory: %s",
                   g_strerror (errno));
      goto err_mmap;
    }

  dumb_fb->fb_id = fb_id;
  dumb_fb->handle = create_arg.handle;
  dumb_fb->map = map;
  dumb_fb->map_size = create_arg.size;
  dumb_fb->width = width;
  dumb_fb->height = height;
  dumb_fb->stride_bytes = create_arg.pitch;
  dumb_fb->drm_format = format;
  dumb_fb->dmabuf_fd = -1;

  return TRUE;

err_mmap:
err_map_dumb:
  drmModeRmFB (kms_fd, fb_id);

err_add_fb:
  destroy_arg = (struct drm_mode_destroy_dumb) {
    .handle = create_arg.handle
  };
  drmIoctl (kms_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);

err_ioctl:
  return FALSE;
}

static int
meta_dumb_buffer_ensure_dmabuf_fd (MetaDumbBuffer *dumb_fb,
                                   MetaGpuKms     *gpu_kms)
{
  int ret;
  int kms_fd;
  int dmabuf_fd;

  if (dumb_fb->dmabuf_fd != -1)
    return dumb_fb->dmabuf_fd;

  kms_fd = meta_gpu_kms_get_fd (gpu_kms);

  ret = drmPrimeHandleToFD (kms_fd, dumb_fb->handle, DRM_CLOEXEC,
                            &dmabuf_fd);
  if (ret)
    {
      g_debug ("Failed to export dumb drm buffer: %s",
               g_strerror (errno));
      return -1;
    }

  dumb_fb->dmabuf_fd = dmabuf_fd;

  return dumb_fb->dmabuf_fd;
}

static void
release_dumb_fb (MetaDumbBuffer *dumb_fb,
                 MetaGpuKms     *gpu_kms)
{
  struct drm_mode_destroy_dumb destroy_arg;
  int kms_fd;

  if (!dumb_fb->map)
    return;

  if (dumb_fb->dmabuf_fd != -1)
    close (dumb_fb->dmabuf_fd);

  munmap (dumb_fb->map, dumb_fb->map_size);

  kms_fd = meta_gpu_kms_get_fd (gpu_kms);

  drmModeRmFB (kms_fd, dumb_fb->fb_id);

  destroy_arg = (struct drm_mode_destroy_dumb) {
    .handle = dumb_fb->handle
  };
  drmIoctl (kms_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);

  *dumb_fb = (MetaDumbBuffer) {
    .dmabuf_fd = -1,
  };
}

static gboolean
meta_renderer_native_init_onscreen (CoglOnscreen *onscreen,
                                    GError      **error)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *cogl_context = framebuffer->context;
  CoglDisplay *cogl_display = cogl_context->display;
  CoglDisplayEGL *cogl_display_egl = cogl_display->winsys;
  CoglOnscreenEGL *onscreen_egl;
  MetaOnscreenNative *onscreen_native;

  g_return_val_if_fail (cogl_display_egl->egl_context, FALSE);

  onscreen->winsys = g_slice_new0 (CoglOnscreenEGL);
  onscreen_egl = onscreen->winsys;

  onscreen_native = g_slice_new0 (MetaOnscreenNative);
  onscreen_egl->platform = onscreen_native;

  /*
   * Don't actually initialize anything here, since we may not have the
   * information available yet, and there is no way to pass it at this stage.
   * To properly allocate a MetaOnscreenNative, the caller must call
   * meta_onscreen_native_allocate() after cogl_framebuffer_allocate().
   *
   * TODO: Turn CoglFramebuffer/CoglOnscreen into GObjects, so it's possible
   * to add backend specific properties.
   */

  return TRUE;
}

static gboolean
meta_onscreen_native_allocate (CoglOnscreen *onscreen,
                               GError      **error)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
  MetaRendererNativeGpuData *renderer_gpu_data;
  struct gbm_surface *gbm_surface;
  EGLSurface egl_surface;
  int width;
  int height;
#ifdef HAVE_EGL_DEVICE
  EGLStreamKHR egl_stream;
#endif

  onscreen_native->pending_set_crtc = TRUE;

  /* If a kms_fd is set then the display width and height
   * won't be available until meta_renderer_native_set_layout
   * is called. In that case, defer creating the surface
   * until then.
   */
  width = cogl_framebuffer_get_width (framebuffer);
  height = cogl_framebuffer_get_height (framebuffer);
  if (width == 0 || height == 0)
    return TRUE;

  renderer_gpu_data =
    meta_renderer_native_get_gpu_data (onscreen_native->renderer_native,
                                       onscreen_native->render_gpu);
  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      if (!meta_renderer_native_create_surface_gbm (onscreen,
                                                    width, height,
                                                    &gbm_surface,
                                                    &egl_surface,
                                                    error))
        return FALSE;

      onscreen_native->gbm.surface = gbm_surface;
      onscreen_egl->egl_surface = egl_surface;
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      if (!init_dumb_fb (&onscreen_native->egl.dumb_fb,
                         onscreen_native->render_gpu,
                         width, height,
                         DRM_FORMAT_XRGB8888,
                         error))
        return FALSE;

      if (!meta_renderer_native_create_surface_egl_device (onscreen,
                                                           width, height,
                                                           &egl_stream,
                                                           &egl_surface,
                                                           error))
        return FALSE;

      onscreen_native->egl.stream = egl_stream;
      onscreen_egl->egl_surface = egl_surface;
      break;
#endif /* HAVE_EGL_DEVICE */
    }

  return TRUE;
}

static void
destroy_egl_surface (CoglOnscreen *onscreen)
{
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;

  if (onscreen_egl->egl_surface != EGL_NO_SURFACE)
    {
      MetaOnscreenNative *onscreen_native = onscreen_egl->platform;
      MetaEgl *egl = meta_onscreen_native_get_egl (onscreen_native);
      CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
      CoglContext *cogl_context = framebuffer->context;
      CoglRenderer *cogl_renderer = cogl_context->display->renderer;
      CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;

      meta_egl_destroy_surface (egl,
                                cogl_renderer_egl->edpy,
                                onscreen_egl->egl_surface,
                                NULL);
      onscreen_egl->egl_surface = EGL_NO_SURFACE;
    }
}

static void
meta_renderer_native_release_onscreen (CoglOnscreen *onscreen)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *cogl_context = framebuffer->context;
  CoglDisplay *cogl_display = cogl_context_get_display (cogl_context);
  CoglDisplayEGL *cogl_display_egl = cogl_display->winsys;
  CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
  MetaOnscreenNative *onscreen_native;
  MetaRendererNative *renderer_native;
  MetaRendererNativeGpuData *renderer_gpu_data;

  /* If we never successfully allocated then there's nothing to do */
  if (onscreen_egl == NULL)
    return;

  onscreen_native = onscreen_egl->platform;
  renderer_native = onscreen_native->renderer_native;

  if (onscreen_egl->egl_surface != EGL_NO_SURFACE &&
      (cogl_display_egl->current_draw_surface == onscreen_egl->egl_surface ||
       cogl_display_egl->current_read_surface == onscreen_egl->egl_surface))
    {
      if (!_cogl_winsys_egl_make_current (cogl_display,
                                          cogl_display_egl->dummy_surface,
                                          cogl_display_egl->dummy_surface,
                                          cogl_display_egl->egl_context))
        g_warning ("Failed to clear current context");
    }

  renderer_gpu_data =
    meta_renderer_native_get_gpu_data (renderer_native,
                                       onscreen_native->render_gpu);
  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      /* flip state takes a reference on the onscreen so there should
       * never be outstanding flips when we reach here. */
      g_return_if_fail (onscreen_native->gbm.next_fb == NULL);

      free_current_bo (onscreen);

      destroy_egl_surface (onscreen);

      if (onscreen_native->gbm.surface)
        {
          gbm_surface_destroy (onscreen_native->gbm.surface);
          onscreen_native->gbm.surface = NULL;
        }
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      release_dumb_fb (&onscreen_native->egl.dumb_fb,
                       onscreen_native->render_gpu);

      destroy_egl_surface (onscreen);

      if (onscreen_native->egl.stream != EGL_NO_STREAM_KHR)
        {
          MetaEgl *egl = meta_onscreen_native_get_egl (onscreen_native);
          CoglRenderer *cogl_renderer = cogl_context->display->renderer;
          CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;

          meta_egl_destroy_stream (egl,
                                   cogl_renderer_egl->edpy,
                                   onscreen_native->egl.stream,
                                   NULL);
          onscreen_native->egl.stream = EGL_NO_STREAM_KHR;
        }
      break;
#endif /* HAVE_EGL_DEVICE */
    }

  g_clear_pointer (&onscreen_native->secondary_gpu_state,
                   secondary_gpu_state_free);

  g_slice_free (MetaOnscreenNative, onscreen_native);
  g_slice_free (CoglOnscreenEGL, onscreen->winsys);
  onscreen->winsys = NULL;
}

static const CoglWinsysEGLVtable
_cogl_winsys_egl_vtable = {
  .add_config_attributes = meta_renderer_native_add_egl_config_attributes,
  .choose_config = meta_renderer_native_choose_egl_config,
  .display_setup = meta_renderer_native_setup_egl_display,
  .display_destroy = meta_renderer_native_destroy_egl_display,
  .context_created = meta_renderer_native_egl_context_created,
  .cleanup_context = meta_renderer_native_egl_cleanup_context,
  .context_init = meta_renderer_native_init_egl_context
};

static void
meta_renderer_native_queue_modes_reset (MetaRendererNative *renderer_native)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  GList *l;

  for (l = meta_renderer_get_views (renderer); l; l = l->next)
    {
      ClutterStageView *stage_view = l->data;
      CoglFramebuffer *framebuffer =
        clutter_stage_view_get_onscreen (stage_view);
      CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);
      CoglOnscreenEGL *onscreen_egl = onscreen->winsys;
      MetaOnscreenNative *onscreen_native = onscreen_egl->platform;

      onscreen_native->pending_set_crtc = TRUE;
    }

  renderer_native->pending_unset_disabled_crtcs = TRUE;
}

static CoglOnscreen *
meta_renderer_native_create_onscreen (MetaRendererNative   *renderer_native,
                                      MetaGpuKms           *render_gpu,
                                      MetaOutput           *output,
                                      MetaCrtc             *crtc,
                                      CoglContext          *context,
                                      int                   width,
                                      int                   height,
                                      GError              **error)
{
  CoglOnscreen *onscreen;
  CoglOnscreenEGL *onscreen_egl;
  MetaOnscreenNative *onscreen_native;

  onscreen = cogl_onscreen_new (context, width, height);

  if (!cogl_framebuffer_allocate (COGL_FRAMEBUFFER (onscreen), error))
    {
      cogl_object_unref (onscreen);
      return NULL;
    }

  onscreen_egl = onscreen->winsys;
  onscreen_native = onscreen_egl->platform;
  onscreen_native->renderer_native = renderer_native;
  onscreen_native->render_gpu = render_gpu;
  onscreen_native->output = output;
  onscreen_native->crtc = crtc;

  if (META_GPU_KMS (meta_crtc_get_gpu (crtc)) != render_gpu)
    {
      if (!init_secondary_gpu_state (renderer_native, onscreen, error))
        {
          cogl_object_unref (onscreen);
          return NULL;
        }
    }

  return onscreen;
}

static CoglOffscreen *
meta_renderer_native_create_offscreen (MetaRendererNative    *renderer,
                                       CoglContext           *context,
                                       gint                   view_width,
                                       gint                   view_height,
                                       GError               **error)
{
  CoglOffscreen *fb;
  CoglTexture2D *tex;

  tex = cogl_texture_2d_new_with_size (context, view_width, view_height);
  cogl_primitive_texture_set_auto_mipmap (COGL_PRIMITIVE_TEXTURE (tex), FALSE);

  if (!cogl_texture_allocate (COGL_TEXTURE (tex), error))
    {
      cogl_object_unref (tex);
      return FALSE;
    }

  fb = cogl_offscreen_new_with_texture (COGL_TEXTURE (tex));
  cogl_object_unref (tex);
  if (!cogl_framebuffer_allocate (COGL_FRAMEBUFFER (fb), error))
    {
      cogl_object_unref (fb);
      return FALSE;
    }

  return fb;
}

static int64_t
meta_renderer_native_get_clock_time (CoglContext *context)
{
  CoglRenderer *cogl_renderer = cogl_context_get_renderer (context);
  MetaGpuKms *gpu_kms = cogl_renderer->custom_winsys_user_data;

  return meta_gpu_kms_get_current_time_ns (gpu_kms);
}

static const CoglWinsysVtable *
get_native_cogl_winsys_vtable (CoglRenderer *cogl_renderer)
{
  static gboolean vtable_inited = FALSE;
  static CoglWinsysVtable vtable;

  if (!vtable_inited)
    {
      /* The this winsys is a subclass of the EGL winsys so we
         start by copying its vtable */

      parent_vtable = _cogl_winsys_egl_get_vtable ();
      vtable = *parent_vtable;

      vtable.id = COGL_WINSYS_ID_CUSTOM;
      vtable.name = "EGL_KMS";

      vtable.renderer_connect = meta_renderer_native_connect;
      vtable.renderer_disconnect = meta_renderer_native_disconnect;
      vtable.renderer_create_dma_buf = meta_renderer_native_create_dma_buf;

      vtable.onscreen_init = meta_renderer_native_init_onscreen;
      vtable.onscreen_deinit = meta_renderer_native_release_onscreen;

      /* The KMS winsys doesn't support swap region */
      vtable.onscreen_swap_region = NULL;
      vtable.onscreen_swap_buffers_with_damage =
        meta_onscreen_native_swap_buffers_with_damage;

      vtable.context_get_clock_time = meta_renderer_native_get_clock_time;

      vtable_inited = TRUE;
    }

  return &vtable;
}

static CoglRenderer *
create_cogl_renderer_for_gpu (MetaGpuKms *gpu_kms)
{
  CoglRenderer *cogl_renderer;

  cogl_renderer = cogl_renderer_new ();
  cogl_renderer_set_custom_winsys (cogl_renderer,
                                   get_native_cogl_winsys_vtable,
                                   gpu_kms);

  return cogl_renderer;
}

static CoglRenderer *
meta_renderer_native_create_cogl_renderer (MetaRenderer *renderer)
{
  MetaRendererNative *renderer_native = META_RENDERER_NATIVE (renderer);

  return create_cogl_renderer_for_gpu (renderer_native->primary_gpu_kms);
}

static void
meta_onscreen_native_set_view (CoglOnscreen     *onscreen,
                               MetaRendererView *view)
{
  CoglOnscreenEGL *onscreen_egl;
  MetaOnscreenNative *onscreen_native;

  onscreen_egl = onscreen->winsys;
  onscreen_native = onscreen_egl->platform;
  onscreen_native->view = view;
}

static MetaMonitorTransform
calculate_view_transform (MetaMonitorManager *monitor_manager,
                          MetaLogicalMonitor *logical_monitor,
                          MetaOutput         *output,
                          MetaCrtc           *crtc)
{
  MetaMonitorTransform crtc_transform;

  crtc = meta_output_get_assigned_crtc (output);
  crtc_transform =
    meta_output_logical_to_crtc_transform (output, logical_monitor->transform);

  if (meta_monitor_manager_is_transform_handled (monitor_manager,
                                                 crtc,
                                                 crtc_transform))
    return META_MONITOR_TRANSFORM_NORMAL;
  else
    return crtc_transform;
}

static gboolean
should_force_shadow_fb (MetaRendererNative *renderer_native,
                        MetaGpuKms         *primary_gpu)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  int kms_fd;
  uint64_t prefer_shadow = 0;

  if (meta_renderer_is_hardware_accelerated (renderer))
    return FALSE;

  kms_fd = meta_gpu_kms_get_fd (primary_gpu);
  if (drmGetCap (kms_fd, DRM_CAP_DUMB_PREFER_SHADOW, &prefer_shadow) == 0)
    {
      if (prefer_shadow)
        {
          static gboolean logged_once = FALSE;

          if (!logged_once)
            {
              g_message ("Forcing shadow framebuffer");
              logged_once = TRUE;
            }

          return TRUE;
        }
    }

  return FALSE;
}

static MetaRendererView *
meta_renderer_native_create_view (MetaRenderer       *renderer,
                                  MetaLogicalMonitor *logical_monitor,
                                  MetaOutput         *output,
                                  MetaCrtc           *crtc)
{
  MetaRendererNative *renderer_native = META_RENDERER_NATIVE (renderer);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  CoglContext *cogl_context =
    cogl_context_from_renderer_native (renderer_native);
  CoglDisplay *cogl_display = cogl_context_get_display (cogl_context);
  CoglDisplayEGL *cogl_display_egl;
  CoglOnscreenEGL *onscreen_egl;
  MetaCrtcConfig *crtc_config;
  MetaMonitorTransform view_transform;
  CoglOnscreen *onscreen = NULL;
  CoglOffscreen *offscreen = NULL;
  CoglOffscreen *shadowfb = NULL;
  float scale;
  int onscreen_width;
  int onscreen_height;
  MetaRectangle view_layout;
  MetaRendererView *view;
  GError *error = NULL;

  crtc_config = crtc->config;
  onscreen_width = crtc_config->mode->width;
  onscreen_height = crtc_config->mode->height;

  onscreen = meta_renderer_native_create_onscreen (renderer_native,
                                                   renderer_native->primary_gpu_kms,
                                                   output,
                                                   crtc,
                                                   cogl_context,
                                                   onscreen_width,
                                                   onscreen_height,
                                                   &error);
  if (!onscreen)
    g_error ("Failed to allocate onscreen framebuffer: %s", error->message);

  view_transform = calculate_view_transform (monitor_manager,
                                             logical_monitor,
                                             output,
                                             crtc);
  if (view_transform != META_MONITOR_TRANSFORM_NORMAL)
    {
      int offscreen_width;
      int offscreen_height;

      if (meta_monitor_transform_is_rotated (view_transform))
        {
          offscreen_width = onscreen_height;
          offscreen_height = onscreen_width;
        }
      else
        {
          offscreen_width = onscreen_width;
          offscreen_height = onscreen_height;
        }

      offscreen = meta_renderer_native_create_offscreen (renderer_native,
                                                         cogl_context,
                                                         offscreen_width,
                                                         offscreen_height,
                                                         &error);
      if (!offscreen)
        g_error ("Failed to allocate back buffer texture: %s", error->message);
    }

  if (should_force_shadow_fb (renderer_native,
                              renderer_native->primary_gpu_kms))
    {
      int shadow_width;
      int shadow_height;

      /* The shadowfb must be the same size as the on-screen framebuffer */
      shadow_width = cogl_framebuffer_get_width (COGL_FRAMEBUFFER (onscreen));
      shadow_height = cogl_framebuffer_get_height (COGL_FRAMEBUFFER (onscreen));

      shadowfb = meta_renderer_native_create_offscreen (renderer_native,
                                                        cogl_context,
                                                        shadow_width,
                                                        shadow_height,
                                                        &error);
      if (!shadowfb)
        g_error ("Failed to allocate shadow buffer texture: %s", error->message);
    }

  if (meta_is_stage_views_scaled ())
    scale = meta_logical_monitor_get_scale (logical_monitor);
  else
    scale = 1.0;

  meta_rectangle_from_graphene_rect (&crtc->config->layout,
                                     META_ROUNDING_STRATEGY_ROUND,
                                     &view_layout);
  view = g_object_new (META_TYPE_RENDERER_VIEW,
                       "layout", &view_layout,
                       "scale", scale,
                       "framebuffer", onscreen,
                       "offscreen", offscreen,
                       "shadowfb", shadowfb,
                       "transform", view_transform,
                       NULL);
  g_clear_pointer (&offscreen, cogl_object_unref);
  g_clear_pointer (&shadowfb, cogl_object_unref);

  meta_onscreen_native_set_view (onscreen, view);

  if (!meta_onscreen_native_allocate (onscreen, &error))
    {
      g_warning ("Could not create onscreen: %s", error->message);
      cogl_object_unref (onscreen);
      g_object_unref (view);
      g_error_free (error);
      return NULL;
    }

  cogl_object_unref (onscreen);

  /* Ensure we don't point to stale surfaces when creating the offscreen */
  onscreen_egl = onscreen->winsys;
  cogl_display_egl = cogl_display->winsys;
  _cogl_winsys_egl_make_current (cogl_display,
                                 onscreen_egl->egl_surface,
                                 onscreen_egl->egl_surface,
                                 cogl_display_egl->egl_context);

  return view;
}

static void
meta_renderer_native_rebuild_views (MetaRenderer *renderer)
{
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  MetaRendererClass *parent_renderer_class =
    META_RENDERER_CLASS (meta_renderer_native_parent_class);

  meta_kms_discard_pending_page_flips (kms);

  parent_renderer_class->rebuild_views (renderer);

  meta_renderer_native_queue_modes_reset (META_RENDERER_NATIVE (renderer));
}

void
meta_renderer_native_finish_frame (MetaRendererNative *renderer_native)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  MetaKmsUpdate *kms_update = NULL;

  renderer_native->frame_counter++;

  if (renderer_native->pending_unset_disabled_crtcs)
    {
      kms_update = unset_disabled_crtcs (backend, kms);
      renderer_native->pending_unset_disabled_crtcs = FALSE;
    }

  if (kms_update)
    post_pending_update (kms);
}

int64_t
meta_renderer_native_get_frame_counter (MetaRendererNative *renderer_native)
{
  return renderer_native->frame_counter;
}

static gboolean
create_secondary_egl_config (MetaEgl               *egl,
                             MetaRendererNativeMode mode,
                             EGLDisplay             egl_display,
                             EGLConfig             *egl_config,
                             GError               **error)
{
  EGLint attributes[] = {
    EGL_RED_SIZE, 1,
    EGL_GREEN_SIZE, 1,
    EGL_BLUE_SIZE, 1,
    EGL_ALPHA_SIZE, EGL_DONT_CARE,
    EGL_BUFFER_SIZE, EGL_DONT_CARE,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_NONE
  };

  switch (mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      return choose_egl_config_from_gbm_format (egl,
                                                egl_display,
                                                attributes,
                                                GBM_FORMAT_XRGB8888,
                                                egl_config,
                                                error);
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      return meta_egl_choose_first_config (egl,
                                           egl_display,
                                           attributes,
                                           egl_config,
                                           error);
#endif
    }

  return FALSE;
}

static EGLContext
create_secondary_egl_context (MetaEgl   *egl,
                              EGLDisplay egl_display,
                              EGLConfig  egl_config,
                              GError   **error)
{
  EGLint attributes[] = {
    EGL_CONTEXT_CLIENT_VERSION, 3,
    EGL_NONE
  };

  return meta_egl_create_context (egl,
                                  egl_display,
                                  egl_config,
                                  EGL_NO_CONTEXT,
                                  attributes,
                                  error);
}

static void
meta_renderer_native_ensure_gles3 (MetaRendererNative *renderer_native)
{
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);

  if (renderer_native->gles3)
    return;

  renderer_native->gles3 = meta_gles3_new (egl);
}

static gboolean
init_secondary_gpu_data_gpu (MetaRendererNativeGpuData *renderer_gpu_data,
                             GError                   **error)
{
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);
  EGLDisplay egl_display = renderer_gpu_data->egl_display;
  EGLConfig egl_config;
  EGLContext egl_context;
  const char **missing_gl_extensions;
  const char *renderer_str;

  if (!create_secondary_egl_config (egl, renderer_gpu_data->mode, egl_display,
                                    &egl_config, error))
    return FALSE;

  egl_context = create_secondary_egl_context (egl, egl_display, egl_config, error);
  if (egl_context == EGL_NO_CONTEXT)
    return FALSE;

  meta_renderer_native_ensure_gles3 (renderer_native);

  if (!meta_egl_make_current (egl,
                              egl_display,
                              EGL_NO_SURFACE,
                              EGL_NO_SURFACE,
                              egl_context,
                              error))
    {
      meta_egl_destroy_context (egl, egl_display, egl_context, NULL);
      return FALSE;
    }

  renderer_str = (const char *) glGetString (GL_RENDERER);
  if (g_str_has_prefix (renderer_str, "llvmpipe") ||
      g_str_has_prefix (renderer_str, "softpipe") ||
      g_str_has_prefix (renderer_str, "swrast"))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Do not want to use software renderer (%s), falling back to CPU copy path",
                   renderer_str);
      goto out_fail_with_context;
    }

  if (!meta_gles3_has_extensions (renderer_native->gles3,
                                  &missing_gl_extensions,
                                  "GL_OES_EGL_image_external",
                                  NULL))
    {
      char *missing_gl_extensions_str;

      missing_gl_extensions_str = g_strjoinv (", ",
                                              (char **) missing_gl_extensions);
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Missing OpenGL ES extensions: %s",
                   missing_gl_extensions_str);
      g_free (missing_gl_extensions_str);
      g_free (missing_gl_extensions);

      goto out_fail_with_context;
    }

  renderer_gpu_data->secondary.is_hardware_rendering = TRUE;
  renderer_gpu_data->secondary.egl_context = egl_context;
  renderer_gpu_data->secondary.egl_config = egl_config;
  renderer_gpu_data->secondary.copy_mode = META_SHARED_FRAMEBUFFER_COPY_MODE_SECONDARY_GPU;

  renderer_gpu_data->secondary.has_EGL_EXT_image_dma_buf_import_modifiers =
    meta_egl_has_extensions (egl, egl_display, NULL,
                             "EGL_EXT_image_dma_buf_import_modifiers",
                             NULL);

  return TRUE;

out_fail_with_context:
  meta_egl_make_current (egl,
                         egl_display,
                         EGL_NO_SURFACE,
                         EGL_NO_SURFACE,
                         EGL_NO_CONTEXT,
                         NULL);
  meta_egl_destroy_context (egl, egl_display, egl_context, NULL);

  return FALSE;
}

static void
init_secondary_gpu_data_cpu (MetaRendererNativeGpuData *renderer_gpu_data)
{
  renderer_gpu_data->secondary.is_hardware_rendering = FALSE;

  /* First try ZERO, it automatically falls back to PRIMARY as needed */
  renderer_gpu_data->secondary.copy_mode =
    META_SHARED_FRAMEBUFFER_COPY_MODE_ZERO;
}

static void
init_secondary_gpu_data (MetaRendererNativeGpuData *renderer_gpu_data)
{
  GError *error = NULL;

  if (init_secondary_gpu_data_gpu (renderer_gpu_data, &error))
    return;

  g_warning ("Failed to initialize accelerated iGPU/dGPU framebuffer sharing: %s",
             error->message);
  g_error_free (error);

  init_secondary_gpu_data_cpu (renderer_gpu_data);
}

static gboolean
gpu_kms_is_hardware_rendering (MetaRendererNative *renderer_native,
                               MetaGpuKms         *gpu_kms)
{
  MetaRendererNativeGpuData *data;

  data = meta_renderer_native_get_gpu_data (renderer_native, gpu_kms);
  return data->secondary.is_hardware_rendering;
}

static EGLDisplay
init_gbm_egl_display (MetaRendererNative  *renderer_native,
                      struct gbm_device   *gbm_device,
                      GError             **error)
{
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);
  EGLDisplay egl_display;

  if (!meta_egl_has_extensions (egl, EGL_NO_DISPLAY, NULL,
                                "EGL_MESA_platform_gbm",
                                NULL) &&
      !meta_egl_has_extensions (egl, EGL_NO_DISPLAY, NULL,
                                "EGL_KHR_platform_gbm",
                                NULL))
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Missing extension for GBM renderer: EGL_KHR_platform_gbm");
      return EGL_NO_DISPLAY;
    }

  egl_display = meta_egl_get_platform_display (egl,
                                               EGL_PLATFORM_GBM_KHR,
                                               gbm_device, NULL, error);
  if (egl_display == EGL_NO_DISPLAY)
    return EGL_NO_DISPLAY;

  if (!meta_egl_initialize (egl, egl_display, error))
    return EGL_NO_DISPLAY;

  return egl_display;
}

static MetaRendererNativeGpuData *
create_renderer_gpu_data_gbm (MetaRendererNative  *renderer_native,
                              MetaGpuKms          *gpu_kms,
                              GError             **error)
{
  struct gbm_device *gbm_device;
  int kms_fd;
  MetaRendererNativeGpuData *renderer_gpu_data;
  g_autoptr (GError) local_error = NULL;

  kms_fd = meta_gpu_kms_get_fd (gpu_kms);

  gbm_device = gbm_create_device (kms_fd);
  if (!gbm_device)
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Failed to create gbm device: %s", g_strerror (errno));
      return NULL;
    }

  renderer_gpu_data = meta_create_renderer_native_gpu_data (gpu_kms);
  renderer_gpu_data->renderer_native = renderer_native;
  renderer_gpu_data->gbm.device = gbm_device;
  renderer_gpu_data->mode = META_RENDERER_NATIVE_MODE_GBM;

  renderer_gpu_data->egl_display = init_gbm_egl_display (renderer_native,
                                                         gbm_device,
                                                         &local_error);
  if (renderer_gpu_data->egl_display == EGL_NO_DISPLAY)
    {
      g_debug ("GBM EGL init for %s failed: %s",
               meta_gpu_kms_get_file_path (gpu_kms),
               local_error->message);

      init_secondary_gpu_data_cpu (renderer_gpu_data);
      return renderer_gpu_data;
    }

  init_secondary_gpu_data (renderer_gpu_data);
  return renderer_gpu_data;
}

#ifdef HAVE_EGL_DEVICE
static const char *
get_drm_device_file (MetaEgl     *egl,
                     EGLDeviceEXT device,
                     GError     **error)
{
  if (!meta_egl_egl_device_has_extensions (egl, device,
                                           NULL,
                                           "EGL_EXT_device_drm",
                                           NULL))
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Missing required EGLDevice extension EGL_EXT_device_drm");
      return NULL;
    }

  return meta_egl_query_device_string (egl, device,
                                       EGL_DRM_DEVICE_FILE_EXT,
                                       error);
}

static EGLDeviceEXT
find_egl_device (MetaRendererNative  *renderer_native,
                 MetaGpuKms          *gpu_kms,
                 GError             **error)
{
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);
  const char **missing_extensions;
  EGLint num_devices;
  EGLDeviceEXT *devices;
  const char *kms_file_path;
  EGLDeviceEXT device;
  EGLint i;

  if (!meta_egl_has_extensions (egl,
                                EGL_NO_DISPLAY,
                                &missing_extensions,
                                "EGL_EXT_device_base",
                                NULL))
    {
      char *missing_extensions_str;

      missing_extensions_str = g_strjoinv (", ", (char **) missing_extensions);
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Missing EGL extensions required for EGLDevice renderer: %s",
                   missing_extensions_str);
      g_free (missing_extensions_str);
      g_free (missing_extensions);
      return EGL_NO_DEVICE_EXT;
    }

  if (!meta_egl_query_devices (egl, 0, NULL, &num_devices, error))
    return EGL_NO_DEVICE_EXT;

  devices = g_new0 (EGLDeviceEXT, num_devices);
  if (!meta_egl_query_devices (egl, num_devices, devices, &num_devices,
                               error))
    {
      g_free (devices);
      return EGL_NO_DEVICE_EXT;
    }

  kms_file_path = meta_gpu_kms_get_file_path (gpu_kms);

  device = EGL_NO_DEVICE_EXT;
  for (i = 0; i < num_devices; i++)
    {
      const char *egl_device_drm_path;

      g_clear_error (error);

      egl_device_drm_path = get_drm_device_file (egl, devices[i], error);
      if (!egl_device_drm_path)
        continue;

      if (g_str_equal (egl_device_drm_path, kms_file_path))
        {
          device = devices[i];
          break;
        }
    }
  g_free (devices);

  if (device == EGL_NO_DEVICE_EXT)
    {
      if (!*error)
        g_set_error (error, G_IO_ERROR,
                     G_IO_ERROR_FAILED,
                     "Failed to find matching EGLDeviceEXT");
      return EGL_NO_DEVICE_EXT;
    }

  return device;
}

static EGLDisplay
get_egl_device_display (MetaRendererNative  *renderer_native,
                        MetaGpuKms          *gpu_kms,
                        EGLDeviceEXT         egl_device,
                        GError             **error)
{
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);
  int kms_fd = meta_gpu_kms_get_fd (gpu_kms);
  EGLint platform_attribs[] = {
    EGL_DRM_MASTER_FD_EXT, kms_fd,
    EGL_NONE
  };

  return meta_egl_get_platform_display (egl, EGL_PLATFORM_DEVICE_EXT,
                                        (void *) egl_device,
                                        platform_attribs,
                                        error);
}

static int
count_drm_devices (MetaRendererNative *renderer_native)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);

  return g_list_length (meta_backend_get_gpus (backend));
}

static MetaRendererNativeGpuData *
create_renderer_gpu_data_egl_device (MetaRendererNative  *renderer_native,
                                     MetaGpuKms          *gpu_kms,
                                     GError             **error)
{
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);
  const char **missing_extensions;
  EGLDeviceEXT egl_device;
  EGLDisplay egl_display;
  MetaRendererNativeGpuData *renderer_gpu_data;

  if (count_drm_devices (renderer_native) != 1)
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "EGLDevice currently only works with single GPU systems");
      return NULL;
    }

  egl_device = find_egl_device (renderer_native, gpu_kms, error);
  if (egl_device == EGL_NO_DEVICE_EXT)
    return NULL;

  egl_display = get_egl_device_display (renderer_native, gpu_kms,
                                        egl_device, error);
  if (egl_display == EGL_NO_DISPLAY)
    return NULL;

  if (!meta_egl_initialize (egl, egl_display, error))
    return NULL;

  if (!meta_egl_has_extensions (egl,
                                egl_display,
                                &missing_extensions,
                                "EGL_NV_output_drm_flip_event",
                                "EGL_EXT_output_base",
                                "EGL_EXT_output_drm",
                                "EGL_KHR_stream",
                                "EGL_KHR_stream_producer_eglsurface",
                                "EGL_EXT_stream_consumer_egloutput",
                                "EGL_EXT_stream_acquire_mode",
                                NULL))
    {
      char *missing_extensions_str;

      missing_extensions_str = g_strjoinv (", ", (char **) missing_extensions);
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Missing EGL extensions required for EGLDevice renderer: %s",
                   missing_extensions_str);
      meta_egl_terminate (egl, egl_display, NULL);
      g_free (missing_extensions_str);
      g_free (missing_extensions);
      return NULL;
    }

  renderer_gpu_data = meta_create_renderer_native_gpu_data (gpu_kms);
  renderer_gpu_data->renderer_native = renderer_native;
  renderer_gpu_data->egl.device = egl_device;
  renderer_gpu_data->mode = META_RENDERER_NATIVE_MODE_EGL_DEVICE;
  renderer_gpu_data->egl_display = egl_display;

  return renderer_gpu_data;
}
#endif /* HAVE_EGL_DEVICE */

static MetaRendererNativeGpuData *
meta_renderer_native_create_renderer_gpu_data (MetaRendererNative  *renderer_native,
                                               MetaGpuKms          *gpu_kms,
                                               GError             **error)
{
  MetaRendererNativeGpuData *renderer_gpu_data;
  GError *gbm_error = NULL;
#ifdef HAVE_EGL_DEVICE
  GError *egl_device_error = NULL;
#endif

#ifdef HAVE_EGL_DEVICE
  /* Try to initialize the EGLDevice backend first. Whenever we use a
   * non-NVIDIA GPU, the EGLDevice enumeration function won't find a match, and
   * we'll fall back to GBM (which will always succeed as it has a software
   * rendering fallback)
   */
  renderer_gpu_data = create_renderer_gpu_data_egl_device (renderer_native,
                                                           gpu_kms,
                                                           &egl_device_error);
  if (renderer_gpu_data)
    return renderer_gpu_data;
#endif

  renderer_gpu_data = create_renderer_gpu_data_gbm (renderer_native,
                                                    gpu_kms,
                                                    &gbm_error);
  if (renderer_gpu_data)
    {
#ifdef HAVE_EGL_DEVICE
      g_error_free (egl_device_error);
#endif
      return renderer_gpu_data;
    }

  g_set_error (error, G_IO_ERROR,
               G_IO_ERROR_FAILED,
               "Failed to initialize renderer: "
               "%s"
#ifdef HAVE_EGL_DEVICE
               ", %s"
#endif
               , gbm_error->message
#ifdef HAVE_EGL_DEVICE
               , egl_device_error->message
#endif
  );

  g_error_free (gbm_error);
#ifdef HAVE_EGL_DEVICE
  g_error_free (egl_device_error);
#endif

  return NULL;
}

static gboolean
create_renderer_gpu_data (MetaRendererNative  *renderer_native,
                          MetaGpuKms          *gpu_kms,
                          GError             **error)
{
  MetaRendererNativeGpuData *renderer_gpu_data;

  renderer_gpu_data =
    meta_renderer_native_create_renderer_gpu_data (renderer_native,
                                                   gpu_kms,
                                                   error);
  if (!renderer_gpu_data)
    return FALSE;

  g_hash_table_insert (renderer_native->gpu_datas,
                       gpu_kms,
                       renderer_gpu_data);

  return TRUE;
}

static void
on_gpu_added (MetaBackendNative  *backend_native,
              MetaGpuKms         *gpu_kms,
              MetaRendererNative *renderer_native)
{
  MetaBackend *backend = META_BACKEND (backend_native);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  CoglDisplay *cogl_display = cogl_context_get_display (cogl_context);
  GError *error = NULL;

  if (!create_renderer_gpu_data (renderer_native, gpu_kms, &error))
    {
      g_warning ("on_gpu_added: could not create gpu_data for gpu %s: %s",
                 meta_gpu_kms_get_file_path (gpu_kms), error->message);
      g_clear_error (&error);
    }

  _cogl_winsys_egl_ensure_current (cogl_display);
}

static void
on_power_save_mode_changed (MetaMonitorManager *monitor_manager,
                            MetaRendererNative *renderer_native)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  MetaPowerSave power_save_mode;

  power_save_mode = meta_monitor_manager_get_power_save_mode (monitor_manager);
  if (power_save_mode == META_POWER_SAVE_ON)
    meta_renderer_native_queue_modes_reset (renderer_native);
  else
    meta_kms_discard_pending_page_flips (kms);
}

void
meta_renderer_native_reset_modes (MetaRendererNative *renderer_native)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  MetaKmsUpdate *kms_update;

  kms_update = unset_disabled_crtcs (backend, kms);

  if (kms_update)
    post_pending_update (kms);
}

static MetaGpuKms *
choose_primary_gpu_unchecked (MetaBackend        *backend,
                              MetaRendererNative *renderer_native)
{
  GList *gpus = meta_backend_get_gpus (backend);
  GList *l;
  int allow_sw;

  /*
   * Check first hardware rendering devices, and if none found,
   * then software rendering devices.
   */
  for (allow_sw = 0; allow_sw < 2; allow_sw++)
  {
    /* Prefer a platform device */
    for (l = gpus; l; l = l->next)
      {
        MetaGpuKms *gpu_kms = META_GPU_KMS (l->data);

        if (meta_gpu_kms_is_platform_device (gpu_kms) &&
            (allow_sw == 1 ||
             gpu_kms_is_hardware_rendering (renderer_native, gpu_kms)))
          return gpu_kms;
      }

    /* Otherwise a device we booted with */
    for (l = gpus; l; l = l->next)
      {
        MetaGpuKms *gpu_kms = META_GPU_KMS (l->data);

        if (meta_gpu_kms_is_boot_vga (gpu_kms) &&
            (allow_sw == 1 ||
             gpu_kms_is_hardware_rendering (renderer_native, gpu_kms)))
          return gpu_kms;
      }

    /* Fall back to any device */
    for (l = gpus; l; l = l->next)
      {
        MetaGpuKms *gpu_kms = META_GPU_KMS (l->data);

        if (allow_sw == 1 ||
            gpu_kms_is_hardware_rendering (renderer_native, gpu_kms))
          return gpu_kms;
      }
  }

  g_assert_not_reached ();
  return NULL;
}

static MetaGpuKms *
choose_primary_gpu (MetaBackend         *backend,
                    MetaRendererNative  *renderer_native,
                    GError             **error)
{
  MetaGpuKms *gpu_kms;
  MetaRendererNativeGpuData *renderer_gpu_data;

  gpu_kms = choose_primary_gpu_unchecked (backend, renderer_native);
  renderer_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                         gpu_kms);
  if (renderer_gpu_data->egl_display == EGL_NO_DISPLAY)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "The GPU %s chosen as primary is not supported by EGL.",
                   meta_gpu_kms_get_file_path (gpu_kms));
      return NULL;
    }

  return gpu_kms;
}

static gboolean
meta_renderer_native_initable_init (GInitable     *initable,
                                    GCancellable  *cancellable,
                                    GError       **error)
{
  MetaRendererNative *renderer_native = META_RENDERER_NATIVE (initable);
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  GList *gpus;
  GList *l;

  gpus = meta_backend_get_gpus (backend);
  for (l = gpus; l; l = l->next)
    {
      MetaGpuKms *gpu_kms = META_GPU_KMS (l->data);

      if (!create_renderer_gpu_data (renderer_native, gpu_kms, error))
        return FALSE;
    }

  renderer_native->primary_gpu_kms = choose_primary_gpu (backend,
                                                         renderer_native,
                                                         error);
  if (!renderer_native->primary_gpu_kms)
    return FALSE;

  return TRUE;
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_iface->init = meta_renderer_native_initable_init;
}

static void
meta_renderer_native_finalize (GObject *object)
{
  MetaRendererNative *renderer_native = META_RENDERER_NATIVE (object);

  if (renderer_native->power_save_page_flip_onscreens)
    {
      g_list_free_full (renderer_native->power_save_page_flip_onscreens,
                        (GDestroyNotify) cogl_object_unref);
      g_clear_handle_id (&renderer_native->power_save_page_flip_source_id,
                         g_source_remove);
    }

  g_hash_table_destroy (renderer_native->gpu_datas);
  g_clear_object (&renderer_native->gles3);

  G_OBJECT_CLASS (meta_renderer_native_parent_class)->finalize (object);
}

static void
meta_renderer_native_constructed (GObject *object)
{
  MetaRendererNative *renderer_native = META_RENDERER_NATIVE (object);
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaSettings *settings = meta_backend_get_settings (backend);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  if (meta_settings_is_experimental_feature_enabled (
        settings, META_EXPERIMENTAL_FEATURE_KMS_MODIFIERS))
    renderer_native->use_modifiers = TRUE;

  g_signal_connect (backend, "gpu-added",
                    G_CALLBACK (on_gpu_added), renderer_native);
  g_signal_connect (monitor_manager, "power-save-mode-changed",
                    G_CALLBACK (on_power_save_mode_changed), renderer_native);

  G_OBJECT_CLASS (meta_renderer_native_parent_class)->constructed (object);
}

static void
meta_renderer_native_init (MetaRendererNative *renderer_native)
{
  renderer_native->gpu_datas =
    g_hash_table_new_full (NULL, NULL,
                           NULL,
                           (GDestroyNotify) meta_renderer_native_gpu_data_free);
}

static void
meta_renderer_native_class_init (MetaRendererNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaRendererClass *renderer_class = META_RENDERER_CLASS (klass);

  object_class->finalize = meta_renderer_native_finalize;
  object_class->constructed = meta_renderer_native_constructed;

  renderer_class->create_cogl_renderer = meta_renderer_native_create_cogl_renderer;
  renderer_class->create_view = meta_renderer_native_create_view;
  renderer_class->rebuild_views = meta_renderer_native_rebuild_views;
}

MetaRendererNative *
meta_renderer_native_new (MetaBackendNative  *backend_native,
                          GError            **error)
{
  return g_initable_new (META_TYPE_RENDERER_NATIVE,
                         NULL,
                         error,
                         "backend", backend_native,
                         NULL);
}
