/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Muffin size/position constraints */

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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 */

#include <config.h>
#include "boxes-private.h"
#include "constraints.h"
#include "workspace-private.h"
#include "place.h"
#include <meta/prefs.h>
#include "xprops.h"
#include <stdlib.h>
#include <math.h>

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
  MetaFrameBorders    *borders;
  gboolean             must_free_borders;
  ActionType           action_type;
  gboolean             is_user_action;

  /* I know that these two things probably look similar at first, but they
   * have much different uses.  See doc/how-constraints-works.txt for for
   * explanation of the differences and similarity between resize_gravity
   * and fixed_directions
   */
  int                  resize_gravity;
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
} ConstraintInfo;

static gboolean do_screen_and_monitor_relative_constraints (MetaWindow     *window,
                                                            GList          *region_spanning_rectangles,
                                                            ConstraintInfo *info,
                                                            gboolean        check_only);
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
                                          MetaFrameBorders    *orig_borders,
                                          MetaMoveResizeFlags  flags,
                                          int                  resize_gravity,
                                          const MetaRectangle *orig,
                                          MetaRectangle       *new);
static void place_window_if_needed       (MetaWindow     *window,
                                          ConstraintInfo *info);
static void update_onscreen_requirements (MetaWindow     *window,
                                          ConstraintInfo *info);

static inline void get_size_limits   (const MetaWindow        *window,
                                      const MetaFrameBorders  *borders,
                                            gboolean           include_frame,
                                            MetaRectangle     *min_size,
                                            MetaRectangle     *max_size);

typedef gboolean (* ConstraintFunc) (MetaWindow         *window,
                                     ConstraintInfo     *info,
                                     ConstraintPriority  priority,
                                     gboolean            check_only);

typedef struct {
  ConstraintFunc func;
  const char* name;
} Constraint;

