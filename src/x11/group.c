/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2002 Red Hat Inc.
 * Copyright (C) 2003 Rob Adams
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

/**
 * SECTION:group
 * @title: MetaGroup
 * @short_description: Mutter window groups
 *
 */

#include "config.h"

#include "x11/group-private.h"

#include <X11/Xlib-xcb.h>

#include "core/window-private.h"
#include "meta/util.h"
#include "meta/window.h"
#include "x11/group-props.h"
#include "x11/meta-x11-display-private.h"

static MetaGroup*
meta_group_new (MetaX11Display *x11_display,
                Window          group_leader)
{
  g_autofree MetaGroup *group = NULL;
#define N_INITIAL_PROPS 3
  Atom initial_props[N_INITIAL_PROPS];
  int i;

  g_assert (N_INITIAL_PROPS == (int) G_N_ELEMENTS (initial_props));

  group = g_new0 (MetaGroup, 1);

  group->x11_display = x11_display;
  group->windows = NULL;
  group->group_leader = group_leader;
  group->refcount = 1; /* owned by caller, hash table has only weak ref */

  xcb_connection_t *xcb_conn = XGetXCBConnection (x11_display->xdisplay);
  xcb_generic_error_t *e;
  g_autofree xcb_get_window_attributes_reply_t *attrs =
    xcb_get_window_attributes_reply (xcb_conn,
                                     xcb_get_window_attributes (xcb_conn, group_leader),
                                     &e);
  if (e)
    return NULL;

  const uint32_t events[] = { attrs->your_event_mask | XCB_EVENT_MASK_PROPERTY_CHANGE };
  xcb_change_window_attributes (xcb_conn, group_leader,
                                XCB_CW_EVENT_MASK, events);

  if (x11_display->groups_by_leader == NULL)
    x11_display->groups_by_leader = g_hash_table_new (meta_unsigned_long_hash,
                                                      meta_unsigned_long_equal);

  g_assert (g_hash_table_lookup (x11_display->groups_by_leader, &group_leader) == NULL);

  g_hash_table_insert (x11_display->groups_by_leader,
                       &group->group_leader,
                       group);

  /* Fill these in the order we want them to be gotten */
  i = 0;
  initial_props[i++] = x11_display->atom_WM_CLIENT_MACHINE;
  initial_props[i++] = x11_display->atom__NET_WM_PID;
  initial_props[i++] = x11_display->atom__NET_STARTUP_ID;
  g_assert (N_INITIAL_PROPS == i);

  meta_group_reload_properties (group, initial_props, N_INITIAL_PROPS);

  meta_topic (META_DEBUG_GROUPS,
              "Created new group with leader 0x%lx\n",
              group->group_leader);

  return g_steal_pointer (&group);
}

static void
meta_group_unref (MetaGroup *group)
{
  g_return_if_fail (group->refcount > 0);

  group->refcount -= 1;
  if (group->refcount == 0)
    {
      meta_topic (META_DEBUG_GROUPS,
                  "Destroying group with leader 0x%lx\n",
                  group->group_leader);

      g_assert (group->x11_display->groups_by_leader != NULL);

      g_hash_table_remove (group->x11_display->groups_by_leader,
                           &group->group_leader);

      /* mop up hash table, this is how it gets freed on display close */
      if (g_hash_table_size (group->x11_display->groups_by_leader) == 0)
        {
          g_hash_table_destroy (group->x11_display->groups_by_leader);
          group->x11_display->groups_by_leader = NULL;
        }

      g_free (group->wm_client_machine);
      g_free (group->startup_id);

      g_free (group);
    }
}

/**
 * meta_window_get_group: (skip)
 * @window: a #MetaWindow
 *
 */
MetaGroup*
meta_window_get_group (MetaWindow *window)
{
  if (window->unmanaging)
    return NULL;

  return window->group;
}

