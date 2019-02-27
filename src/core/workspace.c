/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* 
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2003 Rob Adams
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 */

/**
 * SECTION:workspace
 * @title: MetaWorkspace
 * @short_description: Workspaces
 *
 * A workspace is a set of windows which all live on the same
 * screen.  (You may also see the name "desktop" around the place,
 * which is the EWMH's name for the same thing.)  Only one workspace
 * of a screen may be active at once; all windows on all other workspaces
 * are unmapped.
 */


#include <config.h>
#include "screen-private.h"
#include <meta/workspace.h>
#include "workspace-private.h"
#include "boxes-private.h"
#include <meta/errors.h>
#include <meta/prefs.h>

#include <meta/compositor.h>

#include <X11/Xatom.h>
#include <string.h>
#ifdef HAVE_LIBCANBERRA
#include <canberra-gtk.h>
#endif

enum {
  PROP_0,

  PROP_N_WINDOWS,
};

void meta_workspace_queue_calc_showing   (MetaWorkspace *workspace);
static void focus_ancestor_or_top_window (MetaWorkspace *workspace,
                                          MetaWindow    *not_this_one,
                                          guint32        timestamp);
static void free_this                    (gpointer candidate,
                                          gpointer dummy);

G_DEFINE_TYPE (MetaWorkspace, meta_workspace, G_TYPE_OBJECT);

enum
{
  WINDOW_ADDED,
  WINDOW_REMOVED,

  LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

static void
meta_workspace_finalize (GObject *object)
{
  /* Actual freeing done in meta_workspace_remove() for now */
  
  G_OBJECT_CLASS (meta_workspace_parent_class)->finalize (object);
}

static void
meta_workspace_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_workspace_get_property (GObject      *object,
                             guint         prop_id,
                             GValue       *value,
                             GParamSpec   *pspec)
{
  MetaWorkspace *ws = META_WORKSPACE (object);

  switch (prop_id)
    {
    case PROP_N_WINDOWS:
      /*
       * This is reliable, but not very efficient; should we store
       * the list lenth ?
       */
      g_value_set_uint (value, g_list_length (ws->windows));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_workspace_class_init (MetaWorkspaceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec   *pspec;

  object_class->finalize     = meta_workspace_finalize;
  object_class->get_property = meta_workspace_get_property;
  object_class->set_property = meta_workspace_set_property;

  signals[WINDOW_ADDED] = g_signal_new ("window-added",
                                        G_TYPE_FROM_CLASS (klass),
                                        G_SIGNAL_RUN_LAST,
                                        0,
                                        NULL, NULL, NULL,
                                        G_TYPE_NONE, 1,
                                        META_TYPE_WINDOW);
  signals[WINDOW_REMOVED] = g_signal_new ("window-removed",
                                          G_TYPE_FROM_CLASS (klass),
                                          G_SIGNAL_RUN_LAST,
                                          0,
                                          NULL, NULL, NULL,
                                          G_TYPE_NONE, 1,
                                          META_TYPE_WINDOW);

  pspec = g_param_spec_uint ("n-windows",
                             "N Windows",
                             "Number of windows",
                             0, G_MAXUINT, 0,
                             G_PARAM_READABLE);

  g_object_class_install_property (object_class,
                                   PROP_N_WINDOWS,
                                   pspec);
}

static void
meta_workspace_init (MetaWorkspace *workspace)
{
}

static void
maybe_add_to_list (MetaScreen *screen, MetaWindow *window, gpointer data)
{
  GList **mru_list = data;

  if (window->on_all_workspaces)
    *mru_list = g_list_prepend (*mru_list, window);
}

LOCAL_SYMBOL MetaWorkspace*
meta_workspace_new (MetaScreen *screen)
{
  MetaWorkspace *workspace;

  workspace = g_object_new (META_TYPE_WORKSPACE, NULL);

  workspace->screen = screen;
  workspace->screen->workspaces =
    g_list_append (workspace->screen->workspaces, workspace);
  workspace->windows = NULL;
  workspace->mru_list = NULL;
  meta_screen_foreach_window (screen, maybe_add_to_list, &workspace->mru_list);

  workspace->work_areas_invalid = TRUE;
  workspace->work_area_monitor = NULL;
  workspace->work_area_screen.x = 0;
  workspace->work_area_screen.y = 0;
  workspace->work_area_screen.width = 0;
  workspace->work_area_screen.height = 0;

  workspace->screen_region = NULL;
  workspace->monitor_region = NULL;
  workspace->screen_edges = NULL;
  workspace->monitor_edges = NULL;
  workspace->list_containing_self = g_list_prepend (NULL, workspace);
  workspace->snapped_windows = NULL;
  workspace->builtin_struts = NULL;
  workspace->all_struts = NULL;

  workspace->showing_desktop = FALSE;
  
  return workspace;
}

/* Foreach function for workspace_free_struts() */
static void
free_this (gpointer candidate, gpointer dummy)
{
  g_free (candidate);
}

/*
 * Frees the combined struts list of a workspace.
 *
 * \param workspace  The workspace.
 */
static void
workspace_free_all_struts (MetaWorkspace *workspace)
{
  if (workspace->all_struts == NULL)
    return;
    
  g_slist_foreach (workspace->all_struts, free_this, NULL);
  g_slist_free (workspace->all_struts);
  workspace->all_struts = NULL;
}

/*
 * Frees the struts list set with meta_workspace_set_builtin_struts
 *
 * \param workspace  The workspace.
 */
static void
workspace_free_builtin_struts (MetaWorkspace *workspace)
{
  if (workspace->builtin_struts == NULL)
    return;
    
  g_slist_foreach (workspace->builtin_struts, free_this, NULL);
  g_slist_free (workspace->builtin_struts);
  workspace->builtin_struts = NULL;
}

LOCAL_SYMBOL void
meta_workspace_remove (MetaWorkspace *workspace)
{
  GList *tmp;
  MetaScreen *screen;
  int i;

  g_return_if_fail (workspace != workspace->screen->active_workspace);

  /* Here we assume all the windows are already on another workspace
   * as well, so they won't be "orphaned"
   */
  
  tmp = workspace->windows;
  while (tmp != NULL)
    {
      GList *next;
      MetaWindow *window = tmp->data;
      next = tmp->next;

      /* pop front of list we're iterating over */
      meta_workspace_remove_window (workspace, window);
      g_assert (window->workspace != NULL);

      tmp = next;
    }

  g_assert (workspace->windows == NULL);

  screen = workspace->screen;
  
  workspace->screen->workspaces =
    g_list_remove (workspace->screen->workspaces, workspace);
  
  g_free (workspace->work_area_monitor);

  g_list_free (workspace->mru_list);
  g_list_free (workspace->list_containing_self);

  workspace_free_builtin_struts (workspace);

  /* screen.c:update_num_workspaces(), which calls us, removes windows from
   * workspaces first, which can cause the workareas on the workspace to be
   * invalidated (and hence for struts/regions/edges to be freed).
   * So, no point trying to double free it; that causes a crash
   * anyway.  #361804.
   */

  if (!workspace->work_areas_invalid)
    {
      workspace_free_all_struts (workspace);
      for (i = 0; i < screen->n_monitor_infos; i++)
        meta_rectangle_free_list_and_elements (workspace->monitor_region[i]);
      g_free (workspace->monitor_region);
      meta_rectangle_free_list_and_elements (workspace->screen_region);
      meta_rectangle_free_list_and_elements (workspace->screen_edges);
      meta_rectangle_free_list_and_elements (workspace->monitor_edges);
    }

  g_object_unref (workspace);

  /* don't bother to reset names, pagers can just ignore
   * extra ones
   */
}

static gboolean
emit_window_added (MetaWindow *window)
{
  if (window == NULL || window->workspace == NULL)
    return FALSE;

  g_signal_emit (window->workspace, signals[WINDOW_ADDED], 0, window);
  return FALSE;
}

LOCAL_SYMBOL void
meta_workspace_add_window (MetaWorkspace *workspace,
                           MetaWindow    *window)
{
  g_return_if_fail (window->workspace == NULL);
  
  /* If the window is on all workspaces, we want to add it to all mru
   * lists, otherwise just add it to this workspaces mru list
   */
  if (window->on_all_workspaces) 
    {
      if (window->workspace == NULL)
        {
          GList* tmp = window->screen->workspaces;
          while (tmp)
            {
              MetaWorkspace* work = (MetaWorkspace*) tmp->data;
              if (!g_list_find (work->mru_list, window))
                work->mru_list = g_list_prepend (work->mru_list, window);

              tmp = tmp->next;
            }
        }
    }
  else
    {
      g_assert (g_list_find (workspace->mru_list, window) == NULL);
      workspace->mru_list = g_list_prepend (workspace->mru_list, window);
    }

  workspace->windows = g_list_prepend (workspace->windows, window);

  window->workspace = workspace;

  meta_window_set_current_workspace_hint (window);
  
  if (window->struts)
    {
      meta_topic (META_DEBUG_WORKAREA,
                  "Invalidating work area of workspace %d since we're adding window %s to it\n",
                  meta_workspace_index (workspace), window->desc);
      meta_workspace_invalidate_work_area (workspace);
    }

  /* queue a move_resize since changing workspaces may change
   * the relevant struts
   */
  meta_window_queue (window, META_QUEUE_CALC_SHOWING|META_QUEUE_MOVE_RESIZE);

  clutter_threads_add_timeout (20, (GSourceFunc) emit_window_added, window);
  g_object_notify (G_OBJECT (workspace), "n-windows");
}

LOCAL_SYMBOL void
meta_workspace_remove_window (MetaWorkspace *workspace,
                              MetaWindow    *window)
{
  g_return_if_fail (window->workspace == workspace);

  workspace->windows = g_list_remove (workspace->windows, window);
  window->workspace = NULL;

  /* If the window is on all workspaces, we don't want to remove it
   * from the MRU list unless this causes it to be removed from all 
   * workspaces
   */
  if (window->on_all_workspaces) 
    {
      GList* tmp = window->screen->workspaces;
      while (tmp)
        {
          MetaWorkspace* work = (MetaWorkspace*) tmp->data;
          work->mru_list = g_list_remove (work->mru_list, window);

          tmp = tmp->next;
        }
    }
  else
    {
      workspace->mru_list = g_list_remove (workspace->mru_list, window);
      g_assert (g_list_find (workspace->mru_list, window) == NULL);
    }

  meta_window_set_current_workspace_hint (window);
  
  if (window->struts)
    {
      meta_topic (META_DEBUG_WORKAREA,
                  "Invalidating work area of workspace %d since we're removing window %s from it\n",
                  meta_workspace_index (workspace), window->desc);
      meta_workspace_invalidate_work_area (workspace);
    }

  /* queue a move_resize since changing workspaces may change
   * the relevant struts
   */
  meta_window_queue (window, META_QUEUE_CALC_SHOWING|META_QUEUE_MOVE_RESIZE);

  g_signal_emit (workspace, signals[WINDOW_REMOVED], 0, window);
  g_object_notify (G_OBJECT (workspace), "n-windows");
}

LOCAL_SYMBOL void
meta_workspace_relocate_windows (MetaWorkspace *workspace,
                                 MetaWorkspace *new_home)
{
  GList *tmp;
  GList *copy;
  
  g_return_if_fail (workspace != new_home);

  /* can't modify list we're iterating over */
  copy = g_list_copy (workspace->windows);
  
  tmp = copy;
  while (tmp != NULL)
    {
      MetaWindow *window = tmp->data;

      meta_workspace_remove_window (workspace, window);
      meta_workspace_add_window (new_home, window);
      
      tmp = tmp->next;
    }

  g_list_free (copy);
  
  g_assert (workspace->windows == NULL);
}

LOCAL_SYMBOL void
meta_workspace_queue_calc_showing  (MetaWorkspace *workspace)
{
  GList *tmp;

  tmp = workspace->windows;
  while (tmp != NULL)
    {
      meta_window_queue (tmp->data, META_QUEUE_CALC_SHOWING);

      tmp = tmp->next;
    }
}

static void
workspace_switch_sound(MetaWorkspace *from,
                       MetaWorkspace *to)
{
#ifdef HAVE_LIBCANBERRA
  MetaWorkspaceLayout layout;
  int i, nw, x, y, fi, ti;
  const char *e;

  nw = meta_screen_get_n_workspaces(from->screen);
  fi = meta_workspace_index(from);
  ti = meta_workspace_index(to);

  meta_screen_calc_workspace_layout(from->screen,
                                    nw,
                                    fi,
                                    &layout);

  for (i = 0; i < nw; i++)
    if (layout.grid[i] == ti)
      break;

  if (i >= nw)
    {
      meta_bug("Failed to find destination workspace in layout\n");
      goto finish;
    }

  y = i / layout.cols;
  x = i % layout.cols;

  /* We priorize horizontal over vertical movements here. The
     rationale for this is that horizontal movements are probably more
     interesting for sound effects because speakers are usually
     positioned on a horizontal and not a vertical axis. i.e. your
     spatial "Woosh!" effects will easily be able to encode horizontal
     movement but not such much vertical movement. */

  if (x < layout.current_col)
    e = "desktop-switch-left";
  else if (x > layout.current_col)
    e = "desktop-switch-right";
  else if (y < layout.current_row)
    e = "desktop-switch-up";
  else if (y > layout.current_row)
    e = "desktop-switch-down";
  else
    {
      meta_bug("Uh, origin and destination workspace at same logic position!\n");
      goto finish;
    }

  ca_context_play(ca_gtk_context_get(), 1,
                  CA_PROP_EVENT_ID, e,
                  CA_PROP_EVENT_DESCRIPTION, "Desktop switched",
                  CA_PROP_CANBERRA_CACHE_CONTROL, "permanent",
                  NULL);

 finish:
  meta_screen_free_workspace_layout (&layout);
#endif /* HAVE_LIBCANBERRA */
}

static MetaMotionDirection
get_wrapped_horizontal_direction (gint                from,
                                  gint                to,
                                  MetaMotionDirection suggested_dir,
                                  gint                num_workspaces)
{
  MetaMotionDirection ret = 0;
  gboolean wrap = meta_prefs_get_workspace_cycle();

  if (suggested_dir != 0 && wrap)
    {
      if (meta_ui_get_direction () == META_UI_DIRECTION_RTL)
        {
          if (suggested_dir == META_MOTION_LEFT)
            suggested_dir = META_MOTION_RIGHT;
          else
          if (suggested_dir == META_MOTION_RIGHT)
            suggested_dir = META_MOTION_LEFT;
        }

      return suggested_dir;
    }

  if (meta_ui_get_direction() == META_UI_DIRECTION_RTL)
    {
      if (from < to)
        if (wrap)
          ret = (to - from) <= ((num_workspaces - to) + from) ? META_MOTION_LEFT : META_MOTION_RIGHT;
        else
          ret = META_MOTION_LEFT;
      else if (from > to)
        if (wrap)
          ret = (from - to) <= ((num_workspaces - from) + to) ? META_MOTION_RIGHT : META_MOTION_LEFT;
        else
          ret = META_MOTION_RIGHT;
    }
  else
    {
      if (from < to)
        if (wrap)
          ret = (to - from) <= ((num_workspaces - to) + from) ? META_MOTION_RIGHT : META_MOTION_LEFT;
        else
          ret = META_MOTION_RIGHT;
      else if (from > to)
        if (wrap)
          ret = (from - to) <= ((num_workspaces - from) + to) ? META_MOTION_LEFT : META_MOTION_RIGHT;
        else
          ret = META_MOTION_LEFT;
    }

  return ret;
}

static void
meta_workspace_activate_internal (MetaWorkspace       *workspace,
                                  MetaWindow          *focus_this,
                                  MetaMotionDirection  suggested_dir,
                                  guint32              timestamp)
{
  MetaWorkspace  *old;
  MetaWindow     *move_window;
  MetaScreen     *screen;
  MetaDisplay    *display;
  MetaCompositor *comp;
  MetaWorkspaceLayout layout1, layout2;
  gint num_workspaces, current_space, new_space;
  MetaMotionDirection direction;
  
  meta_verbose ("Activating workspace %d\n",
                meta_workspace_index (workspace));
  
  if (workspace->screen->active_workspace == workspace)
    return;

  /* Free any cached pointers to the workspaces's edges from
   * a current resize or move operation */
  meta_display_cleanup_edges (workspace->screen->display);

  if (workspace->screen->active_workspace)
    workspace_switch_sound (workspace->screen->active_workspace, workspace);

  /* Note that old can be NULL; e.g. when starting up */
  old = workspace->screen->active_workspace;

  workspace->screen->active_workspace = workspace;

  meta_screen_set_active_workspace_hint (workspace->screen);

  /* If the "show desktop" mode is active for either the old workspace
   * or the new one *but not both*, then update the
   * _net_showing_desktop hint
   */
  if (old && (old->showing_desktop ^ workspace->showing_desktop))
    meta_screen_update_showing_desktop_hint (workspace->screen);

  if (old == NULL)
    return;

  move_window = NULL;
  if (workspace->screen->display->grab_op == META_GRAB_OP_MOVING ||
      workspace->screen->display->grab_op == META_GRAB_OP_KEYBOARD_MOVING)
    move_window = workspace->screen->display->grab_window;
      
  if (move_window != NULL)
    {
      if (move_window->on_all_workspaces)
        move_window = NULL; /* don't move it after all */

      /* We put the window on the new workspace, flip spaces,
       * then remove from old workspace, so the window
       * never gets unmapped and we maintain the button grab
       * on it.
       *
       * \bug  This comment appears to be the reverse of what happens
       */
      if (move_window && (move_window->workspace != workspace))
        {
          meta_workspace_remove_window (old, move_window);
          meta_workspace_add_window (workspace, move_window);
        }
    }

  meta_workspace_queue_calc_showing (old);
  meta_workspace_queue_calc_showing (workspace);

  /* FIXME: Why do we need this?!?  Isn't it handled in the lines above? */
  if (move_window)
      /* Removes window from other spaces */
      meta_window_change_workspace (move_window, workspace);

   /*
    * Notify the compositor that the active workspace is changing.
    */
   screen = workspace->screen;
   display = screen->display;
   comp = display->compositor;
   direction = 0;

   current_space = meta_workspace_index (old);
   new_space     = meta_workspace_index (workspace);
   num_workspaces = g_list_length (screen->workspaces);
   meta_screen_calc_workspace_layout (workspace->screen, num_workspaces,
                                      current_space, &layout1);

   meta_screen_calc_workspace_layout (workspace->screen, num_workspaces,
                                      new_space, &layout2);

   direction = get_wrapped_horizontal_direction (layout1.current_col, layout2.current_col, suggested_dir, num_workspaces);

   if (layout1.current_row < layout2.current_row)
     {
       if (!direction)
         direction = META_MOTION_DOWN;
       else if (direction == META_MOTION_RIGHT)
         direction = META_MOTION_DOWN_RIGHT;
       else
         direction = META_MOTION_DOWN_LEFT;
     }

   if (layout1.current_row > layout2.current_row)
     {
       if (!direction)
         direction = META_MOTION_UP;
       else if (direction == META_MOTION_RIGHT)
         direction = META_MOTION_UP_RIGHT;
       else
         direction = META_MOTION_UP_LEFT;
     }

   meta_screen_free_workspace_layout (&layout1);
   meta_screen_free_workspace_layout (&layout2);

   meta_compositor_switch_workspace (comp, screen, old, workspace, direction);

  /* This needs to be done after telling the compositor we are switching
   * workspaces since focusing a window will cause it to be immediately
   * shown and that would confuse the compositor if it didn't know we
   * were in a workspace switch.
   */
  if (focus_this)
    {
      meta_window_activate (focus_this, timestamp);
    }
  else if (move_window)
    {
      meta_window_raise (move_window);
    }
  else
    {
      meta_topic (META_DEBUG_FOCUS, "Focusing default window on new workspace\n");
      meta_workspace_focus_default_window (workspace, NULL, timestamp);
    }

   /* Emit switched signal from screen.c */
   meta_screen_workspace_switched (screen, current_space, new_space, direction);
}

/**
 * meta_workspace_activate_with_focus:
 * @workspace: a #MetaWorkspace
 * @focus_this: the #MetaWindow to be focused, or %NULL
 * @timestamp: timestamp for @focus_this
 *
 * Switches to @workspace and possibly activates the window @focus_this.
 *
 * The window @focus_this is activated by calling meta_window_activate()
 * which will unminimize it and transient parents, raise it and give it
 * the focus.
 *
 * If a window is currently being moved by the user, it will be
 * moved to @workspace.
 *
 * The advantage of calling this function instead of meta_workspace_activate()
 * followed by meta_window_activate() is that it happens as a unit, so
 * no other window gets focused first before @focus_this.
 */
void
meta_workspace_activate_with_focus (MetaWorkspace *workspace,
                                    MetaWindow    *focus_this,
                                    guint32        timestamp)
{
  meta_workspace_activate_internal (workspace, focus_this, 0, timestamp);
}

void
meta_workspace_activate (MetaWorkspace *workspace,
                         guint32        timestamp)
{
  meta_workspace_activate_internal (workspace, NULL, 0, timestamp);
}

/**
 * meta_workspace_activate_with_direction_hint:
 * @workspace: a #MetaWorkspace
 * @direction: the suggested #MetaMotionDirection
 * @timestamp: timestamp for @focus_this
 *
 * Switches to @workspace in the specified @direction (if possible)
 */

void
meta_workspace_activate_with_direction_hint (MetaWorkspace       *workspace,
                                             MetaMotionDirection  direction,
                                             guint32              timestamp)
{
  meta_workspace_activate_internal (workspace, NULL, direction, timestamp);
}

int
meta_workspace_index (MetaWorkspace *workspace)
{
  int ret = g_list_index (workspace->screen->workspaces, workspace);
  /* return value is negative if the workspace is invalid */
  return ret;
}

void
meta_workspace_update_window_hints (MetaWorkspace *workspace)
{
  GList *l = workspace->windows;
  while (l)
    {
      MetaWindow *win = l->data;

      meta_window_set_current_workspace_hint (win);

      l = l->next;
    }
}

/**
 * meta_workspace_list_windows:
 * @workspace: a #MetaWorkspace
 *
 * Gets windows contained on the workspace, including workspace->windows
 * and also sticky windows. Override-redirect windows are not included.
 *
 * Return value: (transfer container) (element-type MetaWindow): the list of windows.
 */
GList*
meta_workspace_list_windows (MetaWorkspace *workspace)
{
  GSList *display_windows;
  GSList *tmp;
  GList *workspace_windows;
  
  display_windows = meta_display_list_windows (workspace->screen->display,
                                               META_LIST_DEFAULT);

  workspace_windows = NULL;
  tmp = display_windows;
  while (tmp != NULL)
    {
      MetaWindow *window = tmp->data;

      if (meta_window_located_on_workspace (window, workspace))
        workspace_windows = g_list_prepend (workspace_windows,
                                            window);

      tmp = tmp->next;
    }

  g_slist_free (display_windows);

  return workspace_windows;
}

LOCAL_SYMBOL void
meta_workspace_invalidate_work_area (MetaWorkspace *workspace)
{
  GList *tmp;
  GList *windows;
  int i;
  
  if (workspace->work_areas_invalid)
    {
      meta_topic (META_DEBUG_WORKAREA,
                  "Work area for workspace %d is already invalid\n",
                  meta_workspace_index (workspace));
      return;
    }

  meta_topic (META_DEBUG_WORKAREA,
              "Invalidating work area for workspace %d\n",
              meta_workspace_index (workspace));

  /* If we are in the middle of a resize or move operation, we
   * might have cached pointers to the workspace's edges */
  if (workspace == workspace->screen->active_workspace)
    meta_display_cleanup_edges (workspace->screen->display);

  g_free (workspace->work_area_monitor);
  workspace->work_area_monitor = NULL;
      
  workspace_free_all_struts (workspace);

  for (i = 0; i < workspace->screen->n_monitor_infos; i++)
    meta_rectangle_free_list_and_elements (workspace->monitor_region[i]);
  g_free (workspace->monitor_region);
  meta_rectangle_free_list_and_elements (workspace->screen_region);
  meta_rectangle_free_list_and_elements (workspace->screen_edges);
  meta_rectangle_free_list_and_elements (workspace->monitor_edges);
  workspace->monitor_region = NULL;
  workspace->screen_region = NULL;
  workspace->screen_edges = NULL;
  workspace->monitor_edges = NULL;
  
  workspace->work_areas_invalid = TRUE;

  /* redo the size/position constraints on all windows */
  windows = meta_workspace_list_windows (workspace);
  tmp = windows;
  while (tmp != NULL)
    {
      MetaWindow *w = tmp->data;

      meta_window_queue (w, META_QUEUE_MOVE_RESIZE);
      
      tmp = tmp->next;
    }

  g_list_free (windows);

  meta_screen_queue_workarea_recalc (workspace->screen);
}

static MetaStrut *
copy_strut(MetaStrut *original)
{
  return g_memdup(original, sizeof(MetaStrut));
}

static GSList *
copy_strut_list(GSList *original)
{
  GSList *result = NULL;

  while (original)
    {
      result = g_slist_prepend (result, copy_strut (original->data));
      original = original->next;
    }

  return g_slist_reverse (result);
}

static void
ensure_work_areas_validated (MetaWorkspace *workspace)
{
  GList         *windows;
  GList         *tmp;
  MetaRectangle  work_area;
  int            i;  /* C89 absolutely sucks... */

  if (!workspace->work_areas_invalid)
    return;

  g_assert (workspace->all_struts == NULL);
  g_assert (workspace->monitor_region == NULL);
  g_assert (workspace->screen_region == NULL);
  g_assert (workspace->screen_edges == NULL);
  g_assert (workspace->monitor_edges == NULL);

  /* STEP 1: Get the list of struts */

  workspace->all_struts = copy_strut_list (workspace->builtin_struts);

  windows = meta_workspace_list_windows (workspace);
  for (tmp = windows; tmp != NULL; tmp = tmp->next)
    {
      MetaWindow *win = tmp->data;
      GSList *s_iter;

      for (s_iter = win->struts; s_iter != NULL; s_iter = s_iter->next) {
        workspace->all_struts = g_slist_prepend (workspace->all_struts,
                                                 copy_strut(s_iter->data));
      }
    }
  g_list_free (windows);

  /* STEP 2: Get the maximal/spanning rects for the onscreen and
   *         on-single-monitor regions
   */  
  g_assert (workspace->monitor_region == NULL);
  g_assert (workspace->screen_region   == NULL);

  workspace->monitor_region = g_new (GList*,
                                      workspace->screen->n_monitor_infos);
  for (i = 0; i < workspace->screen->n_monitor_infos; i++)
    {
      workspace->monitor_region[i] =
        meta_rectangle_get_minimal_spanning_set_for_region (
          &workspace->screen->monitor_infos[i].rect,
          workspace->all_struts);
    }
  workspace->screen_region =
    meta_rectangle_get_minimal_spanning_set_for_region (
      &workspace->screen->rect,
      workspace->all_struts);

  /* STEP 3: Get the work areas (region-to-maximize-to) for the screen and
   *         monitors.
   */
  work_area = workspace->screen->rect;  /* start with the screen */
  if (workspace->screen_region == NULL)
    work_area = meta_rect (0, 0, -1, -1);
  else
    meta_rectangle_clip_to_region (workspace->screen_region,
                                   FIXED_DIRECTION_NONE,
                                   &work_area);

  /* Lots of paranoia checks, forcing work_area_screen to be sane */
#define MIN_SANE_AREA 100
  if (work_area.width < MIN_SANE_AREA)
    {
      meta_warning ("struts occupy an unusually large percentage of the screen; "
                    "available remaining width = %d < %d",
                    work_area.width, MIN_SANE_AREA);
      if (work_area.width < 1)
        {
          work_area.x = (workspace->screen->rect.width - MIN_SANE_AREA)/2;
          work_area.width = MIN_SANE_AREA;
        }
      else
        {
          int amount = (MIN_SANE_AREA - work_area.width)/2;
          work_area.x     -=   amount;
          work_area.width += 2*amount;
        }
    }
  if (work_area.height < MIN_SANE_AREA)
    {
      meta_warning ("struts occupy an unusually large percentage of the screen; "
                    "available remaining height = %d < %d",
                    work_area.height, MIN_SANE_AREA);
      if (work_area.height < 1)
        {
          work_area.y = (workspace->screen->rect.height - MIN_SANE_AREA)/2;
          work_area.height = MIN_SANE_AREA;
        }
      else
        {
          int amount = (MIN_SANE_AREA - work_area.height)/2;
          work_area.y      -=   amount;
          work_area.height += 2*amount;
        }
    }
  workspace->work_area_screen = work_area;
  meta_topic (META_DEBUG_WORKAREA,
              "Computed work area for workspace %d: %d,%d %d x %d\n",
              meta_workspace_index (workspace),
              workspace->work_area_screen.x,
              workspace->work_area_screen.y,
              workspace->work_area_screen.width,
              workspace->work_area_screen.height);    

  /* Now find the work areas for each monitor */
  g_free (workspace->work_area_monitor);
  workspace->work_area_monitor = g_new (MetaRectangle,
                                         workspace->screen->n_monitor_infos);

  for (i = 0; i < workspace->screen->n_monitor_infos; i++)
    {
      work_area = workspace->screen->monitor_infos[i].rect;

      if (workspace->monitor_region[i] == NULL)
        /* FIXME: constraints.c untested with this, but it might be nice for
         * a screen reader or magnifier.
         */
        work_area = meta_rect (work_area.x, work_area.y, -1, -1);
      else
        meta_rectangle_clip_to_region (workspace->monitor_region[i],
                                       FIXED_DIRECTION_NONE,
                                       &work_area);

      workspace->work_area_monitor[i] = work_area;
      meta_topic (META_DEBUG_WORKAREA,
                  "Computed work area for workspace %d "
                  "monitor %d: %d,%d %d x %d\n",
                  meta_workspace_index (workspace),
                  i,
                  workspace->work_area_monitor[i].x,
                  workspace->work_area_monitor[i].y,
                  workspace->work_area_monitor[i].width,
                  workspace->work_area_monitor[i].height);
    }

  /* STEP 4: Make sure the screen_region is nonempty (separate from step 2
   *         since it relies on step 3).
   */  
  if (workspace->screen_region == NULL)
    {
      MetaRectangle *nonempty_region;
      nonempty_region = g_new (MetaRectangle, 1);
      *nonempty_region = workspace->work_area_screen;
      workspace->screen_region = g_list_prepend (NULL, nonempty_region);
    }

  /* STEP 5: Cache screen and monitor edges for edge resistance and snapping */
  g_assert (workspace->screen_edges    == NULL);
  g_assert (workspace->monitor_edges  == NULL);
  workspace->screen_edges =
    meta_rectangle_find_onscreen_edges (&workspace->screen->rect,
                                        workspace->all_struts);
  tmp = NULL;
  for (i = 0; i < workspace->screen->n_monitor_infos; i++)
    tmp = g_list_prepend (tmp, &workspace->screen->monitor_infos[i].rect);
  workspace->monitor_edges =
    meta_rectangle_find_nonintersected_monitor_edges (tmp,
                                                       workspace->all_struts);
  g_list_free (tmp);

  /* We're all done, YAAY!  Record that everything has been validated. */
  workspace->work_areas_invalid = FALSE;
}

static gboolean
strut_lists_equal (GSList *l,
                   GSList *m)
{
  for (; l && m; l = l->next, m = m->next)
    {
      MetaStrut *a = l->data;
      MetaStrut *b = m->data;

      if (a->side != b->side ||
          !meta_rectangle_equal (&a->rect, &b->rect))
        return FALSE;
    }

  return l == NULL && m == NULL;
}

/**
 * meta_workspace_set_builtin_struts:
 * @workspace: a #MetaWorkspace
 * @struts: (element-type Meta.Strut) (transfer none): list of #MetaStrut
 *
 * Sets a list of struts that will be used in addition to the struts
 * of the windows in the workspace when computing the work area of
 * the workspace.
 */
void
meta_workspace_set_builtin_struts (MetaWorkspace *workspace,
                                   GSList        *struts)
{
  MetaScreen *screen = workspace->screen;
  GSList *l;

  for (l = struts; l; l = l->next)
    {
      MetaStrut *strut = l->data;
      int idx = meta_screen_get_monitor_index_for_rect (screen, &strut->rect);

      switch (strut->side)
        {
        case META_SIDE_TOP:
          if (meta_screen_get_monitor_neighbor (screen, idx, META_SCREEN_UP))
            continue;

          strut->rect.height += strut->rect.y;
          strut->rect.y = 0;
          break;
        case META_SIDE_BOTTOM:
          if (meta_screen_get_monitor_neighbor (screen, idx, META_SCREEN_DOWN))
            continue;

          strut->rect.height = screen->rect.height - strut->rect.y;
          break;
        case META_SIDE_LEFT:
          if (meta_screen_get_monitor_neighbor (screen, idx, META_SCREEN_LEFT))
            continue;

          strut->rect.width += strut->rect.x;
          strut->rect.x = 0;
          break;
        case META_SIDE_RIGHT:
          if (meta_screen_get_monitor_neighbor (screen, idx, META_SCREEN_RIGHT))
            continue;

          strut->rect.width = screen->rect.width - strut->rect.x;
          break;
        }
    }
    
  /* Reordering doesn't actually matter, so we don't catch all
   * no-impact changes, but this is just a (possibly unnecessary
   * anyways) optimization */
  if (strut_lists_equal (struts, workspace->builtin_struts))
    return;

  workspace_free_builtin_struts (workspace);
  workspace->builtin_struts = copy_strut_list (struts);

  meta_workspace_invalidate_work_area (workspace);
}

void
meta_workspace_update_snapped_windows (MetaWorkspace *workspace)
{
  GList *window_list = meta_workspace_list_windows (workspace);
  GList *old = workspace->snapped_windows;
  workspace->snapped_windows = NULL;

  GList *iter;
  MetaWindow *window;

  for (iter = window_list; iter != NULL; iter = iter->next)
  {
    window = (MetaWindow *) iter->data;
    if (window->tile_type == META_WINDOW_TILE_TYPE_SNAPPED)
        workspace->snapped_windows = g_list_prepend (workspace->snapped_windows, window);
  }
  g_list_free (old);
  g_list_free (window_list);

  meta_workspace_recalc_for_snapped_windows (workspace);
}

gboolean
meta_workspace_has_snapped_windows (MetaWorkspace *workspace)
{
    return g_list_length (workspace->snapped_windows) > 0;
}

void
meta_workspace_recalc_for_snapped_windows (MetaWorkspace *workspace)
{
    GList *window_list = meta_workspace_list_windows (workspace);
    GList *iter;
    MetaWindow *win;
    for (iter = window_list; iter != NULL; iter = iter->next)
    {
        win = META_WINDOW (iter->data);
        if (meta_window_get_maximized (win))
        {
            meta_window_queue(win, META_QUEUE_MOVE_RESIZE);
        }
    }
    g_list_free (window_list);
}

/**
 * meta_workspace_get_work_area_for_monitor:
 * @workspace: a #MetaWorkspace
 * @which_monitor: a monitor index
 * @area: (out): location to store the work area
 *
 * Stores the work area for @which_monitor on @workspace
 * in @area.
 */
void
meta_workspace_get_work_area_for_monitor (MetaWorkspace *workspace,
                                          int            which_monitor,
                                          MetaRectangle *area)
{
  g_assert (which_monitor >= 0);

  ensure_work_areas_validated (workspace);
  g_assert (which_monitor < workspace->screen->n_monitor_infos);
  
  *area = workspace->work_area_monitor[which_monitor];
}

void
meta_workspace_get_work_area_all_monitors (MetaWorkspace *workspace,
                                           MetaRectangle *area)
{
  ensure_work_areas_validated (workspace);
  
  *area = workspace->work_area_screen;
}

LOCAL_SYMBOL GList*
meta_workspace_get_onscreen_region (MetaWorkspace *workspace)
{
  ensure_work_areas_validated (workspace);

  return workspace->screen_region;
}

LOCAL_SYMBOL GList*
meta_workspace_get_onmonitor_region (MetaWorkspace *workspace,
                                     int            which_monitor)
{
  ensure_work_areas_validated (workspace);

  return workspace->monitor_region[which_monitor];
}

#ifdef WITH_VERBOSE_MODE
static char *
meta_motion_direction_to_string (MetaMotionDirection direction)
{
  switch (direction)
    {
    case META_MOTION_UP:
      return "Up";
    case META_MOTION_DOWN:
      return "Down";
    case META_MOTION_LEFT:
      return "Left";
    case META_MOTION_RIGHT:
      return "Right";
    case META_MOTION_UP_RIGHT:
      return "Up-Right";
    case META_MOTION_DOWN_RIGHT:
      return "Down-Right";
    case META_MOTION_UP_LEFT:
      return "Up-Left";
    case META_MOTION_DOWN_LEFT:
      return "Down-Left";
    case META_MOTION_NOT_EXIST_YET:
      return "Nothing";
    }

  return "Unknown";
}
#endif /* WITH_VERBOSE_MODE */

/**
 * meta_workspace_get_neighbor:
 * @workspace: a #MetaWorkspace
 * @direction: a #MetaMotionDirection, direction in which to look for the neighbor
 *
 * Gets the neighbor of the #MetaWorkspace in the given direction
 *
 * Return value: (transfer none): the neighbor #MetaWorkspace
 */
MetaWorkspace*
meta_workspace_get_neighbor (MetaWorkspace      *workspace,
                             MetaMotionDirection direction)
{
  MetaWorkspaceLayout layout;  
  int i, current_space, num_workspaces;
  gboolean ltr, cycle;

  current_space = meta_workspace_index (workspace);
  num_workspaces = g_list_length (workspace->screen->workspaces);
  meta_screen_calc_workspace_layout (workspace->screen, num_workspaces,
                                     current_space, &layout);
  cycle = meta_prefs_get_workspace_cycle();

  meta_verbose ("Getting neighbor of %d in direction %s\n",
                current_space, meta_motion_direction_to_string (direction));
  
  ltr = meta_ui_get_direction() == META_UI_DIRECTION_LTR;

  switch (direction) 
    {
    case META_MOTION_LEFT:
      layout.current_col -= ltr ? 1 : -1;
      break;
    case META_MOTION_RIGHT:
      layout.current_col += ltr ? 1 : -1;
      break;
    case META_MOTION_UP:
      layout.current_row -= 1;
      break;
    case META_MOTION_DOWN:
      layout.current_row += 1;
      break;
    default:;
    }

  if (layout.current_col < 0)
    layout.current_col = (cycle == 1)? layout.cols - 1 : 0;
  if (layout.current_col >= layout.cols)
    layout.current_col = (cycle == 1)? 0 : layout.cols - 1;
  if (layout.current_row < 0)
    layout.current_row = 0;
  if (layout.current_row >= layout.rows)
    layout.current_row = layout.rows - 1;

  i = layout.grid[layout.current_row * layout.cols + layout.current_col];

  if (i < 0)
    i = current_space;

  if (i >= num_workspaces)
    meta_bug ("calc_workspace_layout left an invalid (too-high) workspace number %d in the grid\n",
              i);
    
  meta_verbose ("Neighbor workspace is %d at row %d col %d\n",
                i, layout.current_row, layout.current_col);

  meta_screen_free_workspace_layout (&layout);
  
  return meta_screen_get_workspace_by_index (workspace->screen, i);
}

LOCAL_SYMBOL const char*
meta_workspace_get_name (MetaWorkspace *workspace)
{
  return meta_prefs_get_workspace_name (meta_workspace_index (workspace));
}

void
meta_workspace_focus_default_window (MetaWorkspace *workspace,
                                     MetaWindow    *not_this_one,
                                     guint32        timestamp)
{
  if (timestamp == CurrentTime)
    {
      meta_warning ("CurrentTime used to choose focus window; "
                    "focus window may not be correct.\n");
    }


  if (meta_prefs_get_focus_mode () == C_DESKTOP_FOCUS_MODE_CLICK ||
      !workspace->screen->display->mouse_mode)
    focus_ancestor_or_top_window (workspace, not_this_one, timestamp);
  else
    {
      MetaWindow * window;
      window = meta_screen_get_mouse_window (workspace->screen, not_this_one);
      if (window &&
          window->type != META_WINDOW_DOCK &&
          window->type != META_WINDOW_DESKTOP)
        {
          if (timestamp == CurrentTime)
            {

              /* We would like for this to never happen.  However, if
               * it does happen then we kludge since using CurrentTime
               * can mean ugly race conditions--and we can avoid these
               * by allowing EnterNotify events (which come with
               * timestamps) to handle focus.
               */

              meta_topic (META_DEBUG_FOCUS,
                          "Not focusing mouse window %s because EnterNotify events should handle that\n", window->desc);
            }
          else
            {
              meta_topic (META_DEBUG_FOCUS,
                          "Focusing mouse window %s\n", window->desc);
              meta_window_focus (window, timestamp);
            }

          if (workspace->screen->display->autoraise_window != window &&
              meta_prefs_get_auto_raise ()) 
            {
              meta_display_queue_autoraise_callback (workspace->screen->display,
                                                     window);
            }
        }
      else if (meta_prefs_get_focus_mode () == C_DESKTOP_FOCUS_MODE_SLOPPY)
        focus_ancestor_or_top_window (workspace, not_this_one, timestamp);
      else if (meta_prefs_get_focus_mode () == C_DESKTOP_FOCUS_MODE_MOUSE)
        {
          meta_topic (META_DEBUG_FOCUS,
                      "Setting focus to no_focus_window, since no valid "
                      "window to focus found.\n");
          meta_display_focus_the_no_focus_window (workspace->screen->display,
                                                  workspace->screen,
                                                  timestamp);
        }
    }
}

static gboolean
record_ancestor (MetaWindow *window,
                 void       *data)
{
  MetaWindow **result = data;

  *result = window;
  return FALSE; /* quit with the first ancestor we find */
}

/* Focus ancestor of not_this_one if there is one */
static void
focus_ancestor_or_top_window (MetaWorkspace *workspace,
                              MetaWindow    *not_this_one,
                              guint32        timestamp)
{
  MetaWindow *window = NULL;

  if (not_this_one)
    meta_topic (META_DEBUG_FOCUS,
                "Focusing MRU window excluding %s\n", not_this_one->desc);
  else
    meta_topic (META_DEBUG_FOCUS,
                "Focusing MRU window\n");

  /* First, check to see if we need to focus an ancestor of a window */  
  if (not_this_one)
    {
      MetaWindow *ancestor;
      ancestor = NULL;
      meta_window_foreach_ancestor (not_this_one, record_ancestor, &ancestor);
      if (ancestor != NULL &&
          (ancestor->on_all_workspaces ||
           ancestor->workspace == workspace) &&
          meta_window_showing_on_its_workspace (ancestor))
        {
          meta_topic (META_DEBUG_FOCUS,
                      "Focusing %s, ancestor of %s\n", 
                      ancestor->desc, not_this_one->desc);
      
          meta_window_focus (ancestor, timestamp);

          /* Also raise the window if in click-to-focus */
          if (meta_prefs_get_focus_mode () == C_DESKTOP_FOCUS_MODE_CLICK)
            meta_window_raise (ancestor);

          return;
        }
    }

  window = meta_stack_get_default_focus_window (workspace->screen->stack,
                                                workspace,
                                                not_this_one);

  if (window)
    {
      meta_topic (META_DEBUG_FOCUS,
                  "Focusing workspace MRU window %s\n", window->desc);
      
      meta_window_focus (window, timestamp);

      /* Also raise the window if in click-to-focus */
      if (meta_prefs_get_focus_mode () == C_DESKTOP_FOCUS_MODE_CLICK)
        meta_window_raise (window);
    }
  else
    {
      meta_topic (META_DEBUG_FOCUS, "No MRU window to focus found; focusing no_focus_window.\n");
      meta_display_focus_the_no_focus_window (workspace->screen->display,
                                              workspace->screen,
                                              timestamp);
    }
}
