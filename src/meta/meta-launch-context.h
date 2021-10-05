/*
 * Copyright (C) 2018 Red Hat
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */
#ifndef META_LAUNCH_CONTEXT_H
#define META_LAUNCH_CONTEXT_H

#include <meta/workspace.h>

META_EXPORT
G_DECLARE_FINAL_TYPE (MetaLaunchContext, meta_launch_context, META, LAUNCH_CONTEXT, GAppLaunchContext)

#define META_TYPE_LAUNCH_CONTEXT (meta_launch_context_get_type ())

META_EXPORT
void meta_launch_context_set_timestamp (MetaLaunchContext *context,
                                        uint32_t           timestamp);

META_EXPORT
void meta_launch_context_set_workspace (MetaLaunchContext *context,
                                        MetaWorkspace     *workspace);

#endif /* META_LAUNCH_CONTEXT_H */
