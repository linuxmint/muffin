#include "clutter-build-config.h"

#include <glib-object.h>

#include "clutter-actor.h"
#include "clutter-stage-window.h"
#include "clutter-private.h"

/**
 * SECTION:clutter-stage-window
 * @short_description: Handles the implementation for ClutterStage
 *
 * #ClutterStageWindow is an interface that provides the implementation for the
 * #ClutterStage actor, abstracting away the specifics of the windowing system.
 */

G_DEFINE_INTERFACE (ClutterStageWindow, clutter_stage_window, G_TYPE_OBJECT);

static void
clutter_stage_window_default_init (ClutterStageWindowInterface *iface)
{
  GParamSpec *pspec;

  pspec = g_param_spec_object ("backend",
                               "Backend",
                               "Back pointer to the Backend instance",
                               CLUTTER_TYPE_BACKEND,
                               G_PARAM_WRITABLE |
                               G_PARAM_CONSTRUCT_ONLY |
                               G_PARAM_STATIC_STRINGS);
  g_object_interface_install_property (iface, pspec);

  pspec = g_param_spec_object ("wrapper",
                               "Wrapper",
                               "Back pointer to the Stage actor",
                               CLUTTER_TYPE_STAGE,
                               G_PARAM_WRITABLE |
                               G_PARAM_CONSTRUCT_ONLY |
                               G_PARAM_STATIC_STRINGS);
  g_object_interface_install_property (iface, pspec);
}

/**
 * _clutter_stage_window_get_wrapper:
 * @window: a #ClutterStageWindow object
 *
 * Returns the pointer to the #ClutterStage it's part of.
 */
ClutterActor *
_clutter_stage_window_get_wrapper (ClutterStageWindow *window)
{
  return CLUTTER_STAGE_WINDOW_GET_IFACE (window)->get_wrapper (window);
}

void
_clutter_stage_window_set_title (ClutterStageWindow *window,
                                 const gchar        *title)
{
  ClutterStageWindowInterface *iface = CLUTTER_STAGE_WINDOW_GET_IFACE (window);

  if (iface->set_title)
    iface->set_title (window, title);
}

void
_clutter_stage_window_set_cursor_visible (ClutterStageWindow *window,
                                          gboolean            is_visible)
{
  ClutterStageWindowInterface *iface = CLUTTER_STAGE_WINDOW_GET_IFACE (window);

  if (iface->set_cursor_visible)
    iface->set_cursor_visible (window, is_visible);
}

gboolean
_clutter_stage_window_realize (ClutterStageWindow *window)
{
  return CLUTTER_STAGE_WINDOW_GET_IFACE (window)->realize (window);
}

void
_clutter_stage_window_unrealize (ClutterStageWindow *window)
{
  CLUTTER_STAGE_WINDOW_GET_IFACE (window)->unrealize (window);
}

void
_clutter_stage_window_show (ClutterStageWindow *window,
                            gboolean            do_raise)
{
  CLUTTER_STAGE_WINDOW_GET_IFACE (window)->show (window, do_raise);
}

void
_clutter_stage_window_hide (ClutterStageWindow *window)
{
  CLUTTER_STAGE_WINDOW_GET_IFACE (window)->hide (window);
}

void
_clutter_stage_window_resize (ClutterStageWindow *window,
                              gint                width,
                              gint                height)
{
  CLUTTER_STAGE_WINDOW_GET_IFACE (window)->resize (window, width, height);
}

void
_clutter_stage_window_get_geometry (ClutterStageWindow    *window,
                                    cairo_rectangle_int_t *geometry)
{
  CLUTTER_STAGE_WINDOW_GET_IFACE (window)->get_geometry (window, geometry);
}

void
_clutter_stage_window_schedule_update  (ClutterStageWindow *window,
                                        int                 sync_delay)
{
  ClutterStageWindowInterface *iface;

  g_return_if_fail (CLUTTER_IS_STAGE_WINDOW (window));

  iface = CLUTTER_STAGE_WINDOW_GET_IFACE (window);
  if (iface->schedule_update == NULL)
    {
      g_assert (!clutter_feature_available (CLUTTER_FEATURE_SWAP_EVENTS));
      return;
    }

  iface->schedule_update (window, sync_delay);
}

