/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2004, 2005 Elijah Newren
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

#ifndef META_WORKSPACE_H
#define META_WORKSPACE_H

#include <meta/types.h>
#include <meta/boxes.h>

#define META_TYPE_WORKSPACE            (meta_workspace_get_type ())
#define META_WORKSPACE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_WORKSPACE, MetaWorkspace))
#define META_WORKSPACE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_WORKSPACE, MetaWorkspaceClass))
#define META_IS_WORKSPACE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_WORKSPACE))
#define META_IS_WORKSPACE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_WORKSPACE))
#define META_WORKSPACE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_WORKSPACE, MetaWorkspaceClass))

typedef struct _MetaWorkspaceClass   MetaWorkspaceClass;

META_EXPORT
GType meta_workspace_get_type (void);

META_EXPORT
int  meta_workspace_index (MetaWorkspace *workspace);

META_EXPORT
MetaDisplay *meta_workspace_get_display (MetaWorkspace *workspace);

META_EXPORT
GList* meta_workspace_list_windows (MetaWorkspace *workspace);

META_EXPORT
void meta_workspace_get_work_area_for_monitor (MetaWorkspace *workspace,
                                               int            which_monitor,
                                               MetaRectangle *area);

META_EXPORT
void meta_workspace_get_work_area_all_monitors (MetaWorkspace *workspace,
                                                MetaRectangle *area);

META_EXPORT
void meta_workspace_activate (MetaWorkspace *workspace, guint32 timestamp);

META_EXPORT
void meta_workspace_activate_with_focus (MetaWorkspace *workspace,
                                         MetaWindow    *focus_this,
                                         guint32        timestamp);

META_EXPORT
void meta_workspace_set_builtin_struts (MetaWorkspace *workspace,
                                        GSList        *struts);

META_EXPORT
MetaWorkspace* meta_workspace_get_neighbor (MetaWorkspace      *workspace,
                                            MetaMotionDirection direction);

#endif
