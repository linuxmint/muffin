/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010 Intel Corporation
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
 *   Øyvind Kolås <pippin@linux.intel.com>
 */

/**
 * SECTION:clutter-animator
 * @short_description: Multi-actor tweener
 * @See_Also: #ClutterAnimatable, #ClutterInterval, #ClutterAlpha,
 *   #ClutterTimeline
 *
 * #ClutterAnimator is an object providing declarative animations for
 * #GObject properties belonging to one or more #GObject<!-- -->s to
 * #ClutterIntervals.
 *
 * #ClutterAnimator is used to build and describe complex animations
 * in terms of "key frames". #ClutterAnimator is meant to be used
 * through the #ClutterScript definition format, but it comes with a
 * convenience C API.
 *
 * #ClutterAnimator is available since Clutter 1.2
 *
 * #ClutterAnimator has been deprecated in Clutter 1.12. If you
 * want to combine multiple transitions using key frames, use
 * #ClutterKeyframeTransition and #ClutterTransitionGroup instead.
 *
 * ## Key Frames
 *
 * Every animation handled by a #ClutterAnimator can be
 * described in terms of "key frames". For each #GObject property
 * there can be multiple key frames, each one defined by the end
 * value for the property to be computed starting from the current
 * value to a specific point in time, using a given easing
 * mode.
 *
 * The point in time is defined using a value representing
 * the progress in the normalized interval of [ 0, 1 ]. This maps
 * the value returned by clutter_timeline_get_duration().
 *
 * ## ClutterAnimator description for ClutterScript
 *
 * #ClutterAnimator defines a custom "properties" key
 * which allows describing the key frames for objects as
 * an array of key frames.
 *
 * The `properties` array has the following syntax:
 *
 * |[
 *  {
 *    "properties" : [
 *      {
 *        "object" : object_id
 *        "name" : property_name
 *        "ease-in" : true_or_false
 *        "interpolation" : interpolation_value
 *        "keys" : [
 *          [ progress, easing_mode, final_value ]
 *        ]
 *    ]
 *  }
 * ]|
 *
 * The following JSON fragment defines a #ClutterAnimator
 * with the duration of 1 second and operating on the x and y
 * properties of a #ClutterActor named "rect-01", with two frames
 * for each property. The first frame will linearly move the actor
 * from its current position to the 100, 100 position in 20 percent
 * of the duration of the animation; the second will using a cubic
 * easing to move the actor to the 200, 200 coordinates.
 *
 * |[
 *  {
 *    "type" : "ClutterAnimator",
 *    "duration" : 1000,
 *    "properties" : [
 *      {
 *        "object" : "rect-01",
 *        "name" : "x",
 *        "ease-in" : true,
 *        "keys" : [
 *          [ 0.2, "linear",       100.0 ],
 *          [ 1.0, "easeOutCubic", 200.0 ]
 *        ]
 *      },
 *      {
 *        "object" : "rect-01",
 *        "name" : "y",
 *        "ease-in" : true,
 *        "keys" : [
 *          [ 0.2, "linear",       100.0 ],
 *          [ 1.0, "easeOutCubic", 200.0 ]
 *        ]
 *      }
 *    ]
 *  }
 * ]|
 */

#ifdef HAVE_CONFIG_H
#include "clutter-build-config.h"
#endif

#include <string.h>
#include <math.h>

#include <gobject/gvaluecollector.h>

#define CLUTTER_DISABLE_DEPRECATION_WARNINGS

#include "clutter-animator.h"

#include "clutter-alpha.h"
#include "clutter-debug.h"
#include "clutter-enum-types.h"
#include "clutter-interval.h"
#include "clutter-private.h"
#include "clutter-script-private.h"
#include "clutter-scriptable.h"

/* progress values varying by less than this are considered equal */
#define PROGRESS_EPSILON  0.00001

struct _ClutterAnimatorPrivate
{
  ClutterTimeline  *timeline;
  ClutterTimeline  *slave_timeline;

  GList            *score;

  GHashTable       *properties;
};

struct _ClutterAnimatorKey
{
  GObject             *object;
  const gchar         *property_name;
  guint                mode;

  GValue               value;

  /* normalized progress, between 0.0 and 1.0 */
  gdouble              progress;

  /* back-pointer to the animator which owns the key */
  ClutterAnimator     *animator;

  /* interpolation mode */
  ClutterInterpolation interpolation;

  /* ease from the current object state into the animation when it starts */
  guint                ease_in : 1;

  /* This key is already being destroyed and shouldn't
   * trigger additional weak unrefs
   */
  guint                is_inert : 1;

  gint                 ref_count;
};

enum
{
  PROP_0,

  PROP_DURATION,
  PROP_TIMELINE,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

static void clutter_scriptable_init (ClutterScriptableIface *iface);

G_DEFINE_TYPE_WITH_CODE (ClutterAnimator,
                         clutter_animator,
                         G_TYPE_OBJECT,
                         G_ADD_PRIVATE (ClutterAnimator)
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_SCRIPTABLE,
                                                clutter_scriptable_init));
/**
 * clutter_animator_new:
 *
 * Creates a new #ClutterAnimator instance
 *
 * Return value: a new #ClutterAnimator.
 *
 * Since: 1.2
 *
 * Deprecated: 1.12: Use #ClutterKeyframeTransition instead
 */
ClutterAnimator *
clutter_animator_new (void)
{
  return g_object_new (CLUTTER_TYPE_ANIMATOR, NULL);
}

/***/

typedef struct _PropObjectKey {
  GObject      *object;
  const gchar  *property_name;
  guint         mode;
  gdouble       progress;
} PropObjectKey;

/* Iterator that walks the keys of a property*/
typedef struct _PropertyIter {
  PropObjectKey       *key;
  ClutterInterval     *interval;
  ClutterAlpha        *alpha;

  GList               *current;

  gdouble              start;    /* the progress of current */
  gdouble              end;      /* until which progress it is valid */
  ClutterInterpolation interpolation;

  guint                ease_in : 1;
} PropertyIter;

static PropObjectKey *
prop_actor_key_new (GObject     *object,
                    const gchar *property_name)
{
  PropObjectKey *key = g_slice_new0 (PropObjectKey);

  key->object = object;
  key->property_name = g_intern_string (property_name);

  return key;
}

static void
prop_actor_key_free (gpointer key)
{
  if (key != NULL)
    g_slice_free (PropObjectKey, key);
}

static void
property_iter_free (gpointer key)
{
  if (key != NULL)
    {
      PropertyIter *property_iter = key;

      g_object_unref (property_iter->interval);
      g_object_unref (property_iter->alpha);

      g_slice_free (PropertyIter, property_iter);
    }
}

static PropertyIter *
property_iter_new (ClutterAnimator *animator,
                   PropObjectKey   *key,
                   GType            type)
{
  ClutterAnimatorPrivate *priv = animator->priv;
  PropertyIter *property_iter = g_slice_new (PropertyIter);
  ClutterInterval *interval = g_object_new (CLUTTER_TYPE_INTERVAL,
                                            "value-type", type,
                                            NULL);

  /* we own this interval */
  g_object_ref_sink (interval);

  property_iter->interval = interval;
  property_iter->key = key;
  property_iter->alpha = clutter_alpha_new ();
  clutter_alpha_set_timeline (property_iter->alpha, priv->slave_timeline);

  /* as well as the alpha */
  g_object_ref_sink (property_iter->alpha);

  return property_iter;
}

