/* Clutter.
 * An OpenGL based 'interactive canvas' library.
 * Authored By Matthew Allum  <mallum@openedhand.com>
 * Copyright (C) 2006-2007 OpenedHand
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

#ifndef __CLUTTER_BACKEND_X11_H__
#define __CLUTTER_BACKEND_X11_H__

#include <glib-object.h>
#include <clutter/clutter-event.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "clutter-x11.h"

#include "clutter-backend-private.h"

#include "xsettings/xsettings-client.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_BACKEND_X11                (clutter_backend_x11_get_type ())
#define CLUTTER_BACKEND_X11(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_BACKEND_X11, ClutterBackendX11))
#define CLUTTER_IS_BACKEND_X11(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_BACKEND_X11))
#define CLUTTER_BACKEND_X11_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), CLUTTER_TYPE_BACKEND_X11, ClutterBackendX11Class))
#define CLUTTER_IS_BACKEND_X11_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), CLUTTER_TYPE_BACKEND_X11))
#define CLUTTER_BACKEND_X11_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), CLUTTER_TYPE_BACKEND_X11, ClutterBackendX11Class))

typedef struct _ClutterBackendX11       ClutterBackendX11;
typedef struct _ClutterBackendX11Class  ClutterBackendX11Class;
typedef struct _ClutterX11EventFilter   ClutterX11EventFilter;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterBackendX11, g_object_unref)

struct _ClutterX11EventFilter
{
  ClutterX11FilterFunc func;
  gpointer             data;

};

struct _ClutterBackendX11
{
  ClutterBackend parent_instance;

  Display *xdpy;
  gchar   *display_name;

  Screen  *xscreen;
  int      xscreen_num;
  int      xscreen_width;
  int      xscreen_height;

  Window   xwin_root;

  /* event source */
  GSList  *event_filters;

  /* props */
  Atom atom_NET_WM_PID;
  Atom atom_NET_WM_PING;
  Atom atom_NET_WM_STATE;
  Atom atom_NET_WM_USER_TIME;
  Atom atom_WM_PROTOCOLS;
  Atom atom_WM_DELETE_WINDOW;
  Atom atom_XEMBED;
  Atom atom_XEMBED_INFO;
  Atom atom_NET_WM_NAME;
  Atom atom_UTF8_STRING;

  Time last_event_time;

  XSettingsClient *xsettings;
  Window xsettings_xwin;
};

struct _ClutterBackendX11Class
{
  ClutterBackendClass parent_class;
};

CLUTTER_EXPORT
GType clutter_backend_x11_get_type (void) G_GNUC_CONST;

ClutterBackend *clutter_backend_x11_new (void);

/* Private to glx/eglx backends */
void            _clutter_x11_select_events (Window xwin);

G_END_DECLS

#endif /* __CLUTTER_BACKEND_X11_H__ */
