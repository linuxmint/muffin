/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter size/position constraints */

/*
 * Copyright (C) 2002, 2003 Red Hat, Inc.
 * Copyright (C) 2003, 2004 Rob Adams
 * Copyright (C) 2005, 2006 Elijah Newren
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

#include "core/constraints.h"

#include <stdlib.h>
#include <math.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor-manager-private.h"
#include "core/boxes-private.h"
#include "core/meta-workspace-manager-private.h"
#include "core/place.h"
#include "core/workspace-private.h"
#include "meta/prefs.h"

#if 0
 // This is the short and sweet version of how to hack on this file; see
 // doc/how-constraints-works.txt for the gory details.  The basics of
 // understanding this file can be shown by the steps needed to add a new
 // constraint, which are:
 //   1) Add a new entry in the ConstraintPriority enum; higher values
 //      have higher priority
 //   2) Write a new function following the format of the example below,
 //      "constrain_whatever".
 //   3) Add your function to the all_constraints and all_constraint_names
 //      arrays (the latter of which is for debugging purposes)
 //
 // An example constraint function, constrain_whatever:
 //
 // /* constrain_whatever does the following:
 //  *   Quits (returning true) if priority is higher than PRIORITY_WHATEVER
 //  *   If check_only is TRUE
 //  *     Returns whether the constraint is satisfied or not
 //  *   otherwise
 //  *     Enforces the constraint
 //  * Note that the value of PRIORITY_WHATEVER is centralized with the
 //  * priorities of other constraints in the definition of ConstrainPriority
 //  * for easier maintenance and shuffling of priorities.
 //  */
 // static gboolean
 // constrain_whatever (MetaWindow         *window,
 //                     ConstraintInfo     *info,
 //                     ConstraintPriority  priority,
 //                     gboolean            check_only)
 // {
 //   if (priority > PRIORITY_WHATEVER)
 //     return TRUE;
 //
 //   /* Determine whether constraint applies; note that if the constraint
 //    * cannot possibly be satisfied, constraint_applies should be set to
 //    * false.  If we don't do this, all constraints with a lesser priority
 //    * will be dropped along with this one, and we'd rather apply as many as
 //    * possible.
 //    */
 //   if (!constraint_applies)
 //     return TRUE;
 //
 //   /* Determine whether constraint is already satisfied; if we're only
 //    * checking the status of whether the constraint is satisfied, we end
 //    * here.
 //    */
 //   if (check_only || constraint_already_satisfied)
 //     return constraint_already_satisfied;
 //
 //   /* Enforce constraints */
 //   return TRUE;  /* Note that we exited early if check_only is FALSE; also,
 //                  * we know we can return TRUE here because we exited early
 //                  * if the constraint could not be satisfied; not that the
 //                  * return value is heeded in this case...
 //                  */
 // }
#endif

typedef enum
{
  PRIORITY_MINIMUM = 0, /* Dummy value used for loop start = min(all priorities) */
  PRIORITY_ASPECT_RATIO = 0,
  PRIORITY_ENTIRELY_VISIBLE_ON_SINGLE_MONITOR = 0,
  PRIORITY_ENTIRELY_VISIBLE_ON_WORKAREA = 1,
  PRIORITY_SIZE_HINTS_INCREMENTS = 1,
  PRIORITY_MAXIMIZATION = 2,
  PRIORITY_TILING = 2,
  PRIORITY_FULLSCREEN = 2,
  PRIORITY_SIZE_HINTS_LIMITS = 3,
  PRIORITY_TITLEBAR_VISIBLE = 4,
  PRIORITY_PARTIALLY_VISIBLE_ON_WORKAREA = 4,
  PRIORITY_CUSTOM_RULE = 4,
  PRIORITY_MAXIMUM = 4 /* Dummy value used for loop end = max(all priorities) */
} ConstraintPriority;

typedef enum
{
  ACTION_MOVE,
  ACTION_RESIZE,
  ACTION_MOVE_AND_RESIZE
} ActionType;

typedef struct
{
  MetaRectangle        orig;
  MetaRectangle        current;
  MetaRectangle        temporary;
  int                  rel_x;
  int                  rel_y;
  ActionType           action_type;
  gboolean             is_user_action;

  /* I know that these two things probably look similar at first, but they
   * have much different uses.  See doc/how-constraints-works.txt for for
   * explanation of the differences and similarity between resize_gravity
   * and fixed_directions
   */
  MetaGravity          resize_gravity;
  FixedDirections      fixed_directions;

  /* work_area_monitor - current monitor region minus struts
   * entire_monitor    - current monitor, including strut regions
   */
  MetaRectangle        work_area_monitor;
  MetaRectangle        entire_monitor;

  /* Spanning rectangles for the non-covered (by struts) region of the
   * screen and also for just the current monitor
   */
  GList  *usable_screen_region;
  GList  *usable_monitor_region;

  MetaMoveResizeFlags  flags;
} ConstraintInfo;

static gboolean do_screen_and_monitor_relative_constraints (MetaWindow     *window,
                                                            GList          *region_spanning_rectangles,
                                                            ConstraintInfo *info,
                                                            gboolean        check_only);
static gboolean constrain_custom_rule        (MetaWindow         *window,
                                              ConstraintInfo     *info,
                                              ConstraintPriority  priority,
                                              gboolean            check_only);
static gboolean constrain_modal_dialog       (MetaWindow         *window,
                                              ConstraintInfo     *info,
                                              ConstraintPriority  priority,
                                              gboolean            check_only);
static gboolean constrain_maximization       (MetaWindow         *window,
                                              ConstraintInfo     *info,
                                              ConstraintPriority  priority,
                                              gboolean            check_only);
static gboolean constrain_tiling             (MetaWindow         *window,
                                              ConstraintInfo     *info,
                                              ConstraintPriority  priority,
                                              gboolean            check_only);
static gboolean constrain_fullscreen         (MetaWindow         *window,
                                              ConstraintInfo     *info,
                                              ConstraintPriority  priority,
                                              gboolean            check_only);
static gboolean constrain_size_increments    (MetaWindow         *window,
                                              ConstraintInfo     *info,
                                              ConstraintPriority  priority,
                                              gboolean            check_only);
static gboolean constrain_size_limits        (MetaWindow         *window,
                                              ConstraintInfo     *info,
                                              ConstraintPriority  priority,
                                              gboolean            check_only);
static gboolean constrain_aspect_ratio       (MetaWindow         *window,
                                              ConstraintInfo     *info,
                                              ConstraintPriority  priority,
                                              gboolean            check_only);
static gboolean constrain_to_single_monitor  (MetaWindow         *window,
                                              ConstraintInfo     *info,
                                              ConstraintPriority  priority,
                                              gboolean            check_only);
static gboolean constrain_fully_onscreen     (MetaWindow         *window,
                                              ConstraintInfo     *info,
                                              ConstraintPriority  priority,
                                              gboolean            check_only);
static gboolean constrain_titlebar_visible   (MetaWindow         *window,
                                              ConstraintInfo     *info,
                                              ConstraintPriority  priority,
                                              gboolean            check_only);
static gboolean constrain_partially_onscreen (MetaWindow         *window,
                                              ConstraintInfo     *info,
                                              ConstraintPriority  priority,
                                              gboolean            check_only);

static void setup_constraint_info        (ConstraintInfo      *info,
                                          MetaWindow          *window,
                                          MetaMoveResizeFlags  flags,
                                          MetaGravity          resize_gravity,
                                          const MetaRectangle *orig,
                                          MetaRectangle       *new);
static void place_window_if_needed       (MetaWindow     *window,
                                          ConstraintInfo *info);
static void update_onscreen_requirements (MetaWindow     *window,
                                          ConstraintInfo *info);

typedef gboolean (* ConstraintFunc) (MetaWindow         *window,
                                     ConstraintInfo     *info,
                                     ConstraintPriority  priority,
                                     gboolean            check_only);

typedef struct {
  ConstraintFunc func;
  const char* name;
} Constraint;