static const Constraint all_constraints[] = {
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

LOCAL_SYMBOL void
meta_window_constrain (MetaWindow          *window,
                       MetaFrameBorders    *orig_borders,
                       MetaMoveResizeFlags  flags,
                       int                  resize_gravity,
                       const MetaRectangle *orig,
                       MetaRectangle       *new)
{
  ConstraintInfo info;
  ConstraintPriority priority = PRIORITY_MINIMUM;
  gboolean satisfied = FALSE;

  /* WARNING: orig and new specify positions and sizes of the inner window,
   * not the outer.  This is a common gotcha since half the constraints
   * deal with inner window position/size and half deal with outer.  See
   * doc/how-constraints-works.txt for more information.
   */
  meta_topic (META_DEBUG_GEOMETRY,
              "Constraining %s in move from %d,%d %dx%d to %d,%d %dx%d\n",
              window->desc,
              orig->x, orig->y, orig->width, orig->height,
              new->x,  new->y,  new->width,  new->height);

  setup_constraint_info (&info,
                         window, 
                         orig_borders,
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

  /* We may need to update window->require_fully_onscreen,
   * window->require_on_single_monitor, and perhaps other quantities
   * if this was a user move or user move-and-resize operation.
   */
  update_onscreen_requirements (window, &info);

  /* Ew, what an ugly way to do things.  Destructors (in a real OOP language,
   * not gobject-style--gobject would be more pain than it's worth) or
   * smart pointers would be so much nicer here.  *shrug*
   */
  if (info.must_free_borders)
    g_free (info.borders);
}

static void
setup_constraint_info (ConstraintInfo      *info,
                       MetaWindow          *window,
                       MetaFrameBorders    *orig_borders,
                       MetaMoveResizeFlags  flags,
                       int                  resize_gravity,
                       const MetaRectangle *orig,
                       MetaRectangle       *new)
{
  const MetaMonitorInfo *monitor_info;
  MetaWorkspace *cur_workspace;

  info->orig    = *orig;
  info->current = *new;

  /* Create a fake frame geometry if none really exists */
  if (orig_borders && !window->fullscreen)
    {
      info->borders = orig_borders;
      info->must_free_borders = FALSE;
    }
  else
    {
      info->borders = g_new0 (MetaFrameBorders, 1);
      info->must_free_borders = TRUE;
    }

  if (flags & META_IS_MOVE_ACTION && flags & META_IS_RESIZE_ACTION)
    info->action_type = ACTION_MOVE_AND_RESIZE;
  else if (flags & META_IS_RESIZE_ACTION)
    info->action_type = ACTION_RESIZE;
  else if (flags & META_IS_MOVE_ACTION)
    info->action_type = ACTION_MOVE;
  else
    g_error ("BAD, BAD developer!  No treat for you!  (Fix your calls to "
             "meta_window_move_resize_internal()).\n");

  info->is_user_action = (flags & META_IS_USER_ACTION);

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

  monitor_info =
    meta_screen_get_monitor_for_rect (window->screen, &info->current);
  meta_window_get_work_area_for_monitor (window,
                                         monitor_info->number,
                                         &info->work_area_monitor);

  if (!window->fullscreen || window->fullscreen_monitors[0] == -1)
    {
      info->entire_monitor = monitor_info->rect;
    }
  else
    {
      int i = 0;
      long monitor;

      monitor = window->fullscreen_monitors[i];
      info->entire_monitor =
        window->screen->monitor_infos[monitor].rect;
      for (i = 1; i <= 3; i++)
        {
          monitor = window->fullscreen_monitors[i];
          meta_rectangle_union (&info->entire_monitor,
                                &window->screen->monitor_infos[monitor].rect,
                                &info->entire_monitor);
        }
    }

  cur_workspace = window->screen->active_workspace;
  info->usable_screen_region   = 
    meta_workspace_get_onscreen_region (cur_workspace);
  info->usable_monitor_region = 
    meta_workspace_get_onmonitor_region (cur_workspace, 
                                         monitor_info->number);

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
      MetaRectangle placed_rect = info->orig;
      MetaWorkspace *cur_workspace;
      const MetaMonitorInfo *monitor_info;

      meta_window_place (window, info->borders, info->orig.x, info->orig.y,
                         &placed_rect.x, &placed_rect.y);
      did_placement = TRUE;

      /* placing the window may have changed the monitor.  Find the
       * new monitor and update the ConstraintInfo
       */
      monitor_info =
        meta_screen_get_monitor_for_rect (window->screen, &placed_rect);
      info->entire_monitor = monitor_info->rect;
      meta_window_get_work_area_for_monitor (window,
                                             monitor_info->number,
                                             &info->work_area_monitor);
      cur_workspace = window->screen->active_workspace;
      info->usable_monitor_region = 
        meta_workspace_get_onmonitor_region (cur_workspace, 
                                             monitor_info->number);


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
          window->maximize_vertically_after_placement   ||
          window->fullscreen_after_placement)
        {
          /* define a sane saved_rect so that the user can unmaximize or
           * make unfullscreen to something reasonable.
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

          /* idle_move_resize() uses the user_rect,
           * so make sure it uses the placed coordinates.
           */
          window->user_rect = info->current;

          if (window->maximize_horizontally_after_placement ||
              window->maximize_vertically_after_placement)
            meta_window_maximize_internal (window,   
                (window->maximize_horizontally_after_placement ?
                 META_MAXIMIZE_HORIZONTAL : 0 ) |
                (window->maximize_vertically_after_placement ?
                 META_MAXIMIZE_VERTICAL : 0), &info->current);

          /* maximization may have changed frame geometry */
          if (!window->fullscreen)
            meta_frame_calc_borders (window->frame, info->borders);

          if (window->fullscreen_after_placement)
            {
              window->saved_rect = info->current;
              window->fullscreen = TRUE;
              window->fullscreen_after_placement = FALSE;

              g_object_notify (G_OBJECT (window), "fullscreen");
            }

          window->maximize_horizontally_after_placement = FALSE;
          window->maximize_vertically_after_placement = FALSE;
        }
      if (window->minimize_after_placement)
        {
          meta_window_minimize (window);
          window->minimize_after_placement = FALSE;
        }
      if (window->tile_after_placement)
        {
          window->tile_after_placement = FALSE;

          gulong *tile_info = NULL;
          int nitems;

          if (meta_prop_get_cardinal_list (window->display,
                                           window->xwindow,
                                           window->display->atom__NET_WM_WINDOW_TILE_INFO,
                                           &tile_info, &nitems))
            {
              if (nitems == 8)
                {
                  window->tile_mode = (MetaTileMode) tile_info[0];
                  meta_window_move_resize_frame (window,
                                                 TRUE,
                                                 tile_info[2],
                                                 tile_info[3],
                                                 tile_info[4],
                                                 tile_info[5]);
                  window->custom_snap_size = tile_info[7] == 1;
                  window->tile_monitor_number = tile_info[6];
                  if (tile_info[1] == META_WINDOW_TILE_TYPE_SNAPPED)
                    window->snap_queued = TRUE;
                  else
                    window->snap_queued = FALSE;
                  meta_window_real_tile (window, TRUE);
                  meta_XFree (tile_info);
                }
            }

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

  /* The require onscreen/on-single-monitor and titlebar_visible
   * stuff is relative to the outer window, not the inner
   */
  meta_window_extend_by_frame (window, &info->current, info->borders);

  /* Update whether we want future constraint runs to require the
   * window to be on fully onscreen.
   */
  old = window->require_fully_onscreen;
  window->require_fully_onscreen =
    meta_rectangle_contained_in_region (info->usable_screen_region,
                                        &info->current);
  if (old ^ window->require_fully_onscreen)
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
  if (old ^ window->require_on_single_monitor)
    meta_topic (META_DEBUG_GEOMETRY,
                "require_on_single_monitor for %s toggled to %s\n",
                window->desc, 
                window->require_on_single_monitor ? "TRUE" : "FALSE");

  /* Update whether we want future constraint runs to require the
   * titlebar to be visible.
   */

  MetaRectangle titlebar_rect;

  meta_window_get_titlebar_rect (window, &titlebar_rect);

  titlebar_rect.x += info->current.x;
  titlebar_rect.y += info->current.y;

  old = window->require_titlebar_visible;
  window->require_titlebar_visible =
    meta_rectangle_overlaps_with_region (info->usable_screen_region,
                                         &titlebar_rect);

  if (old ^ window->require_titlebar_visible)
    meta_topic (META_DEBUG_GEOMETRY,
                "require_titlebar_visible for %s toggled to %s\n",
                window->desc,
                window->require_titlebar_visible ? "TRUE" : "FALSE");


  /* Don't forget to restore the position of the window */
  meta_window_unextend_by_frame (window, &info->current, info->borders);
}

static inline void
get_size_limits (const MetaWindow       *window,
                 const MetaFrameBorders *borders,
                       gboolean          include_frame,
                       MetaRectangle    *min_size,
                       MetaRectangle    *max_size)
{
  /* We pack the results into MetaRectangle structs just for convienience; we
   * don't actually use the position of those rects.
   */
  min_size->width  = window->size_hints.min_width;
  min_size->height = window->size_hints.min_height;
  max_size->width  = window->size_hints.max_width;
  max_size->height = window->size_hints.max_height;

  if (include_frame)
    {
      int fw = borders->visible.left + borders->visible.right;
      int fh = borders->visible.top + borders->visible.bottom;

      min_size->width  += fw;
      min_size->height += fh;
      /* Do check to avoid overflow (e.g. max_size->width & max_size->height
       * may be set to G_MAXINT by meta_set_normal_hints()).
       */
      if (max_size->width < (G_MAXINT - fw))
        max_size->width += fw;
      else
        max_size->width = G_MAXINT;
      if (max_size->height < (G_MAXINT - fh))
        max_size->height += fh;
      else
        max_size->height = G_MAXINT;
    }
}

static gboolean
constrain_modal_dialog (MetaWindow         *window,
                        ConstraintInfo     *info,
                        ConstraintPriority  priority,
                        gboolean            check_only)
{
  int x, y;
  MetaWindow *parent = meta_window_get_transient_for (window);
  gboolean constraint_already_satisfied;

  if (!meta_window_is_attached_dialog (window))
    return TRUE;

  x = parent->rect.x + (parent->rect.width / 2  - info->current.width / 2);
  y = parent->rect.y + (parent->rect.height / 2 - info->current.height / 2);
  if (parent->frame)
    {
      x += parent->frame->rect.x;
      y += parent->frame->rect.y;
    }

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
  MetaRectangle target_size;
  MetaRectangle min_size, max_size;
  gboolean hminbad, vminbad;
  gboolean horiz_equal, vert_equal;
  gboolean constraint_already_satisfied;

  if (priority > PRIORITY_MAXIMIZATION)
    return TRUE;

  /* Determine whether constraint applies; exit if it doesn't */
  if ((!window->maximized_horizontally && !window->maximized_vertically) ||
      META_WINDOW_TILED_OR_SNAPPED (window))
    return TRUE;

  /* Calculate target_size = maximized size of (window + frame) */
  if (META_WINDOW_MAXIMIZED (window) &&
      (g_list_length (window->workspace->snapped_windows) == 0 || window->type == META_WINDOW_DESKTOP))
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
      active_workspace_struts = window->screen->active_workspace->all_struts;

      if (g_list_length (window->screen->active_workspace->snapped_windows) > 0) {
        GList *tmp = window->screen->active_workspace->snapped_windows;
        GSList *snapped_windows_as_struts = NULL;
        while (tmp) {
            if (tmp->data == window || META_WINDOW (tmp->data)->minimized ||
                meta_window_get_monitor (window) != meta_window_get_monitor (META_WINDOW (tmp->data))) {
                tmp = tmp->next;
                continue;
            }
            MetaStrut *strut = g_slice_new0 (MetaStrut);
            MetaSide side;
            MetaRectangle rect;
            meta_window_get_outer_rect (META_WINDOW (tmp->data), &rect);
            side = meta_window_get_tile_side (META_WINDOW (tmp->data));
            strut->rect = rect;
            strut->side = side;
            snapped_windows_as_struts = g_slist_prepend (snapped_windows_as_struts, strut);
            tmp = tmp->next;
        }

        target_size = info->current;
        meta_window_extend_by_frame (window, &target_size, info->borders);
        meta_rectangle_expand_to_snapped_borders (&target_size,
                                                  &info->entire_monitor,
                                                   active_workspace_struts,
                                                   snapped_windows_as_struts,
                                                   &window->user_rect);
        g_slist_free (snapped_windows_as_struts);
      } else {
          target_size = info->current;
          meta_rectangle_expand_to_avoiding_struts (&target_size,
                                                    &info->entire_monitor,
                                                    direction,
                                                    active_workspace_struts);
      }
   }
  /* Now make target_size = maximized size of client window */
  if (!meta_window_is_client_decorated(window))
    meta_window_unextend_by_frame (window, &target_size, info->borders);

  /* Check min size constraints; max size constraints are ignored for maximized
   * windows, as per bug 327543.
   */
  get_size_limits (window, info->borders, FALSE, &min_size, &max_size);
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
  MetaRectangle actual_position;
  gboolean hminbad, vminbad;
  gboolean horiz_equal, vert_equal;
  gboolean constraint_already_satisfied;

  if (priority > PRIORITY_TILING)
    return TRUE;

  /* Determine whether constraint applies; exit if it doesn't */
  if (!META_WINDOW_TILED_OR_SNAPPED (window) || window->resizing_tile_type != META_WINDOW_TILE_TYPE_NONE)
    return TRUE;

  /* Calculate target_size - as the tile previews need this as well, we
   * use an external function for the actual calculation
   */
  if (window->tile_mode != META_TILE_NONE)
    meta_window_get_current_tile_area (window, &target_size);
  else
    return TRUE;

  meta_window_get_outer_rect (window, &actual_position);

  if (window->custom_snap_size) {
      switch (window->tile_mode) {
        case META_TILE_LEFT:
            target_size.width = BOX_RIGHT (actual_position) - target_size.x;
            break;
        case META_TILE_RIGHT:
            target_size.width = BOX_RIGHT (target_size) - BOX_LEFT (actual_position);
            target_size.x = BOX_LEFT (actual_position);
            break;
        case META_TILE_TOP:
            target_size.height = BOX_BOTTOM (actual_position) - BOX_TOP (target_size);
            break;
        case META_TILE_BOTTOM:
            target_size.height = BOX_BOTTOM (target_size) - BOX_TOP (actual_position);
            target_size.y = BOX_TOP (actual_position);
            break;
        case META_TILE_ULC:
            if (GRAB_OP (window) == META_GRAB_OP_RESIZING_S ||
                GRAB_OP (window) == META_GRAB_OP_KEYBOARD_RESIZING_S) {
                target_size.width = window->snapped_rect.width;
                target_size.height = BOX_BOTTOM (actual_position) - BOX_TOP (target_size);
            } else if (GRAB_OP (window) == META_GRAB_OP_RESIZING_E ||
                       GRAB_OP (window) == META_GRAB_OP_KEYBOARD_RESIZING_E) {
                target_size.width = BOX_RIGHT (actual_position) - target_size.x;
                target_size.height = window->snapped_rect.height;
            } else {
                target_size.width = BOX_RIGHT (actual_position) - target_size.x;
                target_size.height = BOX_BOTTOM (actual_position) - BOX_TOP (target_size);
            }
            break;
        case META_TILE_LLC:
            if (GRAB_OP (window) == META_GRAB_OP_RESIZING_N ||
                GRAB_OP (window) == META_GRAB_OP_KEYBOARD_RESIZING_N) {
                target_size.width = window->snapped_rect.width;
                target_size.height = BOX_BOTTOM (target_size) - BOX_TOP (actual_position);
                target_size.y = BOX_TOP (actual_position);
            } else if (GRAB_OP (window) == META_GRAB_OP_RESIZING_E ||
                       GRAB_OP (window) == META_GRAB_OP_KEYBOARD_RESIZING_E) {
                target_size.width = BOX_RIGHT (actual_position) - target_size.x;
                target_size.height = window->snapped_rect.height;
                target_size.y = window->snapped_rect.y;
            } else {
                target_size.width = BOX_RIGHT (actual_position) - target_size.x;
                target_size.height = BOX_BOTTOM (target_size) - BOX_TOP (actual_position);
                target_size.y = BOX_TOP (actual_position);
            }
            break;
        case META_TILE_URC:
            if (GRAB_OP (window) == META_GRAB_OP_RESIZING_W ||
                GRAB_OP (window) == META_GRAB_OP_KEYBOARD_RESIZING_W) {
                target_size.width = BOX_RIGHT (target_size) - BOX_LEFT (actual_position);
                target_size.x = BOX_LEFT (actual_position);
                target_size.height = window->snapped_rect.height;
            } else if (GRAB_OP (window) == META_GRAB_OP_RESIZING_S ||
                       GRAB_OP (window) == META_GRAB_OP_KEYBOARD_RESIZING_S) {
                target_size.width = window->snapped_rect.width;
                target_size.x = window->snapped_rect.x;
                target_size.height = BOX_BOTTOM (actual_position) - BOX_TOP (target_size);
            } else {
                target_size.width = BOX_RIGHT (target_size) - BOX_LEFT (actual_position);
                target_size.x = BOX_LEFT (actual_position);
                target_size.height = BOX_BOTTOM (actual_position) - BOX_TOP (target_size);
            }
            break;
        case META_TILE_LRC:
            if (GRAB_OP (window) == META_GRAB_OP_RESIZING_W ||
                GRAB_OP (window) == META_GRAB_OP_KEYBOARD_RESIZING_W) {
                target_size.width = BOX_RIGHT (target_size) - BOX_LEFT (actual_position);
                target_size.x = BOX_LEFT (actual_position);
                target_size.height = window->snapped_rect.height;
                target_size.y = window->snapped_rect.y;
            } else if (GRAB_OP (window) == META_GRAB_OP_RESIZING_N ||
                       GRAB_OP (window) == META_GRAB_OP_KEYBOARD_RESIZING_N) {
                target_size.width = window->snapped_rect.width;
                target_size.x = window->snapped_rect.x;
                target_size.height = BOX_BOTTOM (target_size) - BOX_TOP (actual_position);
                target_size.y = BOX_TOP (actual_position);
            } else {
                target_size.width = BOX_RIGHT (target_size) - BOX_LEFT (actual_position);
                target_size.x = BOX_LEFT (actual_position);
                target_size.height = BOX_BOTTOM (target_size) - BOX_TOP (actual_position);
                target_size.y = BOX_TOP (actual_position);
            }
            break;
        default:
            break;
      }
  }

  meta_window_unextend_by_frame (window, &target_size, info->borders);

  /* Check min size constraints; max size constraints are ignored as for
   * maximized windows.
   */
  get_size_limits (window, info->borders, FALSE, &min_size, &max_size);
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

  get_size_limits (window, info->borders, FALSE, &min_size, &max_size);
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

  if (priority > PRIORITY_SIZE_HINTS_INCREMENTS)
    return TRUE;

  /* Determine whether constraint applies; exit if it doesn't */
  if (META_WINDOW_MAXIMIZED (window) || window->fullscreen || 
      META_WINDOW_TILED_OR_SNAPPED (window) ||
      info->action_type == ACTION_MOVE ||
      window->resizing_tile_type != META_WINDOW_TILE_TYPE_NONE)
    return TRUE;

  /* Determine whether constraint is already satisfied; exit if it is */
  bh = window->size_hints.base_height;
  hi = window->size_hints.height_inc;
  bw = window->size_hints.base_width;
  wi = window->size_hints.width_inc;
  extra_height = (info->current.height - bh) % hi;
  extra_width  = (info->current.width  - bw) % wi;
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
  new_width  = info->current.width  - extra_width;
  new_height = info->current.height - extra_height;

  /* Adjusting down instead of up (as done in the above two lines) may
   * violate minimum size constraints; fix the adjustment if this
   * happens.
   */
  if (new_width  < window->size_hints.min_width)
    new_width  += ((window->size_hints.min_width  - new_width)/wi  + 1)*wi;
  if (new_height < window->size_hints.min_height)
    new_height += ((window->size_hints.min_height - new_height)/hi + 1)*hi;

  /* Figure out what original rect to pass to meta_rectangle_resize_with_gravity
   * See bug 448183
   */
  if (info->action_type == ACTION_MOVE_AND_RESIZE)
    start_rect = &info->current;
  else
    start_rect = &info->orig;
    
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
  get_size_limits (window, info->borders, FALSE, &min_size, &max_size);
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
  
  /* Figure out what original rect to pass to meta_rectangle_resize_with_gravity
   * See bug 448183
   */
  if (info->action_type == ACTION_MOVE_AND_RESIZE)
    start_rect = &info->current;
  else
    start_rect = &info->orig;
  
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
      META_WINDOW_TILED_OR_SNAPPED (window) ||
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
    case WestGravity:
    case NorthGravity:
    case SouthGravity:
    case EastGravity:
      fudge = 2;
      break;

    case NorthWestGravity:
    case SouthWestGravity:
    case CenterGravity:
    case NorthEastGravity:
    case SouthEastGravity:
    case StaticGravity:
    default:
      fudge = 1;
      break;
    }
  constraint_already_satisfied = 
    info->current.width - (info->current.height * minr ) > -minr*fudge &&
    info->current.width - (info->current.height * maxr ) <  maxr*fudge;
  if (check_only || constraint_already_satisfied)
    return constraint_already_satisfied;

  /*** Enforce constraint ***/
  new_width = info->current.width;
  new_height = info->current.height;

  switch (info->resize_gravity)
    {
    case WestGravity:
    case EastGravity:
      /* Yeah, I suck for doing implicit rounding -- sue me */
      new_height = CLAMP (new_height, new_width / maxr,  new_width / minr);
      break;

    case NorthGravity:
    case SouthGravity:
      /* Yeah, I suck for doing implicit rounding -- sue me */
      new_width  = CLAMP (new_width,  new_height * minr, new_height * maxr);
      break;

    case NorthWestGravity:
    case SouthWestGravity:
    case CenterGravity:
    case NorthEastGravity:
    case SouthEastGravity:
    case StaticGravity:
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

  /* Figure out what original rect to pass to meta_rectangle_resize_with_gravity
   * See bug 448183
   */
  if (info->action_type == ACTION_MOVE_AND_RESIZE)
    start_rect = &info->current;
  else
    start_rect = &info->orig;

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
  get_size_limits (window, info->borders, TRUE, &min_size, &max_size);
  meta_window_extend_by_frame (window, &info->current, info->borders);

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
    {
      meta_window_unextend_by_frame (window, &info->current, info->borders);
      return constraint_satisfied;
    }

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
    {
      /* For everything else, shove the rectangle into the relevant region */
      meta_rectangle_shove_into_region (region_spanning_rectangles,
                                        info->fixed_directions,
                                        &info->current);
    }

  meta_window_unextend_by_frame (window, &info->current, info->borders);
  return TRUE;
}

static gboolean
constrain_to_single_monitor (MetaWindow         *window,
                             ConstraintInfo     *info,
                             ConstraintPriority  priority,
                             gboolean            check_only)
{
  if (priority > PRIORITY_ENTIRELY_VISIBLE_ON_SINGLE_MONITOR)
    return TRUE;

  /* Exit early if we know the constraint won't apply--note that this constraint
   * is only meant for normal windows (e.g. we don't want docks to be shoved 
   * "onscreen" by their own strut) and we can't apply it to frameless windows
   * or else users will be unable to move windows such as XMMS across monitors.
   */
  if (window->type == META_WINDOW_DESKTOP   ||
      window->type == META_WINDOW_DOCK      ||
      window->screen->n_monitor_infos == 1  ||
      !window->require_on_single_monitor    ||
      (!window->frame && !meta_window_is_client_decorated (window)) ||
      info->is_user_action)
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
      info->is_user_action)
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
  int scale = meta_prefs_get_ui_scale ();

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
      unconstrained_user_action)
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
  horiz_amount_onscreen = CLAMP (horiz_amount_onscreen, 10 * scale, 75 * scale);
  vert_amount_onscreen  = CLAMP (vert_amount_onscreen,  10 * scale, 75 * scale);
  horiz_amount_offscreen = info->current.width - horiz_amount_onscreen;
  vert_amount_offscreen  = info->current.height - vert_amount_onscreen;
  horiz_amount_offscreen = MAX (horiz_amount_offscreen, 0);
  vert_amount_offscreen  = MAX (vert_amount_offscreen,  0);
  /* Allow the titlebar to touch the bottom panel;  If there is no titlebar,
   * require vert_amount to remain on the screen.
   */
  if (window->frame)
    {
      bottom_amount = info->current.height + info->borders->visible.bottom;
      vert_amount_onscreen = info->borders->visible.top;
    }
  else if (meta_window_is_client_decorated (window))
    {
      vert_amount_onscreen = CSD_TITLEBAR_HEIGHT * scale; /* Hardcoded for now, we don't get this from Gtk */
      bottom_amount = vert_amount_offscreen = MAX ((info->current.height - (vert_amount_onscreen * 2)), 0);
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
  int scale = meta_prefs_get_ui_scale ();

  if (priority > PRIORITY_PARTIALLY_VISIBLE_ON_WORKAREA)
    return TRUE;

  /* Exit early if we know the constraint won't apply--note that this constraint
   * is only meant for normal windows (e.g. we don't want docks to be shoved 
   * "onscreen" by their own strut).
   */
  if (window->type == META_WINDOW_DESKTOP ||
      window->type == META_WINDOW_DOCK)
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
  horiz_amount_onscreen = CLAMP (horiz_amount_onscreen, 10 * scale, 75 * scale);
  vert_amount_onscreen  = CLAMP (vert_amount_onscreen,  10 * scale, 75 * scale);
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
      bottom_amount = info->current.height + info->borders->visible.bottom;
      vert_amount_onscreen = info->borders->visible.top;
    }
  else if (meta_window_is_client_decorated (window))
    {
      top_amount = vert_amount_onscreen = CSD_TITLEBAR_HEIGHT * scale; /* Hardcoded for now, we don't get this from Gtk */
      bottom_amount = vert_amount_offscreen = MAX ((info->current.height - (vert_amount_onscreen * 2)), 0);
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