static guint
prop_actor_hash (gconstpointer value)
{
  const PropObjectKey *info = value;

  return GPOINTER_TO_INT (info->property_name)
       ^ GPOINTER_TO_INT (info->object);
}

static gboolean
prop_actor_equal (gconstpointer a, gconstpointer b)
{
  const PropObjectKey *infoa = a;
  const PropObjectKey *infob = b;

  /* property name strings are interned so we can just compare pointers */
  if (infoa->object == infob->object &&
      (infoa->property_name == infob->property_name))
    return TRUE;

  return FALSE;
}

static gint
sort_actor_prop_progress_func (gconstpointer a,
                               gconstpointer b)
{
  const ClutterAnimatorKey *pa = a;
  const ClutterAnimatorKey *pb = b;

  if (pa->object == pb->object)
    {
      gint pdiff = pb->property_name - pa->property_name;

      if (pdiff)
        return pdiff;

      if (fabs (pa->progress - pb->progress) < PROGRESS_EPSILON)
        return 0;

      if (pa->progress > pb->progress)
        return 1;

      return -1;
    }

  return pa->object - pb->object;
}

static gint
sort_actor_prop_func (gconstpointer a,
                      gconstpointer b)
{
  const ClutterAnimatorKey *pa = a;
  const ClutterAnimatorKey *pb = b;

  if (pa->object == pb->object)
    return pa->property_name - pb->property_name;

  return pa->object - pb->object;
}

static void
clutter_animator_remove_key_internal (ClutterAnimator *animator,
                                      GObject         *object,
                                      const gchar     *property_name,
                                      gdouble          progress,
                                      gboolean         is_inert);

static void
object_disappeared (gpointer  data,
                    GObject  *where_the_object_was)
{
  clutter_animator_remove_key_internal (data, where_the_object_was, NULL, -1.0,
                                        TRUE);
}

static ClutterAnimatorKey *
clutter_animator_key_new (ClutterAnimator *animator,
                          GObject         *object,
                          const gchar     *property_name,
                          gdouble          progress,
                          guint            mode)
{
  ClutterAnimatorKey *animator_key;

  animator_key = g_slice_new (ClutterAnimatorKey);

  animator_key->ref_count = 1;
  animator_key->animator = animator;
  animator_key->object = object;
  animator_key->mode = mode;
  memset (&(animator_key->value), 0, sizeof (GValue));
  animator_key->progress = progress;
  animator_key->property_name = g_intern_string (property_name);
  animator_key->interpolation = CLUTTER_INTERPOLATION_LINEAR;
  animator_key->ease_in = FALSE;
  animator_key->is_inert = FALSE;

  /* keep a weak reference on the animator, so that we can release the
   * back-pointer when needed
   */
  g_object_weak_ref (object, object_disappeared,
                     animator_key->animator);

  return animator_key;
}

static gpointer
clutter_animator_key_copy (gpointer boxed)
{
  ClutterAnimatorKey *key = boxed;

  if (key != NULL)
    key->ref_count += 1;

  return key;
}

static void
clutter_animator_key_free (gpointer boxed)
{
  ClutterAnimatorKey *key = boxed;

  if (key == NULL)
    return;

  key->ref_count -= 1;

  if (key->ref_count > 0)
    return;

  if (!key->is_inert)
    g_object_weak_unref (key->object, object_disappeared, key->animator);

  g_slice_free (ClutterAnimatorKey, key);
}

static void
clutter_animator_dispose (GObject *object)
{
  ClutterAnimator *animator = CLUTTER_ANIMATOR (object);
  ClutterAnimatorPrivate *priv = animator->priv;

  clutter_animator_set_timeline (animator, NULL);
  g_object_unref (priv->slave_timeline);

  G_OBJECT_CLASS (clutter_animator_parent_class)->dispose (object);
}

static void
clutter_animator_finalize (GObject *object)
{
  ClutterAnimator *animator = CLUTTER_ANIMATOR (object);
  ClutterAnimatorPrivate *priv = animator->priv;

  g_list_foreach (priv->score, (GFunc) clutter_animator_key_free, NULL);
  g_list_free (priv->score);
  priv->score = NULL;

  g_hash_table_destroy (priv->properties);

  G_OBJECT_CLASS (clutter_animator_parent_class)->finalize (object);
}

/* XXX: this is copied and slightly modified from glib,
 * there is only one way to do this. */
static GList *
list_find_custom_reverse (GList         *list,
                          gconstpointer  data,
                          GCompareFunc   func)
{
  while (list)
    {
      if (! func (list->data, data))
        return list;

      list = list->prev;
    }

  return NULL;
}

/* Ensures that the interval provided by the animator is correct
 * for the requested progress value.
 */
static void
animation_animator_ensure_animator (ClutterAnimator *animator,
                                    PropertyIter    *property_iter,
                                    PropObjectKey   *key,
                                    gdouble          progress)
{

  if (progress > property_iter->end)
    {
      while (progress > property_iter->end)
        {
          ClutterAnimatorKey *initial_key, *next_key;
          GList *initial, *next;

          initial = g_list_find_custom (property_iter->current->next,
                                        key,
                                        sort_actor_prop_func);

          if (initial)
            {
              initial_key = initial->data;

              clutter_interval_set_initial_value (property_iter->interval,
                                                  &initial_key->value);
              property_iter->current = initial;
              property_iter->start = initial_key->progress;

              next = g_list_find_custom (initial->next,
                                         key,
                                         sort_actor_prop_func);
              if (next)
                {
                  next_key = next->data;

                  property_iter->end = next_key->progress;
                }
              else
                {
                  next_key = initial_key;

                  property_iter->end = property_iter->start;
                }

              clutter_interval_set_final_value (property_iter->interval,
                                                &next_key->value);

              if ((clutter_alpha_get_mode (property_iter->alpha) != next_key->mode))
                clutter_alpha_set_mode (property_iter->alpha, next_key->mode);
            }
          else /* no relevant interval */
            {
              ClutterAnimatorKey *current_key = property_iter->current->data;
              clutter_interval_set_initial_value (property_iter->interval,
                                                  &current_key->value);
              clutter_interval_set_final_value (property_iter->interval,
                                                &current_key->value);
              break;
            }
        }
    }
  else if (progress < property_iter->start)
    {
      while (progress < property_iter->start)
        {
          ClutterAnimatorKey *initial_key, *next_key;
          GList *initial;
          GList *old = property_iter->current;

          initial = list_find_custom_reverse (property_iter->current->prev,
                                              key,
                                              sort_actor_prop_func);

          if (initial)
            {
              initial_key = initial->data;

              clutter_interval_set_initial_value (property_iter->interval,
                                                  &initial_key->value);
              property_iter->current = initial;
              property_iter->end = property_iter->start;
              property_iter->start = initial_key->progress;

              if (old)
                {
                  next_key = old->data;

                  property_iter->end = next_key->progress;
                }
              else
                {
                  next_key = initial_key;

                  property_iter->end = 1.0;
                }

              clutter_interval_set_final_value (property_iter->interval,
                                                &next_key->value);
              if ((clutter_alpha_get_mode (property_iter->alpha) != next_key->mode))
                clutter_alpha_set_mode (property_iter->alpha, next_key->mode);
            }
          else
            break;
        }
    }
}

/* XXX - this might be useful as an internal function exposed somewhere */
static gdouble
cubic_interpolation (const gdouble dx,
                     const gdouble prev,
                     const gdouble j,
                     const gdouble next,
                     const gdouble nextnext)
{
  return (((( - prev + 3 * j - 3 * next + nextnext ) * dx +
            ( 2 * prev - 5 * j + 4 * next - nextnext ) ) * dx +
            ( - prev + next ) ) * dx + (j + j) ) / 2.0;
}