static const Constraint all_constraints[] = {
  {constrain_custom_rule,        "constrain_custom_rule"},
  {constrain_modal_dialog,       "constrain_modal_dialog"},
  {constrain_maximization,       "constrain_maximization"},
  {constrain_tiling,             "constrain_tiling"},
  {constrain_fullscreen,         "constrain_fullscreen"},
  {constrain_size_increments,    "constrain_size_increments"},
  {constrain_size_limits,        "constrain_size_limits"},
  {constrain_aspect_ratio,       "constrain_aspect_ratio"},
  {constrain_to_single_monitor,  "constrain_to_single_monitor"},
  {constrain_fully_onscreen,     "constrain_fully_onscreen"},
  {constrain_titlebar_visible,   "constrain_titlebar_visible"},
  {constrain_partially_onscreen, "constrain_partially_onscreen"},
  {NULL,                         NULL}
};

static gboolean
do_all_constraints (MetaWindow         *window,
                    ConstraintInfo     *info,
                    ConstraintPriority  priority,
                    gboolean            check_only)
{
  const Constraint *constraint;
  gboolean          satisfied;

  constraint = &all_constraints[0];
  satisfied = TRUE;
  while (constraint->func != NULL)
    {
      satisfied = satisfied &&
                  (*constraint->func) (window, info, priority, check_only);

      if (!check_only)
        {
          /* Log how the constraint modified the position */
          meta_topic (META_DEBUG_GEOMETRY,
                      "info->current is %d,%d +%d,%d after %s\n",
                      info->current.x, info->current.y,
                      info->current.width, info->current.height,
                      constraint->name);
        }
      else if (!satisfied)
        {
          /* Log which constraint was not satisfied */
          meta_topic (META_DEBUG_GEOMETRY,
                      "constraint %s not satisfied.\n",
                      constraint->name);
          return FALSE;
        }
      ++constraint;
    }

  return TRUE;
}

void
meta_window_constrain (MetaWindow          *window,
                       MetaMoveResizeFlags  flags,
                       MetaGravity          resize_gravity,
                       const MetaRectangle *orig,
                       MetaRectangle       *new,
                       MetaRectangle       *temporary,
                       int                 *rel_x,
                       int                 *rel_y)
{
  ConstraintInfo info;
  ConstraintPriority priority = PRIORITY_MINIMUM;
  gboolean satisfied = FALSE;

  meta_topic (META_DEBUG_GEOMETRY,
              "Constraining %s in move from %d,%d %dx%d to %d,%d %dx%d\n",
              window->desc,
              orig->x, orig->y, orig->width, orig->height,
              new->x,  new->y,  new->width,  new->height);

  setup_constraint_info (&info,
                         window,
                         flags,
                         resize_gravity,
                         orig,
                         new);
  place_window_if_needed (window, &info);

  while (!satisfied && priority <= PRIORITY_MAXIMUM) {
    gboolean check_only = TRUE;

    /* Individually enforce all the high-enough priority constraints */
    do_all_constraints (window, &info, priority, !check_only);

    /* Check if all high-enough priority constraints are simultaneously
     * satisfied
     */
    satisfied = do_all_constraints (window, &info, priority, check_only);

    /* Drop the least important constraints if we can't satisfy them all */
    priority++;
  }

  /* Make sure we use the constrained position */
  *new = info.current;
  *temporary = info.temporary;
  *rel_x = info.rel_x;
  *rel_y = info.rel_y;

  /* We may need to update window->require_fully_onscreen,
   * window->require_on_single_monitor, and perhaps other quantities
   * if this was a user move or user move-and-resize operation.
   */
  update_onscreen_requirements (window, &info);
}

static void
setup_constraint_info (ConstraintInfo      *info,
                       MetaWindow          *window,
                       MetaMoveResizeFlags  flags,
                       MetaGravity          resize_gravity,
                       const MetaRectangle *orig,
                       MetaRectangle       *new)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaLogicalMonitor *logical_monitor;
  MetaWorkspace *cur_workspace;

  info->orig    = *orig;
  info->current = *new;
  info->temporary = *orig;
  info->rel_x = 0;
  info->rel_y = 0;
  info->flags = flags;

  if (info->current.width < 1)
    info->current.width = 1;
  if (info->current.height < 1)
    info->current.height = 1;

  if (flags & META_MOVE_RESIZE_MOVE_ACTION && flags & META_MOVE_RESIZE_RESIZE_ACTION)
    info->action_type = ACTION_MOVE_AND_RESIZE;
  else if (flags & META_MOVE_RESIZE_RESIZE_ACTION)
    info->action_type = ACTION_RESIZE;
  else if (flags & META_MOVE_RESIZE_MOVE_ACTION)
    info->action_type = ACTION_MOVE;
  else
    g_error ("BAD, BAD developer!  No treat for you!  (Fix your calls to "
             "meta_window_move_resize_internal()).\n");

  info->is_user_action = (flags & META_MOVE_RESIZE_USER_ACTION);

  info->resize_gravity = resize_gravity;

  /* FIXME: fixed_directions might be more sane if we (a) made it
   * depend on the grab_op type instead of current amount of movement
   * (thus implying that it only has effect when user_action is true,
   * and (b) ignored it for aspect ratio windows -- at least in those
   * cases where both directions do actually change size.
   */
  info->fixed_directions = FIXED_DIRECTION_NONE;
  /* If x directions don't change but either y direction does */
  if ( orig->x == new->x && orig->x + orig->width  == new->x + new->width   &&
      (orig->y != new->y || orig->y + orig->height != new->y + new->height))
    {
      info->fixed_directions = FIXED_DIRECTION_X;
    }
  /* If y directions don't change but either x direction does */
  if ( orig->y == new->y && orig->y + orig->height == new->y + new->height  &&
      (orig->x != new->x || orig->x + orig->width  != new->x + new->width ))
    {
      info->fixed_directions = FIXED_DIRECTION_Y;
    }
  /* The point of fixed directions is just that "move to nearest valid
   * position" is sometimes a poorer choice than "move to nearest
   * valid position but only change this coordinate" for windows the
   * user is explicitly moving.  This isn't ever true for things that
   * aren't explicit user interaction, though, so just clear it out.
   */
  if (!info->is_user_action)
    info->fixed_directions = FIXED_DIRECTION_NONE;

  logical_monitor =
    meta_monitor_manager_get_logical_monitor_from_rect (monitor_manager,
                                                        &info->current);
  meta_window_get_work_area_for_logical_monitor (window,
                                                 logical_monitor,
                                                 &info->work_area_monitor);

  if (window->fullscreen && meta_window_has_fullscreen_monitors (window))
    {
      info->entire_monitor = window->fullscreen_monitors.top->rect;
      meta_rectangle_union (&info->entire_monitor,
                            &window->fullscreen_monitors.bottom->rect,
                            &info->entire_monitor);
      meta_rectangle_union (&info->entire_monitor,
                            &window->fullscreen_monitors.left->rect,
                            &info->entire_monitor);
      meta_rectangle_union (&info->entire_monitor,
                            &window->fullscreen_monitors.right->rect,
                            &info->entire_monitor);
    }
  else
    {
      info->entire_monitor = logical_monitor->rect;
      if (window->fullscreen)
        meta_window_adjust_fullscreen_monitor_rect (window, &info->entire_monitor);
    }

  cur_workspace = window->display->workspace_manager->active_workspace;
  info->usable_screen_region   =
    meta_workspace_get_onscreen_region (cur_workspace);
  info->usable_monitor_region =
    meta_workspace_get_onmonitor_region (cur_workspace, logical_monitor);

  /* Log all this information for debugging */
  meta_topic (META_DEBUG_GEOMETRY,
              "Setting up constraint info:\n"
              "  orig: %d,%d +%d,%d\n"
              "  new : %d,%d +%d,%d\n"
              "  action_type     : %s\n"
              "  is_user_action  : %s\n"
              "  resize_gravity  : %s\n"
              "  fixed_directions: %s\n"
              "  work_area_monitor: %d,%d +%d,%d\n"
              "  entire_monitor   : %d,%d +%d,%d\n",
              info->orig.x, info->orig.y, info->orig.width, info->orig.height,
              info->current.x, info->current.y,
                info->current.width, info->current.height,
              (info->action_type == ACTION_MOVE) ? "Move" :
                (info->action_type == ACTION_RESIZE) ? "Resize" :
                (info->action_type == ACTION_MOVE_AND_RESIZE) ? "Move&Resize" :
                "Freakin' Invalid Stupid",
              (info->is_user_action) ? "true" : "false",
              meta_gravity_to_string (info->resize_gravity),
              (info->fixed_directions == FIXED_DIRECTION_NONE) ? "None" :
                (info->fixed_directions == FIXED_DIRECTION_X) ? "X fixed" :
                (info->fixed_directions == FIXED_DIRECTION_Y) ? "Y fixed" :
                "Freakin' Invalid Stupid",
              info->work_area_monitor.x, info->work_area_monitor.y,
                info->work_area_monitor.width,
                info->work_area_monitor.height,
              info->entire_monitor.x, info->entire_monitor.y,
                info->entire_monitor.width, info->entire_monitor.height);
}

