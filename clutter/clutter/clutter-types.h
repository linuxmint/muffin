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
 */

#ifndef __CLUTTER_TYPES_H__
#define __CLUTTER_TYPES_H__

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <cairo.h>
#include <cogl/cogl.h>
#include <clutter/clutter-macros.h>
#include <clutter/clutter-enums.h>

#include <graphene-gobject.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_ACTOR_BOX          (clutter_actor_box_get_type ())
#define CLUTTER_TYPE_KNOT               (clutter_knot_get_type ())
#define CLUTTER_TYPE_MARGIN             (clutter_margin_get_type ())
#define CLUTTER_TYPE_MATRIX             (clutter_matrix_get_type ())
#define CLUTTER_TYPE_PAINT_VOLUME       (clutter_paint_volume_get_type ())
#define CLUTTER_TYPE_PERSPECTIVE        (clutter_perspective_get_type ())

typedef struct _ClutterActor                    ClutterActor;

typedef struct _ClutterStage                    ClutterStage;
typedef struct _ClutterFrameInfo                ClutterFrameInfo;
typedef struct _ClutterContainer                ClutterContainer; /* dummy */
typedef struct _ClutterChildMeta                ClutterChildMeta;
typedef struct _ClutterLayoutMeta               ClutterLayoutMeta;
typedef struct _ClutterActorMeta                ClutterActorMeta;
typedef struct _ClutterLayoutManager            ClutterLayoutManager;
typedef struct _ClutterActorIter                ClutterActorIter;
typedef struct _ClutterPaintNode                ClutterPaintNode;
typedef struct _ClutterContent                  ClutterContent; /* dummy */
typedef struct _ClutterScrollActor	        ClutterScrollActor;

typedef struct _ClutterInterval         	ClutterInterval;
typedef struct _ClutterAnimatable       	ClutterAnimatable; /* dummy */
typedef struct _ClutterTimeline         	ClutterTimeline;
typedef struct _ClutterTransition       	ClutterTransition;
typedef struct _ClutterPropertyTransition       ClutterPropertyTransition;
typedef struct _ClutterKeyframeTransition       ClutterKeyframeTransition;
typedef struct _ClutterTransitionGroup		ClutterTransitionGroup;

typedef struct _ClutterAction                   ClutterAction;
typedef struct _ClutterConstraint               ClutterConstraint;
typedef struct _ClutterEffect                   ClutterEffect;

typedef struct _ClutterPath                     ClutterPath;
typedef struct _ClutterPathNode                 ClutterPathNode;

typedef struct _ClutterActorBox                 ClutterActorBox;
typedef struct _ClutterColor                    ClutterColor;
typedef struct _ClutterKnot                     ClutterKnot;
typedef struct _ClutterMargin                   ClutterMargin;
typedef struct _ClutterPerspective              ClutterPerspective;

typedef struct _ClutterAlpha            	ClutterAlpha;
typedef struct _ClutterAnimation                ClutterAnimation;
typedef struct _ClutterState            	ClutterState;

typedef struct _ClutterInputDeviceTool          ClutterInputDeviceTool;
typedef struct _ClutterInputDevice              ClutterInputDevice;
typedef struct _ClutterVirtualInputDevice       ClutterVirtualInputDevice;

typedef struct _ClutterInputMethod              ClutterInputMethod;
typedef struct _ClutterInputFocus               ClutterInputFocus;

typedef CoglMatrix                              ClutterMatrix;

typedef union _ClutterEvent                     ClutterEvent;

/**
 * ClutterEventSequence:
 *
 * The #ClutterEventSequence structure is an opaque
 * type used to denote the event sequence of a touch event.
 *
 * Since: 1.12
 */
typedef struct _ClutterEventSequence            ClutterEventSequence;

typedef struct _ClutterShader                   ClutterShader; /* deprecated */

