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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef META_PLUGIN_H_
#define META_PLUGIN_H_

#include <X11/extensions/Xfixes.h>
#include <gmodule.h>

#include "clutter/clutter.h"
#include "meta/compositor-mutter.h"
#include "meta/compositor.h"
#include "meta/meta-close-dialog.h"
#include "meta/meta-inhibit-shortcuts-dialog.h"
#include "meta/meta-version.h"
#include "meta/types.h"

#define META_TYPE_PLUGIN (meta_plugin_get_type ())

META_EXPORT
G_DECLARE_DERIVABLE_TYPE (MetaPlugin, meta_plugin, META, PLUGIN, GObject)

typedef struct _MetaPluginVersion MetaPluginVersion;
typedef struct _MetaPluginInfo    MetaPluginInfo;

/**
 * MetaPluginClass:
 * @start: virtual function called when the compositor starts managing a screen
 * @minimize: virtual function called when a window is minimized
 * @size_change: virtual function called when a window changes size to/from constraints
 * @map: virtual function called when a window is mapped
 * @destroy: virtual function called when a window is destroyed
 * @switch_workspace: virtual function called when the user switches to another
 * workspace
 * @kill_window_effects: virtual function called when the effects on a window
 * need to be killed prematurely; the plugin must call the completed() callback
 * as if the effect terminated naturally
 * @kill_switch_workspace: virtual function called when the workspace-switching
 * effect needs to be killed prematurely
 * @xevent_filter: virtual function called when handling each event
 * @keybinding_filter: virtual function called when handling each keybinding
 * @plugin_info: virtual function that returns information about the
 * #MetaPlugin
 */
struct _MetaPluginClass
{
  /*< private >*/
  GObjectClass parent_class;

  /*< public >*/

  /**
   * MetaPluginClass::start:
   *
   * Virtual function called when the compositor starts managing a screen
   */
  void (*start)            (MetaPlugin         *plugin);

  /**
   * MetaPluginClass::minimize:
   * @actor: a #MetaWindowActor
   *
   * Virtual function called when the window represented by @actor is minimized.
   */
  void (*minimize)         (MetaPlugin         *plugin,
                            MetaWindowActor    *actor);

  /**
   * MetaPluginClass::unminimize:
   * @actor: a #MetaWindowActor
   *
   * Virtual function called when the window represented by @actor is unminimized.
   */
  void (*unminimize)       (MetaPlugin         *plugin,
                            MetaWindowActor    *actor);

  void (*size_changed)     (MetaPlugin         *plugin,
                            MetaWindowActor    *actor);

  void (*size_change)      (MetaPlugin         *plugin,
                            MetaWindowActor    *actor,
                            MetaSizeChange      which_change,
                            MetaRectangle      *old_frame_rect,
                            MetaRectangle      *old_buffer_rect);

  /**
   * MetaPluginClass::map:
   * @actor: a #MetaWindowActor
   *
   * Virtual function called when the window represented by @actor is mapped.
   */
  void (*map)              (MetaPlugin         *plugin,
                            MetaWindowActor    *actor);

  /**
   * MetaPluginClass::destroy:
   * @actor: a #MetaWindowActor
   *
   * Virtual function called when the window represented by @actor is destroyed.
   */
  void (*destroy)          (MetaPlugin         *plugin,
                            MetaWindowActor    *actor);

  /**
   * MetaPluginClass::switch_workspace:
   * @from: origin workspace
   * @to: destination workspace
   * @direction: a #MetaMotionDirection
   *
   * Virtual function called when the window represented by @actor is destroyed.
   */
  void (*switch_workspace) (MetaPlugin         *plugin,
                            gint                from,
                            gint                to,
                            MetaMotionDirection direction);

  void (*show_tile_preview) (MetaPlugin      *plugin,
                             MetaWindow      *window,
                             MetaRectangle   *tile_rect,
                             int              tile_monitor_number);
  void (*hide_tile_preview) (MetaPlugin      *plugin);

  void (*show_window_menu)  (MetaPlugin         *plugin,
                             MetaWindow         *window,
                             MetaWindowMenuType  menu,
                             int                 x,
                             int                 y);

  void (*show_window_menu_for_rect)  (MetaPlugin         *plugin,
		                      MetaWindow         *window,
				      MetaWindowMenuType  menu,
				      MetaRectangle      *rect);