static MetaRectangle *
get_start_rect_for_resize (MetaWindow     *window,
                           ConstraintInfo *info)
{
  if (!info->is_user_action && info->action_type == ACTION_MOVE_AND_RESIZE)
    return &info->current;
  else
    return &info->orig;
}

static void
place_window_if_needed(MetaWindow     *window,
                       ConstraintInfo *info)
{
  gboolean did_placement;

  /* Do placement if any, so we go ahead and apply position
   * constraints in a move-only context. Don't place
   * maximized/minimized/fullscreen windows until they are
   * unmaximized, unminimized and unfullscreened.
   */
  did_placement = FALSE;
  if (!window->placed &&
      window->calc_placement &&
      !(window->maximized_horizontally ||
        window->maximized_vertically) &&
      !window->minimized &&
      !window->fullscreen)
    {
      MetaBackend *backend = meta_get_backend ();
      MetaMonitorManager *monitor_manager =
        meta_backend_get_monitor_manager (backend);
      MetaRectangle orig_rect;
      MetaRectangle placed_rect;
      MetaWorkspace *cur_workspace;
      MetaLogicalMonitor *logical_monitor;

      placed_rect = (MetaRectangle) {
        .x = window->rect.x,
        .y = window->rect.y,
        .width = info->current.width,
        .height = info->current.height
      };

      orig_rect = info->orig;

      if (window->placement.rule)
        {
          meta_window_process_placement (window,
                                         window->placement.rule,
                                         &info->rel_x, &info->rel_y);
          placed_rect.x = window->placement.rule->parent_rect.x + info->rel_x;
          placed_rect.y = window->placement.rule->parent_rect.y + info->rel_y;
        }
      else
        {
          meta_window_place (window, orig_rect.x, orig_rect.y,
                             &placed_rect.x, &placed_rect.y);
        }
      did_placement = TRUE;

      /* placing the window may have changed the monitor.  Find the
       * new monitor and update the ConstraintInfo
       */
      logical_monitor =
        meta_monitor_manager_get_logical_monitor_from_rect (monitor_manager,
                                                            &placed_rect);
      info->entire_monitor = logical_monitor->rect;
      meta_window_get_work_area_for_logical_monitor (window,
                                                     logical_monitor,
                                                     &info->work_area_monitor);
      cur_workspace = window->display->workspace_manager->active_workspace;
      info->usable_monitor_region =
        meta_workspace_get_onmonitor_region (cur_workspace, logical_monitor);

      info->current.x = placed_rect.x;
      info->current.y = placed_rect.y;

      /* Since we just barely placed the window, there's no reason to
       * consider any of the directions fixed.
       */
      info->fixed_directions = FIXED_DIRECTION_NONE;
    }

  if (window->placed || did_placement)
    {
      if (window->maximize_horizontally_after_placement ||
          window->maximize_vertically_after_placement)
        {
          /* define a sane saved_rect so that the user can unmaximize to
           * something reasonable.
           */
          if (info->current.width >= info->work_area_monitor.width)
            {
              info->current.width = .75 * info->work_area_monitor.width;
              info->current.x = info->work_area_monitor.x +
                       .125 * info->work_area_monitor.width;
            }
          if (info->current.height >= info->work_area_monitor.height)
            {
              info->current.height = .75 * info->work_area_monitor.height;
              info->current.y = info->work_area_monitor.y +
                       .083 * info->work_area_monitor.height;
            }

          /* idle_move_resize() uses the unconstrained_rect, so make sure it
           * uses the placed coordinates (bug #556696).
           */
          window->unconstrained_rect = info->current;

          if (window->maximize_horizontally_after_placement ||
              window->maximize_vertically_after_placement)
            meta_window_maximize_internal (window,
                (window->maximize_horizontally_after_placement ?
                 META_MAXIMIZE_HORIZONTAL : 0 ) |
                (window->maximize_vertically_after_placement ?
                 META_MAXIMIZE_VERTICAL : 0), &info->current);

          window->maximize_horizontally_after_placement = FALSE;
          window->maximize_vertically_after_placement = FALSE;
        }
      if (window->minimize_after_placement)
        {
          meta_window_minimize (window);
          window->minimize_after_placement = FALSE;
        }
    }
}

static void
update_onscreen_requirements (MetaWindow     *window,
                              ConstraintInfo *info)
{
  gboolean old;

  /* We only apply the various onscreen requirements to normal windows */
  if (window->type == META_WINDOW_DESKTOP ||
      window->type == META_WINDOW_DOCK)
    return;

  /* We don't want to update the requirements for fullscreen windows;
   * fullscreen windows are specially handled anyway, and it updating
   * the requirements when windows enter fullscreen mode mess up the
   * handling of the window when it leaves that mode (especially when
   * the application sends a bunch of configurerequest events).  See
   * #353699.
   */
  if (window->fullscreen)
    return;

  /* USABILITY NOTE: Naturally, I only want the require_fully_onscreen,
   * require_on_single_monitor, and require_titlebar_visible flags to
   * *become false* due to user interactions (which is allowed since
   * certain constraints are ignored for user interactions regardless of
   * the setting of these flags).  However, whether to make these flags
   * *become true* due to just an application interaction is a little
   * trickier.  It's possible that users may find not doing that strange
   * since two application interactions that resize in opposite ways don't
   * necessarily end up cancelling--but it may also be strange for the user
   * to have an application resize the window so that it's onscreen, the
   * user forgets about it, and then later the app is able to resize itself
   * off the screen.  Anyway, for now, I think the latter is the more
   * problematic case but this may need to be revisited.
   */

  /* Update whether we want future constraint runs to require the
   * window to be on fully onscreen.
   */
  old = window->require_fully_onscreen;
  window->require_fully_onscreen =
    meta_rectangle_contained_in_region (info->usable_screen_region,
                                        &info->current);
  if (old != window->require_fully_onscreen)
    meta_topic (META_DEBUG_GEOMETRY,
                "require_fully_onscreen for %s toggled to %s\n",
                window->desc,
                window->require_fully_onscreen ? "TRUE" : "FALSE");

  /* Update whether we want future constraint runs to require the
   * window to be on a single monitor.
   */
  old = window->require_on_single_monitor;
  window->require_on_single_monitor =
    meta_rectangle_contained_in_region (info->usable_monitor_region,
                                        &info->current);
  if (old != window->require_on_single_monitor)
    meta_topic (META_DEBUG_GEOMETRY,
                "require_on_single_monitor for %s toggled to %s\n",
                window->desc,
                window->require_on_single_monitor ? "TRUE" : "FALSE");

  /* Update whether we want future constraint runs to require the
   * titlebar to be visible.
   */
  if (window->frame && window->decorated)
    {
      MetaRectangle titlebar_rect, frame_rect;

      meta_window_get_titlebar_rect (window, &titlebar_rect);
      meta_window_get_frame_rect (window, &frame_rect);

      /* translate into screen coordinates */
      titlebar_rect.x = frame_rect.x;
      titlebar_rect.y = frame_rect.y;

      old = window->require_titlebar_visible;
      window->require_titlebar_visible =
        meta_rectangle_overlaps_with_region (info->usable_screen_region,
                                             &titlebar_rect);
      if (old != window->require_titlebar_visible)
        meta_topic (META_DEBUG_GEOMETRY,
                    "require_titlebar_visible for %s toggled to %s\n",
                    window->desc,
                    window->require_titlebar_visible ? "TRUE" : "FALSE");
    }
}

static inline void
get_size_limits (MetaWindow    *window,
                 MetaRectangle *min_size,
                 MetaRectangle *max_size)
{
  /* We pack the results into MetaRectangle structs just for convienience; we
   * don't actually use the position of those rects.
   */
  min_size->x = min_size->y = max_size->x = max_size->y = 0;
  min_size->width  = window->size_hints.min_width;
  min_size->height = window->size_hints.min_height;
  max_size->width  = window->size_hints.max_width;
  max_size->height = window->size_hints.max_height;

  meta_window_client_rect_to_frame_rect (window, min_size, min_size);
  meta_window_client_rect_to_frame_rect (window, max_size, max_size);
}