/**
 * ClutterPaintVolume:
 *
 * #ClutterPaintVolume is an opaque structure
 * whose members cannot be directly accessed.
 *
 * A #ClutterPaintVolume represents an
 * a bounding volume whose internal representation isn't defined but
 * can be set and queried in terms of an axis aligned bounding box.
 *
 * A #ClutterPaintVolume for a #ClutterActor
 * is defined to be relative from the current actor modelview matrix.
 *
 * Other internal representation and methods for describing the
 * bounding volume may be added in the future.
 *
 * Since: 1.4
 */
typedef struct _ClutterPaintVolume      ClutterPaintVolume;

/**
 * ClutterActorBox:
 * @x1: X coordinate of the top left corner
 * @y1: Y coordinate of the top left corner
 * @x2: X coordinate of the bottom right corner
 * @y2: Y coordinate of the bottom right corner
 *
 * Bounding box of an actor. The coordinates of the top left and right bottom
 * corners of an actor. The coordinates of the two points are expressed in
 * pixels with sub-pixel precision
 */
struct _ClutterActorBox
{
  gfloat x1;
  gfloat y1;

  gfloat x2;
  gfloat y2;
};

/**
 * CLUTTER_ACTOR_BOX_INIT:
 * @x_1: the X coordinate of the top left corner
 * @y_1: the Y coordinate of the top left corner
 * @x_2: the X coordinate of the bottom right corner
 * @y_2: the Y coordinate of the bottom right corner
 *
 * A simple macro for initializing a #ClutterActorBox when declaring
 * it, e.g.:
 *
 * |[
 *   ClutterActorBox box = CLUTTER_ACTOR_BOX_INIT (0, 0, 400, 600);
 * ]|
 *
 * Since: 1.10
 */
#define CLUTTER_ACTOR_BOX_INIT(x_1,y_1,x_2,y_2)         { (x_1), (y_1), (x_2), (y_2) }

/**
 * CLUTTER_ACTOR_BOX_INIT_ZERO:
 *
 * A simple macro for initializing a #ClutterActorBox to 0 when
 * declaring it, e.g.:
 *
 * |[
 *   ClutterActorBox box = CLUTTER_ACTOR_BOX_INIT_ZERO;
 * ]|
 *
 * Since: 1.12
 */
#define CLUTTER_ACTOR_BOX_INIT_ZERO                     CLUTTER_ACTOR_BOX_INIT (0.f, 0.f, 0.f, 0.f)

CLUTTER_EXPORT
GType            clutter_actor_box_get_type      (void) G_GNUC_CONST;
CLUTTER_EXPORT
ClutterActorBox *clutter_actor_box_new           (gfloat                 x_1,
                                                  gfloat                 y_1,
                                                  gfloat                 x_2,
                                                  gfloat                 y_2);
CLUTTER_EXPORT
ClutterActorBox *clutter_actor_box_alloc         (void);
CLUTTER_EXPORT
ClutterActorBox *clutter_actor_box_init          (ClutterActorBox       *box,
                                                  gfloat                 x_1,
                                                  gfloat                 y_1,
                                                  gfloat                 x_2,
                                                  gfloat                 y_2);
CLUTTER_EXPORT
void             clutter_actor_box_init_rect     (ClutterActorBox       *box,
                                                  gfloat                 x,
                                                  gfloat                 y,
                                                  gfloat                 width,
                                                  gfloat                 height);
CLUTTER_EXPORT
ClutterActorBox *clutter_actor_box_copy          (const ClutterActorBox *box);
CLUTTER_EXPORT
void             clutter_actor_box_free          (ClutterActorBox       *box);
CLUTTER_EXPORT
gboolean         clutter_actor_box_equal         (const ClutterActorBox *box_a,
                                                  const ClutterActorBox *box_b);
CLUTTER_EXPORT
gfloat           clutter_actor_box_get_x         (const ClutterActorBox *box);
CLUTTER_EXPORT
gfloat           clutter_actor_box_get_y         (const ClutterActorBox *box);
CLUTTER_EXPORT
gfloat           clutter_actor_box_get_width     (const ClutterActorBox *box);
CLUTTER_EXPORT
gfloat           clutter_actor_box_get_height    (const ClutterActorBox *box);
CLUTTER_EXPORT
void             clutter_actor_box_get_origin    (const ClutterActorBox *box,
                                                  gfloat                *x,
                                                  gfloat                *y);
