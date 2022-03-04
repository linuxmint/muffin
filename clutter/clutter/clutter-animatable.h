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

#ifndef __CLUTTER_ANIMATABLE_H__
#define __CLUTTER_ANIMATABLE_H__

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <clutter/clutter-types.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_ANIMATABLE (clutter_animatable_get_type ())

CLUTTER_EXPORT
G_DECLARE_INTERFACE (ClutterAnimatable, clutter_animatable,
                     CLUTTER, ANIMATABLE,
                     GObject)

/**
 * ClutterAnimatableInterface:
 * @animate_property: virtual function for custom interpolation of a
 *   property. This virtual function is deprecated
 * @find_property: virtual function for retrieving the #GParamSpec of
 *   an animatable property
 * @get_initial_state: virtual function for retrieving the initial
 *   state of an animatable property
 * @set_final_state: virtual function for setting the state of an
 *   animatable property
 * @interpolate_value: virtual function for interpolating the progress
 *   of a property
 *
 * Base interface for #GObject<!-- -->s that can be animated by a
 * a #ClutterAnimation.
 *
 * Since: 1.0
 */
struct _ClutterAnimatableInterface
{
  /*< private >*/
  GTypeInterface parent_iface;

  /*< public >*/
  gboolean    (* animate_property)  (ClutterAnimatable *animatable,
                                     ClutterAnimation  *animation,
                                     const gchar       *property_name,
                                     const GValue      *initial_value,
                                     const GValue      *final_value,
                                     gdouble            progress,
                                     GValue            *value);
  GParamSpec *(* find_property)     (ClutterAnimatable *animatable,
                                     const gchar       *property_name);
  void        (* get_initial_state) (ClutterAnimatable *animatable,
                                     const gchar       *property_name,
                                     GValue            *value);
  void        (* set_final_state)   (ClutterAnimatable *animatable,
                                     const gchar       *property_name,
                                     const GValue      *value);
  gboolean    (* interpolate_value) (ClutterAnimatable *animatable,
                                     const gchar       *property_name,
                                     ClutterInterval   *interval,
                                     gdouble            progress,
                                     GValue            *value);
};

CLUTTER_EXPORT
GParamSpec *clutter_animatable_find_property     (ClutterAnimatable *animatable,
                                                  const gchar       *property_name);
CLUTTER_EXPORT
void        clutter_animatable_get_initial_state (ClutterAnimatable *animatable,
                                                  const gchar       *property_name,
                                                  GValue            *value);
CLUTTER_EXPORT
void        clutter_animatable_set_final_state   (ClutterAnimatable *animatable,
                                                  const gchar       *property_name,
                                                  const GValue      *value);
CLUTTER_EXPORT
gboolean    clutter_animatable_interpolate_value (ClutterAnimatable *animatable,
                                                  const gchar       *property_name,
                                                  ClutterInterval   *interval,
                                                  gdouble            progress,
                                                  GValue            *value);

G_END_DECLS

#endif /* __CLUTTER_ANIMATABLE_H__ */