static void
placement_rule_flip_horizontally (MetaPlacementRule *placement_rule)
{
  if (placement_rule->anchor & META_PLACEMENT_ANCHOR_LEFT)
    {
      placement_rule->anchor &= ~META_PLACEMENT_ANCHOR_LEFT;
      placement_rule->anchor |= META_PLACEMENT_ANCHOR_RIGHT;
    }
  else if (placement_rule->anchor & META_PLACEMENT_ANCHOR_RIGHT)
    {
      placement_rule->anchor &= ~META_PLACEMENT_ANCHOR_RIGHT;
      placement_rule->anchor |= META_PLACEMENT_ANCHOR_LEFT;
    }

  if (placement_rule->gravity & META_PLACEMENT_GRAVITY_LEFT)
    {
      placement_rule->gravity &= ~META_PLACEMENT_GRAVITY_LEFT;
      placement_rule->gravity |= META_PLACEMENT_GRAVITY_RIGHT;
    }
  else if (placement_rule->gravity & META_PLACEMENT_GRAVITY_RIGHT)
    {
      placement_rule->gravity &= ~META_PLACEMENT_GRAVITY_RIGHT;
      placement_rule->gravity |= META_PLACEMENT_GRAVITY_LEFT;
    }
}

static void
placement_rule_flip_vertically (MetaPlacementRule *placement_rule)
{
  if (placement_rule->anchor & META_PLACEMENT_ANCHOR_TOP)
    {
      placement_rule->anchor &= ~META_PLACEMENT_ANCHOR_TOP;
      placement_rule->anchor |= META_PLACEMENT_ANCHOR_BOTTOM;
    }
  else if (placement_rule->anchor & META_PLACEMENT_ANCHOR_BOTTOM)
    {
      placement_rule->anchor &= ~META_PLACEMENT_ANCHOR_BOTTOM;
      placement_rule->anchor |= META_PLACEMENT_ANCHOR_TOP;
    }

  if (placement_rule->gravity & META_PLACEMENT_GRAVITY_TOP)
    {
      placement_rule->gravity &= ~META_PLACEMENT_GRAVITY_TOP;
      placement_rule->gravity |= META_PLACEMENT_GRAVITY_BOTTOM;
    }
  else if (placement_rule->gravity & META_PLACEMENT_GRAVITY_BOTTOM)
    {
      placement_rule->gravity &= ~META_PLACEMENT_GRAVITY_BOTTOM;
      placement_rule->gravity |= META_PLACEMENT_GRAVITY_TOP;
    }
}

static void
try_flip_window_position (MetaWindow                       *window,
                          ConstraintInfo                   *info,
                          MetaPlacementRule                *placement_rule,
                          MetaPlacementConstraintAdjustment constraint_adjustment,
                          int                               parent_x,
                          int                               parent_y,
                          MetaRectangle                    *rect,
                          int                              *rel_x,
                          int                              *rel_y,
                          MetaRectangle                    *intersection)
{
  MetaPlacementRule flipped_rule = *placement_rule;;
  MetaRectangle flipped_rect;
  MetaRectangle flipped_intersection;
  int flipped_rel_x;
  int flipped_rel_y;

  switch (constraint_adjustment)
    {
    case META_PLACEMENT_CONSTRAINT_ADJUSTMENT_FLIP_X:
      placement_rule_flip_horizontally (&flipped_rule);
      break;
    case META_PLACEMENT_CONSTRAINT_ADJUSTMENT_FLIP_Y:
      placement_rule_flip_vertically (&flipped_rule);
      break;

    default:
      g_assert_not_reached ();
    }

  flipped_rect = info->current;
  meta_window_process_placement (window, &flipped_rule,
                                 &flipped_rel_x, &flipped_rel_y);
  flipped_rect.x = parent_x + flipped_rel_x;
  flipped_rect.y = parent_y + flipped_rel_y;
  meta_rectangle_intersect (&flipped_rect, &info->work_area_monitor,
                            &flipped_intersection);

  if ((constraint_adjustment == META_PLACEMENT_CONSTRAINT_ADJUSTMENT_FLIP_X &&
       flipped_intersection.width == flipped_rect.width) ||
      (constraint_adjustment == META_PLACEMENT_CONSTRAINT_ADJUSTMENT_FLIP_Y &&
       flipped_intersection.height == flipped_rect.height))
    {
      *placement_rule = flipped_rule;
      *rect = flipped_rect;
      *rel_x = flipped_rel_x;
      *rel_y = flipped_rel_y;
      *intersection = flipped_intersection;
    }
}

static gboolean
is_custom_rule_satisfied (MetaRectangle     *rect,
                          MetaPlacementRule *placement_rule,
                          MetaRectangle     *intersection)
{
  uint32_t x_constrain_actions, y_constrain_actions;

  x_constrain_actions = (META_PLACEMENT_CONSTRAINT_ADJUSTMENT_SLIDE_X |
                         META_PLACEMENT_CONSTRAINT_ADJUSTMENT_FLIP_X);
  y_constrain_actions = (META_PLACEMENT_CONSTRAINT_ADJUSTMENT_SLIDE_Y |
                         META_PLACEMENT_CONSTRAINT_ADJUSTMENT_FLIP_Y);
  if ((placement_rule->constraint_adjustment & x_constrain_actions &&
       rect->width != intersection->width) ||
      (placement_rule->constraint_adjustment & y_constrain_actions &&
       rect->height != intersection->height))
    return FALSE;
  else
    return TRUE;
}

