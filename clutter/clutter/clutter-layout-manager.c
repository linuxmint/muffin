/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2009  Intel Corporation.
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
 * Author:
 *   Emmanuele Bassi <ebassi@linux.intel.com>
 */

/**
 * SECTION:clutter-layout-manager
 * @short_description: Layout managers base class
 *
 * #ClutterLayoutManager is a base abstract class for layout managers. A
 * layout manager implements the layouting policy for a composite or a
 * container actor: it controls the preferred size of the actor to which
 * it has been paired, and it controls the allocation of its children.
 *
 * Any composite or container #ClutterActor subclass can delegate the
 * layouting of its children to a #ClutterLayoutManager. Clutter provides
 * a generic container using #ClutterLayoutManager called #ClutterBox.
 *
 * Clutter provides some simple #ClutterLayoutManager sub-classes, like
 * #ClutterFlowLayout and #ClutterBinLayout.
 *
 * ## Implementing a ClutterLayoutManager
 * The implementation of a layout manager does not differ from  the
 * implementation of the size requisition and allocation bits of
 * #ClutterActor, so you should read the relative documentation
 * forr subclassing #ClutterActor.
 *
 * The layout manager implementation can hold a back pointer to the
 * #ClutterContainer by implementing the #ClutterLayoutManagerClass.set_container()
 * virtual function. The layout manager should not hold a real reference (i.e.
 * call g_object_ref()) on the container actor, to avoid reference cycles.
 *
 * If a layout manager has properties affecting the layout policies then it should
 * emit the #ClutterLayoutManager::layout-changed signal on itself by using the
 * clutter_layout_manager_layout_changed() function whenever one of these properties
 * changes.
 *
 * ## Layout Properties
 *
 * If a layout manager has layout properties, that is properties that
 * should exist only as the result of the presence of a specific (layout
 * manager, container actor, child actor) combination, and it wishes to store
 * those properties inside a #ClutterLayoutMeta, then it should override the
 * #ClutterLayoutManagerClass.get_child_meta_type() virtual function to return
 * the #GType of the #ClutterLayoutMeta sub-class used to store the layout
 * properties; optionally, the #ClutterLayoutManager sub-class might also
 * override the #ClutterLayoutManagerClass.create_child_meta() virtual function
 * to control how the #ClutterLayoutMeta instance is created, otherwise the
 * default implementation will be equivalent to:
 *
 * |[
 *  ClutterLayoutManagerClass *klass;
 *  GType meta_type;
 *
 *  klass = CLUTTER_LAYOUT_MANAGER_GET_CLASS (manager);
 *  meta_type = klass->get_child_meta_type (manager);
 *
 *  return g_object_new (meta_type,
 *                       "manager", manager,
 *                       "container", container,
 *                       "actor", actor,
 *                       NULL);
 * ]|
 *
 * Where `manager` is the  #ClutterLayoutManager, `container` is the
 * #ClutterContainer using the #ClutterLayoutManager, and `actor` is
 * the #ClutterActor child of the #ClutterContainer.
 *
 * ## Using ClutterLayoutManager with ClutterScript
 *
 * #ClutterLayoutManager instances can be created in the same way
 * as other objects in #ClutterScript; properties can be set using the
 * common syntax.
 *
 * Layout properties can be set on children of a container with
 * a #ClutterLayoutManager using the `layout::` modifier on the property
 * name, for instance:
 *
 * |[
 * {
 *   "type" : "ClutterBox",
 *   "layout-manager" : { "type" : "ClutterTableLayout" },
 *   "children" : [
 *     {
 *       "type" : "ClutterTexture",
 *       "filename" : "image-00.png",
 *
 *       "layout::row" : 0,
 *       "layout::column" : 0,
 *       "layout::x-align" : "left",
 *       "layout::y-align" : "center",
 *       "layout::x-expand" : true,
 *       "layout::y-expand" : true
 *     },
 *     {
 *       "type" : "ClutterTexture",
 *       "filename" : "image-01.png",
 *
 *       "layout::row" : 0,
 *       "layout::column" : 1,
 *       "layout::x-align" : "right",
 *       "layout::y-align" : "center",
 *       "layout::x-expand" : true,
 *       "layout::y-expand" : true
 *     }
 *   ]
 * }
 * ]|
 *
 * #ClutterLayoutManager is available since Clutter 1.2
 */

#ifdef HAVE_CONFIG_H
#include "clutter-build-config.h"
#endif

#include <glib-object.h>
#include <gobject/gvaluecollector.h>

