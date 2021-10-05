/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2017 Red Hat
 * Copyright (c) 2018 DisplayLink (UK) Ltd.
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
 */

#include "config.h"

#define GL_GLEXT_PROTOTYPES

#include "backends/native/meta-renderer-native-gles3.h"

#include <GLES3/gl3.h>
#include <drm_fourcc.h>
#include <errno.h>
#include <gio/gio.h>
#include <string.h>

#include "backends/meta-egl-ext.h"
#include "backends/meta-gles3.h"
#include "backends/meta-gles3-table.h"

/*
 * GL/gl.h being included may conflit with gl3.h on some architectures.
 * Make sure that hasn't happened on any architecture.
 */
#ifdef GL_VERSION_1_1
#error "Somehow included OpenGL headers when we shouldn't have"
#endif

static void
paint_egl_image (MetaGles3   *gles3,
                 EGLImageKHR  egl_image,
                 int          width,
                 int          height)
{
  GLuint texture;
  GLuint framebuffer;

  meta_gles3_clear_error (gles3);

  GLBAS (gles3, glGenFramebuffers, (1, &framebuffer));
  GLBAS (gles3, glBindFramebuffer, (GL_READ_FRAMEBUFFER, framebuffer));

  GLBAS (gles3, glActiveTexture, (GL_TEXTURE0));
  GLBAS (gles3, glGenTextures, (1, &texture));
  GLBAS (gles3, glBindTexture, (GL_TEXTURE_2D, texture));
  GLEXT (gles3, glEGLImageTargetTexture2DOES, (GL_TEXTURE_2D, egl_image));
  GLBAS (gles3, glTexParameteri, (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                  GL_NEAREST));
  GLBAS (gles3, glTexParameteri, (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                  GL_NEAREST));
  GLBAS (gles3, glTexParameteri, (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                                  GL_CLAMP_TO_EDGE));
  GLBAS (gles3, glTexParameteri, (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                                  GL_CLAMP_TO_EDGE));
  GLBAS (gles3, glTexParameteri, (GL_TEXTURE_2D, GL_TEXTURE_WRAP_R_OES,
                                  GL_CLAMP_TO_EDGE));

  GLBAS (gles3, glFramebufferTexture2D, (GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                         GL_TEXTURE_2D, texture, 0));

  GLBAS (gles3, glBindFramebuffer, (GL_READ_FRAMEBUFFER, framebuffer));
  GLBAS (gles3, glBlitFramebuffer, (0, height, width, 0,
                                    0, 0, width, height,
                                    GL_COLOR_BUFFER_BIT,
                                    GL_NEAREST));

  GLBAS (gles3, glDeleteTextures, (1, &texture));
  GLBAS (gles3, glDeleteFramebuffers, (1, &framebuffer));
}

gboolean
meta_renderer_native_gles3_blit_shared_bo (MetaEgl        *egl,
                                           MetaGles3      *gles3,
                                           EGLDisplay      egl_display,
                                           EGLContext      egl_context,
                                           EGLSurface      egl_surface,
                                           struct gbm_bo  *shared_bo,
                                           GError        **error)
{
  int shared_bo_fd;
  unsigned int width;
  unsigned int height;
  uint32_t i, n_planes;
  uint32_t strides[4] = { 0 };
  uint32_t offsets[4] = { 0 };
  uint64_t modifiers[4] = { 0 };
  int fds[4] = { -1, -1, -1, -1 };
  uint32_t format;
  EGLImageKHR egl_image;
  gboolean use_modifiers;

  shared_bo_fd = gbm_bo_get_fd (shared_bo);
  if (shared_bo_fd < 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to export gbm_bo: %s", strerror (errno));
      return FALSE;
    }

  width = gbm_bo_get_width (shared_bo);
  height = gbm_bo_get_height (shared_bo);
  format = gbm_bo_get_format (shared_bo);

  n_planes = gbm_bo_get_plane_count (shared_bo);
  for (i = 0; i < n_planes; i++)
    {
      strides[i] = gbm_bo_get_stride_for_plane (shared_bo, i);
      offsets[i] = gbm_bo_get_offset (shared_bo, i);
      modifiers[i] = gbm_bo_get_modifier (shared_bo);
      fds[i] = shared_bo_fd;
    }

  /* Workaround for https://gitlab.gnome.org/GNOME/mutter/issues/18 */
  if (modifiers[0] == DRM_FORMAT_MOD_LINEAR ||
      modifiers[0] == DRM_FORMAT_MOD_INVALID)
    use_modifiers = FALSE;
  else
    use_modifiers = TRUE;

  egl_image = meta_egl_create_dmabuf_image (egl,
                                            egl_display,
                                            width,
                                            height,
                                            format,
                                            n_planes,
                                            fds,
                                            strides,
                                            offsets,
                                            use_modifiers ? modifiers : NULL,
                                            error);
  close (shared_bo_fd);

  if (!egl_image)
    return FALSE;

  paint_egl_image (gles3, egl_image, width, height);

  meta_egl_destroy_image (egl, egl_display, egl_image, NULL);

  return TRUE;
}
