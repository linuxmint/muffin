/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2019 Red Hat, Inc
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

#include "core/frame.h"
#include "core/stack.h"
#include "core/window-private.h"
#include "x11/meta-x11-display-private.h"
#include "x11/meta-x11-stack-private.h"

struct _MetaX11Stack
{
  GObject parent;
  MetaX11Display *x11_display;

  /*
   * A sequence of all the Windows (X handles, not MetaWindows) of the windows
   * we manage, sorted in order.  Suitable to be passed into _NET_CLIENT_LIST.
   */
  GArray *xwindows;

  /*
   * MetaWindows waiting to be added to the xwindows list, after
   * being added to the MetaStack.
   *
   * The order of the elements in this list is not important; what is important
   * is the stack_position element of each window.
   */
  GList *added;

  /*
   * Windows (X handles, not MetaWindows) waiting to be removed from the
   * xwindows list, after being removed from the MetaStack.
   *
   * The order of the elements in this list is not important.
   */
  GList *removed;
};

enum
{
  PROP_DISPLAY = 1,
  N_PROPS
};

static GParamSpec *pspecs[N_PROPS] = { 0 };

G_DEFINE_TYPE (MetaX11Stack, meta_x11_stack, G_TYPE_OBJECT)

static void
meta_x11_stack_init (MetaX11Stack *x11_stack)
{
  x11_stack->xwindows = g_array_new (FALSE, FALSE, sizeof (Window));
}

