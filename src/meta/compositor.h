/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2008 Iain Holmes
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

#ifndef META_COMPOSITOR_H
#define META_COMPOSITOR_H

#include <glib.h>

#include <meta/types.h>
#include <meta/boxes.h>
#include <meta/window.h>
#include <meta/workspace.h>

#define META_TYPE_COMPOSITOR (meta_compositor_get_type ())
META_EXPORT
G_DECLARE_DERIVABLE_TYPE (MetaCompositor, meta_compositor,
                          META, COMPOSITOR, GObject)

/**
 * MetaCompEffect:
 * @META_COMP_EFFECT_CREATE: The window is newly created
 *   (also used for a window that was previously on a different
 *   workspace and is changed to become visible on the active
 *   workspace.)
 * @META_COMP_EFFECT_UNMINIMIZE: The window should be shown
 *   as unminimizing from its icon geometry.
 * @META_COMP_EFFECT_DESTROY: The window is being destroyed
 * @META_COMP_EFFECT_MINIMIZE: The window should be shown
 *   as minimizing to its icon geometry.
 * @META_COMP_EFFECT_NONE: No effect, the window should be
 *   shown or hidden immediately.
 *
 * Indicates the appropriate effect to show the user for
 * meta_compositor_show_window() and meta_compositor_hide_window()
 */
typedef enum
{
  META_COMP_EFFECT_CREATE,
  META_COMP_EFFECT_UNMINIMIZE,
  META_COMP_EFFECT_DESTROY,
  META_COMP_EFFECT_MINIMIZE,
  META_COMP_EFFECT_NONE
} MetaCompEffect;

typedef enum
{
  META_SIZE_CHANGE_MAXIMIZE,
  META_SIZE_CHANGE_UNMAXIMIZE,
  META_SIZE_CHANGE_FULLSCREEN,
  META_SIZE_CHANGE_UNFULLSCREEN,
} MetaSizeChange;

META_EXPORT
void            meta_compositor_destroy (MetaCompositor *compositor);

META_EXPORT
void meta_compositor_manage   (MetaCompositor *compositor);

META_EXPORT
void meta_compositor_unmanage (MetaCompositor *compositor);

META_EXPORT
void meta_compositor_window_shape_changed (MetaCompositor *compositor,
                                           MetaWindow     *window);

META_EXPORT
void meta_compositor_window_opacity_changed (MetaCompositor *compositor,
                                             MetaWindow     *window);

META_EXPORT
gboolean meta_compositor_filter_keybinding (MetaCompositor *compositor,
                                            MetaKeyBinding *binding);

META_EXPORT
void meta_compositor_add_window        (MetaCompositor      *compositor,
                                        MetaWindow          *window);

META_EXPORT
void meta_compositor_remove_window     (MetaCompositor      *compositor,
                                        MetaWindow          *window);

META_EXPORT
void meta_compositor_show_window       (MetaCompositor      *compositor,
                                        MetaWindow          *window,
                                        MetaCompEffect       effect);

META_EXPORT
void meta_compositor_hide_window       (MetaCompositor      *compositor,
                                        MetaWindow          *window,
                                        MetaCompEffect       effect);

META_EXPORT
void meta_compositor_switch_workspace  (MetaCompositor      *compositor,
                                        MetaWorkspace       *from,
                                        MetaWorkspace       *to,
                                        MetaMotionDirection  direction);

META_EXPORT
void meta_compositor_size_change_window (MetaCompositor      *compositor,
                                         MetaWindow          *window,
                                         MetaSizeChange       which_change,
                                         MetaRectangle       *old_frame_rect,
                                         MetaRectangle       *old_buffer_rect);

META_EXPORT
void meta_compositor_sync_window_geometry (MetaCompositor *compositor,
                                           MetaWindow     *window,
                                           gboolean        did_placement);

META_EXPORT
void meta_compositor_sync_updates_frozen  (MetaCompositor *compositor,
                                           MetaWindow     *window);

META_EXPORT
void meta_compositor_queue_frame_drawn    (MetaCompositor *compositor,
                                           MetaWindow     *window,
                                           gboolean        no_delay_frame);

META_EXPORT
void meta_compositor_sync_stack                (MetaCompositor *compositor,
                                                GList          *stack);

META_EXPORT
void meta_compositor_flash_display             (MetaCompositor *compositor,
                                                MetaDisplay    *display);

META_EXPORT
void meta_compositor_show_tile_preview (MetaCompositor *compositor,
                                        MetaWindow     *window,
                                        MetaRectangle  *tile_rect,
                                        int             tile_monitor_number);

META_EXPORT
void meta_compositor_hide_tile_preview (MetaCompositor *compositor);

META_EXPORT
void meta_compositor_show_window_menu (MetaCompositor     *compositor,
                                       MetaWindow         *window,
				       MetaWindowMenuType  menu,
                                       int                 x,
                                       int                 y);

META_EXPORT
void meta_compositor_show_window_menu_for_rect (MetaCompositor     *compositor,
                                                MetaWindow         *window,
				                MetaWindowMenuType  menu,
                                                MetaRectangle      *rect);

#endif /* META_COMPOSITOR_H */