/* try to get a floating point key value from a key for a property,
 * failing use the closest key in that direction or the starting point.
 */
static gfloat
list_try_get_rel (GList *list,
                  gint   count)
{
  ClutterAnimatorKey *key;
  GList *iter = list;
  GList *best = list;

  if (count > 0)
    {
      while (count -- && iter != NULL)
        {
          iter = g_list_find_custom (iter->next, list->data,
                                     sort_actor_prop_func);
          if (iter != NULL)
            best = iter;
        }
    }
  else
    {
      while (count ++ < 0 && iter != NULL)
        {
          iter = list_find_custom_reverse (iter->prev, list->data,
                                           sort_actor_prop_func);
          if (iter != NULL)
            best = iter;
        }
    }

  if (best != NULL && best->data != NULL)
    {
      key = best->data;

      return g_value_get_float (&(key->value));
    }

  return 0;
}

static void
animation_animator_new_frame (ClutterTimeline  *timeline,
                              gint              msecs,
                              ClutterAnimator  *animator)
{
  gdouble progress;
  GHashTableIter iter;
  gpointer key, value;

  progress  = 1.0 * msecs / clutter_timeline_get_duration (timeline);

  /* for each property that is managed figure out the GValue to set,
   * avoid creating new ClutterInterval's for each interval crossed
   */
  g_hash_table_iter_init (&iter, animator->priv->properties);

  key = value = NULL;
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      PropObjectKey      *prop_actor_key = key;
      PropertyIter       *property_iter   = value;
      ClutterAnimatorKey *start_key;
      gdouble             sub_progress;

      animation_animator_ensure_animator (animator, property_iter,
                                          key,
                                          progress);
      start_key = property_iter->current->data;

      if (property_iter->end == property_iter->start)
        sub_progress = 0.0; /* we're past the final value */
      else
        sub_progress = (progress - property_iter->start)
                     / (property_iter->end - property_iter->start);

      /* only change values if we active (delayed start) */
      if (sub_progress >= 0.0 && sub_progress <= 1.0)
        {
          GValue tmp_value = G_VALUE_INIT;
          GType int_type;

          g_value_init (&tmp_value, G_VALUE_TYPE (&start_key->value));

          clutter_timeline_advance (animator->priv->slave_timeline,
                                    sub_progress * 10000);

          sub_progress = clutter_alpha_get_alpha (property_iter->alpha);
          int_type = clutter_interval_get_value_type (property_iter->interval);

          if (property_iter->interpolation == CLUTTER_INTERPOLATION_CUBIC &&
              int_type == G_TYPE_FLOAT)
            {
              gdouble prev, current, next, nextnext;
              gdouble res;

              if ((property_iter->ease_in == FALSE ||
                  (property_iter->ease_in &&
                   list_find_custom_reverse (property_iter->current->prev,
                                             property_iter->current->data,
                                             sort_actor_prop_func))))
                {
                  current = g_value_get_float (&start_key->value);
                  prev = list_try_get_rel (property_iter->current, -1);
                }
              else
                {
                  /* interpolated and easing in */
                  clutter_interval_get_initial_value (property_iter->interval,
                                                      &tmp_value);
                  prev = current = g_value_get_float (&tmp_value);
                }

               next = list_try_get_rel (property_iter->current, 1);
               nextnext = list_try_get_rel (property_iter->current, 2);
               res = cubic_interpolation (sub_progress, prev, current, next,
                                          nextnext);

               g_value_set_float (&tmp_value, res);
            }
          else
            clutter_interval_compute_value (property_iter->interval,
                                            sub_progress,
                                            &tmp_value);

          g_object_set_property (prop_actor_key->object,
                                 prop_actor_key->property_name,
                                 &tmp_value);

          g_value_unset (&tmp_value);
        }
    }
}

static void
animation_animator_started (ClutterTimeline *timeline,
                            ClutterAnimator *animator)
{
  GList *k;

  /* Ensure that animators exist for all involved properties */
  for (k = animator->priv->score; k != NULL; k = k->next)
    {
      ClutterAnimatorKey *key = k->data;
      PropertyIter       *property_iter;
      PropObjectKey      *prop_actor_key;

      prop_actor_key = prop_actor_key_new (key->object, key->property_name);
      property_iter = g_hash_table_lookup (animator->priv->properties,
                                          prop_actor_key);
      if (property_iter)
        {
          prop_actor_key_free (prop_actor_key);
        }
      else
        {
          GObjectClass *klass = G_OBJECT_GET_CLASS (key->object);
          GParamSpec *pspec;

          pspec = g_object_class_find_property (klass, key->property_name);

          property_iter = property_iter_new (animator, prop_actor_key,
                                           G_PARAM_SPEC_VALUE_TYPE (pspec));
          g_hash_table_insert (animator->priv->properties,
                               prop_actor_key,
                               property_iter);
        }
    }

  /* initialize animator with initial list pointers */
  {
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init (&iter, animator->priv->properties);
    while (g_hash_table_iter_next (&iter, &key, &value))
      {
        PropertyIter *property_iter = value;
        ClutterAnimatorKey *initial_key, *next_key;
        GList *initial;
        GList *next;

        initial = g_list_find_custom (animator->priv->score,
                                      key,
                                      sort_actor_prop_func);
        g_assert (initial != NULL);
        initial_key = initial->data;
        clutter_interval_set_initial_value (property_iter->interval,
                                            &initial_key->value);

        property_iter->current       = initial;
        property_iter->start         = initial_key->progress;
        property_iter->ease_in       = initial_key->ease_in;
        property_iter->interpolation = initial_key->interpolation;

        if (property_iter->ease_in)
          {
            GValue tmp_value = G_VALUE_INIT;
            GType int_type;

            int_type = clutter_interval_get_value_type (property_iter->interval);
            g_value_init (&tmp_value, int_type);

            g_object_get_property (initial_key->object,
                                   initial_key->property_name,
                                   &tmp_value);

            clutter_interval_set_initial_value (property_iter->interval,
                                                &tmp_value);

            g_value_unset (&tmp_value);
          }

        next = g_list_find_custom (initial->next, key, sort_actor_prop_func);
        if (next)
          {
            next_key = next->data;
            property_iter->end = next_key->progress;
          }
        else
          {
            next_key = initial_key;
            property_iter->end = 1.0;
          }

        clutter_interval_set_final_value (property_iter->interval,
                                          &next_key->value);
        if ((clutter_alpha_get_mode (property_iter->alpha) != next_key->mode))
          clutter_alpha_set_mode (property_iter->alpha, next_key->mode);
      }
  }
}

/**
 * clutter_animator_compute_value:
 * @animator: a #ClutterAnimator
 * @object: a #GObject
 * @property_name: the name of the property on object to check
 * @progress: a value between 0.0 and 1.0
 * @value: an initialized value to store the computed result
 *
 * Compute the value for a managed property at a given progress.
 *
 * If the property is an ease-in property, the current value of the property
 * on the object will be used as the starting point for computation.
 *
 * Return value: %TRUE if the computation yields has a value, otherwise (when
 *   an error occurs or the progress is before any of the keys) %FALSE is
 *   returned and the #GValue is left untouched
 *
 * Since: 1.2
 * Deprecated: 1.12: Use #ClutterKeyframeTransition instead
 */
