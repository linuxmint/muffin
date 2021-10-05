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

#ifndef META_STAGE_X11_H
#define META_STAGE_X11_H

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "backends/meta-backend-private.h"
#include "clutter/clutter-mutter.h"
#include "clutter/x11/clutter-x11.h"

G_BEGIN_DECLS

#define META_TYPE_STAGE_X11                  (meta_stage_x11_get_type ())
#define META_STAGE_X11(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_STAGE_X11, MetaStageX11))
#define META_IS_STAGE_X11(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_STAGE_X11))
#define META_STAGE_X11_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), META_TYPE_STAGE_X11, MetaStageX11Class))
#define META_IS_STAGE_X11_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), META_TYPE_STAGE_X11))
#define META_STAGE_X11_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), META_TYPE_STAGE_X11, MetaStageX11Class))

typedef struct _MetaStageX11         MetaStageX11;
typedef struct _MetaStageX11Class    MetaStageX11Class;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaStageX11, g_object_unref)

typedef enum
{
  STAGE_X11_WITHDRAWN = 1 << 1
} MetaStageX11State;

struct _MetaStageX11
{
  ClutterStageCogl parent_instance;

  MetaBackend *backend;

  CoglOnscreen *onscreen;
  Window xwin;
  gint xwin_width;
  gint xwin_height; /* FIXME target_width / height */

  CoglFrameClosure *frame_closure;

  gchar *title;

  guint clipped_redraws_cool_off;

  MetaStageX11State wm_state;

  guint is_cursor_visible     : 1;
  guint viewport_initialized  : 1;
  guint accept_focus          : 1;
};

struct _MetaStageX11Class
{
  ClutterStageCoglClass parent_class;
};

CLUTTER_EXPORT
GType meta_stage_x11_get_type (void) G_GNUC_CONST;

/* Private to subclasses */
void            meta_stage_x11_set_user_time                (MetaStageX11 *stage_x11,
							     guint32       user_time);

gboolean meta_stage_x11_translate_event (MetaStageX11 *stage_x11,
					 XEvent       *xevent,
					 ClutterEvent *event);

ClutterStage *meta_x11_get_stage_from_window (Window win);

Window       meta_x11_get_stage_window  (ClutterStage *stage);


G_END_DECLS

#endif /* META_STAGE_H */
