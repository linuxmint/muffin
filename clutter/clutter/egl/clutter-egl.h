/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *
 * Copyright (C) 2006 OpenedHand
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 */

/**
 * SECTION:clutter-egl
 * @short_description: EGL specific API
 *
 * The EGL backend for Clutter provides some EGL specific API
 *
 * You need to include `clutter-egl.h` to have access to the functions documented here.
 */

#ifndef __CLUTTER_EGL_H__
#define __CLUTTER_EGL_H__

#include <glib.h>

#include "clutter-egl-headers.h"
#include <clutter/clutter.h>

G_BEGIN_DECLS

/**
 * clutter_egl_get_egl_display:
 *
 * Retrieves the  #EGLDisplay used by Clutter.
 *
 * Return value: the EGL display
 *
 * Since: 1.6
 */
CLUTTER_EXPORT
EGLDisplay      clutter_egl_get_egl_display     (void);

G_END_DECLS

#endif /* __CLUTTER_EGL_H__ */
