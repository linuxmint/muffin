/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Utilities for use with Cogl
 *
 * Copyright 2010 Red Hat, Inc.
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
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 */

#ifndef __META_COGL_UTILS_H__
#define __META_COGL_UTILS_H__

#define COGL_ENABLE_EXPERIMENTAL_API
#define CLUTTER_ENABLE_EXPERIMENTAL_API

#include <cogl/cogl.h>
#include <clutter/clutter.h>

CoglHandle meta_create_color_texture_4ub (guint8           red,
                                          guint8           green,
                                          guint8           blue,
                                          guint8           alpha,
                                          CoglTextureFlags flags);
CoglHandle meta_create_texture_material  (CoglHandle src_texture);

CoglTexture * meta_cogl_texture_new_from_data_wrapper                (int  width,
                                                                      int  height,
                                                         CoglTextureFlags  flags,
                                                          CoglPixelFormat  format,
                                                          CoglPixelFormat  internal_format,
                                                                      int  rowstride,
                                                            const uint8_t *data);

CoglTexture * meta_cogl_texture_new_from_file_wrapper         (const char *filename,
                                                         CoglTextureFlags  flags,
                                                          CoglPixelFormat  internal_format);

CoglTexture * meta_cogl_texture_new_with_size_wrapper                (int  width,
                                                                      int  height,
                                                         CoglTextureFlags  flags,
                                                          CoglPixelFormat  internal_format);

#endif /* __META_COGL_UTILS_H__ */