static gboolean
constrain_custom_rule (MetaWindow         *window,
                       ConstraintInfo     *info,
                       ConstraintPriority  priority,
                       gboolean            check_only)
{
  MetaPlacementRule *placement_rule;
  MetaRectangle intersection;
  gboolean constraint_satisfied;
  MetaRectangle temporary_rect;
  MetaRectangle adjusted_unconstrained;
  int adjusted_rel_x;
  int adjusted_rel_y;
  MetaPlacementRule current_rule;
  MetaWindow *parent;
  int parent_x, parent_y;

  if (priority > PRIORITY_CUSTOM_RULE)
    return TRUE;

  placement_rule = meta_window_get_placement_rule (window);
  if (!placement_rule)
    return TRUE;

  parent = meta_window_get_transient_for (window);
  if (window->placement.state == META_PLACEMENT_STATE_CONSTRAINED_FINISHED)
    {
      placement_rule->parent_rect.x = parent->rect.x;
      placement_rule->parent_rect.y = parent->rect.y;
    }
  parent_x = placement_rule->parent_rect.x;
  parent_y = placement_rule->parent_rect.y;

  /*
   * Calculate the temporary position, meaning a position that will be
   * applied if the new constrained position requires asynchronous
   * configuration of the window. This happens for example when the parent
   * moves, causing this window to change relative position, meaning it can
   * only have its newly constrained position applied when the configuration is
   * acknowledged.
   */

  switch (window->placement.state)
    {
    case META_PLACEMENT_STATE_UNCONSTRAINED:
      temporary_rect = info->current;
      break;
    case META_PLACEMENT_STATE_CONSTRAINED_CONFIGURED:
    case META_PLACEMENT_STATE_CONSTRAINED_PENDING:
    case META_PLACEMENT_STATE_CONSTRAINED_FINISHED:
    case META_PLACEMENT_STATE_INVALIDATED:
      temporary_rect = (MetaRectangle) {
        .x = parent->rect.x + window->placement.current.rel_x,
        .y = parent->rect.y + window->placement.current.rel_y,
        .width = info->current.width,
        .height = info->current.height,
      };
      break;
    }

  /*
   * Calculate an adjusted current position. Depending on the rule
   * configuration and placement state, this may result in window being
   * reconstrained.
   */

  adjusted_unconstrained = temporary_rect;

  if (window->placement.state == META_PLACEMENT_STATE_INVALIDATED ||
      window->placement.state == META_PLACEMENT_STATE_UNCONSTRAINED ||
      (window->placement.state == META_PLACEMENT_STATE_CONSTRAINED_FINISHED &&
       placement_rule->is_reactive))
    {
      meta_window_process_placement (window, placement_rule,
                                     &adjusted_rel_x,
                                     &adjusted_rel_y);
      adjusted_unconstrained.x = parent_x + adjusted_rel_x;
      adjusted_unconstrained.y = parent_y + adjusted_rel_y;
    }
  else if (window->placement.state == META_PLACEMENT_STATE_CONSTRAINED_PENDING)
    {
      adjusted_rel_x = window->placement.pending.rel_x;
      adjusted_rel_y = window->placement.pending.rel_y;
      adjusted_unconstrained.x = window->placement.pending.x;
      adjusted_unconstrained.y = window->placement.pending.y;
    }
  else
    {
      adjusted_rel_x = window->placement.current.rel_x;
      adjusted_rel_y = window->placement.current.rel_y;
    }

  meta_rectangle_intersect (&adjusted_unconstrained, &info->work_area_monitor,
                            &intersection);

  constraint_satisfied = (meta_rectangle_equal (&info->current,
                                                &adjusted_unconstrained) &&
                          is_custom_rule_satisfied (&adjusted_unconstrained,
                                                    placement_rule,
                                                    &intersection));

  if (check_only)
    return constraint_satisfied;

  info->current = adjusted_unconstrained;
  info->rel_x = adjusted_rel_x;
  info->rel_y = adjusted_rel_y;
  info->temporary = temporary_rect;

  switch (window->placement.state)
    {
    case META_PLACEMENT_STATE_CONSTRAINED_FINISHED:
      if (!placement_rule->is_reactive)
        return TRUE;
      break;
    case META_PLACEMENT_STATE_CONSTRAINED_PENDING:
    case META_PLACEMENT_STATE_CONSTRAINED_CONFIGURED:
      return TRUE;
    case META_PLACEMENT_STATE_UNCONSTRAINED:
    case META_PLACEMENT_STATE_INVALIDATED:
      break;
    }

  if (constraint_satisfied)
    goto done;

  /*
   * Process the placement rule in order either until constraints are
   * satisfied, or there are no more rules to process.
   */

  current_rule = *placement_rule;

  if (info->current.width != intersection.width &&
      (current_rule.constraint_adjustment &
       META_PLACEMENT_CONSTRAINT_ADJUSTMENT_FLIP_X))
    {
      try_flip_window_position (window, info, &current_rule,
                                META_PLACEMENT_CONSTRAINT_ADJUSTMENT_FLIP_X,
                                parent_x,
                                parent_y,
                                &info->current,
                                &info->rel_x,
                                &info->rel_y,
                                &intersection);
    }
  if (info->current.height != intersection.height &&
      (current_rule.constraint_adjustment &
       META_PLACEMENT_CONSTRAINT_ADJUSTMENT_FLIP_Y))
    {
      try_flip_window_position (window, info, &current_rule,
                                META_PLACEMENT_CONSTRAINT_ADJUSTMENT_FLIP_Y,
                                parent_x,
                                parent_y,
                                &info->current,
                                &info->rel_x,
                                &info->rel_y,
                                &intersection);
    }

  meta_rectangle_intersect (&info->current, &info->work_area_monitor,
                            &intersection);
  constraint_satisfied = is_custom_rule_satisfied (&info->current,
                                                   placement_rule,
                                                   &intersection);

  if (constraint_satisfied)
    goto done;

  if (current_rule.constraint_adjustment &
      META_PLACEMENT_CONSTRAINT_ADJUSTMENT_SLIDE_X)
    {
      int current_x2;
      int work_area_monitor_x2;
      int new_x;

      current_x2 = info->current.x + info->current.width;
      work_area_monitor_x2 = (info->work_area_monitor.x +
                              info->work_area_monitor.width);

      if (current_x2 > work_area_monitor_x2)
        {
          new_x = MAX (info->work_area_monitor.x,
                       work_area_monitor_x2 - info->current.width);
        }
      else if (info->current.x < info->work_area_monitor.x)
        {
          new_x = info->work_area_monitor.x;
        }
      else
        {
          new_x = info->current.x;
        }

      info->rel_x += new_x - info->current.x;
      info->current.x = new_x;
    }
  if (current_rule.constraint_adjustment &
      META_PLACEMENT_CONSTRAINT_ADJUSTMENT_SLIDE_Y)
    {
      int current_y2;
      int work_area_monitor_y2;
      int new_y;

      current_y2 = info->current.y + info->current.height;
      work_area_monitor_y2 = (info->work_area_monitor.y +
                              info->work_area_monitor.height);

      if (current_y2 > work_area_monitor_y2)
        {
          new_y = MAX (info->work_area_monitor.y,
                       work_area_monitor_y2 - info->current.height);
        }
      else if (info->current.y < info->work_area_monitor.y)
        {
          new_y = info->work_area_monitor.y;
        }
      else
        {
          new_y = info->current.y;
        }

      info->rel_y += new_y - info->current.y;
      info->current.y = new_y;
    }

  meta_rectangle_intersect (&info->current, &info->work_area_monitor,
                            &intersection);
  constraint_satisfied = is_custom_rule_satisfied (&info->current,
                                                   placement_rule,
                                                   &intersection);

  if (constraint_satisfied)
    goto done;

  if (current_rule.constraint_adjustment &
      META_PLACEMENT_CONSTRAINT_ADJUSTMENT_RESIZE_X)
    {
      int new_x;
      new_x = intersection.x;
      info->current.width = intersection.width;
      info->rel_x += new_x - info->current.x;
      info->current.x = new_x;
    }
  if (current_rule.constraint_adjustment &
      META_PLACEMENT_CONSTRAINT_ADJUSTMENT_RESIZE_Y)
    {
      int new_y;
      new_y = intersection.y;
      info->current.height = intersection.height;
      info->rel_y += new_y - info->current.y;
      info->current.y = new_y;
    }

done:
  window->placement.state = META_PLACEMENT_STATE_CONSTRAINED_PENDING;

  window->placement.pending.rel_x = info->rel_x;
  window->placement.pending.rel_y = info->rel_y;
  window->placement.pending.x = info->current.x;
  window->placement.pending.y = info->current.y;

  return TRUE;
}

static gboolean
constrain_modal_dialog (MetaWindow         *window,
                        ConstraintInfo     *info,
                        ConstraintPriority  priority,
                        gboolean            check_only)
{
  int x, y;
  MetaWindow *parent = meta_window_get_transient_for (window);
  MetaRectangle child_rect, parent_rect;
  gboolean constraint_already_satisfied;

  if (!parent ||
      !meta_window_is_attached_dialog (window) ||
      meta_window_get_placement_rule (window))
    return TRUE;

  /* We want to center the dialog on the parent, including the decorations
     for both of them. info->current is in client X window coordinates, so we need
     to convert them to frame coordinates, apply the centering and then
     convert back to client.
  */

  child_rect = info->current;

  meta_window_get_frame_rect (parent, &parent_rect);

  child_rect.x = parent_rect.x + (parent_rect.width / 2  - child_rect.width / 2);
  child_rect.y = parent_rect.y + (parent_rect.height / 2 - child_rect.height / 2);
  x = child_rect.x;
  y = child_rect.y;

  constraint_already_satisfied = (x == info->current.x) && (y == info->current.y);

  if (check_only || constraint_already_satisfied)
    return constraint_already_satisfied;

  info->current.y = y;
  info->current.x = x;
  /* The calculated position above may need adjustment to make sure the
   * dialog does not end up partially off-screen */
  return do_screen_and_monitor_relative_constraints (window,
                                                     info->usable_screen_region,
                                                     info,
                                                     check_only);
}