CLUTTER_EXPORT
void             clutter_actor_box_get_size      (const ClutterActorBox *box,
                                                  gfloat                *width,
                                                  gfloat                *height);
CLUTTER_EXPORT
gfloat           clutter_actor_box_get_area      (const ClutterActorBox *box);
CLUTTER_EXPORT
gboolean         clutter_actor_box_contains      (const ClutterActorBox *box,
                                                  gfloat                 x,
                                                  gfloat                 y);
CLUTTER_EXPORT
void             clutter_actor_box_from_vertices (ClutterActorBox          *box,
                                                  const graphene_point3d_t  verts[]);
CLUTTER_EXPORT
void             clutter_actor_box_interpolate   (const ClutterActorBox *initial,
                                                  const ClutterActorBox *final,
                                                  gdouble                progress,
                                                  ClutterActorBox       *result);
CLUTTER_EXPORT
void             clutter_actor_box_clamp_to_pixel (ClutterActorBox       *box);
CLUTTER_EXPORT
void             clutter_actor_box_union          (const ClutterActorBox *a,
                                                   const ClutterActorBox *b,
                                                   ClutterActorBox       *result);

CLUTTER_EXPORT
void             clutter_actor_box_set_origin     (ClutterActorBox       *box,
                                                   gfloat                 x,
                                                   gfloat                 y);
CLUTTER_EXPORT
void             clutter_actor_box_set_size       (ClutterActorBox       *box,
                                                   gfloat                 width,
                                                   gfloat                 height);

CLUTTER_EXPORT
void             clutter_actor_box_scale          (ClutterActorBox       *box,
                                                   gfloat                 scale);

/**
 * ClutterKnot:
 * @x: X coordinate of the knot
 * @y: Y coordinate of the knot
 *
 * Point in a path behaviour.
 *
 * Since: 0.2
 */
struct _ClutterKnot
{
  gint x;
  gint y;
};

CLUTTER_EXPORT
GType        clutter_knot_get_type (void) G_GNUC_CONST;
CLUTTER_EXPORT
ClutterKnot *clutter_knot_copy     (const ClutterKnot *knot);
CLUTTER_EXPORT
void         clutter_knot_free     (ClutterKnot       *knot);
CLUTTER_EXPORT
gboolean     clutter_knot_equal    (const ClutterKnot *knot_a,
                                    const ClutterKnot *knot_b);

/**
 * ClutterPathNode:
 * @type: the node's type
 * @points: the coordinates of the node
 *
 * Represents a single node of a #ClutterPath.
 *
 * Some of the coordinates in @points may be unused for some node
 * types. %CLUTTER_PATH_MOVE_TO and %CLUTTER_PATH_LINE_TO use only one
 * pair of coordinates, %CLUTTER_PATH_CURVE_TO uses all three and
 * %CLUTTER_PATH_CLOSE uses none.
 *
 * Since: 1.0
 */
struct _ClutterPathNode
{
  ClutterPathNodeType type;

  ClutterKnot points[3];
};

CLUTTER_EXPORT
GType clutter_path_node_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterPathNode *clutter_path_node_copy  (const ClutterPathNode *node);
CLUTTER_EXPORT
void             clutter_path_node_free  (ClutterPathNode       *node);
CLUTTER_EXPORT
gboolean         clutter_path_node_equal (const ClutterPathNode *node_a,
                                          const ClutterPathNode *node_b);

/*
 * ClutterPaintVolume
 */

CLUTTER_EXPORT
GType clutter_paint_volume_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterPaintVolume *clutter_paint_volume_copy                (const ClutterPaintVolume *pv);
CLUTTER_EXPORT
void                clutter_paint_volume_free                (ClutterPaintVolume       *pv);

