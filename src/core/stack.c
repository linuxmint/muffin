/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * SECTION:stack
 * @short_description: Which windows cover which other windows
 */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2002, 2003 Red Hat, Inc.
 * Copyright (C) 2004 Rob Adams
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

#include "config.h"

#include "core/stack.h"

#include "backends/meta-logical-monitor.h"
#include "cogl/cogl.h"
#include "core/frame.h"
#include "core/main-private.h"
#include "core/meta-workspace-manager-private.h"
#include "core/window-private.h"
#include "meta/group.h"
#include "meta/prefs.h"
#include "meta/workspace.h"
#include "x11/meta-x11-display-private.h"

#define WINDOW_TRANSIENT_FOR_WHOLE_GROUP(w)        \
  (meta_window_has_transient_type (w) && w->transient_for == NULL)

static void meta_window_set_stack_position_no_sync (MetaWindow *window,
                                                    int         position);
static void stack_do_relayer (MetaStack *stack);
static void stack_do_constrain (MetaStack *stack);
static void stack_do_resort (MetaStack *stack);
static void stack_ensure_sorted (MetaStack *stack);

enum
{
  PROP_DISPLAY = 1,
  N_PROPS
};

enum
{
  CHANGED,
  WINDOW_ADDED,
  WINDOW_REMOVED,
  N_SIGNALS
};

static GParamSpec *pspecs[N_PROPS] = { 0 };
static guint signals[N_SIGNALS] = { 0 };

G_DEFINE_TYPE (MetaStack, meta_stack, G_TYPE_OBJECT)

static void
on_stack_changed (MetaStack *stack)
{
  MetaDisplay *display = stack->display;
  GArray *all_root_children_stacked;
  GList *l;
  GArray *hidden_stack_ids;
  GList *sorted;

  meta_topic (META_DEBUG_STACK, "Syncing window stack to server\n");

  all_root_children_stacked = g_array_new (FALSE, FALSE, sizeof (uint64_t));
  hidden_stack_ids = g_array_new (FALSE, FALSE, sizeof (uint64_t));

  meta_topic (META_DEBUG_STACK, "Bottom to top: ");
  meta_push_no_msg_prefix ();

  sorted = meta_stack_list_windows (stack, NULL);

  for (l = sorted; l; l = l->next)
    {
      MetaWindow *w = l->data;
      uint64_t top_level_window;
      uint64_t stack_id;

      if (w->unmanaging)
        continue;

      meta_topic (META_DEBUG_STACK, "%u:%d - %s ",
		  w->layer, w->stack_position, w->desc);

      if (w->frame)
	top_level_window = w->frame->xwindow;
      else
	top_level_window = w->xwindow;

      if (w->client_type == META_WINDOW_CLIENT_TYPE_X11)
        stack_id = top_level_window;
      else
        stack_id = w->stamp;

      /* We don't restack hidden windows along with the rest, though they are
       * reflected in the _NET hints. Hidden windows all get pushed below
       * the screens fullscreen guard_window. */
      if (w->hidden)
	{
          g_array_append_val (hidden_stack_ids, stack_id);
	  continue;
	}

      g_array_append_val (all_root_children_stacked, stack_id);
    }

  meta_topic (META_DEBUG_STACK, "\n");
  meta_pop_no_msg_prefix ();

  if (display->x11_display)
    {
      uint64_t guard_window_id;

      /* The screen guard window sits above all hidden windows and acts as
       * a barrier to input reaching these windows. */
      guard_window_id = display->x11_display->guard_window;
      g_array_append_val (hidden_stack_ids, guard_window_id);
    }

  /* Sync to server */

  meta_topic (META_DEBUG_STACK, "Restacking %u windows\n",
              all_root_children_stacked->len);

  meta_stack_tracker_restack_managed (display->stack_tracker,
                                      (uint64_t *)all_root_children_stacked->data,
                                      all_root_children_stacked->len);
  meta_stack_tracker_restack_at_bottom (display->stack_tracker,
                                        (uint64_t *)hidden_stack_ids->data,
                                        hidden_stack_ids->len);

  g_array_free (hidden_stack_ids, TRUE);
  g_array_free (all_root_children_stacked, TRUE);
  g_list_free (sorted);
}