  /**
   * MetaPluginClass::kill_window_effects:
   * @actor: a #MetaWindowActor
   *
   * Virtual function called when the effects on @actor need to be killed
   * prematurely; the plugin must call the completed() callback as if the effect
   * terminated naturally.
   */
  void (*kill_window_effects)      (MetaPlugin      *plugin,
                                    MetaWindowActor *actor);

  /**
   * MetaPluginClass::kill_switch_workspace:
   *
   * Virtual function called when the workspace-switching effect needs to be
   * killed prematurely.
   */
  void (*kill_switch_workspace)    (MetaPlugin     *plugin);

  /**
   * MetaPluginClass::xevent_filter:
   * @event: (type xlib.XEvent):
   *
   * Virtual function called when handling each event.
   *
   * Returns: %TRUE if the plugin handled the event type (i.e., if the return
   * value is %FALSE, there will be no subsequent call to the manager
   * completed() callback, and the compositor must ensure that any appropriate
   * post-effect cleanup is carried out.
   */
  gboolean (*xevent_filter) (MetaPlugin       *plugin,
                             XEvent           *event);

  /**
   * MetaPluginClass::keybinding_filter:
   * @binding: a #MetaKeyBinding
   *
   * Virtual function called when handling each keybinding.
   *
   * Returns: %TRUE if the plugin handled the keybinding.
   */
  gboolean (*keybinding_filter) (MetaPlugin     *plugin,
                                 MetaKeyBinding *binding);

  /**
   * MetaPluginClass::confirm_display_config:
   * @plugin: a #MetaPlugin
   *
   * Virtual function called when the display configuration changes.
   * The common way to implement this function is to show some form
   * of modal dialog that should ask the user if everything was ok.
   *
   * When confirmed by the user, the plugin must call meta_plugin_complete_display_change()
   * to make the configuration permanent. If that function is not
   * called within the timeout, the previous configuration will be
   * reapplied.
   */
  void (*confirm_display_change) (MetaPlugin *plugin);

  /**
   * MetaPluginClass::plugin_info:
   * @plugin: a #MetaPlugin
   *
   * Virtual function that returns information about the #MetaPlugin.
   *
   * Returns: a #MetaPluginInfo.
   */
  const MetaPluginInfo * (*plugin_info) (MetaPlugin *plugin);

  /**
   * MetaPluginClass::create_close_dialog:
   * @plugin: a #MetaPlugin
   * @window: a #MetaWindow
   *
   * Virtual function called to create a "force quit" dialog
   * on non-responsive clients.
   */
  MetaCloseDialog * (* create_close_dialog) (MetaPlugin *plugin,
                                             MetaWindow *window);

  /**
   * MetaPluginClass::create_inhibit_shortcuts_dialog:
   * @plugin: a #MetaPlugin
   * @window: a #MetaWindow
   *
   * Virtual function called to create a "inhibit shortcuts" dialog
   * when a client requests compositor shortcuts to be inhibited.
   */
  MetaInhibitShortcutsDialog * (* create_inhibit_shortcuts_dialog) (MetaPlugin *plugin,
                                                                    MetaWindow *window);

  /**
   * MetaPluginClass::locate_pointer:
   *
   * Virtual function called when the user triggered the "locate-pointer"
   * mechanism.
   * The common way to implement this function is to show some animation
   * on screen to draw user attention on the pointer location.
   */
  void (*locate_pointer) (MetaPlugin      *plugin);
};

/**
 * MetaPluginInfo:
 * @name: name of the plugin
 * @version: version of the plugin
 * @author: author of the plugin
 * @license: license of the plugin
 * @description: description of the plugin
 */
struct _MetaPluginInfo
{
  const gchar *name;
  const gchar *version;
  const gchar *author;
  const gchar *license;
  const gchar *description;
};

META_EXPORT
const MetaPluginInfo * meta_plugin_get_info (MetaPlugin *plugin);

/**
 * MetaPluginVersion:
 * @version_major: major component of the version number of Meta with which the plugin was compiled
 * @version_minor: minor component of the version number of Meta with which the plugin was compiled
 * @version_micro: micro component of the version number of Meta with which the plugin was compiled
 * @version_api: version of the plugin API
 */
