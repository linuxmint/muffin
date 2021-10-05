/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * MetaBackgroundImageCache:
 *
 * Simple cache for background textures loaded from files
 *
 * Copyright 2014 Red Hat, Inc.
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __META_BACKGROUND_IMAGE_H__
#define __META_BACKGROUND_IMAGE_H__

#include <gio/gio.h>
#include <glib-object.h>

#include "cogl/cogl.h"
#include "meta/display.h"

#define META_TYPE_BACKGROUND_IMAGE (meta_background_image_get_type ())

META_EXPORT
G_DECLARE_FINAL_TYPE (MetaBackgroundImage,
                      meta_background_image,
                      META, BACKGROUND_IMAGE,
                      GObject)

META_EXPORT
gboolean     meta_background_image_is_loaded   (MetaBackgroundImage *image);

META_EXPORT
gboolean     meta_background_image_get_success (MetaBackgroundImage *image);

META_EXPORT
CoglTexture *meta_background_image_get_texture (MetaBackgroundImage *image);


#define META_TYPE_BACKGROUND_IMAGE_CACHE (meta_background_image_cache_get_type ())

META_EXPORT
G_DECLARE_FINAL_TYPE (MetaBackgroundImageCache,
                      meta_background_image_cache,
                      META, BACKGROUND_IMAGE_CACHE,
                      GObject)

META_EXPORT
MetaBackgroundImageCache *meta_background_image_cache_get_default (void);

META_EXPORT
MetaBackgroundImage *meta_background_image_cache_load  (MetaBackgroundImageCache *cache,
                                                        GFile                    *file);

META_EXPORT
void                 meta_background_image_cache_purge (MetaBackgroundImageCache *cache,
                                                        GFile                    *file);

#endif /* __META_BACKGROUND_IMAGE_H__ */
