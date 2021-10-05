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

#include "config.h"

#include "compositor/meta-plugin-manager.h"

#include <stdlib.h>
#include <string.h>

#include "clutter/x11/clutter-x11.h"
#include "compositor/compositor-private.h"
#include "compositor/meta-module.h"
#include "core/meta-close-dialog-default-private.h"
#include "core/meta-inhibit-shortcuts-dialog-default-private.h"
#include "core/window-private.h"
#include "meta/meta-x11-errors.h"
#include "meta/prefs.h"
#include "meta/workspace.h"

static GType plugin_type = G_TYPE_NONE;

struct MetaPluginManager
{
  MetaCompositor *compositor;
  MetaPlugin *plugin;
};

void
meta_plugin_manager_set_plugin_type (GType gtype)
{
  if (plugin_type != G_TYPE_NONE)
    meta_fatal ("Mutter plugin already set: %s", g_type_name (plugin_type));

  plugin_type = gtype;
}

/*
 * Loads the given plugin.
 */
void
meta_plugin_manager_load (const gchar       *plugin_name)
{
  const gchar *dpath = MUTTER_PLUGIN_DIR "/";
  gchar       *path;
  MetaModule  *module;

  if (g_path_is_absolute (plugin_name))
    path = g_strdup (plugin_name);
  else
    path = g_strconcat (dpath, plugin_name, ".so", NULL);

  module = g_object_new (META_TYPE_MODULE, "path", path, NULL);
  if (!module || !g_type_module_use (G_TYPE_MODULE (module)))
    {
      /* This is fatal under the assumption that a monitoring
       * process like gnome-session will take over and handle
       * our untimely exit.
       */
      g_printerr ("Unable to load plugin module [%s]: %s",
                  path, g_module_error());
      exit (1);
    }

  meta_plugin_manager_set_plugin_type (meta_module_get_plugin_type (module));

  g_type_module_unuse (G_TYPE_MODULE (module));
  g_free (path);
}

static void
on_confirm_display_change (MetaMonitorManager *monitors,
                           MetaPluginManager  *plugin_mgr)
{
  meta_plugin_manager_confirm_display_change (plugin_mgr);
}

MetaPluginManager *
meta_plugin_manager_new (MetaCompositor *compositor)
{
  MetaPluginManager *plugin_mgr;
  MetaPluginClass *klass;
  MetaPlugin *plugin;
  MetaMonitorManager *monitors;

  plugin_mgr = g_new0 (MetaPluginManager, 1);
  plugin_mgr->compositor = compositor;
  plugin_mgr->plugin = plugin = g_object_new (plugin_type, NULL);

  _meta_plugin_set_compositor (plugin, compositor);

  klass = META_PLUGIN_GET_CLASS (plugin);

  if (klass->start)
    klass->start (plugin);

  monitors = meta_monitor_manager_get ();
  g_signal_connect (monitors, "confirm-display-change",
                    G_CALLBACK (on_confirm_display_change), plugin_mgr);

  return plugin_mgr;
}

static void
meta_plugin_manager_kill_window_effects (MetaPluginManager *plugin_mgr,
                                         MetaWindowActor   *actor)
{
  MetaPlugin        *plugin = plugin_mgr->plugin;
  MetaPluginClass   *klass = META_PLUGIN_GET_CLASS (plugin);

  if (klass->kill_window_effects)
    klass->kill_window_effects (plugin, actor);
}

static void
meta_plugin_manager_kill_switch_workspace (MetaPluginManager *plugin_mgr)
{
  MetaPlugin        *plugin = plugin_mgr->plugin;
  MetaPluginClass   *klass = META_PLUGIN_GET_CLASS (plugin);

  if (klass->kill_switch_workspace)
    klass->kill_switch_workspace (plugin);
}

/*
 * Public method that the compositor hooks into for events that require
 * no additional parameters.
 *
 * Returns TRUE if the plugin handled the event type (i.e.,
 * if the return value is FALSE, there will be no subsequent call to the
 * manager completed() callback, and the compositor must ensure that any
 * appropriate post-effect cleanup is carried out.
 */