gboolean
clutter_animator_compute_value (ClutterAnimator *animator,
                                GObject         *object,
                                const gchar     *property_name,
                                gdouble          progress,
                                GValue          *value)
{
  ClutterAnimatorPrivate *priv;
  ClutterAnimatorKey   key;
  ClutterAnimatorKey  *previous;
  ClutterAnimatorKey  *next = NULL;
  GParamSpec          *pspec;
  GList               *initial_l;
  GList               *previous_l;
  GList               *next_l;
  gboolean             ease_in;
  ClutterInterpolation interpolation;

  g_return_val_if_fail (CLUTTER_IS_ANIMATOR (animator), FALSE);
  g_return_val_if_fail (G_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (property_name, FALSE);
  g_return_val_if_fail (value, FALSE);

  priv = animator->priv;

  ease_in = clutter_animator_property_get_ease_in (animator, object,
                                                   property_name);
  interpolation = clutter_animator_property_get_interpolation (animator,
                                                   object, property_name);

  property_name = g_intern_string (property_name);

  pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (object),
                                        property_name);

  key.object        = object;
  key.property_name = property_name;

  initial_l = g_list_find_custom (animator->priv->score, &key,
                                  sort_actor_prop_func);
  if (initial_l == NULL)
    return FALSE;

  /* first find the interval we belong in, that is the first interval
   * existing between keys
   */

  for (previous_l = initial_l, next_l = previous_l->next ;
       previous_l->next ;
       previous_l = previous_l->next, next_l = previous_l->next)
    {
       previous = previous_l->data;
       if (next_l)
         {
           next = next_l->data;
           if (next->object != object ||
               next->property_name != property_name)
             {
               next_l = NULL;
               next = NULL;
             }
         }
       else
         next = NULL;

       if (progress < previous->progress)
         {
            /* we are before the defined values */

            /* value has not been set */
            return FALSE;
         }

       if (!next && previous->progress <= progress)
         {
            /* we only had one key for this object/property */
            /* and we are past it, that is our value */
            g_value_copy (&previous->value, value);
            return TRUE;
         }

       if (next && next->progress >= progress)
         {
            ClutterInterval *interval;
            ClutterAlpha    *alpha;

            gdouble sub_progress = (progress - previous->progress)
                                 / (next->progress - previous->progress);
            /* this should be our interval */
            interval = g_object_new (CLUTTER_TYPE_INTERVAL,
                                     "value-type", pspec->value_type,
                                     NULL);

            if (ease_in && previous_l == initial_l)
              {
                GValue tmp_value = {0, };
                g_value_init (&tmp_value, pspec->value_type);
                g_object_get_property (object, property_name, &tmp_value);
                clutter_interval_set_initial_value (interval, &tmp_value);
                g_value_unset (&tmp_value);
              }
            else
              clutter_interval_set_initial_value (interval, &previous->value);

            clutter_interval_set_final_value (interval, &next->value);

            alpha = clutter_alpha_new ();
            clutter_alpha_set_timeline (alpha, priv->slave_timeline);
            clutter_alpha_set_mode (alpha, next->mode);

            clutter_timeline_advance (priv->slave_timeline,
                                      sub_progress * 10000);
            sub_progress = clutter_alpha_get_alpha (alpha);

            if (interpolation == CLUTTER_INTERPOLATION_CUBIC &&
                pspec->value_type == G_TYPE_FLOAT)
              {
                gdouble prev, current, nextv, nextnext;
                gdouble res;

                if ((ease_in == FALSE ||
                    (ease_in &&
                     list_find_custom_reverse (previous_l->prev,
                                               previous_l->data,
                                               sort_actor_prop_func))))
                  {
                    current = g_value_get_float (&previous->value);
                    prev = list_try_get_rel (previous_l, -1);
                  }
                else
                  {
                    /* interpolated and easing in */
                    GValue tmp_value = {0, };
                    g_value_init (&tmp_value, pspec->value_type);
                    clutter_interval_get_initial_value (interval,
                                                        &tmp_value);
                    prev = current = g_value_get_float (&tmp_value);
                    g_value_unset (&tmp_value);
                  }

                 nextv = list_try_get_rel (previous_l, 1);
                 nextnext = list_try_get_rel (previous_l, 2);
                 res = cubic_interpolation (sub_progress, prev, current, nextv,
                                            nextnext);
                 g_value_set_float (value, res);
              }
            else
              clutter_interval_compute_value (interval,
                                              sub_progress,
                                              value);

            g_object_ref_sink (interval);
            g_object_unref (interval);
            g_object_ref_sink (alpha);
            g_object_unref (alpha);

            return TRUE;
         }

    }

  if (!next)
    return FALSE;

  /* We're at, or past the end, use the last value */
  g_value_copy (&next->value, value);

  return TRUE;
}


/**
 * clutter_animator_set_timeline:
 * @animator: a #ClutterAnimator
 * @timeline: a #ClutterTimeline
 *
 * Sets an external timeline that will be used for driving the animation
 *
 * Since: 1.2
 * Deprecated: 1.12: Use #ClutterKeyframeTransition instead
 */
void
clutter_animator_set_timeline (ClutterAnimator *animator,
                               ClutterTimeline *timeline)
{
  ClutterAnimatorPrivate *priv;

  g_return_if_fail (CLUTTER_IS_ANIMATOR (animator));

  priv = animator->priv;

  if (priv->timeline != NULL)
    {
      g_signal_handlers_disconnect_by_func (priv->timeline,
                                            animation_animator_new_frame,
                                            animator);
      g_signal_handlers_disconnect_by_func (priv->timeline,
                                            animation_animator_started,
                                            animator);
      g_object_unref (priv->timeline);
    }

  priv->timeline = timeline;
  if (timeline != NULL)
    {
      g_object_ref (priv->timeline);

      g_signal_connect (priv->timeline, "new-frame",
                        G_CALLBACK (animation_animator_new_frame),
                        animator);
      g_signal_connect (priv->timeline, "started",
                        G_CALLBACK (animation_animator_started),
                        animator);
    }
}

/**
 * clutter_animator_get_timeline:
 * @animator: a #ClutterAnimator
 *
 * Get the timeline hooked up for driving the #ClutterAnimator
 *
 * Return value: (transfer none): the #ClutterTimeline that drives the animator
 *
 * Since: 1.2
 * Deprecated: 1.12: Use #ClutterKeyframeTransition instead
 */
ClutterTimeline *
clutter_animator_get_timeline (ClutterAnimator *animator)
{
  g_return_val_if_fail (CLUTTER_IS_ANIMATOR (animator), NULL);
  return animator->priv->timeline;
}

/**
 * clutter_animator_start:
 * @animator: a #ClutterAnimator
 *
 * Start the ClutterAnimator, this is a thin wrapper that rewinds
 * and starts the animators current timeline.
 *
 * Return value: (transfer none): the #ClutterTimeline that drives
 *   the animator. The returned timeline is owned by the #ClutterAnimator
 *   and it should not be unreferenced
 *
 * Since: 1.2
 * Deprecated: 1.12: Use #ClutterKeyframeTransition instead
 */
ClutterTimeline *
clutter_animator_start (ClutterAnimator *animator)
{
  ClutterAnimatorPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_ANIMATOR (animator), NULL);

  priv = animator->priv;

  clutter_timeline_rewind (priv->timeline);
  clutter_timeline_start (priv->timeline);

  return priv->timeline;
}

