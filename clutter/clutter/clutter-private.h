/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *
 * Copyright (C) 2006 OpenedHand
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 */

#ifndef __CLUTTER_PRIVATE_H__
#define __CLUTTER_PRIVATE_H__

#include <string.h>
#include <glib.h>

#include <cogl-pango/cogl-pango.h>

#include "clutter-backend.h"
#include "clutter-effect.h"
#include "clutter-event.h"
#include "clutter-feature.h"
#include "clutter-id-pool.h"
#include "clutter-layout-manager.h"
#include "clutter-master-clock.h"
#include "clutter-settings.h"
#include "clutter-stage-manager.h"
#include "clutter-stage.h"

G_BEGIN_DECLS

typedef struct _ClutterMainContext      ClutterMainContext;
typedef struct _ClutterVertex4          ClutterVertex4;

#define CLUTTER_REGISTER_VALUE_TRANSFORM_TO(TYPE_TO,func)             { \
  g_value_register_transform_func (g_define_type_id, TYPE_TO, func);    \
}

#define CLUTTER_REGISTER_VALUE_TRANSFORM_FROM(TYPE_FROM,func)         { \
  g_value_register_transform_func (TYPE_FROM, g_define_type_id, func);  \
}

#define CLUTTER_REGISTER_INTERVAL_PROGRESS(func)                      { \
  clutter_interval_register_progress_func (g_define_type_id, func);     \
}

#define CLUTTER_PRIVATE_FLAGS(a)	 (((ClutterActor *) (a))->private_flags)
#define CLUTTER_SET_PRIVATE_FLAGS(a,f)	 (CLUTTER_PRIVATE_FLAGS (a) |= (f))
#define CLUTTER_UNSET_PRIVATE_FLAGS(a,f) (CLUTTER_PRIVATE_FLAGS (a) &= ~(f))

#define CLUTTER_ACTOR_IS_TOPLEVEL(a)            ((CLUTTER_PRIVATE_FLAGS (a) & CLUTTER_IS_TOPLEVEL) != FALSE)
#define CLUTTER_ACTOR_IN_DESTRUCTION(a)         ((CLUTTER_PRIVATE_FLAGS (a) & CLUTTER_IN_DESTRUCTION) != FALSE)
#define CLUTTER_ACTOR_IN_REPARENT(a)            ((CLUTTER_PRIVATE_FLAGS (a) & CLUTTER_IN_REPARENT) != FALSE)
#define CLUTTER_ACTOR_IN_PAINT(a)               ((CLUTTER_PRIVATE_FLAGS (a) & CLUTTER_IN_PAINT) != FALSE)
#define CLUTTER_ACTOR_IN_PICK(a)                ((CLUTTER_PRIVATE_FLAGS (a) & CLUTTER_IN_PICK) != FALSE)
#define CLUTTER_ACTOR_IN_RELAYOUT(a)            ((CLUTTER_PRIVATE_FLAGS (a) & CLUTTER_IN_RELAYOUT) != FALSE)
#define CLUTTER_ACTOR_IN_PREF_WIDTH(a)          ((CLUTTER_PRIVATE_FLAGS (a) & CLUTTER_IN_PREF_WIDTH) != FALSE)
#define CLUTTER_ACTOR_IN_PREF_HEIGHT(a)         ((CLUTTER_PRIVATE_FLAGS (a) & CLUTTER_IN_PREF_HEIGHT) != FALSE)
#define CLUTTER_ACTOR_IN_PREF_SIZE(a)           ((CLUTTER_PRIVATE_FLAGS (a) & (CLUTTER_IN_PREF_HEIGHT|CLUTTER_IN_PREF_WIDTH)) != FALSE)

#define CLUTTER_PARAM_READABLE  (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)
#define CLUTTER_PARAM_WRITABLE  (G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS)
#define CLUTTER_PARAM_READWRITE (G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS)

#define CLUTTER_PARAM_ANIMATABLE        (1 << G_PARAM_USER_SHIFT)

/* automagic interning of a static string */
#define I_(str)  (g_intern_static_string ((str)))

/* keep this for source compatibility with clutter */
#define P_(String) (String)
#define N_(String) (String)