void
meta_window_compute_group (MetaWindow* window)
{
  MetaGroup *group;
  MetaWindow *ancestor;
  MetaX11Display *x11_display = window->display->x11_display;

  /* use window->xwindow if no window->xgroup_leader */

  group = NULL;

  /* Determine the ancestor of the window; its group setting will override the
   * normal grouping rules; see bug 328211.
   */
  ancestor = meta_window_find_root_ancestor (window);

  if (x11_display->groups_by_leader)
    {
      if (ancestor != window)
        group = ancestor->group;
      else if (window->xgroup_leader != None)
        group = g_hash_table_lookup (x11_display->groups_by_leader,
                                     &window->xgroup_leader);
      else
        group = g_hash_table_lookup (x11_display->groups_by_leader,
                                     &window->xwindow);
    }

  if (group != NULL)
    {
      window->group = group;
      group->refcount += 1;
    }
  else
    {
      if (ancestor != window && ancestor->xgroup_leader != None)
        group = meta_group_new (x11_display,
                                ancestor->xgroup_leader);
      else if (window->xgroup_leader != None)
        group = meta_group_new (x11_display,
                                window->xgroup_leader);
      else
        group = meta_group_new (x11_display,
                                window->xwindow);

      window->group = group;
    }

  if (!window->group)
    return;

  window->group->windows = g_slist_prepend (window->group->windows, window);

  meta_topic (META_DEBUG_GROUPS,
              "Adding %s to group with leader 0x%lx\n",
              window->desc, group->group_leader);
}

static void
remove_window_from_group (MetaWindow *window)
{
  if (window->group != NULL)
    {
      meta_topic (META_DEBUG_GROUPS,
                  "Removing %s from group with leader 0x%lx\n",
                  window->desc, window->group->group_leader);

      window->group->windows =
        g_slist_remove (window->group->windows,
                        window);
      meta_group_unref (window->group);
      window->group = NULL;
    }
}

void
meta_window_group_leader_changed (MetaWindow *window)
{
  remove_window_from_group (window);
  meta_window_compute_group (window);
}

void
meta_window_shutdown_group (MetaWindow *window)
{
  remove_window_from_group (window);
}

/**
 * meta_x11_display_lookup_group: (skip)
 * @x11_display: a #MetaX11Display
 * @group_leader: a X window
 *
 */
MetaGroup *
meta_x11_display_lookup_group (MetaX11Display *x11_display,
                               Window          group_leader)
{
  MetaGroup *group;

  group = NULL;

  if (x11_display->groups_by_leader)
    group = g_hash_table_lookup (x11_display->groups_by_leader,
                                 &group_leader);

  return group;
}

/**
 * meta_group_list_windows:
 * @group: A #MetaGroup
 *
 * Returns: (transfer container) (element-type Meta.Window): List of windows
 */
GSList*
meta_group_list_windows (MetaGroup *group)
{
  return g_slist_copy (group->windows);
}

void
meta_group_update_layers (MetaGroup *group)
{
  GSList *tmp;
  GSList *frozen_stacks;

  if (group->windows == NULL)
    return;

  frozen_stacks = NULL;
  tmp = group->windows;
  while (tmp != NULL)
    {
      MetaWindow *window = tmp->data;

      /* we end up freezing the same stack a lot of times,
       * but doesn't hurt anything. have to handle
       * groups that span 2 screens.
       */
      meta_stack_freeze (window->display->stack);
      frozen_stacks = g_slist_prepend (frozen_stacks, window->display->stack);

      meta_stack_update_layer (window->display->stack,
                               window);

      tmp = tmp->next;
    }

  tmp = frozen_stacks;
  while (tmp != NULL)
    {
      meta_stack_thaw (tmp->data);
      tmp = tmp->next;
    }

  g_slist_free (frozen_stacks);
}

const char*
meta_group_get_startup_id (MetaGroup *group)
{
  return group->startup_id;
}

/**
 * meta_group_property_notify: (skip)
 * @group: a #MetaGroup
 * @event: a X event
 *
 */
gboolean
meta_group_property_notify (MetaGroup  *group,
                            XEvent     *event)
{
  meta_group_reload_property (group,
                              event->xproperty.atom);

  return TRUE;

}

int
meta_group_get_size (MetaGroup *group)
{
  if (!group)
    return 0;

  return group->refcount;
}