/**
 * clutter_animator_set_duration:
 * @animator: a #ClutterAnimator
 * @duration: milliseconds a run of the animator should last.
 *
 * Runs the timeline of the #ClutterAnimator with a duration in msecs
 * as specified.
 *
 * Since: 1.2
 * Deprecated: 1.12: Use #ClutterKeyframeTransition instead
 */
void
clutter_animator_set_duration (ClutterAnimator *animator,
                               guint            duration)
{
  g_return_if_fail (CLUTTER_IS_ANIMATOR (animator));

  clutter_timeline_set_duration (animator->priv->timeline, duration);
}

/**
 * clutter_animator_get_duration:
 * @animator: a #ClutterAnimator
 *
 * Retrieves the current duration of an animator
 *
 * Return value: the duration of the animation, in milliseconds
 *
 * Since: 1.2
 * Deprecated: 1.12: Use #ClutterKeyframeTransition instead
 */
guint
clutter_animator_get_duration  (ClutterAnimator *animator)
{
  g_return_val_if_fail (CLUTTER_IS_ANIMATOR (animator), 0);

  return clutter_timeline_get_duration (animator->priv->timeline);
}

/**
 * clutter_animator_set:
 * @animator: a #ClutterAnimator
 * @first_object: a #GObject
 * @first_property_name: the property to specify a key for
 * @first_mode: the id of the alpha function to use
 * @first_progress: at which stage of the animation this value applies; the
 *   range is a normalized floating point value between 0 and 1
 * @...: the value first_property_name should have for first_object
 *   at first_progress, followed by more (object, property_name, mode,
 *   progress, value) tuples, followed by %NULL
 *
 * Adds multiple keys to a #ClutterAnimator, specifying the value a given
 * property should have at a given progress of the animation. The mode
 * specified is the mode used when going to this key from the previous key of
 * the @property_name
 *
 * If a given (object, property, progress) tuple already exist the mode and
 * value will be replaced with the new values.
 *
 * Since: 1.2
 * Deprecated: 1.12: Use #ClutterKeyframeTransition instead
 */
void
clutter_animator_set (ClutterAnimator *animator,
                      gpointer         first_object,
                      const gchar     *first_property_name,
                      guint            first_mode,
                      gdouble          first_progress,
                      ...)
{
  GObject      *object;
  const gchar  *property_name;
  guint         mode;
  gdouble       progress;
  va_list       args;

  g_return_if_fail (CLUTTER_IS_ANIMATOR (animator));

  object = first_object;
  property_name = first_property_name;

  g_return_if_fail (object);
  g_return_if_fail (property_name);

  mode = first_mode;
  progress = first_progress;

  va_start (args, first_progress);

  while (object != NULL)
    {
      GParamSpec *pspec;
      GObjectClass *klass;
      GValue value = G_VALUE_INIT;
      gchar *error = NULL;

      klass = G_OBJECT_GET_CLASS (object);
      pspec = g_object_class_find_property (klass, property_name);

      if (!pspec)
        {
          g_warning ("Cannot bind property '%s': object of type '%s' "
                     "do not have this property",
                     property_name, G_OBJECT_TYPE_NAME (object));
          break;
        }

      G_VALUE_COLLECT_INIT (&value, G_PARAM_SPEC_VALUE_TYPE (pspec),
                            args, 0,
                            &error);

      if (error)
        {
          g_warning ("%s: %s", G_STRLOC, error);
          free (error);
          break;
        }

      clutter_animator_set_key (animator,
                                object,
                                property_name,
                                mode,
                                progress,
                                &value);

      object= va_arg (args, GObject *);
      if (object)
        {
          property_name = va_arg (args, gchar*);
          if (!property_name)
           {
             g_warning ("%s: expected a property name", G_STRLOC);
             break;
           }
          mode = va_arg (args, guint);
          progress = va_arg (args, gdouble);
        }
    }

  va_end (args);
}

static inline void
clutter_animator_set_key_internal (ClutterAnimator    *animator,
                                   ClutterAnimatorKey *key)
{
  ClutterAnimatorPrivate *priv = animator->priv;
  GList                  *old_item;
  GList                  *initial_item;
  ClutterAnimatorKey     *initial_key = NULL;

  if ((initial_item = g_list_find_custom (animator->priv->score, key,
                                          sort_actor_prop_func)))
    initial_key = initial_item->data;

  /* The first key for a property specifies ease-in and interpolation,
   * if we are replacing; or becoming a new first key we should
   * inherit the old flags.
   */
  if (initial_key &&
      initial_key->progress >= key->progress)
    {
      key->interpolation = initial_key->interpolation;
      key->ease_in = initial_key->ease_in;
    }

  old_item = g_list_find_custom (priv->score, key,
                                 sort_actor_prop_progress_func);

  /* replace the key if we already have a similar one */
  if (old_item != NULL)
    {
      ClutterAnimatorKey *old_key = old_item->data;

      clutter_animator_key_free (old_key);

      priv->score = g_list_remove (priv->score, old_key);
    }

  priv->score = g_list_insert_sorted (priv->score, key,
                                      sort_actor_prop_progress_func);

  /* if the animator is already running reinitialize internal iterators */
  if (clutter_timeline_is_playing (priv->timeline))
    animation_animator_started (priv->timeline, animator);
}

/**
 * clutter_animator_set_key:
 * @animator: a #ClutterAnimator
 * @object: a #GObject
 * @property_name: the property to specify a key for
 * @mode: the id of the alpha function to use
 * @progress: the normalized range at which stage of the animation this
 *   value applies
 * @value: the value property_name should have at progress.
 *
 * Sets a single key in the #ClutterAnimator for the @property_name of
 * @object at @progress.
 *
 * See also: clutter_animator_set()
 *
 * Return value: (transfer none): The animator instance
 *
 * Since: 1.2
 * Deprecated: 1.12: Use #ClutterKeyframeTransition instead
 */
ClutterAnimator *
clutter_animator_set_key (ClutterAnimator *animator,
                          GObject         *object,
                          const gchar     *property_name,
                          guint            mode,
                          gdouble          progress,
                          const GValue    *value)
{
  ClutterAnimatorKey *animator_key;

  g_return_val_if_fail (CLUTTER_IS_ANIMATOR (animator), NULL);
  g_return_val_if_fail (G_IS_OBJECT (object), NULL);
  g_return_val_if_fail (property_name, NULL);
  g_return_val_if_fail (value, NULL);

  property_name = g_intern_string (property_name);

  animator_key = clutter_animator_key_new (animator,
                                           object, property_name,
                                           progress,
                                           mode);

  g_value_init (&animator_key->value, G_VALUE_TYPE (value));
  g_value_copy (value, &animator_key->value);

  clutter_animator_set_key_internal (animator, animator_key);

  return animator;
}

/**
 * clutter_animator_get_keys:
 * @animator: a #ClutterAnimator instance
 * @object: (allow-none): a #GObject to search for, or %NULL for all objects
 * @property_name: (allow-none): a specific property name to query for,
 *   or %NULL for all properties
 * @progress: a specific progress to search for, or a negative value for all
 *   progresses
 *
 * Returns a list of pointers to opaque structures with accessor functions
 * that describe the keys added to an animator.
 *
 * Return value: (transfer container) (element-type Clutter.AnimatorKey): a
 *   list of #ClutterAnimatorKey<!-- -->s; the contents of the list are owned
 *   by the #ClutterAnimator, but you should free the returned list when done,
 *   using g_list_free()
 *
 * Since: 1.2
 * Deprecated: 1.12: Use #ClutterKeyframeTransition instead
 */
