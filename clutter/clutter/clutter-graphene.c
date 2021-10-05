/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
 *
 * Copyright (C) 2019 Endless, Inc
 * Copyright (C) 2009, 2010 Intel Corp
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

#include "clutter-build-config.h"

#include "clutter-graphene.h"

#include "clutter-private.h"
#include "clutter-types.h"

static gboolean
graphene_point_progress (const GValue *a,
                         const GValue *b,
                         double        progress,
                         GValue       *retval)
{
  const graphene_point_t *ap = g_value_get_boxed (a);
  const graphene_point_t *bp = g_value_get_boxed (b);
  graphene_point_t res;

  graphene_point_interpolate (ap, bp, progress, &res);

  g_value_set_boxed (retval, &res);

  return TRUE;
}

static gboolean
graphene_point3d_progress (const GValue *a,
                           const GValue *b,
                           double        progress,
                           GValue       *retval)
{
  const graphene_point3d_t *av = g_value_get_boxed (a);
  const graphene_point3d_t *bv = g_value_get_boxed (b);
  graphene_point3d_t res;

  graphene_point3d_interpolate (av, bv, progress, &res);

  g_value_set_boxed (retval, &res);

  return TRUE;
}

static gboolean
graphene_rect_progress (const GValue *a,
                        const GValue *b,
                        double        progress,
                        GValue       *retval)
{
  const graphene_rect_t *rect_a = g_value_get_boxed (a);
  const graphene_rect_t *rect_b = g_value_get_boxed (b);
  graphene_rect_t res;

  graphene_rect_interpolate (rect_a, rect_b, progress, &res);

  g_value_set_boxed (retval, &res);

  return TRUE;
}

static gboolean
graphene_size_progress (const GValue *a,
                        const GValue *b,
                        double        progress,
                        GValue       *retval)
{
  const graphene_size_t *as = g_value_get_boxed (a);
  const graphene_size_t *bs = g_value_get_boxed (b);
  graphene_size_t res;

  graphene_size_interpolate (as, bs, progress, &res);

  g_value_set_boxed (retval, &res);

  return TRUE;
}

void
clutter_graphene_init (void)
{
  clutter_interval_register_progress_func (GRAPHENE_TYPE_POINT,
                                           graphene_point_progress);
  clutter_interval_register_progress_func (GRAPHENE_TYPE_POINT3D,
                                           graphene_point3d_progress);
  clutter_interval_register_progress_func (GRAPHENE_TYPE_RECT,
                                           graphene_rect_progress);
  clutter_interval_register_progress_func (GRAPHENE_TYPE_SIZE,
                                           graphene_size_progress);
}