/**
 * _clutter_stage_window_get_update_time:
 * @window: a #ClutterStageWindow object
 *
 * See _clutter_stage_get_update_time() for more info.
 *
 * Returns: The timestamp of the update time
 */
gint64
_clutter_stage_window_get_update_time (ClutterStageWindow *window)
{
  ClutterStageWindowInterface *iface;

  g_return_val_if_fail (CLUTTER_IS_STAGE_WINDOW (window), 0);

  iface = CLUTTER_STAGE_WINDOW_GET_IFACE (window);
  if (iface->get_update_time == NULL)
    {
      g_assert (!clutter_feature_available (CLUTTER_FEATURE_SWAP_EVENTS));
      return 0;
    }

  return iface->get_update_time (window);
}

/**
 * _clutter_stage_window_clear_update_time:
 * @window: a #ClutterStageWindow object
 *
 * Clears the update time. See _clutter_stage_clear_update_time() for more info.
 */
void
_clutter_stage_window_clear_update_time (ClutterStageWindow *window)
{
  ClutterStageWindowInterface *iface;

  g_return_if_fail (CLUTTER_IS_STAGE_WINDOW (window));

  iface = CLUTTER_STAGE_WINDOW_GET_IFACE (window);
  if (iface->clear_update_time == NULL)
    {
      g_assert (!clutter_feature_available (CLUTTER_FEATURE_SWAP_EVENTS));
      return;
    }

  iface->clear_update_time (window);
}

int64_t
_clutter_stage_window_get_next_presentation_time (ClutterStageWindow *window)
{
  ClutterStageWindowInterface *iface;

  g_return_val_if_fail (CLUTTER_IS_STAGE_WINDOW (window), 0);

  iface = CLUTTER_STAGE_WINDOW_GET_IFACE (window);

  /* If not implemented then just revert to the old behaviour... */
  if (iface->get_next_presentation_time == NULL)
    return _clutter_stage_window_get_update_time (window);

  return iface->get_next_presentation_time (window);
}

void
_clutter_stage_window_set_accept_focus (ClutterStageWindow *window,
                                        gboolean            accept_focus)
{
  ClutterStageWindowInterface *iface;

  g_return_if_fail (CLUTTER_IS_STAGE_WINDOW (window));

  iface = CLUTTER_STAGE_WINDOW_GET_IFACE (window);
  if (iface->set_accept_focus)
    iface->set_accept_focus (window, accept_focus);
}

void
_clutter_stage_window_redraw (ClutterStageWindow *window)
{
  ClutterStageWindowInterface *iface;

  g_return_if_fail (CLUTTER_IS_STAGE_WINDOW (window));

  iface = CLUTTER_STAGE_WINDOW_GET_IFACE (window);
  if (iface->redraw)
    iface->redraw (window);
}

gboolean
_clutter_stage_window_can_clip_redraws (ClutterStageWindow *window)
{
  ClutterStageWindowInterface *iface;

  g_return_val_if_fail (CLUTTER_IS_STAGE_WINDOW (window), FALSE);

  iface = CLUTTER_STAGE_WINDOW_GET_IFACE (window);
  if (iface->can_clip_redraws != NULL)
    return iface->can_clip_redraws (window);

  return FALSE;
}

GList *
_clutter_stage_window_get_views (ClutterStageWindow *window)
{
  ClutterStageWindowInterface *iface = CLUTTER_STAGE_WINDOW_GET_IFACE (window);

  return iface->get_views (window);
}

void
_clutter_stage_window_finish_frame (ClutterStageWindow *window)
{
  ClutterStageWindowInterface *iface = CLUTTER_STAGE_WINDOW_GET_IFACE (window);

  if (iface->finish_frame)
    iface->finish_frame (window);
}

int64_t
_clutter_stage_window_get_frame_counter (ClutterStageWindow *window)
{
  ClutterStageWindowInterface *iface = CLUTTER_STAGE_WINDOW_GET_IFACE (window);

  if (iface->get_frame_counter)
    return iface->get_frame_counter (window);
  else
    return 0;
}