GList *
clutter_animator_get_keys (ClutterAnimator *animator,
                           GObject         *object,
                           const gchar     *property_name,
                           gdouble          progress)
{
  GList *keys = NULL;
  GList *k;

  g_return_val_if_fail (CLUTTER_IS_ANIMATOR (animator), NULL);
  g_return_val_if_fail (object == NULL || G_IS_OBJECT (object), NULL);

  property_name = g_intern_string (property_name);

  for (k = animator->priv->score; k; k = k->next)
    {
      ClutterAnimatorKey *key = k->data;

      if ((object == NULL || (object == key->object)) &&
          (property_name == NULL || (property_name == key->property_name)) &&
          (progress < 0  || fabs (progress - key->progress) < PROGRESS_EPSILON))
        {
          keys = g_list_prepend (keys, key);
        }
    }

  return g_list_reverse (keys);
}

static void
clutter_animator_remove_key_internal (ClutterAnimator *animator,
                                      GObject         *object,
                                      const gchar     *property_name,
                                      gdouble          progress,
                                      gboolean         is_inert)
{
  ClutterAnimatorPrivate *priv;
  GList *k;

  g_return_if_fail (CLUTTER_IS_ANIMATOR (animator));
  g_return_if_fail (object == NULL || G_IS_OBJECT (object));

  property_name = g_intern_string (property_name);

  priv = animator->priv;

again:
  for (k = priv->score; k != NULL; k = k->next)
    {
      ClutterAnimatorKey *key = k->data;

      if ((object == NULL        || (object == key->object)) &&
          (property_name == NULL || ((property_name == key->property_name))) &&
          (progress < 0  || fabs (progress - key->progress) < PROGRESS_EPSILON)
         )
        {
          ClutterAnimatorKey *prev_key = NULL;
          key->is_inert = is_inert;


          /* FIXME: non performant since we reiterate the list many times */

          prev_key = k->prev ? k->prev->data : NULL;

          if (!prev_key || prev_key->object   != key->object ||
                           prev_key->property_name != key->property_name)
            { /* We are removing the first key for a property ... */
              ClutterAnimatorKey *next_key = k->next ? k->next->data : NULL;
              if (next_key && next_key->object == key->object &&
                              next_key->property_name == key->property_name)
                {
                  /* ... and there is a key of our own type following us,
                   * copy interpolation/ease_in flags to the new first key
                   */
                  next_key->interpolation = key->interpolation;
                  next_key->ease_in = key->ease_in;
                }
            }

          clutter_animator_key_free (key);
          priv->score = g_list_remove (priv->score, key);
          goto again;
        }
    }

  /* clear off cached state for all properties, this is regenerated in a
   * correct state by animation_animator_started
   */
  g_hash_table_remove_all (priv->properties);

  /* if the animator is already running reinitialize internal iterators */
  if (priv->timeline != NULL && clutter_timeline_is_playing (priv->timeline))
    animation_animator_started (priv->timeline, animator);
}

/**
 * clutter_animator_remove_key:
 * @animator: a #ClutterAnimator
 * @object: (allow-none): a #GObject to search for, or %NULL for all
 * @property_name: (allow-none): a specific property name to query for,
 *   or %NULL for all
 * @progress: a specific progress to search for or a negative value
 *   for all
 *
 * Removes all keys matching the conditions specificed in the arguments.
 *
 * Since: 1.2
 * Deprecated: 1.12: Use #ClutterKeyframeTransition instead
 */
void
clutter_animator_remove_key (ClutterAnimator *animator,
                             GObject         *object,
                             const gchar     *property_name,
                             gdouble          progress)
{
  clutter_animator_remove_key_internal (animator, object, property_name,
                                        progress, FALSE);
}




typedef struct _ParseClosure {
  ClutterAnimator *animator;
  ClutterScript *script;

  GValue *value;

  gboolean result;
} ParseClosure;

static ClutterInterpolation
resolve_interpolation (JsonNode *node)
{
  if ((JSON_NODE_TYPE (node) != JSON_NODE_VALUE))
    return CLUTTER_INTERPOLATION_LINEAR;

  if (json_node_get_value_type (node) == G_TYPE_INT64)
    {
      return json_node_get_int (node);
    }
  else if (json_node_get_value_type (node) == G_TYPE_STRING)
    {
      const gchar *str = json_node_get_string (node);
      gboolean res;
      gint enum_value;

      res = _clutter_script_enum_from_string (CLUTTER_TYPE_INTERPOLATION,
                                              str,
                                              &enum_value);
      if (res)
        return enum_value;
    }

  return CLUTTER_INTERPOLATION_LINEAR;
}

static void
parse_animator_property (JsonArray *array,
                         guint      index_,
                         JsonNode  *element,
                         gpointer   data)
{
  ParseClosure *clos = data;
  JsonObject *object;
  JsonArray *keys;
  GObject *gobject;
  const gchar *id_, *pname;
  GObjectClass *klass;
  GParamSpec *pspec;
  GSList *valid_keys = NULL;
  GList *array_keys, *k;
  ClutterInterpolation interpolation = CLUTTER_INTERPOLATION_LINEAR;
  gboolean ease_in = FALSE;

  if (JSON_NODE_TYPE (element) != JSON_NODE_OBJECT)
    {
      g_warning ("The 'properties' member of a ClutterAnimator description "
                 "should be an array of objects, but the element %d of the "
                 "array is of type '%s'. The element will be ignored.",
                 index_,
                 json_node_type_name (element));
      return;
    }

  object = json_node_get_object (element);

  if (!json_object_has_member (object, "object") ||
      !json_object_has_member (object, "name") ||
      !json_object_has_member (object, "keys"))
    {
      g_warning ("The property description at index %d is missing one of "
                 "the mandatory fields: object, name and keys",
                 index_);
      return;
    }

  id_ = json_object_get_string_member (object, "object");
  gobject = clutter_script_get_object (clos->script, id_);
  if (gobject == NULL)
    {
      g_warning ("No object with id '%s' has been defined.", id_);
      return;
    }

  pname = json_object_get_string_member (object, "name");
  klass = G_OBJECT_GET_CLASS (gobject);
  pspec = g_object_class_find_property (klass, pname);
  if (pspec == NULL)
    {
      g_warning ("The object of type '%s' and name '%s' has no "
                 "property named '%s'",
                 G_OBJECT_TYPE_NAME (gobject),
                 id_,
                 pname);
      return;
    }

  if (json_object_has_member (object, "ease-in"))
    ease_in = json_object_get_boolean_member (object, "ease-in");

  if (json_object_has_member (object, "interpolation"))
    {
      JsonNode *node = json_object_get_member (object, "interpolation");

      interpolation = resolve_interpolation (node);
    }

  keys = json_object_get_array_member (object, "keys");
  if (keys == NULL)
    {
      g_warning ("The property description at index %d has an invalid "
                 "key field of type '%s' when an array was expected.",
                 index_,
                 json_node_type_name (json_object_get_member (object, "keys")));
      return;
    }

  if (G_IS_VALUE (clos->value))
    valid_keys = g_slist_reverse (g_value_get_pointer (clos->value));
  else
    g_value_init (clos->value, G_TYPE_POINTER);

  array_keys = json_array_get_elements (keys);
  for (k = array_keys; k != NULL; k = k->next)
    {
      JsonNode *node = k->data;
      JsonArray *key = json_node_get_array (node);
      ClutterAnimatorKey *animator_key;
      gdouble progress;
      gulong mode;
      gboolean res;

      progress = json_array_get_double_element (key, 0);
      mode = _clutter_script_resolve_animation_mode (json_array_get_element (key, 1));

      animator_key = clutter_animator_key_new (clos->animator,
                                               gobject,
                                               pname,
                                               progress,
                                               mode);

      res = _clutter_script_parse_node (clos->script,
                                        &(animator_key->value),
                                        pname,
                                        json_array_get_element (key, 2),
                                        pspec);
      if (!res)
        {
          g_warning ("Unable to parse the key value for the "
                     "property '%s' (progress: %.2f) at index %d",
                     pname,
                     progress,
                     index_);
          continue;
        }

      animator_key->ease_in = ease_in;
      animator_key->interpolation = interpolation;

      valid_keys = g_slist_prepend (valid_keys, animator_key);
    }

  g_list_free (array_keys);

  g_value_set_pointer (clos->value, g_slist_reverse (valid_keys));

  clos->result = TRUE;
}

