/*
 * Wayland Support
 *
 * Copyright (C) 2012,2013 Intel Corporation
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

#ifndef META_WAYLAND_VERSIONS_H
#define META_WAYLAND_VERSIONS_H

/* Protocol objects, will never change version */
/* #define META_WL_DISPLAY_VERSION  1 */
/* #define META_WL_REGISTRY_VERSION 1 */
#define META_WL_CALLBACK_VERSION 1

/* Not handled by mutter-wayland directly */
/* #define META_WL_SHM_VERSION        1 */
/* #define META_WL_SHM_POOL_VERSION   1 */
/* #define META_WL_DRM_VERSION        1 */
/* #define META_WL_BUFFER_VERSION     1 */

/* Global/master objects (version exported by wl_registry and negotiated through bind) */
#define META_WL_COMPOSITOR_VERSION          4
#define META_WL_DATA_DEVICE_MANAGER_VERSION 3
#define META_XDG_WM_BASE_VERSION            3
#define META_ZXDG_SHELL_V6_VERSION          1
#define META_WL_SHELL_VERSION               1
#define META_WL_SEAT_VERSION                5
#define META_WL_OUTPUT_VERSION              2
#define META_XSERVER_VERSION                1
#define META_GTK_SHELL1_VERSION             3
#define META_WL_SUBCOMPOSITOR_VERSION       1
#define META_ZWP_POINTER_GESTURES_V1_VERSION    1
#define META_ZXDG_EXPORTER_V1_VERSION       1
#define META_ZXDG_IMPORTER_V1_VERSION       1
#define META_ZWP_LINUX_DMABUF_V1_VERSION    3
#define META_ZWP_KEYBOARD_SHORTCUTS_INHIBIT_V1_VERSION 1
#define META_ZXDG_OUTPUT_V1_VERSION         3
#define META_ZWP_XWAYLAND_KEYBOARD_GRAB_V1_VERSION 1
#define META_GTK_TEXT_INPUT_VERSION         1
#define META_ZWP_TEXT_INPUT_V3_VERSION      1
#define META_WP_VIEWPORTER_VERSION          1

#endif
