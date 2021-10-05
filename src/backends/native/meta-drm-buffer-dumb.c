/*
 * Copyright (C) 2011 Intel Corporation.
 * Copyright (C) 2016 Red Hat
 * Copyright (C) 2018 DisplayLink (UK) Ltd.
 * Copyright (C) 2018 Canonical Ltd.
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

#include "backends/native/meta-drm-buffer-dumb.h"

struct _MetaDrmBufferDumb
{
  MetaDrmBuffer parent;

  uint32_t fb_id;
};

G_DEFINE_TYPE (MetaDrmBufferDumb, meta_drm_buffer_dumb, META_TYPE_DRM_BUFFER)

MetaDrmBufferDumb *
meta_drm_buffer_dumb_new (uint32_t dumb_fb_id)
{
  MetaDrmBufferDumb *buffer_dumb;

  buffer_dumb = g_object_new (META_TYPE_DRM_BUFFER_DUMB, NULL);
  buffer_dumb->fb_id = dumb_fb_id;

  return buffer_dumb;
}

static uint32_t
meta_drm_buffer_dumb_get_fb_id (MetaDrmBuffer *buffer)
{
  return META_DRM_BUFFER_DUMB (buffer)->fb_id;
}

static void
meta_drm_buffer_dumb_init (MetaDrmBufferDumb *buffer_dumb)
{
}

static void
meta_drm_buffer_dumb_class_init (MetaDrmBufferDumbClass *klass)
{
  MetaDrmBufferClass *buffer_class = META_DRM_BUFFER_CLASS (klass);

  buffer_class->get_fb_id = meta_drm_buffer_dumb_get_fb_id;
}