static gboolean
clutter_animator_parse_custom_node (ClutterScriptable *scriptable,
                                    ClutterScript     *script,
                                    GValue            *value,
                                    const gchar       *name,
                                    JsonNode          *node)
{
  ParseClosure parse_closure;

  if (strcmp (name, "properties") != 0)
    return FALSE;

  if (JSON_NODE_TYPE (node) != JSON_NODE_ARRAY)
    return FALSE;

  parse_closure.animator = CLUTTER_ANIMATOR (scriptable);
  parse_closure.script = script;
  parse_closure.value = value;
  parse_closure.result = FALSE;

  json_array_foreach_element (json_node_get_array (node),
                              parse_animator_property,
                              &parse_closure);

  /* we return TRUE if we had at least one key parsed */

  return parse_closure.result;
}

static void
clutter_animator_set_custom_property (ClutterScriptable *scriptable,
                                      ClutterScript     *script,
                                      const gchar       *name,
                                      const GValue      *value)
{
  if (strcmp (name, "properties") == 0)
    {
      ClutterAnimator *animator = CLUTTER_ANIMATOR (scriptable);
      GSList *keys = g_value_get_pointer (value);
      GSList *k;

      for (k = keys; k != NULL; k = k->next)
        clutter_animator_set_key_internal (animator, k->data);

      g_slist_free (keys);
    }
  else
    g_object_set_property (G_OBJECT (scriptable), name, value);
}

static void
clutter_scriptable_init (ClutterScriptableIface *iface)
{
  iface->parse_custom_node = clutter_animator_parse_custom_node;
  iface->set_custom_property = clutter_animator_set_custom_property;
}