static void
meta_stack_init (MetaStack *stack)
{
  g_signal_connect (stack, "changed",
                    G_CALLBACK (on_stack_changed), NULL);
}

static void
meta_stack_finalize (GObject *object)
{
  MetaStack *stack = META_STACK (object);

  g_list_free (stack->sorted);

  G_OBJECT_CLASS (meta_stack_parent_class)->finalize (object);
}

static void
meta_stack_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  MetaStack *stack = META_STACK (object);

  switch (prop_id)
    {
    case PROP_DISPLAY:
      stack->display = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_stack_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  MetaStack *stack = META_STACK (object);

  switch (prop_id)
    {
    case PROP_DISPLAY:
      g_value_set_object (value, stack->display);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_stack_class_init (MetaStackClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = meta_stack_set_property;
  object_class->get_property = meta_stack_get_property;
  object_class->finalize = meta_stack_finalize;

  signals[CHANGED] =
    g_signal_new ("changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
  signals[WINDOW_ADDED] =
    g_signal_new ("window-added",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1, META_TYPE_WINDOW);
  signals[WINDOW_REMOVED] =
    g_signal_new ("window-removed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1, META_TYPE_WINDOW);

  pspecs[PROP_DISPLAY] =
    g_param_spec_object ("display",
                         "Display",
                         "Display",
                         META_TYPE_DISPLAY,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, N_PROPS, pspecs);
}

MetaStack *
meta_stack_new (MetaDisplay *display)
{
  return g_object_new (META_TYPE_STACK,
                       "display", display,
                       NULL);
}

static void
meta_stack_changed (MetaStack *stack)
{
  /* Bail out if frozen */
  if (stack->freeze_count > 0)
    return;

  stack_ensure_sorted (stack);
  g_signal_emit (stack, signals[CHANGED], 0);
}

void
meta_stack_add (MetaStack  *stack,
                MetaWindow *window)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;

  COGL_TRACE_BEGIN_SCOPED (MetaStackAdd,
                           "Stack (add window)");

  g_return_if_fail (meta_window_is_stackable (window));

  meta_topic (META_DEBUG_STACK, "Adding window %s to the stack\n", window->desc);

  if (meta_window_is_in_stack (window))
    meta_bug ("Window %s had stack position already\n", window->desc);

  stack->sorted = g_list_prepend (stack->sorted, window);
  stack->need_resort = TRUE; /* may not be needed as we add to top */
  stack->need_constrain = TRUE;
  stack->need_relayer = TRUE;

  g_signal_emit (stack, signals[WINDOW_ADDED], 0, window);

  window->stack_position = stack->n_positions;
  stack->n_positions += 1;
  meta_topic (META_DEBUG_STACK,
              "Window %s has stack_position initialized to %d\n",
              window->desc, window->stack_position);

  meta_stack_changed (stack);
  meta_stack_update_window_tile_matches (stack, workspace_manager->active_workspace);
}

void
meta_stack_remove (MetaStack  *stack,
                   MetaWindow *window)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;

  COGL_TRACE_BEGIN_SCOPED (MetaStackRemove,
                           "Stack (remove window)");

  meta_topic (META_DEBUG_STACK, "Removing window %s from the stack\n", window->desc);

  /* Set window to top position, so removing it will not leave gaps
   * in the set of positions
   */
  meta_window_set_stack_position_no_sync (window,
                                          stack->n_positions - 1);
  window->stack_position = -1;
  stack->n_positions -= 1;

  stack->sorted = g_list_remove (stack->sorted, window);

  g_signal_emit (stack, signals[WINDOW_REMOVED], 0, window);

  meta_stack_changed (stack);
  meta_stack_update_window_tile_matches (stack, workspace_manager->active_workspace);
}

void
meta_stack_update_layer (MetaStack  *stack,
                         MetaWindow *window)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
  stack->need_relayer = TRUE;

  meta_stack_changed (stack);
  meta_stack_update_window_tile_matches (stack, workspace_manager->active_workspace);
}

void
meta_stack_update_transient (MetaStack  *stack,
                             MetaWindow *window)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
  stack->need_constrain = TRUE;

  meta_stack_changed (stack);
  meta_stack_update_window_tile_matches (stack, workspace_manager->active_workspace);
}