struct _MetaPluginVersion
{
  /*
   * Version information; the first three numbers match the Meta version
   * with which the plugin was compiled (see clutter-plugins/simple.c for sample
   * code).
   */
  guint version_major;
  guint version_minor;
  guint version_micro;

  /*
   * Version of the plugin API; this is unrelated to the matacity version
   * per se. The API version is checked by the plugin manager and must match
   * the one used by it (see clutter-plugins/default.c for sample code).
   */
  guint version_api;
};

/*
 * Convenience macro to set up the plugin type. Based on GEdit.
 */
#define META_PLUGIN_DECLARE_WITH_CODE(ObjectName, object_name, CODE)    \
  G_MODULE_EXPORT MetaPluginVersion meta_plugin_version =               \
    {                                                                   \
      META_MAJOR_VERSION,                                               \
      META_MINOR_VERSION,                                               \
      META_MICRO_VERSION,                                               \
      META_PLUGIN_API_VERSION                                           \
    };                                                                  \
                                                                        \
  /* Prototypes */                                                      \
  G_MODULE_EXPORT GType                                                 \
  object_name##_get_type (void);                                        \
                                                                        \
  G_MODULE_EXPORT GType                                                 \
  meta_plugin_register_type (GTypeModule *type_module);                 \
                                                                        \
                                                                        \
  G_DEFINE_DYNAMIC_TYPE_EXTENDED(ObjectName, object_name,               \
                                 META_TYPE_PLUGIN, 0, CODE)             \
                                                                        \
  /* Unused, but required by G_DEFINE_DYNAMIC_TYPE */                   \
  static void                                                           \
  object_name##_class_finalize (ObjectName##Class *klass) {}            \
                                                                        \
  G_MODULE_EXPORT GType                                                 \
  meta_plugin_register_type (GTypeModule *type_module)                  \
  {                                                                     \
    object_name##_register_type (type_module);                          \
    return object_name##_get_type ();                                   \
  }                                                                     \

#define META_PLUGIN_DECLARE(ObjectName, object_name)                    \
  META_PLUGIN_DECLARE_WITH_CODE(ObjectName, object_name, {})

META_EXPORT
void
meta_plugin_switch_workspace_completed (MetaPlugin *plugin);

META_EXPORT
void
meta_plugin_minimize_completed (MetaPlugin      *plugin,
                                MetaWindowActor *actor);

META_EXPORT
void
meta_plugin_unminimize_completed (MetaPlugin      *plugin,
                                  MetaWindowActor *actor);

META_EXPORT
void
meta_plugin_size_change_completed (MetaPlugin      *plugin,
                                   MetaWindowActor *actor);

META_EXPORT
void
meta_plugin_map_completed (MetaPlugin      *plugin,
                           MetaWindowActor *actor);

META_EXPORT
void
meta_plugin_destroy_completed (MetaPlugin      *plugin,
                               MetaWindowActor *actor);

META_EXPORT
void
meta_plugin_complete_display_change (MetaPlugin *plugin,
                                     gboolean    ok);

/**
 * MetaModalOptions:
 * @META_MODAL_POINTER_ALREADY_GRABBED: if set the pointer is already
 *   grabbed by the plugin and should not be grabbed again.
 * @META_MODAL_KEYBOARD_ALREADY_GRABBED: if set the keyboard is already
 *   grabbed by the plugin and should not be grabbed again.
 *
 * Options that can be provided when calling meta_plugin_begin_modal().
 */
typedef enum
{
  META_MODAL_POINTER_ALREADY_GRABBED = 1 << 0,
  META_MODAL_KEYBOARD_ALREADY_GRABBED = 1 << 1
} MetaModalOptions;

META_EXPORT
gboolean
meta_plugin_begin_modal (MetaPlugin      *plugin,
                         MetaModalOptions options,
                         guint32          timestamp);

META_EXPORT
void
meta_plugin_end_modal (MetaPlugin *plugin,
                       guint32     timestamp);

META_EXPORT
MetaDisplay *meta_plugin_get_display (MetaPlugin *plugin);

void _meta_plugin_set_compositor (MetaPlugin *plugin, MetaCompositor *compositor);

/* XXX: Putting this in here so it's in the public header. */
META_EXPORT
void     meta_plugin_manager_set_plugin_type (GType gtype);

#endif /* META_PLUGIN_H_ */