#define CLUTTER_DISABLE_DEPRECATION_WARNINGS
#include "deprecated/clutter-container.h"
#include "deprecated/clutter-alpha.h"

#include "clutter-debug.h"
#include "clutter-layout-manager.h"
#include "clutter-layout-meta.h"
#include "clutter-marshal.h"
#include "clutter-private.h"
#include "clutter-timeline.h"

#define LAYOUT_MANAGER_WARN_NOT_IMPLEMENTED(m,method)   G_STMT_START {  \
        GObject *_obj = G_OBJECT (m);                                   \
        g_warning ("Layout managers of type %s do not implement "       \
                   "the ClutterLayoutManager::%s method",               \
                   G_OBJECT_TYPE_NAME (_obj),                           \
                   (method));                           } G_STMT_END

enum
{
  LAYOUT_CHANGED,

  LAST_SIGNAL
};

G_DEFINE_ABSTRACT_TYPE (ClutterLayoutManager,
                        clutter_layout_manager,
                        G_TYPE_INITIALLY_UNOWNED)

static GQuark quark_layout_meta  = 0;
static GQuark quark_layout_alpha = 0;

static guint manager_signals[LAST_SIGNAL] = { 0, };

static void
layout_manager_freeze_layout_change (ClutterLayoutManager *manager)
{
  gpointer is_frozen;

  CLUTTER_NOTE (LAYOUT, "Freezing changes for manager '%s'[%p]",
                G_OBJECT_TYPE_NAME (manager),
                manager);

  is_frozen = g_object_get_data (G_OBJECT (manager), "freeze-change");
  if (is_frozen == NULL)
    g_object_set_data (G_OBJECT (manager), "freeze-change",
                       GUINT_TO_POINTER (1));
  else
    {
      guint level = GPOINTER_TO_UINT (is_frozen) + 1;

      g_object_set_data (G_OBJECT (manager), "freeze-change",
                         GUINT_TO_POINTER (level));
    }
}

static void
layout_manager_thaw_layout_change (ClutterLayoutManager *manager)
{
  gpointer is_frozen;

  is_frozen = g_object_get_data (G_OBJECT (manager), "freeze-change");
  if (is_frozen == NULL)
    g_critical (G_STRLOC ": Mismatched thaw; you have to call "
                "clutter_layout_manager_freeze_layout_change() prior to "
                "calling clutter_layout_manager_thaw_layout_change()");
  else
    {
      guint level = GPOINTER_TO_UINT (is_frozen);

      g_assert (level > 0);

      CLUTTER_NOTE (LAYOUT, "Thawing changes for manager '%s'[%p]",
                    G_OBJECT_TYPE_NAME (manager),
                    manager);

      level -= 1;
      if (level == 0)
        g_object_set_data (G_OBJECT (manager), "freeze-change", NULL);
      else
        g_object_set_data (G_OBJECT (manager), "freeze-change",
                           GUINT_TO_POINTER (level));
    }

}

static void
layout_manager_real_get_preferred_width (ClutterLayoutManager *manager,
                                         ClutterContainer     *container,
                                         gfloat                for_height,
                                         gfloat               *min_width_p,
                                         gfloat               *nat_width_p)
{
  LAYOUT_MANAGER_WARN_NOT_IMPLEMENTED (manager, "get_preferred_width");

  if (min_width_p)
    *min_width_p = 0.0;

  if (nat_width_p)
    *nat_width_p = 0.0;
}

static void
layout_manager_real_get_preferred_height (ClutterLayoutManager *manager,
                                          ClutterContainer     *container,
                                          gfloat                for_width,
                                          gfloat               *min_height_p,
                                          gfloat               *nat_height_p)
{
  LAYOUT_MANAGER_WARN_NOT_IMPLEMENTED (manager, "get_preferred_height");

  if (min_height_p)
    *min_height_p = 0.0;

  if (nat_height_p)
    *nat_height_p = 0.0;
}

static void
layout_manager_real_allocate (ClutterLayoutManager   *manager,
                              ClutterContainer       *container,
                              const ClutterActorBox  *allocation,
                              ClutterAllocationFlags  flags)
{
  LAYOUT_MANAGER_WARN_NOT_IMPLEMENTED (manager, "allocate");
}

static void
layout_manager_real_set_container (ClutterLayoutManager *manager,
                                   ClutterContainer     *container)
{
  if (container != NULL)
    g_object_set_data (G_OBJECT (container), "clutter-layout-manager", manager);
}

