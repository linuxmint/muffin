/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010  Intel Corporation.
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

#ifndef __CLUTTER_CONSTRAINT_H__
#define __CLUTTER_CONSTRAINT_H__

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <clutter/clutter-actor-meta.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_CONSTRAINT                 (clutter_constraint_get_type ())
#define CLUTTER_CONSTRAINT(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_CONSTRAINT, ClutterConstraint))
#define CLUTTER_IS_CONSTRAINT(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_CONSTRAINT))
#define CLUTTER_CONSTRAINT_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), CLUTTER_TYPE_CONSTRAINT, ClutterConstraintClass))
#define CLUTTER_IS_CONSTRAINT_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), CLUTTER_TYPE_CONSTRAINT))
#define CLUTTER_CONSTRAINT_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), CLUTTER_TYPE_CONSTRAINT, ClutterConstraintClass))

typedef struct _ClutterConstraintClass          ClutterConstraintClass;

/**
 * ClutterConstraint:
 *
 * The #ClutterConstraint structure contains only
 * private data and should be accessed using the provided API
 *
 * Since: 1.4
 */
struct _ClutterConstraint
{
  /*< private >*/
  ClutterActorMeta parent_instance;
};

/**
 * ClutterConstraintClass:
 * @update_allocation: virtual function used to update the allocation
 *   of the #ClutterActor using the #ClutterConstraint
 * @update_preferred_size: virtual function used to update the preferred
 *   size of the #ClutterActor using the #ClutterConstraint; optional,
 *   since 1.22
 *
 * The #ClutterConstraintClass structure contains
 * only private data
 *
 * Since: 1.4
 */
struct _ClutterConstraintClass
{
  /*< private >*/
  ClutterActorMetaClass parent_class;

  /*< public >*/
  void (* update_allocation) (ClutterConstraint *constraint,
                              ClutterActor      *actor,
                              ClutterActorBox   *allocation);

  void (* update_preferred_size) (ClutterConstraint  *constraint,
                                  ClutterActor       *actor,
                                  ClutterOrientation  direction,
                                  float               for_size,
                                  float              *minimum_size,
                                  float              *natural_size);

  /*< private >*/
  void (* _clutter_constraint1) (void);
  void (* _clutter_constraint2) (void);
  void (* _clutter_constraint3) (void);
  void (* _clutter_constraint4) (void);
  void (* _clutter_constraint5) (void);
  void (* _clutter_constraint6) (void);
  void (* _clutter_constraint7) (void);
};

CLUTTER_EXPORT
GType clutter_constraint_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
void clutter_constraint_update_preferred_size (ClutterConstraint  *constraint,
                                               ClutterActor       *actor,
                                               ClutterOrientation  direction,
                                               float               for_size,
                                               float              *minimum_size,
                                               float              *natural_size);

/* ClutterActor API */
CLUTTER_EXPORT
void               clutter_actor_add_constraint            (ClutterActor      *self,
                                                            ClutterConstraint *constraint);
CLUTTER_EXPORT
void               clutter_actor_add_constraint_with_name  (ClutterActor      *self,
                                                            const gchar       *name,
                                                            ClutterConstraint *constraint);
CLUTTER_EXPORT
void               clutter_actor_remove_constraint         (ClutterActor      *self,
                                                            ClutterConstraint *constraint);
CLUTTER_EXPORT
void               clutter_actor_remove_constraint_by_name (ClutterActor      *self,
                                                            const gchar       *name);
CLUTTER_EXPORT
GList *            clutter_actor_get_constraints           (ClutterActor      *self);
CLUTTER_EXPORT
ClutterConstraint *clutter_actor_get_constraint            (ClutterActor      *self,
                                                            const gchar       *name);
CLUTTER_EXPORT
void               clutter_actor_clear_constraints         (ClutterActor      *self);

CLUTTER_EXPORT
gboolean           clutter_actor_has_constraints           (ClutterActor      *self);

G_END_DECLS

#endif /* __CLUTTER_CONSTRAINT_H__ */