/* raise/lower within a layer */
void
meta_stack_raise (MetaStack  *stack,
                  MetaWindow *window)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
  GList *l;
  int max_stack_position = window->stack_position;
  MetaWorkspace *workspace;

  stack_ensure_sorted (stack);

  workspace = meta_window_get_workspace (window);
  for (l = stack->sorted; l; l = l->next)
    {
      MetaWindow *w = (MetaWindow *) l->data;
      if (meta_window_located_on_workspace (w, workspace) &&
          w->stack_position > max_stack_position)
        max_stack_position = w->stack_position;
    }

  if (max_stack_position == window->stack_position)
    return;

  meta_window_set_stack_position_no_sync (window, max_stack_position);

  meta_stack_changed (stack);
  meta_stack_update_window_tile_matches (stack, workspace_manager->active_workspace);
}

void
meta_stack_lower (MetaStack  *stack,
                  MetaWindow *window)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
  GList *l;
  int min_stack_position = window->stack_position;
  MetaWorkspace *workspace;

  stack_ensure_sorted (stack);

  workspace = meta_window_get_workspace (window);
  for (l = stack->sorted; l; l = l->next)
    {
      MetaWindow *w = (MetaWindow *) l->data;
      if (meta_window_located_on_workspace (w, workspace) &&
          w->stack_position < min_stack_position)
        min_stack_position = w->stack_position;
    }

  if (min_stack_position == window->stack_position)
    return;

  meta_window_set_stack_position_no_sync (window, min_stack_position);

  meta_stack_changed (stack);
  meta_stack_update_window_tile_matches (stack, workspace_manager->active_workspace);
}

void
meta_stack_freeze (MetaStack *stack)
{
  stack->freeze_count += 1;
}

void
meta_stack_thaw (MetaStack *stack)
{
  g_return_if_fail (stack->freeze_count > 0);

  stack->freeze_count -= 1;
  meta_stack_changed (stack);
  meta_stack_update_window_tile_matches (stack, NULL);
}

void
meta_stack_update_window_tile_matches (MetaStack     *stack,
                                       MetaWorkspace *workspace)
{
  GList *windows, *tmp;

  if (stack->freeze_count > 0)
    return;

  windows = meta_stack_list_windows (stack, workspace);
  tmp = windows;
  while (tmp)
    {
      meta_window_compute_tile_match ((MetaWindow *) tmp->data);
      tmp = tmp->next;
    }

  g_list_free (windows);
}

/* Front of the layer list is the topmost window,
 * so the lower stack position is later in the list
 */
static int
compare_window_position (void *a,
                         void *b)
{
  MetaWindow *window_a = a;
  MetaWindow *window_b = b;

  /* Go by layer, then stack_position */
  if (window_a->layer < window_b->layer)
    return 1; /* move window_a later in list */
  else if (window_a->layer > window_b->layer)
    return -1;
  else if (window_a->stack_position < window_b->stack_position)
    return 1; /* move window_a later in list */
  else if (window_a->stack_position > window_b->stack_position)
    return -1;
  else
    return 0; /* not reached */
}

