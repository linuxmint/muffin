/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2016 Red Hat Inc.
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
 */

#ifndef __CLUTTER_MUFFIN_H__
#define __CLUTTER_MUFFIN_H__

#define __CLUTTER_H_INSIDE__

#include "clutter-backend.h"
#include "clutter-macros.h"
#include "clutter-stage-view.h"
#include "cogl/clutter-stage-cogl.h"
#include "x11/clutter-stage-x11.h"

#include "clutter-actor-private.h"

#include "clutter-action.h"
#include "clutter-actor-meta-private.h"
#include "clutter-animatable.h"
#include "clutter-color-static.h"
#include "clutter-color.h"
#include "clutter-constraint-private.h"
#include "clutter-container.h"
#include "clutter-content-private.h"
#include "clutter-debug.h"
#include "clutter-easing.h"
#include "clutter-effect-private.h"
#include "clutter-enum-types.h"
#include "clutter-fixed-layout.h"
#include "clutter-flatten-effect.h"
#include "clutter-interval.h"
#include "clutter-main.h"
#include "clutter-marshal.h"
#include "clutter-paint-nodes.h"
#include "clutter-paint-node-private.h"
#include "clutter-paint-volume-private.h"
#include "clutter-private.h"
#include "clutter-property-transition.h"
#include "clutter-scriptable.h"
#include "clutter-script-private.h"
#include "clutter-stage-private.h"
#include "clutter-timeline.h"
#include "clutter-transition.h"
#include "clutter-units.h"

#include "deprecated/clutter-actor.h"
#include "deprecated/clutter-behaviour.h"
#include "deprecated/clutter-container.h"

typedef enum _SyncMethod /* In order of priority */
{                        /* SUPPORTED  LATENCY      SMOOTHNESS             */
  SYNC_NONE = 0,         /* Always     High         Poor                   */
  SYNC_FALLBACK,         /* Always     Medium       Medium                 */
  SYNC_SWAP_THROTTLING,  /* Usually    Medium-high  Medium, sometimes best */
  SYNC_PRESENTATION_TIME /* Usually    Low          Good, sometimes best   */
                         /* ^ As you can see SWAP_THROTTLING doesn't add much
                              value. And it does create the the very real
                              risk of blocking the main loop for up to 16ms
                              at a time. So it might be a good idea to retire
                              it in future and instead just make the backends
                              use swap interval 0 + PRESENTATION_TIME. */
} SyncMethod;

CLUTTER_AVAILABLE_IN_MUFFIN
SyncMethod _clutter_get_sync_method (void);

CLUTTER_AVAILABLE_IN_MUFFIN
void _clutter_set_sync_method (SyncMethod sync_method);

CLUTTER_AVAILABLE_IN_MUFFIN
void clutter_set_custom_backend_func (ClutterBackend *(* func) (void));

CLUTTER_AVAILABLE_IN_MUFFIN
gboolean        _clutter_get_sync_to_vblank     (void);

CLUTTER_AVAILABLE_IN_MUFFIN
void            _clutter_set_sync_to_vblank     (gboolean      sync_to_vblank);

CLUTTER_AVAILABLE_IN_MUFFIN
void clutter_master_clock_set_sync_method (SyncMethod method);

CLUTTER_AVAILABLE_IN_MUFFIN
void clutter_stage_x11_update_sync_state (ClutterStage *stage,
                                          SyncMethod    method);

CLUTTER_AVAILABLE_IN_MUFFIN
int64_t clutter_stage_get_frame_counter (ClutterStage *stage);

CLUTTER_AVAILABLE_IN_MUFFIN
void clutter_stage_capture_into (ClutterStage          *stage,
                                 gboolean               paint,
                                 cairo_rectangle_int_t *rect,
                                 uint8_t               *data);

/* 3 entries should be a good compromise, few layout managers
 * will ask for 3 different preferred size in each allocation cycle */
#define N_CACHED_SIZE_REQUESTS 3

/* ClutterActorPrivate  */

struct _ClutterActorPrivate
{
  /* request mode */
  ClutterRequestMode request_mode;

  /* our cached size requests for different width / height */
  SizeRequest width_requests[N_CACHED_SIZE_REQUESTS];
  SizeRequest height_requests[N_CACHED_SIZE_REQUESTS];

  /* An age of 0 means the entry is not set */
  guint cached_height_age;
  guint cached_width_age;

  /* the bounding box of the actor, relative to the parent's
   * allocation
   */
  ClutterActorBox allocation;
  ClutterAllocationFlags allocation_flags;

  /* clip, in actor coordinates */
  ClutterRect clip;

  /* the cached transformation matrix; see apply_transform() */
  CoglMatrix transform;

  guint8 opacity;
  gint opacity_override;

  ClutterOffscreenRedirect offscreen_redirect;

  /* This is an internal effect used to implement the
     offscreen-redirect property */
  ClutterEffect *flatten_effect;