CLUTTER_EXPORT
void                clutter_paint_volume_set_origin          (ClutterPaintVolume       *pv,
                                                              const graphene_point3d_t *origin);
CLUTTER_EXPORT
void                clutter_paint_volume_get_origin          (const ClutterPaintVolume *pv,
                                                              graphene_point3d_t       *vertex);
CLUTTER_EXPORT
void                clutter_paint_volume_set_width           (ClutterPaintVolume       *pv,
                                                              gfloat                    width);
CLUTTER_EXPORT
gfloat              clutter_paint_volume_get_width           (const ClutterPaintVolume *pv);
CLUTTER_EXPORT
void                clutter_paint_volume_set_height          (ClutterPaintVolume       *pv,
                                                              gfloat                    height);
CLUTTER_EXPORT
gfloat              clutter_paint_volume_get_height          (const ClutterPaintVolume *pv);
CLUTTER_EXPORT
void                clutter_paint_volume_set_depth           (ClutterPaintVolume       *pv,
                                                              gfloat                    depth);
CLUTTER_EXPORT
gfloat              clutter_paint_volume_get_depth           (const ClutterPaintVolume *pv);
CLUTTER_EXPORT
void                clutter_paint_volume_union               (ClutterPaintVolume       *pv,
                                                              const ClutterPaintVolume *another_pv);
CLUTTER_EXPORT
void                clutter_paint_volume_union_box           (ClutterPaintVolume       *pv,
                                                              const ClutterActorBox    *box);

CLUTTER_EXPORT
gboolean            clutter_paint_volume_set_from_allocation (ClutterPaintVolume       *pv,
                                                              ClutterActor             *actor);

/**
 * ClutterMargin:
 * @left: the margin from the left
 * @right: the margin from the right
 * @top: the margin from the top
 * @bottom: the margin from the bottom
 *
 * A representation of the components of a margin.
 *
 * Since: 1.10
 */
struct _ClutterMargin
{
  float left;
  float right;
  float top;
  float bottom;
};

CLUTTER_EXPORT
GType clutter_margin_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterMargin * clutter_margin_new      (void) G_GNUC_MALLOC;
CLUTTER_EXPORT
ClutterMargin * clutter_margin_copy     (const ClutterMargin *margin_);
CLUTTER_EXPORT
void            clutter_margin_free     (ClutterMargin       *margin_);

/**
 * ClutterProgressFunc:
 * @a: the initial value of an interval
 * @b: the final value of an interval
 * @progress: the progress factor, between 0 and 1
 * @retval: the value used to store the progress
 *
 * Prototype of the progress function used to compute the value
 * between the two ends @a and @b of an interval depending on
 * the value of @progress.
 *
 * The #GValue in @retval is already initialized with the same
 * type as @a and @b.
 *
 * This function will be called by #ClutterInterval if the
 * type of the values of the interval was registered using
 * clutter_interval_register_progress_func().
 *
 * Return value: %TRUE if the function successfully computed
 *   the value and stored it inside @retval
 *
 * Since: 1.0
 */
typedef gboolean (* ClutterProgressFunc) (const GValue *a,
                                          const GValue *b,
                                          gdouble       progress,
                                          GValue       *retval);

CLUTTER_EXPORT
void clutter_interval_register_progress_func (GType               value_type,
                                              ClutterProgressFunc func);

CLUTTER_EXPORT
GType clutter_matrix_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterMatrix * clutter_matrix_alloc            (void);
CLUTTER_EXPORT
ClutterMatrix * clutter_matrix_init_identity    (ClutterMatrix       *matrix);
CLUTTER_EXPORT
ClutterMatrix * clutter_matrix_init_from_array  (ClutterMatrix       *matrix,
                                                 const float          values[16]);
CLUTTER_EXPORT
ClutterMatrix * clutter_matrix_init_from_matrix (ClutterMatrix       *a,
                                                 const ClutterMatrix *b);
CLUTTER_EXPORT
void            clutter_matrix_free             (ClutterMatrix       *matrix);

G_END_DECLS

#endif /* __CLUTTER_TYPES_H__ */