/*
 * Stacking constraints
 *
 * Assume constraints of the form "AB" meaning "window A must be
 * below window B"
 *
 * If we have windows stacked from bottom to top
 * "ABC" then raise A we get "BCA". Say C is
 * transient for B is transient for A. So
 * we have constraints AB and BC.
 *
 * After raising A, we need to reapply the constraints.
 * If we do this by raising one window at a time -
 *
 *  start:    BCA
 *  apply AB: CAB
 *  apply BC: ABC
 *
 * but apply constraints in the wrong order and it breaks:
 *
 *  start:    BCA
 *  apply BC: BCA
 *  apply AB: CAB
 *
 * We make a directed graph of the constraints by linking
 * from "above windows" to "below windows as follows:
 *
 *   AB -> BC -> CD
 *          \
 *           CE
 *
 * If we then walk that graph and apply the constraints in the order
 * that they appear, we will apply them correctly. Note that the
 * graph MAY have cycles, so we have to guard against that.
 *
 */

typedef struct Constraint Constraint;

struct Constraint
{
  MetaWindow *above;
  MetaWindow *below;

  /* used to keep the constraint in the
   * list of constraints for window "below"
   */
  Constraint *next;

  /* used to create the graph. */
  GSList *next_nodes;

  /* constraint has been applied, used
   * to detect cycles.
   */
  unsigned int applied : 1;

  /* constraint has a previous node in the graph,
   * used to find places to start in the graph.
   * (I think this also has the side effect
   * of preventing cycles, since cycles will
   * have no starting point - so maybe
   * the "applied" flag isn't needed.)
   */
  unsigned int has_prev : 1;
};

/* We index the array of constraints by window
 * stack positions, just because the stack
 * positions are a convenient index.
 */
static void
add_constraint (Constraint **constraints,
                MetaWindow  *above,
                MetaWindow  *below)
{
  Constraint *c;

  /* check if constraint is a duplicate */
  c = constraints[below->stack_position];
  while (c != NULL)
    {
      if (c->above == above)
        return;
      c = c->next;
    }

  /* if not, add the constraint */
  c = g_new (Constraint, 1);
  c->above = above;
  c->below = below;
  c->next = constraints[below->stack_position];
  c->next_nodes = NULL;
  c->applied = FALSE;
  c->has_prev = FALSE;

  constraints[below->stack_position] = c;
}

static void
create_constraints (Constraint **constraints,
                    GList       *windows)
{
  GList *tmp;

  tmp = windows;
  while (tmp != NULL)
    {
      MetaWindow *w = tmp->data;

      if (!meta_window_is_in_stack (w))
        {
          meta_topic (META_DEBUG_STACK, "Window %s not in the stack, not constraining it\n",
                      w->desc);
          tmp = tmp->next;
          continue;
        }

      if (WINDOW_TRANSIENT_FOR_WHOLE_GROUP (w))
        {
          GSList *group_windows;
          GSList *tmp2;
          MetaGroup *group;

          group = meta_window_get_group (w);

          if (group != NULL)
            group_windows = meta_group_list_windows (group);
          else
            group_windows = NULL;

          tmp2 = group_windows;

          while (tmp2 != NULL)
            {
              MetaWindow *group_window = tmp2->data;

              if (!meta_window_is_in_stack (group_window) ||
                  group_window->override_redirect)
                {
                  tmp2 = tmp2->next;
                  continue;
                }

#if 0
              /* old way of doing it */
              if (!(meta_window_is_ancestor_of_transient (w, group_window)) &&
                  !WINDOW_TRANSIENT_FOR_WHOLE_GROUP (group_window))  /* note */;/*note*/
#else
              /* better way I think, so transient-for-group are constrained
               * only above non-transient-type windows in their group
               */
              if (!meta_window_has_transient_type (group_window))
#endif
                {
                  meta_topic (META_DEBUG_STACK, "Constraining %s above %s as it's transient for its group\n",
                              w->desc, group_window->desc);
                  add_constraint (constraints, w, group_window);
                }

              tmp2 = tmp2->next;
            }

          g_slist_free (group_windows);
        }
      else if (w->transient_for != NULL)
        {
          MetaWindow *parent;

          parent = w->transient_for;

          if (parent && meta_window_is_in_stack (parent))
            {
              meta_topic (META_DEBUG_STACK, "Constraining %s above %s due to transiency\n",
                          w->desc, parent->desc);
              add_constraint (constraints, w, parent);
            }
        }

      tmp = tmp->next;
    }
}

