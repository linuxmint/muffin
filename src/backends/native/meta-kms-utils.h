/*
 * Copyright (C) 2018 DisplayLink (UK) Ltd.
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

#ifndef META_KMS_UTILS_H
#define META_KMS_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <xf86drmMode.h>

typedef struct _MetaDrmFormatBuf
{
  char s[5];
} MetaDrmFormatBuf;

float meta_calculate_drm_mode_refresh_rate (const drmModeModeInfo *drm_mode);

const char * meta_drm_format_to_string (MetaDrmFormatBuf *tmp,
                                        uint32_t          drm_format);

#endif /* META_KMS_UTILS_H */