static ClutterLayoutMeta *
layout_manager_real_create_child_meta (ClutterLayoutManager *manager,
                                       ClutterContainer     *container,
                                       ClutterActor         *actor)
{
  ClutterLayoutManagerClass *klass;
  GType meta_type;

  klass = CLUTTER_LAYOUT_MANAGER_GET_CLASS (manager);
  meta_type = klass->get_child_meta_type (manager);

  /* provide a default implementation to reduce common code */
  if (meta_type != G_TYPE_INVALID)
    {
      g_assert (g_type_is_a (meta_type, CLUTTER_TYPE_LAYOUT_META));

      return g_object_new (meta_type,
                           "manager", manager,
                           "container", container,
                           "actor", actor,
                           NULL);
    }

  return NULL;
}

static GType
layout_manager_real_get_child_meta_type (ClutterLayoutManager *manager)
{
  return G_TYPE_INVALID;
}

/* XXX:2.0 - Remove */
static ClutterAlpha *
layout_manager_real_begin_animation (ClutterLayoutManager *manager,
                                     guint                 duration,
                                     gulong                mode)
{
  ClutterTimeline *timeline;
  ClutterAlpha *alpha;

  alpha = g_object_get_qdata (G_OBJECT (manager), quark_layout_alpha);
  if (alpha != NULL)
    {
      clutter_alpha_set_mode (alpha, mode);

      timeline = clutter_alpha_get_timeline (alpha);
      clutter_timeline_set_duration (timeline, duration);
      clutter_timeline_rewind (timeline);

      return alpha;
    };

  timeline = clutter_timeline_new (duration);

  alpha = clutter_alpha_new_full (timeline, mode);

  /* let the alpha take ownership of the timeline */
  g_object_unref (timeline);

  g_signal_connect_swapped (timeline, "completed",
                            G_CALLBACK (clutter_layout_manager_end_animation),
                            manager);
  g_signal_connect_swapped (timeline, "new-frame",
                            G_CALLBACK (clutter_layout_manager_layout_changed),
                            manager);

  g_object_set_qdata_full (G_OBJECT (manager),
                           quark_layout_alpha, alpha,
                           (GDestroyNotify) g_object_unref);

  clutter_timeline_start (timeline);

  return alpha;
}

/* XXX:2.0 - Remove */
static gdouble
layout_manager_real_get_animation_progress (ClutterLayoutManager *manager)
{
  ClutterAlpha *alpha;

  alpha = g_object_get_qdata (G_OBJECT (manager), quark_layout_alpha);
  if (alpha == NULL)
    return 1.0;

  return clutter_alpha_get_alpha (alpha);
}

/* XXX:2.0 - Remove */
static void
layout_manager_real_end_animation (ClutterLayoutManager *manager)
{
  ClutterTimeline *timeline;
  ClutterAlpha *alpha;

  alpha = g_object_get_qdata (G_OBJECT (manager), quark_layout_alpha);
  if (alpha == NULL)
    return;

  timeline = clutter_alpha_get_timeline (alpha);
  g_assert (timeline != NULL);

  if (clutter_timeline_is_playing (timeline))
    clutter_timeline_stop (timeline);

  g_signal_handlers_disconnect_by_func (timeline,
                                        G_CALLBACK (clutter_layout_manager_end_animation),
                                        manager);
  g_signal_handlers_disconnect_by_func (timeline,
                                        G_CALLBACK (clutter_layout_manager_layout_changed),
                                        manager);

  g_object_set_qdata (G_OBJECT (manager), quark_layout_alpha, NULL);

  clutter_layout_manager_layout_changed (manager);
}