static gboolean
constrain_maximization (MetaWindow         *window,
                        ConstraintInfo     *info,
                        ConstraintPriority  priority,
                        gboolean            check_only)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
  MetaRectangle target_size;
  MetaRectangle min_size, max_size;
  gboolean hminbad, vminbad;
  gboolean horiz_equal, vert_equal;
  gboolean constraint_already_satisfied;

  if (priority > PRIORITY_MAXIMIZATION)
    return TRUE;

  /* Determine whether constraint applies; exit if it doesn't */
  if ((!window->maximized_horizontally && !window->maximized_vertically) ||
      META_WINDOW_TILED_SIDE_BY_SIDE (window))
    return TRUE;

  /* Calculate target_size = maximized size of (window + frame) */
  if (META_WINDOW_TILED_MAXIMIZED (window))
    {
      meta_window_get_tile_area (window, window->tile_mode, &target_size);
    }
  else if (META_WINDOW_MAXIMIZED (window))
    {
      target_size = info->work_area_monitor;
    }
  else
    {
      /* Amount of maximization possible in a single direction depends
       * on which struts could occlude the window given its current
       * position.  For example, a vertical partial strut on the right
       * is only relevant for a horizontally maximized window when the
       * window is at a vertical position where it could be occluded
       * by that partial strut.
       */
      MetaDirection  direction;
      GSList        *active_workspace_struts;

      if (window->maximized_horizontally)
        direction = META_DIRECTION_HORIZONTAL;
      else
        direction = META_DIRECTION_VERTICAL;
      active_workspace_struts = workspace_manager->active_workspace->all_struts;

      target_size = info->current;
      meta_rectangle_expand_to_avoiding_struts (&target_size,
                                                &info->entire_monitor,
                                                direction,
                                                active_workspace_struts);
   }

  /* Check min size constraints; max size constraints are ignored for maximized
   * windows, as per bug 327543.
   */
  get_size_limits (window, &min_size, &max_size);
  hminbad = target_size.width < min_size.width && window->maximized_horizontally;
  vminbad = target_size.height < min_size.height && window->maximized_vertically;
  if (hminbad || vminbad)
    return TRUE;

  /* Determine whether constraint is already satisfied; exit if it is */
  horiz_equal = target_size.x      == info->current.x &&
                target_size.width  == info->current.width;
  vert_equal  = target_size.y      == info->current.y &&
                target_size.height == info->current.height;
  constraint_already_satisfied =
    (horiz_equal || !window->maximized_horizontally) &&
    (vert_equal  || !window->maximized_vertically);
  if (check_only || constraint_already_satisfied)
    return constraint_already_satisfied;

  /*** Enforce constraint ***/
  if (window->maximized_horizontally)
    {
      info->current.x      = target_size.x;
      info->current.width  = target_size.width;
    }
  if (window->maximized_vertically)
    {
      info->current.y      = target_size.y;
      info->current.height = target_size.height;
    }
  return TRUE;
}

static gboolean
constrain_tiling (MetaWindow         *window,
                  ConstraintInfo     *info,
                  ConstraintPriority  priority,
                  gboolean            check_only)
{
  MetaRectangle target_size;
  MetaRectangle min_size, max_size;
  gboolean hminbad, vminbad;
  gboolean horiz_equal, vert_equal;
  gboolean constraint_already_satisfied;

  if (priority > PRIORITY_TILING)
    return TRUE;

  /* Determine whether constraint applies; exit if it doesn't */
  if (!META_WINDOW_TILED_SIDE_BY_SIDE (window))
    return TRUE;

  /* Calculate target_size - as the tile previews need this as well, we
   * use an external function for the actual calculation
   */
  meta_window_get_tile_area (window, window->tile_mode, &target_size);

  /* Check min size constraints; max size constraints are ignored as for
   * maximized windows.
   */
  get_size_limits (window, &min_size, &max_size);
  hminbad = target_size.width < min_size.width;
  vminbad = target_size.height < min_size.height;
  if (hminbad || vminbad)
    return TRUE;

  /* Determine whether constraint is already satisfied; exit if it is */
  horiz_equal = target_size.x      == info->current.x &&
                target_size.width  == info->current.width;
  vert_equal  = target_size.y      == info->current.y &&
                target_size.height == info->current.height;
  constraint_already_satisfied = horiz_equal && vert_equal;
  if (check_only || constraint_already_satisfied)
    return constraint_already_satisfied;

  /*** Enforce constraint ***/
  info->current.x      = target_size.x;
  info->current.width  = target_size.width;
  info->current.y      = target_size.y;
  info->current.height = target_size.height;

  return TRUE;
}


static gboolean
constrain_fullscreen (MetaWindow         *window,
                      ConstraintInfo     *info,
                      ConstraintPriority  priority,
                      gboolean            check_only)
{
  MetaRectangle min_size, max_size, monitor;
  gboolean too_big, too_small, constraint_already_satisfied;

  if (priority > PRIORITY_FULLSCREEN)
    return TRUE;

  /* Determine whether constraint applies; exit if it doesn't */
  if (!window->fullscreen)
    return TRUE;

  monitor = info->entire_monitor;

  get_size_limits (window, &min_size, &max_size);
  too_big =   !meta_rectangle_could_fit_rect (&monitor, &min_size);
  too_small = !meta_rectangle_could_fit_rect (&max_size, &monitor);
  if (too_big || too_small)
    return TRUE;

  /* Determine whether constraint is already satisfied; exit if it is */
  constraint_already_satisfied =
    meta_rectangle_equal (&info->current, &monitor);
  if (check_only || constraint_already_satisfied)
    return constraint_already_satisfied;

  /*** Enforce constraint ***/
  info->current = monitor;
  return TRUE;
}

static gboolean
constrain_size_increments (MetaWindow         *window,
                           ConstraintInfo     *info,
                           ConstraintPriority  priority,
                           gboolean            check_only)
{
  int bh, hi, bw, wi, extra_height, extra_width;
  int new_width, new_height;
  gboolean constraint_already_satisfied;
  MetaRectangle *start_rect;
  MetaRectangle client_rect;

  if (priority > PRIORITY_SIZE_HINTS_INCREMENTS)
    return TRUE;

  /* Determine whether constraint applies; exit if it doesn't */
  if (META_WINDOW_MAXIMIZED (window) || window->fullscreen ||
      META_WINDOW_TILED_SIDE_BY_SIDE (window) ||
      info->action_type == ACTION_MOVE)
    return TRUE;

  meta_window_frame_rect_to_client_rect (window, &info->current, &client_rect);

  /* Determine whether constraint is already satisfied; exit if it is */
  bh = window->size_hints.base_height;
  hi = window->size_hints.height_inc;
  bw = window->size_hints.base_width;
  wi = window->size_hints.width_inc;
  extra_height = (client_rect.height - bh) % hi;
  extra_width  = (client_rect.width  - bw) % wi;
  /* ignore size increments for maximized windows */
  if (window->maximized_horizontally)
    extra_width *= 0;
  if (window->maximized_vertically)
    extra_height *= 0;
  /* constraint is satisfied iff there is no extra height or width */
  constraint_already_satisfied =
    (extra_height == 0 && extra_width == 0);

  if (check_only || constraint_already_satisfied)
    return constraint_already_satisfied;

  /*** Enforce constraint ***/
  new_width  = client_rect.width  - extra_width;
  new_height = client_rect.height - extra_height;

  /* Adjusting down instead of up (as done in the above two lines) may
   * violate minimum size constraints; fix the adjustment if this
   * happens.
   */
  if (new_width  < window->size_hints.min_width)
    new_width  += ((window->size_hints.min_width  - new_width)/wi  + 1)*wi;
  if (new_height < window->size_hints.min_height)
    new_height += ((window->size_hints.min_height - new_height)/hi + 1)*hi;

  {
    client_rect.width = new_width;
    client_rect.height = new_height;
    meta_window_client_rect_to_frame_rect (window, &client_rect, &client_rect);
    new_width = client_rect.width;
    new_height = client_rect.height;
  }

  start_rect = get_start_rect_for_resize (window, info);

  /* Resize to the new size */
  meta_rectangle_resize_with_gravity (start_rect,
                                      &info->current,
                                      info->resize_gravity,
                                      new_width,
                                      new_height);
  return TRUE;
}

static gboolean
constrain_size_limits (MetaWindow         *window,
                       ConstraintInfo     *info,
                       ConstraintPriority  priority,
                       gboolean            check_only)
{
  MetaRectangle min_size, max_size;
  gboolean too_big, too_small, constraint_already_satisfied;
  int new_width, new_height;
  MetaRectangle *start_rect;

  if (priority > PRIORITY_SIZE_HINTS_LIMITS)
    return TRUE;

  /* Determine whether constraint applies; exit if it doesn't.
   *
   * Note: The old code didn't apply this constraint for fullscreen or
   * maximized windows--but that seems odd to me.  *shrug*
   */
  if (info->action_type == ACTION_MOVE)
    return TRUE;

  /* Determine whether constraint is already satisfied; exit if it is */
  get_size_limits (window, &min_size, &max_size);
  /* We ignore max-size limits for maximized windows; see #327543 */
  if (window->maximized_horizontally)
    max_size.width = MAX (max_size.width, info->current.width);
  if (window->maximized_vertically)
    max_size.height = MAX (max_size.height, info->current.height);
  too_small = !meta_rectangle_could_fit_rect (&info->current, &min_size);
  too_big   = !meta_rectangle_could_fit_rect (&max_size, &info->current);
  constraint_already_satisfied = !too_big && !too_small;
  if (check_only || constraint_already_satisfied)
    return constraint_already_satisfied;

  /*** Enforce constraint ***/
  new_width  = CLAMP (info->current.width,  min_size.width,  max_size.width);
  new_height = CLAMP (info->current.height, min_size.height, max_size.height);

  start_rect = get_start_rect_for_resize (window, info);

  meta_rectangle_resize_with_gravity (start_rect,
                                      &info->current,
                                      info->resize_gravity,
                                      new_width,
                                      new_height);
  return TRUE;
}