gboolean
meta_plugin_manager_event_simple (MetaPluginManager *plugin_mgr,
                                  MetaWindowActor   *actor,
                                  MetaPluginEffect   event)
{
  MetaPlugin *plugin = plugin_mgr->plugin;
  MetaPluginClass *klass = META_PLUGIN_GET_CLASS (plugin);
  MetaDisplay *display = meta_compositor_get_display (plugin_mgr->compositor);
  gboolean retval = FALSE;

  if (display->display_opening)
    return FALSE;

  switch (event)
    {
    case META_PLUGIN_MINIMIZE:
      if (klass->minimize)
        {
          retval = TRUE;
          meta_plugin_manager_kill_window_effects (plugin_mgr,
                                                   actor);
          klass->minimize (plugin, actor);
        }
      break;
    case META_PLUGIN_UNMINIMIZE:
      if (klass->unminimize)
        {
          retval = TRUE;
          meta_plugin_manager_kill_window_effects (plugin_mgr,
                                                   actor);
          klass->unminimize (plugin, actor);
        }
      break;
    case META_PLUGIN_MAP:
      if (klass->map)
        {
          retval = TRUE;
          meta_plugin_manager_kill_window_effects (plugin_mgr,
                                                   actor);
          klass->map (plugin, actor);
        }
      break;
    case META_PLUGIN_DESTROY:
      if (klass->destroy)
        {
          retval = TRUE;
          meta_plugin_manager_kill_window_effects (plugin_mgr,
                                                   actor);
          klass->destroy (plugin, actor);
        }
      break;
    default:
      g_warning ("Incorrect handler called for event %d", event);
    }

  return retval;
}

void
meta_plugin_manager_event_size_changed (MetaPluginManager *plugin_mgr,
                                        MetaWindowActor   *actor)
{
  MetaPlugin *plugin = plugin_mgr->plugin;
  MetaPluginClass *klass = META_PLUGIN_GET_CLASS (plugin);

  if (klass->size_changed)
    klass->size_changed (plugin, actor);
}

gboolean
meta_plugin_manager_event_size_change (MetaPluginManager *plugin_mgr,
                                       MetaWindowActor   *actor,
                                       MetaSizeChange     which_change,
                                       MetaRectangle     *old_frame_rect,
                                       MetaRectangle     *old_buffer_rect)
{
  MetaPlugin *plugin = plugin_mgr->plugin;
  MetaPluginClass *klass = META_PLUGIN_GET_CLASS (plugin);
  MetaDisplay *display = meta_compositor_get_display (plugin_mgr->compositor);

  if (display->display_opening)
    return FALSE;

  if (!klass->size_change)
    return FALSE;

  meta_plugin_manager_kill_window_effects (plugin_mgr, actor);
  klass->size_change (plugin, actor, which_change, old_frame_rect, old_buffer_rect);
  return TRUE;
}

/*
 * The public method that the compositor hooks into for desktop switching.
 *
 * Returns TRUE if the plugin handled the event type (i.e.,
 * if the return value is FALSE, there will be no subsequent call to the
 * manager completed() callback, and the compositor must ensure that any
 * appropriate post-effect cleanup is carried out.
 */
gboolean
meta_plugin_manager_switch_workspace (MetaPluginManager   *plugin_mgr,
                                      gint                 from,
                                      gint                 to,
                                      MetaMotionDirection  direction)
{
  MetaPlugin *plugin = plugin_mgr->plugin;
  MetaPluginClass *klass = META_PLUGIN_GET_CLASS (plugin);
  MetaDisplay *display = meta_compositor_get_display (plugin_mgr->compositor);
  gboolean retval = FALSE;

  if (display->display_opening)
    return FALSE;

  if (klass->switch_workspace)
    {
      retval = TRUE;
      meta_plugin_manager_kill_switch_workspace (plugin_mgr);
      klass->switch_workspace (plugin, from, to, direction);
    }

  return retval;
}

gboolean
meta_plugin_manager_filter_keybinding (MetaPluginManager *plugin_mgr,
                                       MetaKeyBinding    *binding)
{
  MetaPlugin *plugin = plugin_mgr->plugin;
  MetaPluginClass *klass = META_PLUGIN_GET_CLASS (plugin);

  if (klass->keybinding_filter)
    return klass->keybinding_filter (plugin, binding);

  return FALSE;
}

gboolean
meta_plugin_manager_xevent_filter (MetaPluginManager *plugin_mgr,
                                   XEvent            *xev)
{
  MetaPlugin *plugin = plugin_mgr->plugin;

  return _meta_plugin_xevent_filter (plugin, xev);
}