  /* scene graph */
  ClutterActor *parent;
  ClutterActor *prev_sibling;
  ClutterActor *next_sibling;
  ClutterActor *first_child;
  ClutterActor *last_child;

  gint n_children;

  /* tracks whenever the children of an actor are changed; the
   * age is incremented by 1 whenever an actor is added or
   * removed. the age is not incremented when the first or the
   * last child pointers are changed, or when grandchildren of
   * an actor are changed.
   */
  gint age;

  gchar *name; /* a non-unique name, used for debugging */

  gint32 pick_id; /* per-stage unique id, used for picking */

  /* a back-pointer to the Pango context that we can use
   * to create pre-configured PangoLayout
   */
  PangoContext *pango_context;

  /* the text direction configured for this child - either by
   * application code, or by the actor's parent
   */
  ClutterTextDirection text_direction;

  /* a counter used to toggle the CLUTTER_INTERNAL_CHILD flag */
  gint internal_child;

  /* meta classes */
  ClutterMetaGroup *actions;
  ClutterMetaGroup *constraints;
  ClutterMetaGroup *effects;

  /* delegate object used to allocate the children of this actor */
  ClutterLayoutManager *layout_manager;

  /* delegate object used to paint the contents of this actor */
  ClutterContent *content;

  ClutterActorBox content_box;
  ClutterContentGravity content_gravity;
  ClutterScalingFilter min_filter;
  ClutterScalingFilter mag_filter;
  ClutterContentRepeat content_repeat;

  /* used when painting, to update the paint volume */
  ClutterEffect *current_effect;

  /* This is used to store an effect which needs to be redrawn. A
     redraw can be queued to start from a particular effect. This is
     used by parametrised effects that can cache an image of the
     actor. If a parameter of the effect changes then it only needs to
     redraw the cached image, not the actual actor. The pointer is
     only valid if is_dirty == TRUE. If the pointer is NULL then the
     whole actor is dirty. */
  ClutterEffect *effect_to_redraw;

  /* This is used when painting effects to implement the
     clutter_actor_continue_paint() function. It points to the node in
     the list of effects that is next in the chain */
  const GList *next_effect_to_paint;

  ClutterPaintVolume paint_volume;

  /* NB: This volume isn't relative to this actor, it is in eye
   * coordinates so that it can remain valid after the actor changes.
   */
  ClutterPaintVolume last_paint_volume;

  ClutterStageQueueRedrawEntry *queue_redraw_entry;

  ClutterColor bg_color;

#ifdef CLUTTER_ENABLE_DEBUG
  /* a string used for debugging messages */
  gchar *debug_name;
#endif

  /* a set of clones of the actor */
  GHashTable *clones;

  /* whether the actor is inside a cloned branch; this
   * value is propagated to all the actor's children
   */
  gulong in_cloned_branch;

  GListModel *child_model;
  ClutterActorCreateChildFunc create_child_func;
  gpointer create_child_data;
  GDestroyNotify create_child_notify;

  /* bitfields: KEEP AT THE END */

  /* fixed position and sizes */
  guint position_set                : 1;
  guint min_width_set               : 1;
  guint min_height_set              : 1;
  guint natural_width_set           : 1;
  guint natural_height_set          : 1;
  /* cached request is invalid (implies allocation is too) */
  guint needs_width_request         : 1;
  /* cached request is invalid (implies allocation is too) */
  guint needs_height_request        : 1;
  /* cached allocation is invalid (request has changed, probably) */
  guint needs_allocation            : 1;
  guint show_on_set_parent          : 1;
  guint has_clip                    : 1;
  guint clip_to_allocation          : 1;
  guint enable_model_view_transform : 1;
  guint enable_paint_unmapped       : 1;
  guint has_pointer                 : 1;
  guint propagated_one_redraw       : 1;
  guint paint_volume_valid          : 1;
  guint last_paint_volume_valid     : 1;
  guint in_clone_paint              : 1;
  guint transform_valid             : 1;
  /* This is TRUE if anything has queued a redraw since we were last
     painted. In this case effect_to_redraw will point to an effect
     the redraw was queued from or it will be NULL if the redraw was
     queued without an effect. */
  guint is_dirty                    : 1;
  guint bg_color_set                : 1;
  guint content_box_valid           : 1;
  guint x_expand_set                : 1;
  guint y_expand_set                : 1;
  guint needs_compute_expand        : 1;
  guint needs_x_expand              : 1;
  guint needs_y_expand              : 1;
  guint needs_paint_volume_update   : 1;
};

/* easy way to have properly named fields instead of the dummy ones
 * we use in the public structure
 */
typedef struct _RealActorIter
{
  ClutterActor *root;           /* dummy1 */
  ClutterActor *current;        /* dummy2 */
  gpointer padding_1;           /* dummy3 */
  gint age;                     /* dummy4 */
  gpointer padding_2;           /* dummy5 */
} RealActorIter;

#undef __CLUTTER_H_INSIDE__

#endif /* __CLUTTER_MUFFIN_H__ */