static void
graph_constraints (Constraint **constraints,
                   int          n_constraints)
{
  int i;

  i = 0;
  while (i < n_constraints)
    {
      Constraint *c;

      /* If we have "A below B" and "B below C" then AB -> BC so we
       * add BC to next_nodes in AB.
       */

      c = constraints[i];
      while (c != NULL)
        {
          Constraint *n;

          g_assert (c->below->stack_position == i);

          /* Constraints where ->above is below are our
           * next_nodes and we are their previous
           */
          n = constraints[c->above->stack_position];
          while (n != NULL)
            {
              c->next_nodes = g_slist_prepend (c->next_nodes,
                                               n);
              /* c is a previous node of n */
              n->has_prev = TRUE;

              n = n->next;
            }

          c = c->next;
        }

      ++i;
    }
}

static void
free_constraints (Constraint **constraints,
                  int          n_constraints)
{
  int i;

  i = 0;
  while (i < n_constraints)
    {
      Constraint *c;

      c = constraints[i];
      while (c != NULL)
        {
          Constraint *next = c->next;

          g_slist_free (c->next_nodes);

          g_free (c);

          c = next;
        }

      ++i;
    }
}

static void
ensure_above (MetaWindow *above,
              MetaWindow *below)
{
  gboolean is_transient;

  is_transient = meta_window_has_transient_type (above) ||
                 above->transient_for == below;
  if (is_transient && above->layer < below->layer)
    {
      meta_topic (META_DEBUG_STACK,
		  "Promoting window %s from layer %u to %u due to contraint\n",
		  above->desc, above->layer, below->layer);
      above->layer = below->layer;
    }

  if (above->stack_position < below->stack_position)
    {
      /* move above to below->stack_position bumping below down the stack */
      meta_window_set_stack_position_no_sync (above, below->stack_position);
      g_assert (below->stack_position + 1 == above->stack_position);
    }
  meta_topic (META_DEBUG_STACK, "%s above at %d > %s below at %d\n",
              above->desc, above->stack_position,
              below->desc, below->stack_position);
}

static void
traverse_constraint (Constraint *c)
{
  GSList *tmp;

  if (c->applied)
    return;

  ensure_above (c->above, c->below);
  c->applied = TRUE;

  tmp = c->next_nodes;
  while (tmp != NULL)
    {
      traverse_constraint (tmp->data);

      tmp = tmp->next;
    }
}

static void
apply_constraints (Constraint **constraints,
                   int          n_constraints)
{
  GSList *heads;
  GSList *tmp;
  int i;

  /* List all heads in an ordered constraint chain */
  heads = NULL;
  i = 0;
  while (i < n_constraints)
    {
      Constraint *c;

      c = constraints[i];
      while (c != NULL)
        {
          if (!c->has_prev)
            heads = g_slist_prepend (heads, c);

          c = c->next;
        }

      ++i;
    }

  /* Now traverse the chain and apply constraints */
  tmp = heads;
  while (tmp != NULL)
    {
      Constraint *c = tmp->data;

      traverse_constraint (c);

      tmp = tmp->next;
    }

  g_slist_free (heads);
}

/**
 * stack_do_relayer:
 *
 * Update the layers that windows are in
 */