static void
meta_x11_stack_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  MetaX11Stack *x11_stack = META_X11_STACK (object);

  switch (prop_id)
    {
    case PROP_DISPLAY:
      x11_stack->x11_display = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_x11_stack_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  MetaX11Stack *x11_stack = META_X11_STACK (object);

  switch (prop_id)
    {
    case PROP_DISPLAY:
      g_value_set_object (value, x11_stack->x11_display);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
stack_window_added_cb (MetaStack    *stack,
                       MetaWindow   *window,
                       MetaX11Stack *x11_stack)
{
  if (window->client_type != META_WINDOW_CLIENT_TYPE_X11)
    return;

  x11_stack->added = g_list_prepend (x11_stack->added, window);
}

static void
stack_window_removed_cb (MetaStack    *stack,
                         MetaWindow   *window,
                         MetaX11Stack *x11_stack)
{
  if (window->client_type != META_WINDOW_CLIENT_TYPE_X11)
    return;

  x11_stack->added = g_list_remove (x11_stack->added, window);

  x11_stack->removed = g_list_prepend (x11_stack->removed,
                                   GUINT_TO_POINTER (window->xwindow));
  if (window->frame)
    {
      x11_stack->removed = g_list_prepend (x11_stack->removed,
                                           GUINT_TO_POINTER (window->frame->xwindow));
    }
}

/**
 * stack_do_window_deletions:
 *
 * Go through "deleted" and take the matching windows
 * out of "windows".
 */
static void
x11_stack_do_window_deletions (MetaX11Stack *x11_stack)
{
  GList *tmp;
  int i;

  tmp = x11_stack->removed;
  while (tmp != NULL)
    {
      Window xwindow;
      xwindow = GPOINTER_TO_UINT (tmp->data);

      /* We go from the end figuring removals are more
       * likely to be recent.
       */
      i = x11_stack->xwindows->len;
      while (i > 0)
        {
          --i;

          /* there's no guarantee we'll actually find windows to
           * remove, e.g. the same xwindow could have been
           * added/removed before we ever synced, and we put
           * both the window->xwindow and window->frame->xwindow
           * in the removal list.
           */
          if (xwindow == g_array_index (x11_stack->xwindows, Window, i))
            {
              g_array_remove_index (x11_stack->xwindows, i);
              goto next;
            }
        }

    next:
      tmp = tmp->next;
    }

  g_clear_pointer (&x11_stack->removed, g_list_free);
}

static void
x11_stack_do_window_additions (MetaX11Stack *x11_stack)
{
  GList *tmp;
  gint n_added;

  n_added = g_list_length (x11_stack->added);
  if (n_added > 0)
    {
      meta_topic (META_DEBUG_STACK,
                  "Adding %d windows to sorted list\n",
                  n_added);

      /* stack->added has the most recent additions at the
       * front of the list, so we need to reverse it
       */
      x11_stack->added = g_list_reverse (x11_stack->added);

      tmp = x11_stack->added;
      while (tmp != NULL)
        {
          MetaWindow *w;

          w = tmp->data;
          g_array_append_val (x11_stack->xwindows, w->xwindow);
          tmp = tmp->next;
        }
    }

  g_clear_pointer (&x11_stack->added, g_list_free);
}

/**
 * x11_stack_sync_to_server:
 *
 * Order the windows on the X server to be the same as in our structure.
 * We do this using XRestackWindows if we don't know the previous order,
 * or XConfigureWindow on a few particular windows if we do and can figure
 * out the minimum set of changes.  After that, we set __NET_CLIENT_LIST
 * and __NET_CLIENT_LIST_STACKING.
 *
 * FIXME: Now that we have a good view of the stacking order on the server
 * with MetaStackTracker it should be possible to do a simpler and better
 * job of computing the minimal set of stacking requests needed.
 */
static void
x11_stack_sync_to_xserver (MetaX11Stack *x11_stack)
{
  MetaX11Display *x11_display = x11_stack->x11_display;
  MetaStack *stack = x11_display->display->stack;
  GArray *x11_stacked;
  GList *tmp;
  GList *sorted;

  meta_topic (META_DEBUG_STACK, "Syncing window stack to server\n");

  /* Create stacked xwindow arrays, in bottom-to-top order
   */
  x11_stacked = g_array_new (FALSE, FALSE, sizeof (Window));

  sorted = meta_stack_list_windows (stack, NULL);

  for (tmp = sorted; tmp; tmp = tmp->next)
    {
      MetaWindow *w = tmp->data;

      if (w->client_type == META_WINDOW_CLIENT_TYPE_X11)
        g_array_append_val (x11_stacked, w->xwindow);
    }

  /* Sync _NET_CLIENT_LIST and _NET_CLIENT_LIST_STACKING */

  XChangeProperty (x11_stack->x11_display->xdisplay,
                   x11_stack->x11_display->xroot,
                   x11_stack->x11_display->atom__NET_CLIENT_LIST,
                   XA_WINDOW,
                   32, PropModeReplace,
                   (unsigned char *) x11_stack->xwindows->data,
                   x11_stack->xwindows->len);
  XChangeProperty (x11_stack->x11_display->xdisplay,
                   x11_stack->x11_display->xroot,
                   x11_stack->x11_display->atom__NET_CLIENT_LIST_STACKING,
                   XA_WINDOW,
                   32, PropModeReplace,
                   (unsigned char *) x11_stacked->data,
                   x11_stacked->len);

  g_array_free (x11_stacked, TRUE);
  g_list_free (sorted);
}

static void
stack_changed_cb (MetaX11Stack *x11_stack)
{
  /* Do removals before adds, with paranoid idea that we might re-add
   * the same window IDs.
   */
  x11_stack_do_window_deletions (x11_stack);
  x11_stack_do_window_additions (x11_stack);
  x11_stack_sync_to_xserver (x11_stack);
}

static void
meta_x11_stack_constructed (GObject *object)
{
  MetaX11Stack *x11_stack = META_X11_STACK (object);
  MetaX11Display *x11_display = x11_stack->x11_display;

  G_OBJECT_CLASS (meta_x11_stack_parent_class)->constructed (object);

  g_signal_connect (x11_display->display->stack,
                    "window-added",
                    G_CALLBACK (stack_window_added_cb),
                    x11_stack);
  g_signal_connect (x11_display->display->stack,
                    "window-removed",
                    G_CALLBACK (stack_window_removed_cb),
                    x11_stack);
  g_signal_connect_swapped (x11_display->display->stack,
                            "changed",
                            G_CALLBACK (stack_changed_cb),
                            x11_stack);
}

static void
meta_x11_stack_finalize (GObject *object)
{
  MetaX11Stack *x11_stack = META_X11_STACK (object);
  MetaX11Display *x11_display = x11_stack->x11_display;

  if (x11_display->display && x11_display->display->stack)
    {
      g_signal_handlers_disconnect_by_data (x11_display->display->stack,
                                            x11_stack);
    }

  g_array_free (x11_stack->xwindows, TRUE);
  g_list_free (x11_stack->added);
  g_list_free (x11_stack->removed);

  G_OBJECT_CLASS (meta_x11_stack_parent_class)->finalize (object);
}

static void
meta_x11_stack_class_init (MetaX11StackClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = meta_x11_stack_set_property;
  object_class->get_property = meta_x11_stack_get_property;
  object_class->constructed = meta_x11_stack_constructed;
  object_class->finalize = meta_x11_stack_finalize;

  pspecs[PROP_DISPLAY] =
    g_param_spec_object ("display",
                         "Display",
                         "Display",
                         META_TYPE_X11_DISPLAY,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, N_PROPS, pspecs);
}

MetaX11Stack *
meta_x11_stack_new (MetaX11Display *x11_display)
{
  return g_object_new (META_TYPE_X11_STACK,
                       "display", x11_display,
                       NULL);
}