/* This is a replacement for the nearbyint function which always rounds to the
 * nearest integer. nearbyint is apparently a C99 function so it might not
 * always be available but also it seems in glibc it is defined as a function
 * call so this macro could end up faster anyway. We can't just add 0.5f
 * because it will break for negative numbers. */
#define CLUTTER_NEARBYINT(x) ((int) ((x) < 0.0f ? (x) - 0.5f : (x) + 0.5f))

typedef enum
{
  CLUTTER_ACTOR_UNUSED_FLAG = 0,

  CLUTTER_IN_DESTRUCTION = 1 << 0,
  CLUTTER_IS_TOPLEVEL    = 1 << 1,
  CLUTTER_IN_REPARENT    = 1 << 2,
  CLUTTER_IN_PREF_WIDTH  = 1 << 3,
  CLUTTER_IN_PREF_HEIGHT = 1 << 4,

  /* Used to avoid recursion */
  CLUTTER_IN_PAINT       = 1 << 5,
  CLUTTER_IN_PICK        = 1 << 6,

  /* Used to avoid recursion */
  CLUTTER_IN_RELAYOUT    = 1 << 7,
} ClutterPrivateFlags;

/*
 * ClutterMainContext:
 *
 * The shared state of Clutter
 */
struct _ClutterMainContext
{
  /* the main windowing system backend */
  ClutterBackend *backend;

  /* the object holding all the stage instances */
  ClutterStageManager *stage_manager;

  /* the clock driving all the frame operations */
  ClutterMasterClock *master_clock;

  /* the main event queue */
  GQueue *events_queue;

  /* the event filters added via clutter_event_add_filter. these are
   * ordered from least recently added to most recently added */
  GList *event_filters;

  ClutterPickMode  pick_mode;

  /* default FPS; this is only used if we cannot sync to vblank */
  guint frame_rate;

  /* fb bit masks for col<->id mapping in picking */
  gint fb_r_mask;
  gint fb_g_mask;
  gint fb_b_mask;
  gint fb_r_mask_used;
  gint fb_g_mask_used;
  gint fb_b_mask_used;

  CoglPangoFontMap *font_map;   /* Global font map */

  /* stack of #ClutterEvent */
  GSList *current_event;

  /* list of repaint functions installed through
   * clutter_threads_add_repaint_func()
   */
  GList *repaint_funcs;
  guint last_repaint_id;

  /* main settings singleton */
  ClutterSettings *settings;

  /* boolean flags */
  guint is_initialized          : 1;
  guint defer_display_setup     : 1;
  guint options_parsed          : 1;
  guint show_fps                : 1;
};

/* shared between clutter-main.c and clutter-frame-source.c */
typedef struct
{
  GSourceFunc func;
  gpointer data;
  GDestroyNotify notify;
} ClutterThreadsDispatch;

gboolean _clutter_threads_dispatch      (gpointer data);
void     _clutter_threads_dispatch_free (gpointer data);

CLUTTER_EXPORT
void                    _clutter_threads_acquire_lock                   (void);
CLUTTER_EXPORT
void                    _clutter_threads_release_lock                   (void);

ClutterMainContext *    _clutter_context_get_default                    (void);
void                    _clutter_context_lock                           (void);
void                    _clutter_context_unlock                         (void);
gboolean                _clutter_context_is_initialized                 (void);
ClutterPickMode         _clutter_context_get_pick_mode                  (void);
gboolean                _clutter_context_get_show_fps                   (void);

gboolean      _clutter_feature_init (GError **error);

/* Diagnostic mode */
gboolean        _clutter_diagnostic_enabled     (void);
void            _clutter_diagnostic_message     (const char *fmt, ...) G_GNUC_PRINTF (1, 2);

CLUTTER_EXPORT
void            _clutter_set_sync_to_vblank     (gboolean      sync_to_vblank);

/* use this function as the accumulator if you have a signal with
 * a G_TYPE_BOOLEAN return value; this will stop the emission as
 * soon as one handler returns TRUE
 */
gboolean _clutter_boolean_handled_accumulator (GSignalInvocationHint *ihint,
                                               GValue                *return_accu,
                                               const GValue          *handler_return,
                                               gpointer               dummy);

