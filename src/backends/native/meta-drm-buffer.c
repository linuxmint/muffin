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
 * Author: Daniel van Vugt <daniel.van.vugt@canonical.com>
 */

#include "config.h"

#include "backends/native/meta-drm-buffer.h"

G_DEFINE_ABSTRACT_TYPE (MetaDrmBuffer, meta_drm_buffer, G_TYPE_OBJECT)

uint32_t
meta_drm_buffer_get_fb_id (MetaDrmBuffer *buffer)
{
  return META_DRM_BUFFER_GET_CLASS (buffer)->get_fb_id (buffer);
}

static void
meta_drm_buffer_init (MetaDrmBuffer *buffer)
{
}

static void
meta_drm_buffer_class_init (MetaDrmBufferClass *klass)
{
}