static void
clutter_layout_manager_class_init (ClutterLayoutManagerClass *klass)
{
  quark_layout_meta =
    g_quark_from_static_string ("clutter-layout-manager-child-meta");

  /* XXX:2.0 - Remove */
  quark_layout_alpha =
    g_quark_from_static_string ("clutter-layout-manager-alpha");

  klass->get_preferred_width = layout_manager_real_get_preferred_width;
  klass->get_preferred_height = layout_manager_real_get_preferred_height;
  klass->allocate = layout_manager_real_allocate;
  klass->create_child_meta = layout_manager_real_create_child_meta;
  klass->get_child_meta_type = layout_manager_real_get_child_meta_type;

  /* XXX:2.0 - Remove */
  klass->begin_animation = layout_manager_real_begin_animation;
  klass->get_animation_progress = layout_manager_real_get_animation_progress;
  klass->end_animation = layout_manager_real_end_animation;
  klass->set_container = layout_manager_real_set_container;

  /**
   * ClutterLayoutManager::layout-changed:
   * @manager: the #ClutterLayoutManager that emitted the signal
   *
   * The ::layout-changed signal is emitted each time a layout manager
   * has been changed. Every #ClutterActor using the @manager instance
   * as a layout manager should connect a handler to the ::layout-changed
   * signal and queue a relayout on themselves:
   *
   * |[
   *   static void layout_changed (ClutterLayoutManager *manager,
   *                               ClutterActor         *self)
   *   {
   *     clutter_actor_queue_relayout (self);
   *   }
   *   ...
   *     self->manager = g_object_ref_sink (manager);
   *     g_signal_connect (self->manager, "layout-changed",
   *                       G_CALLBACK (layout_changed),
   *                       self);
   * ]|
   *
   * Sub-classes of #ClutterLayoutManager that implement a layout that
   * can be controlled or changed using parameters should emit the
   * ::layout-changed signal whenever one of the parameters changes,
   * by using clutter_layout_manager_layout_changed().
   *
   * Since: 1.2
   */
  manager_signals[LAYOUT_CHANGED] =
    g_signal_new (I_("layout-changed"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterLayoutManagerClass,
                                   layout_changed),
                  NULL, NULL,
                  _clutter_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

static void
clutter_layout_manager_init (ClutterLayoutManager *manager)
{
}

/**
 * clutter_layout_manager_get_preferred_width:
 * @manager: a #ClutterLayoutManager
 * @container: the #ClutterContainer using @manager
 * @for_height: the height for which the width should be computed, or -1
 * @min_width_p: (out) (allow-none): return location for the minimum width
 *   of the layout, or %NULL
 * @nat_width_p: (out) (allow-none): return location for the natural width
 *   of the layout, or %NULL
 *
 * Computes the minimum and natural widths of the @container according
 * to @manager.
 *
 * See also clutter_actor_get_preferred_width()
 *
 * Since: 1.2
 */
void
clutter_layout_manager_get_preferred_width (ClutterLayoutManager *manager,
                                            ClutterContainer     *container,
                                            gfloat                for_height,
                                            gfloat               *min_width_p,
                                            gfloat               *nat_width_p)
{
  ClutterLayoutManagerClass *klass;

  g_return_if_fail (CLUTTER_IS_LAYOUT_MANAGER (manager));
  g_return_if_fail (CLUTTER_IS_CONTAINER (container));

  klass = CLUTTER_LAYOUT_MANAGER_GET_CLASS (manager);
  klass->get_preferred_width (manager, container, for_height,
                              min_width_p,
                              nat_width_p);
}

/**
 * clutter_layout_manager_get_preferred_height:
 * @manager: a #ClutterLayoutManager
 * @container: the #ClutterContainer using @manager
 * @for_width: the width for which the height should be computed, or -1
 * @min_height_p: (out) (allow-none): return location for the minimum height
 *   of the layout, or %NULL
 * @nat_height_p: (out) (allow-none): return location for the natural height
 *   of the layout, or %NULL
 *
 * Computes the minimum and natural heights of the @container according
 * to @manager.
 *
 * See also clutter_actor_get_preferred_height()
 *
 * Since: 1.2
 */
void
clutter_layout_manager_get_preferred_height (ClutterLayoutManager *manager,
                                             ClutterContainer     *container,
                                             gfloat                for_width,
                                             gfloat               *min_height_p,
                                             gfloat               *nat_height_p)
{
  ClutterLayoutManagerClass *klass;

  g_return_if_fail (CLUTTER_IS_LAYOUT_MANAGER (manager));
  g_return_if_fail (CLUTTER_IS_CONTAINER (container));

  klass = CLUTTER_LAYOUT_MANAGER_GET_CLASS (manager);
  klass->get_preferred_height (manager, container, for_width,
                               min_height_p,
                               nat_height_p);
}

/**
 * clutter_layout_manager_allocate:
 * @manager: a #ClutterLayoutManager
 * @container: the #ClutterContainer using @manager
 * @allocation: the #ClutterActorBox containing the allocated area
 *   of @container
 * @flags: the allocation flags
 *
 * Allocates the children of @container given an area
 *
 * See also clutter_actor_allocate()
 *
 * Since: 1.2
 */
void
clutter_layout_manager_allocate (ClutterLayoutManager   *manager,
                                 ClutterContainer       *container,
                                 const ClutterActorBox  *allocation,
                                 ClutterAllocationFlags  flags)
{
  ClutterLayoutManagerClass *klass;

  g_return_if_fail (CLUTTER_IS_LAYOUT_MANAGER (manager));
  g_return_if_fail (CLUTTER_IS_CONTAINER (container));
  g_return_if_fail (allocation != NULL);

  klass = CLUTTER_LAYOUT_MANAGER_GET_CLASS (manager);
  klass->allocate (manager, container, allocation, flags);
}

/**
 * clutter_layout_manager_layout_changed:
 * @manager: a #ClutterLayoutManager
 *
 * Emits the #ClutterLayoutManager::layout-changed signal on @manager
 *
 * This function should only be called by implementations of the
 * #ClutterLayoutManager class
 *
 * Since: 1.2
 */
void
clutter_layout_manager_layout_changed (ClutterLayoutManager *manager)
{
  gpointer is_frozen;

  g_return_if_fail (CLUTTER_IS_LAYOUT_MANAGER (manager));

  is_frozen = g_object_get_data (G_OBJECT (manager), "freeze-change");
  if (is_frozen == NULL)
    g_signal_emit (manager, manager_signals[LAYOUT_CHANGED], 0);
  else
    CLUTTER_NOTE (LAYOUT, "Layout manager '%s'[%p] has been frozen",
                  G_OBJECT_TYPE_NAME (manager),
                  manager);
}

/**
 * clutter_layout_manager_set_container:
 * @manager: a #ClutterLayoutManager
 * @container: (allow-none): a #ClutterContainer using @manager
 *
 * If the #ClutterLayoutManager sub-class allows it, allow
 * adding a weak reference of the @container using @manager
 * from within the layout manager
 *
 * The layout manager should not increase the reference
 * count of the @container
 *
 * Since: 1.2
 */
void
clutter_layout_manager_set_container (ClutterLayoutManager *manager,
                                      ClutterContainer     *container)
{
  ClutterLayoutManagerClass *klass;

  g_return_if_fail (CLUTTER_IS_LAYOUT_MANAGER (manager));
  g_return_if_fail (container == NULL || CLUTTER_IS_CONTAINER (container));

  klass = CLUTTER_LAYOUT_MANAGER_GET_CLASS (manager);
  if (klass->set_container)
    klass->set_container (manager, container);
}

GType
_clutter_layout_manager_get_child_meta_type (ClutterLayoutManager *manager)
{
  return CLUTTER_LAYOUT_MANAGER_GET_CLASS (manager)->get_child_meta_type (manager);
}

static inline ClutterLayoutMeta *
create_child_meta (ClutterLayoutManager *manager,
                   ClutterContainer     *container,
                   ClutterActor         *actor)
{
  ClutterLayoutManagerClass *klass;
  ClutterLayoutMeta *meta = NULL;

  layout_manager_freeze_layout_change (manager);

  klass = CLUTTER_LAYOUT_MANAGER_GET_CLASS (manager);
  if (klass->get_child_meta_type (manager) != G_TYPE_INVALID)
    meta = klass->create_child_meta (manager, container, actor);

  layout_manager_thaw_layout_change (manager);

  return meta;
}

static inline ClutterLayoutMeta *
get_child_meta (ClutterLayoutManager *manager,
                ClutterContainer     *container,
                ClutterActor         *actor)
{
  ClutterLayoutMeta *layout = NULL;

  layout = g_object_get_qdata (G_OBJECT (actor), quark_layout_meta);
  if (layout != NULL)
    {
      ClutterChildMeta *child = CLUTTER_CHILD_META (layout);

      if (layout->manager == manager &&
          child->container == container &&
          child->actor == actor)
        return layout;

      /* if the LayoutMeta referenced is not attached to the
       * layout manager then we simply ask the layout manager
       * to replace it with the right one
       */
    }

  layout = create_child_meta (manager, container, actor);
  if (layout != NULL)
    {
      g_assert (CLUTTER_IS_LAYOUT_META (layout));
      g_object_set_qdata_full (G_OBJECT (actor), quark_layout_meta,
                               layout,
                               (GDestroyNotify) g_object_unref);
      return layout;
    }

  return NULL;
}

/**
 * clutter_layout_manager_get_child_meta:
 * @manager: a #ClutterLayoutManager
 * @container: a #ClutterContainer using @manager
 * @actor: a #ClutterActor child of @container
 *
 * Retrieves the #ClutterLayoutMeta that the layout @manager associated
 * to the @actor child of @container, eventually by creating one if the
 * #ClutterLayoutManager supports layout properties
 *
 * Return value: (transfer none): a #ClutterLayoutMeta, or %NULL if the
 *   #ClutterLayoutManager does not have layout properties. The returned
 *   layout meta instance is owned by the #ClutterLayoutManager and it
 *   should not be unreferenced
 *
 * Since: 1.0
 */
ClutterLayoutMeta *
clutter_layout_manager_get_child_meta (ClutterLayoutManager *manager,
                                       ClutterContainer     *container,
                                       ClutterActor         *actor)
{
  g_return_val_if_fail (CLUTTER_IS_LAYOUT_MANAGER (manager), NULL);
  g_return_val_if_fail (CLUTTER_IS_CONTAINER (container), NULL);
  g_return_val_if_fail (CLUTTER_IS_ACTOR (actor), NULL);

  return get_child_meta (manager, container, actor);
}

static inline gboolean
layout_set_property_internal (ClutterLayoutManager *manager,
                              GObject              *gobject,
                              GParamSpec           *pspec,
                              const GValue         *value)
{
  if (pspec->flags & G_PARAM_CONSTRUCT_ONLY)
    {
      g_warning ("%s: Child property '%s' of the layout manager of "
                 "type '%s' is constructor-only",
                 G_STRLOC, pspec->name, G_OBJECT_TYPE_NAME (manager));
      return FALSE;
    }

  if (!(pspec->flags & G_PARAM_WRITABLE))
    {
      g_warning ("%s: Child property '%s' of the layout manager of "
                 "type '%s' is not writable",
                 G_STRLOC, pspec->name, G_OBJECT_TYPE_NAME (manager));
      return FALSE;
    }

  g_object_set_property (gobject, pspec->name, value);

  return TRUE;
}

static inline gboolean
layout_get_property_internal (ClutterLayoutManager *manager,
                              GObject              *gobject,
                              GParamSpec           *pspec,
                              GValue               *value)
{
  if (!(pspec->flags & G_PARAM_READABLE))
    {
      g_warning ("%s: Child property '%s' of the layout manager of "
                 "type '%s' is not readable",
                 G_STRLOC, pspec->name, G_OBJECT_TYPE_NAME (manager));
      return FALSE;
    }

  g_object_get_property (gobject, pspec->name, value);

  return TRUE;
}

/**
 * clutter_layout_manager_child_set:
 * @manager: a #ClutterLayoutManager
 * @container: a #ClutterContainer using @manager
 * @actor: a #ClutterActor child of @container
 * @first_property: the first property name
 * @...: a list of property name and value pairs
 *
 * Sets a list of properties and their values on the #ClutterLayoutMeta
 * associated by @manager to a child of @container
 *
 * Languages bindings should use clutter_layout_manager_child_set_property()
 * instead
 *
 * Since: 1.2
 */
void
clutter_layout_manager_child_set (ClutterLayoutManager *manager,
                                  ClutterContainer     *container,
                                  ClutterActor         *actor,
                                  const gchar          *first_property,
                                  ...)
{
  ClutterLayoutMeta *meta;
  GObjectClass *klass;
  const gchar *pname;
  va_list var_args;

  g_return_if_fail (CLUTTER_IS_LAYOUT_MANAGER (manager));
  g_return_if_fail (CLUTTER_IS_CONTAINER (container));
  g_return_if_fail (CLUTTER_IS_ACTOR (actor));
  g_return_if_fail (first_property != NULL);

  meta = get_child_meta (manager, container, actor);
  if (meta == NULL)
    {
      g_warning ("Layout managers of type '%s' do not support "
                 "layout metadata",
                 g_type_name (G_OBJECT_TYPE (manager)));
      return;
    }

  klass = G_OBJECT_GET_CLASS (meta);

  va_start (var_args, first_property);

  pname = first_property;
  while (pname)
    {
      GValue value = G_VALUE_INIT;
      GParamSpec *pspec;
      gchar *error;
      gboolean res;

      pspec = g_object_class_find_property (klass, pname);
      if (pspec == NULL)
        {
          g_warning ("%s: Layout managers of type '%s' have no layout "
                     "property named '%s'",
                     G_STRLOC, G_OBJECT_TYPE_NAME (manager), pname);
          break;
        }

      G_VALUE_COLLECT_INIT (&value, G_PARAM_SPEC_VALUE_TYPE (pspec),
                            var_args, 0,
                            &error);

      if (error)
        {
          g_warning ("%s: %s", G_STRLOC, error);
          free (error);
          break;
        }

      res = layout_set_property_internal (manager, G_OBJECT (meta),
                                          pspec,
                                          &value);

      g_value_unset (&value);

      if (!res)
        break;

      pname = va_arg (var_args, gchar*);
    }

  va_end (var_args);
}

/**
 * clutter_layout_manager_child_set_property:
 * @manager: a #ClutterLayoutManager
 * @container: a #ClutterContainer using @manager
 * @actor: a #ClutterActor child of @container
 * @property_name: the name of the property to set
 * @value: a #GValue with the value of the property to set
 *
 * Sets a property on the #ClutterLayoutMeta created by @manager and
 * attached to a child of @container
 *
 * Since: 1.2
 */
void
clutter_layout_manager_child_set_property (ClutterLayoutManager *manager,
                                           ClutterContainer     *container,
                                           ClutterActor         *actor,
                                           const gchar          *property_name,
                                           const GValue         *value)
{
  ClutterLayoutMeta *meta;
  GObjectClass *klass;
  GParamSpec *pspec;

  g_return_if_fail (CLUTTER_IS_LAYOUT_MANAGER (manager));
  g_return_if_fail (CLUTTER_IS_CONTAINER (container));
  g_return_if_fail (CLUTTER_IS_ACTOR (actor));
  g_return_if_fail (property_name != NULL);
  g_return_if_fail (value != NULL);

  meta = get_child_meta (manager, container, actor);
  if (meta == NULL)
    {
      g_warning ("Layout managers of type '%s' do not support "
                 "layout metadata",
                 g_type_name (G_OBJECT_TYPE (manager)));
      return;
    }

  klass = G_OBJECT_GET_CLASS (meta);

  pspec = g_object_class_find_property (klass, property_name);
  if (pspec == NULL)
    {
      g_warning ("%s: Layout managers of type '%s' have no layout "
                 "property named '%s'",
                 G_STRLOC, G_OBJECT_TYPE_NAME (manager), property_name);
      return;
    }

  layout_set_property_internal (manager, G_OBJECT (meta), pspec, value);
}

/**
 * clutter_layout_manager_child_get:
 * @manager: a #ClutterLayoutManager
 * @container: a #ClutterContainer using @manager
 * @actor: a #ClutterActor child of @container
 * @first_property: the name of the first property
 * @...: a list of property name and return location for the value pairs
 *
 * Retrieves the values for a list of properties out of the
 * #ClutterLayoutMeta created by @manager and attached to the
 * child of a @container
 *
 * Since: 1.2
 */
void
clutter_layout_manager_child_get (ClutterLayoutManager *manager,
                                  ClutterContainer     *container,
                                  ClutterActor         *actor,
                                  const gchar          *first_property,
                                  ...)
{
  ClutterLayoutMeta *meta;
  GObjectClass *klass;
  const gchar *pname;
  va_list var_args;

  g_return_if_fail (CLUTTER_IS_LAYOUT_MANAGER (manager));
  g_return_if_fail (CLUTTER_IS_CONTAINER (container));
  g_return_if_fail (CLUTTER_IS_ACTOR (actor));
  g_return_if_fail (first_property != NULL);

  meta = get_child_meta (manager, container, actor);
  if (meta == NULL)
    {
      g_warning ("Layout managers of type '%s' do not support "
                 "layout metadata",
                 g_type_name (G_OBJECT_TYPE (manager)));
      return;
    }

  klass = G_OBJECT_GET_CLASS (meta);

  va_start (var_args, first_property);

  pname = first_property;
  while (pname)
    {
      GValue value = G_VALUE_INIT;
      GParamSpec *pspec;
      gchar *error;
      gboolean res;

      pspec = g_object_class_find_property (klass, pname);
      if (pspec == NULL)
        {
          g_warning ("%s: Layout managers of type '%s' have no layout "
                     "property named '%s'",
                     G_STRLOC, G_OBJECT_TYPE_NAME (manager), pname);
          break;
        }

      g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (pspec));

      res = layout_get_property_internal (manager, G_OBJECT (meta),
                                          pspec,
                                          &value);
      if (!res)
        {
          g_value_unset (&value);
          break;
        }

      G_VALUE_LCOPY (&value, var_args, 0, &error);
      if (error)
        {
          g_warning ("%s: %s", G_STRLOC, error);
          free (error);
          g_value_unset (&value);
          break;
        }

      g_value_unset (&value);

      pname = va_arg (var_args, gchar*);
    }

  va_end (var_args);
}

/**
 * clutter_layout_manager_child_get_property:
 * @manager: a #ClutterLayoutManager
 * @container: a #ClutterContainer using @manager
 * @actor: a #ClutterActor child of @container
 * @property_name: the name of the property to get
 * @value: a #GValue with the value of the property to get
 *
 * Gets a property on the #ClutterLayoutMeta created by @manager and
 * attached to a child of @container
 *
 * The #GValue must already be initialized to the type of the property
 * and has to be unset with g_value_unset() after extracting the real
 * value out of it
 *
 * Since: 1.2
 */
void
clutter_layout_manager_child_get_property (ClutterLayoutManager *manager,
                                           ClutterContainer     *container,
                                           ClutterActor         *actor,
                                           const gchar          *property_name,
                                           GValue               *value)
{
  ClutterLayoutMeta *meta;
  GObjectClass *klass;
  GParamSpec *pspec;

  g_return_if_fail (CLUTTER_IS_LAYOUT_MANAGER (manager));
  g_return_if_fail (CLUTTER_IS_CONTAINER (container));
  g_return_if_fail (CLUTTER_IS_ACTOR (actor));
  g_return_if_fail (property_name != NULL);
  g_return_if_fail (value != NULL);

  meta = get_child_meta (manager, container, actor);
  if (meta == NULL)
    {
      g_warning ("Layout managers of type %s do not support "
                 "layout metadata",
                 g_type_name (G_OBJECT_TYPE (manager)));
      return;
    }

  klass = G_OBJECT_GET_CLASS (meta);

  pspec = g_object_class_find_property (klass, property_name);
  if (pspec == NULL)
    {
      g_warning ("%s: Layout managers of type '%s' have no layout "
                 "property named '%s'",
                 G_STRLOC, G_OBJECT_TYPE_NAME (manager), property_name);
      return;
    }

  layout_get_property_internal (manager, G_OBJECT (meta), pspec, value);
}

/**
 * clutter_layout_manager_find_child_property:
 * @manager: a #ClutterLayoutManager
 * @name: the name of the property
 *
 * Retrieves the #GParamSpec for the layout property @name inside
 * the #ClutterLayoutMeta sub-class used by @manager
 *
 * Return value: (transfer none): a #GParamSpec describing the property,
 *   or %NULL if no property with that name exists. The returned
 *   #GParamSpec is owned by the layout manager and should not be
 *   modified or freed
 *
 * Since: 1.2
 */
GParamSpec *
clutter_layout_manager_find_child_property (ClutterLayoutManager *manager,
                                            const gchar          *name)
{
  ClutterLayoutManagerClass *klass;
  GObjectClass *meta_klass;
  GParamSpec *pspec;
  GType meta_type;

  klass = CLUTTER_LAYOUT_MANAGER_GET_CLASS (manager);
  meta_type = klass->get_child_meta_type (manager);
  if (meta_type == G_TYPE_INVALID)
    return NULL;

  meta_klass = g_type_class_ref (meta_type);

  pspec = g_object_class_find_property (meta_klass, name);

  g_type_class_unref (meta_klass);

  return pspec;
}

/**
 * clutter_layout_manager_list_child_properties:
 * @manager: a #ClutterLayoutManager
 * @n_pspecs: (out): return location for the number of returned
 *   #GParamSpec<!-- -->s
 *
 * Retrieves all the #GParamSpec<!-- -->s for the layout properties
 * stored inside the #ClutterLayoutMeta sub-class used by @manager
 *
 * Return value: (transfer full) (array length=n_pspecs): the newly-allocated,
 *   %NULL-terminated array of #GParamSpec<!-- -->s. Use free() to free the
 *   resources allocated for the array
 *
 * Since: 1.2
 */
GParamSpec **
clutter_layout_manager_list_child_properties (ClutterLayoutManager *manager,
                                              guint                *n_pspecs)
{
  ClutterLayoutManagerClass *klass;
  GObjectClass *meta_klass;
  GParamSpec **pspecs;
  GType meta_type;

  klass = CLUTTER_LAYOUT_MANAGER_GET_CLASS (manager);
  meta_type = klass->get_child_meta_type (manager);
  if (meta_type == G_TYPE_INVALID)
    return NULL;

  meta_klass = g_type_class_ref (meta_type);

  pspecs = g_object_class_list_properties (meta_klass, n_pspecs);

  g_type_class_unref (meta_klass);

  return pspecs;
}