static gboolean
constrain_aspect_ratio (MetaWindow         *window,
                        ConstraintInfo     *info,
                        ConstraintPriority  priority,
                        gboolean            check_only)
{
  double minr, maxr;
  gboolean constraints_are_inconsistent, constraint_already_satisfied;
  int fudge, new_width, new_height;
  double best_width, best_height;
  double alt_width, alt_height;
  MetaRectangle *start_rect;
  MetaRectangle client_rect;

  if (priority > PRIORITY_ASPECT_RATIO)
    return TRUE;

  /* Determine whether constraint applies; exit if it doesn't. */
  minr =         window->size_hints.min_aspect.x /
         (double)window->size_hints.min_aspect.y;
  maxr =         window->size_hints.max_aspect.x /
         (double)window->size_hints.max_aspect.y;
  constraints_are_inconsistent = minr > maxr;
  if (constraints_are_inconsistent ||
      META_WINDOW_MAXIMIZED (window) || window->fullscreen ||
      META_WINDOW_TILED_SIDE_BY_SIDE (window) ||
      info->action_type == ACTION_MOVE)
    return TRUE;

  /* Determine whether constraint is already satisfied; exit if it is.  We
   * need the following to hold:
   *
   *                 width
   *         minr <= ------ <= maxr
   *                 height
   *
   * But we need to allow for some slight fudging since width and height
   * are integers instead of floating point numbers (this is particularly
   * important when minr == maxr), so we allow width and height to be off
   * a little bit from strictly satisfying these equations.  For just one
   * sided resizing, we have to make the fudge factor a little bigger
   * because of how meta_rectangle_resize_with_gravity treats those as
   * being a resize increment (FIXME: I should handle real resize
   * increments better here...)
   */
  switch (info->resize_gravity)
    {
    case META_GRAVITY_WEST:
    case META_GRAVITY_NORTH:
    case META_GRAVITY_SOUTH:
    case META_GRAVITY_EAST:
      fudge = 2;
      break;

    case META_GRAVITY_NORTH_WEST:
    case META_GRAVITY_SOUTH_WEST:
    case META_GRAVITY_CENTER:
    case META_GRAVITY_NORTH_EAST:
    case META_GRAVITY_SOUTH_EAST:
    case META_GRAVITY_STATIC:
    default:
      fudge = 1;
      break;
    }

  meta_window_frame_rect_to_client_rect (window, &info->current, &client_rect);

  constraint_already_satisfied =
    client_rect.width - (client_rect.height * minr ) > -minr*fudge &&
    client_rect.width - (client_rect.height * maxr ) <  maxr*fudge;
  if (check_only || constraint_already_satisfied)
    return constraint_already_satisfied;

  /*** Enforce constraint ***/
  new_width = client_rect.width;
  new_height = client_rect.height;

  switch (info->resize_gravity)
    {
    case META_GRAVITY_WEST:
    case META_GRAVITY_EAST:
      /* Yeah, I suck for doing implicit rounding -- sue me */
      new_height = CLAMP (new_height, new_width / maxr,  new_width / minr);
      break;

    case META_GRAVITY_NORTH:
    case META_GRAVITY_SOUTH:
      /* Yeah, I suck for doing implicit rounding -- sue me */
      new_width  = CLAMP (new_width,  new_height * minr, new_height * maxr);
      break;

    case META_GRAVITY_NORTH_WEST:
    case META_GRAVITY_SOUTH_WEST:
    case META_GRAVITY_CENTER:
    case META_GRAVITY_NORTH_EAST:
    case META_GRAVITY_SOUTH_EAST:
    case META_GRAVITY_STATIC:
    default:
      /* Find what width would correspond to new_height, and what height would
       * correspond to new_width */
      alt_width  = CLAMP (new_width,  new_height * minr, new_height * maxr);
      alt_height = CLAMP (new_height, new_width / maxr,  new_width / minr);

      /* The line connecting the points (alt_width, new_height) and
       * (new_width, alt_height) provide a range of
       * valid-for-the-aspect-ratio-constraint sizes.  We want the
       * size in that range closest to the value requested, i.e. the
       * point on the line which is closest to the point (new_width,
       * new_height)
       */
      meta_rectangle_find_linepoint_closest_to_point (alt_width, new_height,
                                                      new_width, alt_height,
                                                      new_width, new_height,
                                                      &best_width, &best_height);

      /* Yeah, I suck for doing implicit rounding -- sue me */
      new_width  = best_width;
      new_height = best_height;

      break;
    }

  {
    client_rect.width = new_width;
    client_rect.height = new_height;
    meta_window_client_rect_to_frame_rect (window, &client_rect, &client_rect);
    new_width = client_rect.width;
    new_height = client_rect.height;
  }

  start_rect = get_start_rect_for_resize (window, info);

  meta_rectangle_resize_with_gravity (start_rect,
                                      &info->current,
                                      info->resize_gravity,
                                      new_width,
                                      new_height);

  return TRUE;
}

static gboolean
do_screen_and_monitor_relative_constraints (
  MetaWindow     *window,
  GList          *region_spanning_rectangles,
  ConstraintInfo *info,
  gboolean        check_only)
{
  gboolean exit_early = FALSE, constraint_satisfied;
  MetaRectangle how_far_it_can_be_smushed, min_size, max_size;

#ifdef WITH_VERBOSE_MODE
  if (meta_is_verbose ())
    {
      /* First, log some debugging information */
      char spanning_region[1 + 28 * g_list_length (region_spanning_rectangles)];

      meta_topic (META_DEBUG_GEOMETRY,
             "screen/monitor constraint; region_spanning_rectangles: %s\n",
             meta_rectangle_region_to_string (region_spanning_rectangles, ", ",
                                              spanning_region));
    }
#endif

  /* Determine whether constraint applies; exit if it doesn't */
  how_far_it_can_be_smushed = info->current;
  get_size_limits (window, &min_size, &max_size);

  if (info->action_type != ACTION_MOVE)
    {
      if (!(info->fixed_directions & FIXED_DIRECTION_X))
        how_far_it_can_be_smushed.width = min_size.width;

      if (!(info->fixed_directions & FIXED_DIRECTION_Y))
        how_far_it_can_be_smushed.height = min_size.height;
    }
  if (!meta_rectangle_could_fit_in_region (region_spanning_rectangles,
                                           &how_far_it_can_be_smushed))
    exit_early = TRUE;

  /* Determine whether constraint is already satisfied; exit if it is */
  constraint_satisfied =
    meta_rectangle_contained_in_region (region_spanning_rectangles,
                                        &info->current);
  if (exit_early || constraint_satisfied || check_only)
    return constraint_satisfied;

  /* Enforce constraint */

  /* Clamp rectangle size for resize or move+resize actions */
  if (info->action_type != ACTION_MOVE)
    meta_rectangle_clamp_to_fit_into_region (region_spanning_rectangles,
                                             info->fixed_directions,
                                             &info->current,
                                             &min_size);

  if (info->is_user_action && info->action_type == ACTION_RESIZE)
    /* For user resize, clip to the relevant region */
    meta_rectangle_clip_to_region (region_spanning_rectangles,
                                   info->fixed_directions,
                                   &info->current);
  else
    /* For everything else, shove the rectangle into the relevant region */
    meta_rectangle_shove_into_region (region_spanning_rectangles,
                                      info->fixed_directions,
                                      &info->current);

  return TRUE;
}

