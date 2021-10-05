#ifndef __CLUTTER_STAGE_WINDOW_H__
#define __CLUTTER_STAGE_WINDOW_H__

#include <cogl/cogl.h>
#include <clutter/clutter-types.h>
#include "clutter/clutter-stage-view.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_STAGE_WINDOW (clutter_stage_window_get_type ())

CLUTTER_EXPORT
G_DECLARE_INTERFACE (ClutterStageWindow, clutter_stage_window,
                     CLUTTER, STAGE_WINDOW,
                     GObject)

/*
 * ClutterStageWindowInterface: (skip)
 *
 * The interface implemented by backends for stage windows
 *
 * Since: 0.8
 */
struct _ClutterStageWindowInterface
{
  /*< private >*/
  GTypeInterface parent_iface;

  ClutterActor     *(* get_wrapper)             (ClutterStageWindow *stage_window);

  void              (* set_title)               (ClutterStageWindow *stage_window,
                                                 const gchar        *title);
  void              (* set_cursor_visible)      (ClutterStageWindow *stage_window,
                                                 gboolean            cursor_visible);

  gboolean          (* realize)                 (ClutterStageWindow *stage_window);
  void              (* unrealize)               (ClutterStageWindow *stage_window);

  void              (* show)                    (ClutterStageWindow *stage_window,
                                                 gboolean            do_raise);
  void              (* hide)                    (ClutterStageWindow *stage_window);

  void              (* resize)                  (ClutterStageWindow *stage_window,
                                                 gint                width,
                                                 gint                height);
  void              (* get_geometry)            (ClutterStageWindow *stage_window,
                                                 cairo_rectangle_int_t *geometry);

  void              (* schedule_update)         (ClutterStageWindow *stage_window,
                                                 int                 sync_delay);
  gint64            (* get_update_time)         (ClutterStageWindow *stage_window);
  void              (* clear_update_time)       (ClutterStageWindow *stage_window);

  void              (* set_accept_focus)        (ClutterStageWindow *stage_window,
                                                 gboolean            accept_focus);

  void              (* redraw)                  (ClutterStageWindow *stage_window);

  gboolean          (* can_clip_redraws)        (ClutterStageWindow *stage_window);

  GList            *(* get_views)               (ClutterStageWindow *stage_window);
  int64_t           (* get_frame_counter)       (ClutterStageWindow *stage_window);
  void              (* finish_frame)            (ClutterStageWindow *stage_window);

  int64_t           (* get_next_presentation_time) (ClutterStageWindow *stage_window);
};

ClutterActor *    _clutter_stage_window_get_wrapper        (ClutterStageWindow *window);

void              _clutter_stage_window_set_title          (ClutterStageWindow *window,
                                                            const gchar        *title);
void              _clutter_stage_window_set_cursor_visible (ClutterStageWindow *window,
                                                            gboolean            is_visible);

gboolean          _clutter_stage_window_realize                 (ClutterStageWindow *window);
void              _clutter_stage_window_unrealize               (ClutterStageWindow *window);

void              _clutter_stage_window_show                    (ClutterStageWindow *window,
                                                                 gboolean            do_raise);
void              _clutter_stage_window_hide                    (ClutterStageWindow *window);

void              _clutter_stage_window_resize                  (ClutterStageWindow *window,
                                                                 gint                width,
                                                                 gint                height);
CLUTTER_EXPORT
void              _clutter_stage_window_get_geometry            (ClutterStageWindow *window,
                                                                 cairo_rectangle_int_t *geometry);
void              _clutter_stage_window_schedule_update         (ClutterStageWindow *window,
                                                                 int                 sync_delay);
gint64            _clutter_stage_window_get_update_time         (ClutterStageWindow *window);
void              _clutter_stage_window_clear_update_time       (ClutterStageWindow *window);

void              _clutter_stage_window_set_accept_focus        (ClutterStageWindow *window,
                                                                 gboolean            accept_focus);

void              _clutter_stage_window_redraw                  (ClutterStageWindow *window);

gboolean          _clutter_stage_window_can_clip_redraws        (ClutterStageWindow *window);

GList *           _clutter_stage_window_get_views               (ClutterStageWindow *window);

void              _clutter_stage_window_finish_frame            (ClutterStageWindow *window);

int64_t           _clutter_stage_window_get_frame_counter       (ClutterStageWindow *window);

int64_t           _clutter_stage_window_get_next_presentation_time (ClutterStageWindow *window);

G_END_DECLS

#endif /* __CLUTTER_STAGE_WINDOW_H__ */
