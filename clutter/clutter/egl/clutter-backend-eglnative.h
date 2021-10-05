/* Clutter.
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2006, 2007 OpenedHand
 * Copyright (C) 2010 Intel Corp
 *               2011 Giovanni Campagna <scampa.giovanni@gmail.com>
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
 * Authors:
 *  Matthew Allum
 *  Robert Bragg
 */

#ifndef __CLUTTER_BACKEND_EGL_NATIVE_H__
#define __CLUTTER_BACKEND_EGL_NATIVE_H__

#include <glib-object.h>
#include <gio/gio.h>
#include <cogl/cogl.h>
#include <cogl/cogl-egl.h>
#include <clutter/clutter-event.h>
#include <clutter/clutter-backend.h>

#include "clutter-backend-private.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_BACKEND_EGL_NATIVE                (clutter_backend_egl_native_get_type ())
#define CLUTTER_BACKEND_EGL_NATIVE(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_BACKEND_EGL_NATIVE, ClutterBackendEglNative))
#define CLUTTER_IS_BACKEND_EGL_NATIVE(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_BACKEND_EGL_NATIVE))
#define CLUTTER_BACKEND_EGL_NATIVE_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), CLUTTER_TYPE_BACKEND_EGL_NATIVE, ClutterBackendEglNativeClass))
#define CLUTTER_IS_BACKEND_EGL_NATIVE_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), CLUTTER_TYPE_BACKEND_EGL_NATIVE))
#define CLUTTER_BACKEND_EGL_NATIVE_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), CLUTTER_TYPE_BACKEND_EGL_NATIVE, ClutterBackendEglNativeClass))

typedef struct _ClutterBackendEglNative       ClutterBackendEglNative;
typedef struct _ClutterBackendEglNativeClass  ClutterBackendEglNativeClass;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterBackendEglNative, g_object_unref)

struct _ClutterBackendEglNative
{
  ClutterBackend parent_instance;

  /* event source */
  GSource *event_source;

  /* event timer */
  GTimer *event_timer;

  /* "xsettings" is still the defacto place for Xft settings, even in Wayland */
  GSettings *xsettings;
};

struct _ClutterBackendEglNativeClass
{
  ClutterBackendClass parent_class;
};

CLUTTER_EXPORT
GType clutter_backend_egl_native_get_type (void) G_GNUC_CONST;

ClutterBackend *clutter_backend_egl_native_new (void);

G_END_DECLS

#endif /* __CLUTTER_BACKEND_EGL_NATIVE_H__ */