static void
stack_do_relayer (MetaStack *stack)
{
  GList *tmp;

  if (!stack->need_relayer)
    return;

  meta_topic (META_DEBUG_STACK,
              "Recomputing layers\n");

  tmp = stack->sorted;

  while (tmp != NULL)
    {
      MetaWindow *w;
      MetaStackLayer old_layer;

      w = tmp->data;
      old_layer = w->layer;

      w->layer = meta_window_calculate_layer (w);

      if (w->layer != old_layer)
        {
          meta_topic (META_DEBUG_STACK,
                      "Window %s moved from layer %u to %u\n",
                      w->desc, old_layer, w->layer);
          stack->need_resort = TRUE;
          stack->need_constrain = TRUE;
          /* don't need to constrain as constraining
           * purely operates in terms of stack_position
           * not layer
           */
        }

      tmp = tmp->next;
    }

  stack->need_relayer = FALSE;
}

/**
 * stack_do_constrain:
 *
 * Update stack_position and layer to reflect transiency
 * constraints
 */
static void
stack_do_constrain (MetaStack *stack)
{
  Constraint **constraints;

  /* It'd be nice if this were all faster, probably */

  if (!stack->need_constrain)
    return;

  meta_topic (META_DEBUG_STACK,
              "Reapplying constraints\n");

  constraints = g_new0 (Constraint*,
                        stack->n_positions);

  create_constraints (constraints, stack->sorted);

  graph_constraints (constraints, stack->n_positions);

  apply_constraints (constraints, stack->n_positions);

  free_constraints (constraints, stack->n_positions);
  g_free (constraints);

  stack->need_constrain = FALSE;
}

/**
 * stack_do_resort:
 *
 * Sort stack->sorted with layers having priority over stack_position.
 */
static void
stack_do_resort (MetaStack *stack)
{
  if (!stack->need_resort)
    return;

  meta_topic (META_DEBUG_STACK,
              "Sorting stack list\n");

  stack->sorted = g_list_sort (stack->sorted,
                               (GCompareFunc) compare_window_position);

  meta_display_queue_check_fullscreen (stack->display);

  stack->need_resort = FALSE;
}

/**
 * stack_ensure_sorted:
 *
 * Puts the stack into canonical form.
 *
 * Honour the removed and added lists of the stack, and then recalculate
 * all the layers (if the flag is set), re-run all the constraint calculations
 * (if the flag is set), and finally re-sort the stack (if the flag is set,
 * and if it wasn't already it might have become so during all the previous
 * activity).
 */
static void
stack_ensure_sorted (MetaStack *stack)
{
  stack_do_relayer (stack);
  stack_do_constrain (stack);
  stack_do_resort (stack);
}

MetaWindow *
meta_stack_get_top (MetaStack *stack)
{
  stack_ensure_sorted (stack);

  if (stack->sorted)
    return stack->sorted->data;
  else
    return NULL;
}

MetaWindow *
meta_stack_get_bottom (MetaStack  *stack)
{
  GList *link;

  stack_ensure_sorted (stack);

  link = g_list_last (stack->sorted);
  if (link != NULL)
    return link->data;
  else
    return NULL;
}

MetaWindow *
meta_stack_get_above (MetaStack  *stack,
                      MetaWindow *window,
                      gboolean    only_within_layer)
{
  GList *link;
  MetaWindow *above;

  stack_ensure_sorted (stack);

  link = g_list_find (stack->sorted, window);
  if (link == NULL)
    return NULL;
  if (link->prev == NULL)
    return NULL;

  above = link->prev->data;

  if (only_within_layer &&
      above->layer != window->layer)
    return NULL;
  else
    return above;
}

MetaWindow *
meta_stack_get_below (MetaStack  *stack,
                      MetaWindow *window,
                      gboolean    only_within_layer)
{
  GList *link;
  MetaWindow *below;

  stack_ensure_sorted (stack);

  link = g_list_find (stack->sorted, window);

  if (link == NULL)
    return NULL;
  if (link->next == NULL)
    return NULL;

  below = link->next->data;

  if (only_within_layer &&
      below->layer != window->layer)
    return NULL;
  else
    return below;
}

