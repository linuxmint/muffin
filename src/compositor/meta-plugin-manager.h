/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (c) 2008 Intel Corp.
 *
 * Author: Tomas Frydrych <tf@linux.intel.com>
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
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 */

#ifndef META_PLUGIN_MANAGER_H_
#define META_PLUGIN_MANAGER_H_

#include <meta/types.h>
#include <meta/screen.h>

#define  META_PLUGIN_FROM_MANAGER_
#include <meta/meta-plugin.h>
#undef   META_PLUGIN_FROM_MANAGER_

#define META_PLUGIN_MINIMIZE         (1<<0)
#define META_PLUGIN_MAXIMIZE         (1<<1)
#define META_PLUGIN_UNMAXIMIZE       (1<<2)
#define META_PLUGIN_TILE             (1<<6)
#define META_PLUGIN_MAP              (1<<3)
#define META_PLUGIN_DESTROY          (1<<4)
#define META_PLUGIN_SWITCH_WORKSPACE (1<<5)

#define META_PLUGIN_ALL_EFFECTS      (~0)

/**
 * MetaPluginManager: (skip)
 *
 */
typedef struct MetaPluginManager MetaPluginManager;

MetaPluginManager * meta_plugin_manager_get         (MetaScreen *screen);
MetaPluginManager * meta_plugin_manager_get_default (void);

void     meta_plugin_manager_load         (MetaPluginManager *mgr,
                                           const gchar       *plugin_name);
void     meta_plugin_manager_register     (MetaPluginManager *mgr,
                                           GType              plugin_type);
void     meta_plugin_manager_initialize   (MetaPluginManager *mgr);

gboolean meta_plugin_manager_event_simple (MetaPluginManager *mgr,
                                           MetaWindowActor   *actor,
                                           unsigned long      event);

gboolean meta_plugin_manager_event_maximize    (MetaPluginManager *mgr,
                                                MetaWindowActor   *actor,
                                                unsigned long      event,
                                                gint               target_x,
                                                gint               target_y,
                                                gint               target_width,
                                                gint               target_height);
void     meta_plugin_manager_update_workspaces (MetaPluginManager *mgr);

void meta_plugin_manager_update_workspace (MetaPluginManager *mgr,
                                           MetaWorkspace     *w);

gboolean meta_plugin_manager_switch_workspace (MetaPluginManager   *mgr,
                                               gint                 from,
                                               gint                 to,
                                               MetaMotionDirection  direction);

gboolean meta_plugin_manager_xevent_filter (MetaPluginManager *mgr,
                                            XEvent            *xev);

#endif