static void
clutter_animator_set_property (GObject      *gobject,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  ClutterAnimator *self = CLUTTER_ANIMATOR (gobject);

  switch (prop_id)
    {
    case PROP_DURATION:
      clutter_animator_set_duration (self, g_value_get_uint (value));
      break;

    case PROP_TIMELINE:
      clutter_animator_set_timeline (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_animator_get_property (GObject    *gobject,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  ClutterAnimatorPrivate *priv = CLUTTER_ANIMATOR (gobject)->priv;

  switch (prop_id)
    {
    case PROP_DURATION:
      g_value_set_uint (value, clutter_timeline_get_duration (priv->timeline));
      break;

    case PROP_TIMELINE:
      g_value_set_object (value, priv->timeline);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_animator_class_init (ClutterAnimatorClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = clutter_animator_set_property;
  gobject_class->get_property = clutter_animator_get_property;
  gobject_class->dispose = clutter_animator_dispose;
  gobject_class->finalize = clutter_animator_finalize;

  /**
   * ClutterAnimator:duration:
   *
   * The duration of the #ClutterTimeline used by the #ClutterAnimator
   * to drive the animation
   *
   * Since: 1.2
   * Deprecated: 1.12: Use #ClutterKeyframeTransition instead
   */
  obj_props[PROP_DURATION] =
    g_param_spec_uint ("duration",
                       P_("Duration"),
                       P_("The duration of the animation"),
                       0, G_MAXUINT,
                       2000,
                       CLUTTER_PARAM_READWRITE);

  /**
   * ClutterAnimator:timeline:
   *
   * The #ClutterTimeline used by the #ClutterAnimator to drive the
   * animation
   *
   * Since: 1.2
   * Deprecated: 1.12: Use #ClutterKeyframeTransition instead
   */
  obj_props[PROP_TIMELINE] =
    g_param_spec_object ("timeline",
                         P_("Timeline"),
                         P_("The timeline of the animation"),
                         CLUTTER_TYPE_TIMELINE,
                         CLUTTER_PARAM_READWRITE);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     obj_props);
}

static void
clutter_animator_init (ClutterAnimator *animator)
{
  ClutterAnimatorPrivate *priv;
  ClutterTimeline *timeline;

  animator->priv = priv = clutter_animator_get_instance_private (animator);

  priv->properties = g_hash_table_new_full (prop_actor_hash,
                                            prop_actor_equal,
                                            prop_actor_key_free,
                                            property_iter_free);

  timeline = clutter_timeline_new (2000);
  clutter_animator_set_timeline (animator, timeline);
  g_object_unref (timeline);

  priv->slave_timeline = clutter_timeline_new (10000);
}


/**
 * clutter_animator_property_get_ease_in:
 * @animator: a #ClutterAnimatorKey
 * @object: a #GObject
 * @property_name: the name of a property on object
 *
 * Checks if a property value is to be eased into the animation.
 *
 * Return value: %TRUE if the property is eased in
 *
 * Since: 1.2
 * Deprecated: 1.12: Use #ClutterKeyframeTransition instead
 */
gboolean
clutter_animator_property_get_ease_in (ClutterAnimator *animator,
                                       GObject         *object,
                                       const gchar     *property_name)
{
  ClutterAnimatorKey  key, *initial_key;
  GList              *initial;

  g_return_val_if_fail (CLUTTER_IS_ANIMATOR (animator), FALSE);
  g_return_val_if_fail (G_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (property_name, FALSE);

  key.object        = object;
  key.property_name = g_intern_string (property_name);
  initial = g_list_find_custom (animator->priv->score, &key,
                                sort_actor_prop_func);
  if (initial != NULL)
    {
      initial_key = initial->data;

      return initial_key->ease_in;
    }

  return FALSE;
}

/**
 * clutter_animator_property_set_ease_in:
 * @animator: a #ClutterAnimatorKey
 * @object: a #GObject
 * @property_name: the name of a property on object
 * @ease_in: we are going to be easing in this property
 *
 * Sets whether a property value is to be eased into the animation.
 *
 * Since: 1.2
 * Deprecated: 1.12: Use #ClutterKeyframeTransition instead
 */
void
clutter_animator_property_set_ease_in (ClutterAnimator *animator,
                                       GObject         *object,
                                       const gchar     *property_name,
                                       gboolean         ease_in)
{
  ClutterAnimatorKey  key, *initial_key;
  GList              *initial;

  g_return_if_fail (CLUTTER_IS_ANIMATOR (animator));
  g_return_if_fail (G_IS_OBJECT (object));
  g_return_if_fail (property_name);

  key.object        = object;
  key.property_name = g_intern_string (property_name);
  initial = g_list_find_custom (animator->priv->score, &key,
                                sort_actor_prop_func);
  if (initial)
    {
      initial_key = initial->data;
      initial_key->ease_in = ease_in;
    }
  else
    g_warning ("The animator has no object of type '%s' with a "
               "property named '%s'",
               G_OBJECT_TYPE_NAME (object),
               property_name);
}


/**
 * clutter_animator_property_get_interpolation:
 * @animator: a #ClutterAnimatorKey
 * @object: a #GObject
 * @property_name: the name of a property on object
 *
 * Get the interpolation used by animator for a property on a particular
 * object.
 *
 * Returns: a ClutterInterpolation value.
 * Since: 1.2
 * Deprecated: 1.12: Use #ClutterKeyframeTransition instead
 */
ClutterInterpolation
clutter_animator_property_get_interpolation (ClutterAnimator *animator,
                                             GObject         *object,
                                             const gchar     *property_name)
{
  GList              *initial;
  ClutterAnimatorKey  key, *initial_key;

  g_return_val_if_fail (CLUTTER_IS_ANIMATOR (animator),
                        CLUTTER_INTERPOLATION_LINEAR);
  g_return_val_if_fail (G_IS_OBJECT (object),
                        CLUTTER_INTERPOLATION_LINEAR);
  g_return_val_if_fail (property_name,
                        CLUTTER_INTERPOLATION_LINEAR);

  key.object        = object;
  key.property_name = g_intern_string (property_name);
  initial = g_list_find_custom (animator->priv->score, &key,
                                sort_actor_prop_func);
  if (initial)
    {
      initial_key = initial->data;

      return initial_key->interpolation;
    }

  return CLUTTER_INTERPOLATION_LINEAR;
}

/**
 * clutter_animator_property_set_interpolation:
 * @animator: a #ClutterAnimatorKey
 * @object: a #GObject
 * @property_name: the name of a property on object
 * @interpolation: the #ClutterInterpolation to use
 *
 * Set the interpolation method to use, %CLUTTER_INTERPOLATION_LINEAR causes
 * the values to linearly change between the values, and
 * %CLUTTER_INTERPOLATION_CUBIC causes the values to smoothly change between
 * the values.
 *
 * Since: 1.2
 * Deprecated: 1.12: Use #ClutterKeyframeTransition instead
 */
void
clutter_animator_property_set_interpolation (ClutterAnimator     *animator,
                                             GObject             *object,
                                             const gchar         *property_name,
                                             ClutterInterpolation interpolation)
{
  GList              *initial;
  ClutterAnimatorKey  key, *initial_key;

  g_return_if_fail (CLUTTER_IS_ANIMATOR (animator));
  g_return_if_fail (G_IS_OBJECT (object));
  g_return_if_fail (property_name);

  key.object        = object;
  key.property_name = g_intern_string (property_name);
  initial = g_list_find_custom (animator->priv->score, &key,
                                sort_actor_prop_func);
  if (initial)
    {
      initial_key = initial->data;
      initial_key->interpolation = interpolation;
    }
}

G_DEFINE_BOXED_TYPE (ClutterAnimatorKey, clutter_animator_key,
                     clutter_animator_key_copy,
                     clutter_animator_key_free);

/**
 * clutter_animator_key_get_object:
 * @key: a #ClutterAnimatorKey
 *
 * Retrieves the object a key applies to.
 *
 * Return value: (transfer none): the object an animator_key exist for.
 *
 * Since: 1.2
 * Deprecated: 1.12: Use #ClutterKeyframeTransition instead
 */
GObject *
clutter_animator_key_get_object (const ClutterAnimatorKey *key)
{
  g_return_val_if_fail (key != NULL, NULL);

  return key->object;
}

/**
 * clutter_animator_key_get_property_name:
 * @key: a #ClutterAnimatorKey
 *
 * Retrieves the name of the property a key applies to.
 *
 * Return value: the name of the property an animator_key exist for.
 *
 * Since: 1.2
 * Deprecated: 1.12: Use #ClutterKeyframeTransition instead
 */
const gchar *
clutter_animator_key_get_property_name (const ClutterAnimatorKey *key)
{
  g_return_val_if_fail (key != NULL, NULL);

  return key->property_name;
}

/**
 * clutter_animator_key_get_property_type:
 * @key: a #ClutterAnimatorKey
 *
 * Retrieves the #GType of the property a key applies to
 *
 * You can use this type to initialize the #GValue to pass to
 * clutter_animator_key_get_value()
 *
 * Return value: the #GType of the property
 *
 * Since: 1.2
 * Deprecated: 1.12: Use #ClutterKeyframeTransition instead
 */
GType
clutter_animator_key_get_property_type (const ClutterAnimatorKey *key)
{
  g_return_val_if_fail (key != NULL, G_TYPE_INVALID);

  return G_VALUE_TYPE (&key->value);
}

/**
 * clutter_animator_key_get_mode:
 * @key: a #ClutterAnimatorKey
 *
 * Retrieves the mode of a #ClutterAnimator key, for the first key of a
 * property for an object this represents the whether the animation is
 * open ended and or curved for the remainding keys for the property it
 * represents the easing mode.
 *
 * Return value: the mode of a #ClutterAnimatorKey
 *
 * Since: 1.2
 * Deprecated: 1.12: Use #ClutterKeyframeTransition instead
 */
gulong
clutter_animator_key_get_mode (const ClutterAnimatorKey *key)
{
  g_return_val_if_fail (key != NULL, 0);

  return key->mode;
}

/**
 * clutter_animator_key_get_progress:
 * @key: a #ClutterAnimatorKey
 *
 * Retrieves the progress of an clutter_animator_key
 *
 * Return value: the progress defined for a #ClutterAnimator key.
 *
 * Since: 1.2
 * Deprecated: 1.12: Use #ClutterKeyframeTransition instead
 */
gdouble
clutter_animator_key_get_progress (const ClutterAnimatorKey *key)
{
  g_return_val_if_fail (key != NULL, 0.0);

  return key->progress;
}

/**
 * clutter_animator_key_get_value:
 * @key: a #ClutterAnimatorKey
 * @value: a #GValue initialized with the correct type for the animator key
 *
 * Retrieves a copy of the value for a #ClutterAnimatorKey.
 *
 * The passed in #GValue needs to be already initialized for the value
 * type of the key or to a type that allow transformation from the value
 * type of the key.
 *
 * Use g_value_unset() when done.
 *
 * Return value: %TRUE if the passed #GValue was successfully set, and
 *   %FALSE otherwise
 *
 * Since: 1.2
 * Deprecated: 1.12: Use #ClutterKeyframeTransition instead
 */
gboolean
clutter_animator_key_get_value (const ClutterAnimatorKey *key,
                                GValue                   *value)
{
  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (G_VALUE_TYPE (value) != G_TYPE_INVALID, FALSE);

  if (!g_type_is_a (G_VALUE_TYPE (&key->value), G_VALUE_TYPE (value)))
    {
      if (g_value_type_compatible (G_VALUE_TYPE (&key->value),
                                   G_VALUE_TYPE (value)))
        {
          g_value_copy (&key->value, value);
          return TRUE;
        }

      if (g_value_type_transformable (G_VALUE_TYPE (&key->value),
                                      G_VALUE_TYPE (value)))
        {
          if (g_value_transform (&key->value, value))
            return TRUE;
        }

      g_warning ("%s: Unable to convert from %s to %s for the "
                 "property '%s' of object %s in the animator key",
                 G_STRLOC,
                 g_type_name (G_VALUE_TYPE (&key->value)),
                 g_type_name (G_VALUE_TYPE (value)),
                 key->property_name,
                 G_OBJECT_TYPE_NAME (key->object));

      return FALSE;
    }
  else
    g_value_copy (&key->value, value);

  return TRUE;
}