static gboolean
window_contains_point (MetaWindow *window,
                       int         root_x,
                       int         root_y)
{
  MetaRectangle rect;

  meta_window_get_frame_rect (window, &rect);

  return META_POINT_IN_RECT (root_x, root_y, rect);
}

static gboolean
window_can_get_default_focus (MetaWindow *window)
{
  if (window->unmaps_pending > 0)
    return FALSE;

  if (window->unmanaging)
    return FALSE;

  if (!meta_window_is_focusable (window))
    return FALSE;

  if (!meta_window_should_be_showing (window))
    return FALSE;

  if (window->type == META_WINDOW_DOCK)
    return FALSE;

  return TRUE;
}

static MetaWindow *
get_default_focus_window (MetaStack     *stack,
                          MetaWorkspace *workspace,
                          MetaWindow    *not_this_one,
                          gboolean       must_be_at_point,
                          int            root_x,
                          int            root_y)
{
  /* Find the topmost, focusable, mapped, window.
   * not_this_one is being unfocused or going away, so exclude it.
   */

  GList *l;

  stack_ensure_sorted (stack);

  /* top of this layer is at the front of the list */
  for (l = stack->sorted; l != NULL; l = l->next)
    {
      MetaWindow *window = l->data;

      if (!window)
        continue;

      if (window == not_this_one)
        continue;

      if (!window_can_get_default_focus (window))
        continue;

      if (must_be_at_point && !window_contains_point (window, root_x, root_y))
        continue;

      return window;
    }

  return NULL;
}

MetaWindow *
meta_stack_get_default_focus_window_at_point (MetaStack     *stack,
                                              MetaWorkspace *workspace,
                                              MetaWindow    *not_this_one,
                                              int            root_x,
                                              int            root_y)
{
  return get_default_focus_window (stack, workspace, not_this_one,
                                   TRUE, root_x, root_y);
}

MetaWindow *
meta_stack_get_default_focus_window (MetaStack     *stack,
                                     MetaWorkspace *workspace,
                                     MetaWindow    *not_this_one)
{
  return get_default_focus_window (stack, workspace, not_this_one,
                                   FALSE, 0, 0);
}

GList *
meta_stack_list_windows (MetaStack     *stack,
                         MetaWorkspace *workspace)
{
  GList *workspace_windows = NULL;
  GList *link;

  stack_ensure_sorted (stack); /* do adds/removes */

  link = stack->sorted;

  while (link)
    {
      MetaWindow *window = link->data;

      if (window &&
          (workspace == NULL || meta_window_located_on_workspace (window, workspace)))
        {
          workspace_windows = g_list_prepend (workspace_windows,
                                              window);
        }

      link = link->next;
    }

  return workspace_windows;
}

GList *
meta_stack_get_default_focus_candidates (MetaStack     *stack,
                                         MetaWorkspace *workspace)
{
  GList *windows = meta_stack_list_windows (stack, workspace);
  GList *l;

  for (l = windows; l;)
    {
      GList *next = l->next;

      if (!window_can_get_default_focus (l->data))
        windows = g_list_delete_link (windows, l);

      l = next;
    }

  return windows;
}

int
meta_stack_windows_cmp (MetaStack  *stack,
                        MetaWindow *window_a,
                        MetaWindow *window_b)
{
  /* -1 means a below b */

  stack_ensure_sorted (stack); /* update constraints, layers */

  if (window_a->layer < window_b->layer)
    return -1;
  else if (window_a->layer > window_b->layer)
    return 1;
  else if (window_a->stack_position < window_b->stack_position)
    return -1;
  else if (window_a->stack_position > window_b->stack_position)
    return 1;
  else
    return 0; /* not reached */
}

static int
compare_just_window_stack_position (void *a,
                                    void *b)
{
  MetaWindow *window_a = a;
  MetaWindow *window_b = b;

  if (window_a->stack_position < window_b->stack_position)
    return -1; /* move window_a earlier in list */
  else if (window_a->stack_position > window_b->stack_position)
    return 1;
  else
    return 0; /* not reached */
}