void
meta_plugin_manager_confirm_display_change (MetaPluginManager *plugin_mgr)
{
  MetaPlugin *plugin = plugin_mgr->plugin;
  MetaPluginClass *klass = META_PLUGIN_GET_CLASS (plugin);

  if (klass->confirm_display_change)
    klass->confirm_display_change (plugin);
  else
    meta_plugin_complete_display_change (plugin, TRUE);
}

gboolean
meta_plugin_manager_show_tile_preview (MetaPluginManager *plugin_mgr,
                                       MetaWindow        *window,
                                       MetaRectangle     *tile_rect,
                                       int                tile_monitor_number)
{
  MetaPlugin *plugin = plugin_mgr->plugin;
  MetaPluginClass *klass = META_PLUGIN_GET_CLASS (plugin);
  MetaDisplay *display = meta_compositor_get_display (plugin_mgr->compositor);

  if (display->display_opening)
    return FALSE;

  if (klass->show_tile_preview)
    {
      klass->show_tile_preview (plugin, window, tile_rect, tile_monitor_number);
      return TRUE;
    }

  return FALSE;
}

gboolean
meta_plugin_manager_hide_tile_preview (MetaPluginManager *plugin_mgr)
{
  MetaPlugin *plugin = plugin_mgr->plugin;
  MetaPluginClass *klass = META_PLUGIN_GET_CLASS (plugin);
  MetaDisplay *display = meta_compositor_get_display (plugin_mgr->compositor);

  if (display->display_opening)
    return FALSE;

  if (klass->hide_tile_preview)
    {
      klass->hide_tile_preview (plugin);
      return TRUE;
    }

  return FALSE;
}

void
meta_plugin_manager_show_window_menu (MetaPluginManager  *plugin_mgr,
                                      MetaWindow         *window,
                                      MetaWindowMenuType  menu,
                                      int                 x,
                                      int                 y)
{
  MetaPlugin *plugin = plugin_mgr->plugin;
  MetaPluginClass *klass = META_PLUGIN_GET_CLASS (plugin);
  MetaDisplay *display = meta_compositor_get_display (plugin_mgr->compositor);

  if (display->display_opening)
    return;

  if (klass->show_window_menu)
    klass->show_window_menu (plugin, window, menu, x, y);
}

void
meta_plugin_manager_show_window_menu_for_rect (MetaPluginManager  *plugin_mgr,
                                               MetaWindow         *window,
                                               MetaWindowMenuType  menu,
					       MetaRectangle      *rect)
{
  MetaPlugin *plugin = plugin_mgr->plugin;
  MetaPluginClass *klass = META_PLUGIN_GET_CLASS (plugin);
  MetaDisplay *display = meta_compositor_get_display (plugin_mgr->compositor);

  if (display->display_opening)
    return;

  if (klass->show_window_menu_for_rect)
    klass->show_window_menu_for_rect (plugin, window, menu, rect);
}

MetaCloseDialog *
meta_plugin_manager_create_close_dialog (MetaPluginManager *plugin_mgr,
                                         MetaWindow        *window)
{
  MetaPlugin *plugin = plugin_mgr->plugin;
  MetaPluginClass *klass = META_PLUGIN_GET_CLASS (plugin);

  if (klass->create_close_dialog)
    return klass->create_close_dialog (plugin, window);

  return meta_close_dialog_default_new (window);
}

MetaInhibitShortcutsDialog *
meta_plugin_manager_create_inhibit_shortcuts_dialog (MetaPluginManager *plugin_mgr,
                                                     MetaWindow        *window)
{
  MetaPlugin *plugin = plugin_mgr->plugin;
  MetaPluginClass *klass = META_PLUGIN_GET_CLASS (plugin);

  if (klass->create_inhibit_shortcuts_dialog)
    return klass->create_inhibit_shortcuts_dialog (plugin, window);

  return meta_inhibit_shortcuts_dialog_default_new (window);
}

void
meta_plugin_manager_locate_pointer (MetaPluginManager *plugin_mgr)
{
  MetaPlugin *plugin = plugin_mgr->plugin;
  MetaPluginClass *klass = META_PLUGIN_GET_CLASS (plugin);

  if (klass->locate_pointer)
    klass->locate_pointer (plugin);
}
