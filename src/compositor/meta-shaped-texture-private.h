/*
 * shaped texture
 *
 * An actor to draw a texture clipped to a list of rectangles
 *
 * Authored By Neil Roberts  <neil@linux.intel.com>
 *
 * Copyright (C) 2008 Intel Corporation
 *               2013 Red Hat, Inc.
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
 */

#ifndef __META_SHAPED_TEXTURE_PRIVATE_H__
#define __META_SHAPED_TEXTURE_PRIVATE_H__

#include "backends/meta-monitor-manager-private.h"
#include "meta/meta-shaped-texture.h"

MetaShapedTexture *meta_shaped_texture_new (void);
void meta_shaped_texture_set_texture (MetaShapedTexture *stex,
                                      CoglTexture       *texture);
void meta_shaped_texture_set_is_y_inverted (MetaShapedTexture *stex,
                                            gboolean           is_y_inverted);
void meta_shaped_texture_set_snippet (MetaShapedTexture *stex,
                                      CoglSnippet       *snippet);
void meta_shaped_texture_set_fallback_size (MetaShapedTexture *stex,
                                            int                fallback_width,
                                            int                fallback_height);
cairo_region_t * meta_shaped_texture_get_opaque_region (MetaShapedTexture *stex);
gboolean meta_shaped_texture_is_opaque (MetaShapedTexture *stex);
gboolean meta_shaped_texture_has_alpha (MetaShapedTexture *stex);
void meta_shaped_texture_set_transform (MetaShapedTexture    *stex,
                                        MetaMonitorTransform  transform);
void meta_shaped_texture_set_viewport_src_rect (MetaShapedTexture *stex,
                                                graphene_rect_t   *src_rect);
void meta_shaped_texture_reset_viewport_src_rect (MetaShapedTexture *stex);
void meta_shaped_texture_set_viewport_dst_size (MetaShapedTexture *stex,
                                                int                dst_width,
                                                int                dst_height);
void meta_shaped_texture_reset_viewport_dst_size (MetaShapedTexture *stex);
void meta_shaped_texture_set_buffer_scale (MetaShapedTexture *stex,
                                           int                buffer_scale);
int meta_shaped_texture_get_buffer_scale (MetaShapedTexture *stex);

gboolean meta_shaped_texture_update_area (MetaShapedTexture     *stex,
                                          int                    x,
                                          int                    y,
                                          int                    width,
                                          int                    height,
                                          cairo_rectangle_int_t *clip);

int meta_shaped_texture_get_width (MetaShapedTexture *stex);
int meta_shaped_texture_get_height (MetaShapedTexture *stex);

void meta_shaped_texture_set_clip_region (MetaShapedTexture *stex,
                                          cairo_region_t    *clip_region);

#endif