GList *
meta_stack_get_positions (MetaStack *stack)
{
  GList *tmp;

  /* Make sure to handle any adds or removes */
  stack_ensure_sorted (stack);

  tmp = g_list_copy (stack->sorted);
  tmp = g_list_sort (tmp, (GCompareFunc) compare_just_window_stack_position);

  return tmp;
}

static gint
compare_pointers (gconstpointer a,
                  gconstpointer b)
{
  if (a > b)
    return 1;
  else if (a < b)
    return -1;
  else
    return 0;
}

static gboolean
lists_contain_same_windows (GList *a,
                            GList *b)
{
  GList *copy1, *copy2;
  GList *tmp1, *tmp2;

  if (g_list_length (a) != g_list_length (b))
    return FALSE;

  tmp1 = copy1 = g_list_sort (g_list_copy (a), compare_pointers);
  tmp2 = copy2 = g_list_sort (g_list_copy (b), compare_pointers);

  while (tmp1 && tmp1->data == tmp2->data)   /* tmp2 is non-NULL if tmp1 is */
    {
      tmp1 = tmp1->next;
      tmp2 = tmp2->next;
    }

  g_list_free (copy1);
  g_list_free (copy2);

  return (tmp1 == NULL);    /* tmp2 is non-NULL if tmp1 is */
}

void
meta_stack_set_positions (MetaStack *stack,
                          GList     *windows)
{
  int i;
  GList *tmp;

  /* Make sure any adds or removes aren't in limbo -- is this needed? */
  stack_ensure_sorted (stack);

  if (!lists_contain_same_windows (windows, stack->sorted))
    {
      meta_warning ("This list of windows has somehow changed; not resetting "
                    "positions of the windows.\n");
      return;
    }

  g_list_free (stack->sorted);
  stack->sorted = g_list_copy (windows);

  stack->need_resort = TRUE;
  stack->need_constrain = TRUE;

  i = 0;
  tmp = windows;
  while (tmp != NULL)
    {
      MetaWindow *w = tmp->data;
      w->stack_position = i++;
      tmp = tmp->next;
    }

  meta_topic (META_DEBUG_STACK,
              "Reset the stack positions of (nearly) all windows\n");

  meta_stack_changed (stack);
  meta_stack_update_window_tile_matches (stack, NULL);
}

void
meta_window_set_stack_position_no_sync (MetaWindow *window,
                                        int         position)
{
  int low, high, delta;
  GList *tmp;

  g_return_if_fail (window->display->stack != NULL);
  g_return_if_fail (window->stack_position >= 0);
  g_return_if_fail (position >= 0);
  g_return_if_fail (position < window->display->stack->n_positions);

  if (position == window->stack_position)
    {
      meta_topic (META_DEBUG_STACK, "Window %s already has position %d\n",
                  window->desc, position);
      return;
    }

  window->display->stack->need_resort = TRUE;
  window->display->stack->need_constrain = TRUE;

  if (position < window->stack_position)
    {
      low = position;
      high = window->stack_position - 1;
      delta = 1;
    }
  else
    {
      low = window->stack_position + 1;
      high = position;
      delta = -1;
    }

  tmp = window->display->stack->sorted;
  while (tmp != NULL)
    {
      MetaWindow *w = tmp->data;

      if (w->stack_position >= low &&
          w->stack_position <= high)
        w->stack_position += delta;

      tmp = tmp->next;
    }

  window->stack_position = position;

  meta_topic (META_DEBUG_STACK,
              "Window %s had stack_position set to %d\n",
              window->desc, window->stack_position);
}

void
meta_window_set_stack_position (MetaWindow *window,
                                int         position)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;

  meta_window_set_stack_position_no_sync (window, position);
  meta_stack_changed (window->display->stack);
  meta_stack_update_window_tile_matches (window->display->stack,
                                         workspace_manager->active_workspace);
}