/* use this function as the accumulator if you have a signal with
 * a G_TYPE_BOOLEAN return value; this will stop the emission as
 * soon as one handler returns FALSE
 */
gboolean _clutter_boolean_continue_accumulator (GSignalInvocationHint *ihint,
                                                GValue                *return_accu,
                                                const GValue          *handler_return,
                                                gpointer               dummy);

void _clutter_run_repaint_functions (ClutterRepaintFlags flags);

GType _clutter_layout_manager_get_child_meta_type (ClutterLayoutManager *manager);

void  _clutter_util_fully_transform_vertices (const CoglMatrix         *modelview,
                                              const CoglMatrix         *projection,
                                              const float              *viewport,
                                              const graphene_point3d_t *vertices_in,
                                              graphene_point3d_t       *vertices_out,
                                              int                       n_vertices);

void _clutter_util_rect_from_rectangle (const cairo_rectangle_int_t *src,
                                        graphene_rect_t             *dest);

void _clutter_util_rectangle_int_extents (const graphene_rect_t *src,
                                          cairo_rectangle_int_t *dest);

void _clutter_util_rectangle_offset (const cairo_rectangle_int_t *src,
                                     int                          x,
                                     int                          y,
                                     cairo_rectangle_int_t       *dest);

void _clutter_util_rectangle_union (const cairo_rectangle_int_t *src1,
                                    const cairo_rectangle_int_t *src2,
                                    cairo_rectangle_int_t       *dest);

gboolean _clutter_util_rectangle_intersection (const cairo_rectangle_int_t *src1,
                                               const cairo_rectangle_int_t *src2,
                                               cairo_rectangle_int_t       *dest);

gboolean clutter_util_rectangle_equal (const cairo_rectangle_int_t *src1,
                                       const cairo_rectangle_int_t *src2);


struct _ClutterVertex4
{
  float x;
  float y;
  float z;
  float w;
};

void
_clutter_util_vertex4_interpolate (const ClutterVertex4 *a,
                                   const ClutterVertex4 *b,
                                   double                progress,
                                   ClutterVertex4       *res);

#define CLUTTER_MATRIX_INIT_IDENTITY { \
  1.0f, 0.0f, 0.0f, 0.0f, \
  0.0f, 1.0f, 0.0f, 0.0f, \
  0.0f, 0.0f, 1.0f, 0.0f, \
  0.0f, 0.0f, 0.0f, 1.0f, \
}

float   _clutter_util_matrix_determinant        (const ClutterMatrix *matrix);

void    _clutter_util_matrix_skew_xy            (ClutterMatrix *matrix,
                                                 float          factor);
void    _clutter_util_matrix_skew_xz            (ClutterMatrix *matrix,
                                                 float          factor);
void    _clutter_util_matrix_skew_yz            (ClutterMatrix *matrix,
                                                 float          factor);

gboolean        _clutter_util_matrix_decompose  (const ClutterMatrix *src,
                                                 graphene_point3d_t  *scale_p,
                                                 float                shear_p[3],
                                                 graphene_point3d_t  *rotate_p,
                                                 graphene_point3d_t  *translate_p,
                                                 ClutterVertex4      *perspective_p);

CLUTTER_EXPORT
PangoDirection _clutter_pango_unichar_direction (gunichar ch);

PangoDirection _clutter_pango_find_base_dir     (const gchar *text,
                                                 gint         length);

typedef struct _ClutterPlane
{
  graphene_vec3_t v0;
  graphene_vec3_t n;
} ClutterPlane;

typedef enum _ClutterCullResult
{
  CLUTTER_CULL_RESULT_UNKNOWN,
  CLUTTER_CULL_RESULT_IN,
  CLUTTER_CULL_RESULT_OUT,
  CLUTTER_CULL_RESULT_PARTIAL
} ClutterCullResult;

gboolean        _clutter_has_progress_function  (GType gtype);
gboolean        _clutter_run_progress_function  (GType gtype,
                                                 const GValue *initial,
                                                 const GValue *final,
                                                 gdouble progress,
                                                 GValue *retval);

void            clutter_timeline_cancel_delay (ClutterTimeline *timeline);

G_END_DECLS

#endif /* __CLUTTER_PRIVATE_H__ */
