/*
 * Copyright (C) 2019 Red Hat
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

#ifndef META_KMS_IMPL_TYPES_H
#define META_KMS_IMPL_TYPES_H

#include <stdint.h>

typedef struct _MetaKms MetaKms;
typedef struct _MetaKmsDevice MetaKmsDevice;

typedef struct _MetaKmsPlane MetaKmsPlane;
typedef struct _MetaKmsCrtc MetaKmsCrtc;
typedef struct _MetaKmsConnector MetaKmsConnector;

typedef struct _MetaKmsUpdate MetaKmsUpdate;
typedef struct _MetaKmsPlaneAssignment MetaKmsPlaneAssignment;
typedef struct _MetaKmsModeSet MetaKmsModeSet;

typedef struct _MetaKmsFeedback MetaKmsFeedback;

typedef struct _MetaKmsPageFlipFeedback MetaKmsPageFlipFeedback;

typedef struct _MetaKmsImpl MetaKmsImpl;
typedef struct _MetaKmsImplDevice MetaKmsImplDevice;

/* 16:16 fixed point */
typedef int32_t MetaFixed16;

typedef struct _MetaFixed16Rectangle
{
  MetaFixed16 x;
  MetaFixed16 y;
  MetaFixed16 width;
  MetaFixed16 height;
} MetaFixed16Rectangle;

typedef enum _MetaKmsDeviceFlag
{
  META_KMS_DEVICE_FLAG_NONE = 0,
  META_KMS_DEVICE_FLAG_BOOT_VGA = 1 << 0,
  META_KMS_DEVICE_FLAG_PLATFORM_DEVICE = 1 << 1,
} MetaKmsDeviceFlag;

typedef enum _MetaKmsPlaneType MetaKmsPlaneType;

#endif /* META_KMS_IMPL_TYPES_H */