static gboolean
constrain_to_single_monitor (MetaWindow         *window,
                             ConstraintInfo     *info,
                             ConstraintPriority  priority,
                             gboolean            check_only)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  if (priority > PRIORITY_ENTIRELY_VISIBLE_ON_SINGLE_MONITOR)
    return TRUE;

  /* Exit early if we know the constraint won't apply--note that this constraint
   * is only meant for normal windows (e.g. we don't want docks to be shoved
   * "onscreen" by their own strut) and we can't apply it to frameless windows
   * or else users will be unable to move windows such as XMMS across monitors.
   */
  if (window->type == META_WINDOW_DESKTOP ||
      window->type == META_WINDOW_DOCK ||
      meta_monitor_manager_get_num_logical_monitors (monitor_manager) == 1 ||
      !window->require_on_single_monitor ||
      !window->frame ||
      info->is_user_action ||
      meta_window_get_placement_rule (window))
    return TRUE;

  /* Have a helper function handle the constraint for us */
  return do_screen_and_monitor_relative_constraints (window,
                                                     info->usable_monitor_region,
                                                     info,
                                                     check_only);
}

static gboolean
constrain_fully_onscreen (MetaWindow         *window,
                          ConstraintInfo     *info,
                          ConstraintPriority  priority,
                          gboolean            check_only)
{
  if (priority > PRIORITY_ENTIRELY_VISIBLE_ON_WORKAREA)
    return TRUE;

  /* Exit early if we know the constraint won't apply--note that this constraint
   * is only meant for normal windows (e.g. we don't want docks to be shoved
   * "onscreen" by their own strut).
   */
  if (window->type == META_WINDOW_DESKTOP ||
      window->type == META_WINDOW_DOCK    ||
      window->fullscreen                  ||
      !window->require_fully_onscreen     ||
      info->is_user_action                ||
      meta_window_get_placement_rule (window))
    return TRUE;

  /* Have a helper function handle the constraint for us */
  return do_screen_and_monitor_relative_constraints (window,
                                                     info->usable_screen_region,
                                                     info,
                                                     check_only);
}

static gboolean
constrain_titlebar_visible (MetaWindow         *window,
                            ConstraintInfo     *info,
                            ConstraintPriority  priority,
                            gboolean            check_only)
{
  gboolean unconstrained_user_action;
  gboolean retval;
  int bottom_amount;
  int horiz_amount_offscreen, vert_amount_offscreen;
  int horiz_amount_onscreen,  vert_amount_onscreen;

  if (priority > PRIORITY_TITLEBAR_VISIBLE)
    return TRUE;

  /* Allow the titlebar beyond the top of the screen only if the user wasn't
   * clicking on the frame to start the move.
   */
  unconstrained_user_action =
    info->is_user_action && !window->display->grab_frame_action;

  /* Exit early if we know the constraint won't apply--note that this constraint
   * is only meant for normal windows (e.g. we don't want docks to be shoved
   * "onscreen" by their own strut).
   */
  if (window->type == META_WINDOW_DESKTOP ||
      window->type == META_WINDOW_DOCK    ||
      window->fullscreen                  ||
      !window->require_titlebar_visible   ||
      unconstrained_user_action           ||
      meta_window_get_placement_rule (window))
    return TRUE;

  /* Determine how much offscreen things are allowed.  We first need to
   * figure out how much must remain on the screen.  For that, we use 25%
   * window width/height but clamp to the range of (10,75) pixels.  This is
   * somewhat of a seat of my pants random guess at what might look good.
   * Then, the amount that is allowed off is just the window size minus
   * this amount (but no less than 0 for tiny windows).
   */
  horiz_amount_onscreen = info->current.width  / 4;
  vert_amount_onscreen  = info->current.height / 4;
  horiz_amount_onscreen = CLAMP (horiz_amount_onscreen, 10, 75);
  vert_amount_onscreen  = CLAMP (vert_amount_onscreen,  10, 75);
  horiz_amount_offscreen = info->current.width - horiz_amount_onscreen;
  vert_amount_offscreen  = info->current.height - vert_amount_onscreen;
  horiz_amount_offscreen = MAX (horiz_amount_offscreen, 0);
  vert_amount_offscreen  = MAX (vert_amount_offscreen,  0);
  /* Allow the titlebar to touch the bottom panel;  If there is no titlebar,
   * require vert_amount to remain on the screen.
   */
  if (window->frame)
    {
      MetaFrameBorders borders;
      meta_frame_calc_borders (window->frame, &borders);

      bottom_amount = info->current.height - borders.visible.top;
      vert_amount_onscreen = borders.visible.top;
    }
  else
    bottom_amount = vert_amount_offscreen;

  /* Extend the region, have a helper function handle the constraint,
   * then return the region to its original size.
   */
  meta_rectangle_expand_region_conditionally (info->usable_screen_region,
                                              horiz_amount_offscreen,
                                              horiz_amount_offscreen,
                                              0, /* Don't let titlebar off */
                                              bottom_amount,
                                              horiz_amount_onscreen,
                                              vert_amount_onscreen);
  retval =
    do_screen_and_monitor_relative_constraints (window,
                                                info->usable_screen_region,
                                                info,
                                                check_only);
  meta_rectangle_expand_region_conditionally (info->usable_screen_region,
                                              -horiz_amount_offscreen,
                                              -horiz_amount_offscreen,
                                              0, /* Don't let titlebar off */
                                              -bottom_amount,
                                              horiz_amount_onscreen,
                                              vert_amount_onscreen);

  return retval;
}

static gboolean
constrain_partially_onscreen (MetaWindow         *window,
                              ConstraintInfo     *info,
                              ConstraintPriority  priority,
                              gboolean            check_only)
{
  gboolean retval;
  int top_amount, bottom_amount;
  int horiz_amount_offscreen, vert_amount_offscreen;
  int horiz_amount_onscreen,  vert_amount_onscreen;

  if (priority > PRIORITY_PARTIALLY_VISIBLE_ON_WORKAREA)
    return TRUE;

  /* Exit early if we know the constraint won't apply--note that this constraint
   * is only meant for normal windows (e.g. we don't want docks to be shoved
   * "onscreen" by their own strut).
   */
  if (window->type == META_WINDOW_DESKTOP ||
      window->type == META_WINDOW_DOCK    ||
      meta_window_get_placement_rule (window))
    return TRUE;

  /* Determine how much offscreen things are allowed.  We first need to
   * figure out how much must remain on the screen.  For that, we use 25%
   * window width/height but clamp to the range of (10,75) pixels.  This is
   * somewhat of a seat of my pants random guess at what might look good.
   * Then, the amount that is allowed off is just the window size minus
   * this amount (but no less than 0 for tiny windows).
   */
  horiz_amount_onscreen = info->current.width  / 4;
  vert_amount_onscreen  = info->current.height / 4;
  horiz_amount_onscreen = CLAMP (horiz_amount_onscreen, 10, 75);
  vert_amount_onscreen  = CLAMP (vert_amount_onscreen,  10, 75);
  horiz_amount_offscreen = info->current.width - horiz_amount_onscreen;
  vert_amount_offscreen  = info->current.height - vert_amount_onscreen;
  horiz_amount_offscreen = MAX (horiz_amount_offscreen, 0);
  vert_amount_offscreen  = MAX (vert_amount_offscreen,  0);
  top_amount = vert_amount_offscreen;
  /* Allow the titlebar to touch the bottom panel;  If there is no titlebar,
   * require vert_amount to remain on the screen.
   */
  if (window->frame)
    {
      MetaFrameBorders borders;
      meta_frame_calc_borders (window->frame, &borders);

      bottom_amount = info->current.height - borders.visible.top;
      vert_amount_onscreen = borders.visible.top;
    }
  else
    bottom_amount = vert_amount_offscreen;

  /* Extend the region, have a helper function handle the constraint,
   * then return the region to its original size.
   */
  meta_rectangle_expand_region_conditionally (info->usable_screen_region,
                                              horiz_amount_offscreen,
                                              horiz_amount_offscreen,
                                              top_amount,
                                              bottom_amount,
                                              horiz_amount_onscreen,
                                              vert_amount_onscreen);
  retval =
    do_screen_and_monitor_relative_constraints (window,
                                                info->usable_screen_region,
                                                info,
                                                check_only);
  meta_rectangle_expand_region_conditionally (info->usable_screen_region,
                                              -horiz_amount_offscreen,
                                              -horiz_amount_offscreen,
                                              -top_amount,
                                              -bottom_amount,
                                              horiz_amount_onscreen,
                                              vert_amount_onscreen);

  return retval;
}
