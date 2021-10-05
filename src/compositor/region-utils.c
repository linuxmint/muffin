/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Utilities for region manipulation
 *
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "backends/meta-monitor-transform.h"
#include "compositor/region-utils.h"
#include "core/boxes-private.h"

#include <math.h>

#define META_REGION_MAX_STACK_RECTS 256

#define META_REGION_CREATE_RECTANGLE_ARRAY_SCOPED(n_rects, rects) \
  g_autofree cairo_rectangle_int_t *G_PASTE(__n, __LINE__) = NULL; \
  if (n_rects < META_REGION_MAX_STACK_RECTS) \
    rects = g_newa (cairo_rectangle_int_t, n_rects); \
  else \
    rects = G_PASTE(__n, __LINE__) = g_new (cairo_rectangle_int_t, n_rects);

/* MetaRegionBuilder */

/* Various algorithms in this file require unioning together a set of rectangles
 * that are unsorted or overlap; unioning such a set of rectangles 1-by-1
 * using cairo_region_union_rectangle() produces O(N^2) behavior (if the union
 * adds or removes rectangles in the middle of the region, then it has to
 * move all the rectangles after that.) To avoid this behavior, MetaRegionBuilder
 * creates regions for small groups of rectangles and merges them together in
 * a binary tree.
 *
 * Possible improvement: From a glance at the code, accumulating all the rectangles
 *  into a flat array and then calling the (not usefully documented)
 *  cairo_region_create_rectangles() would have the same behavior and would be
 *  simpler and a bit more efficient.
 */

/* Optimium performance seems to be with MAX_CHUNK_RECTANGLES=4; 8 is about 10% slower.
 * But using 8 may be more robust to systems with slow malloc(). */
#define MAX_CHUNK_RECTANGLES 8

void
meta_region_builder_init (MetaRegionBuilder *builder)
{
  int i;
  for (i = 0; i < META_REGION_BUILDER_MAX_LEVELS; i++)
    builder->levels[i] = NULL;
  builder->n_levels = 1;
}

void
meta_region_builder_add_rectangle (MetaRegionBuilder *builder,
                                   int                x,
                                   int                y,
                                   int                width,
                                   int                height)
{
  cairo_rectangle_int_t rect;
  int i;

  if (builder->levels[0] == NULL)
    builder->levels[0] = cairo_region_create ();

  rect.x = x;
  rect.y = y;
  rect.width = width;
  rect.height = height;

  cairo_region_union_rectangle (builder->levels[0], &rect);
  if (cairo_region_num_rectangles (builder->levels[0]) >= MAX_CHUNK_RECTANGLES)
    {
      for (i = 1; i < builder->n_levels + 1; i++)
        {
          if (builder->levels[i] == NULL)
            {
              if (i < META_REGION_BUILDER_MAX_LEVELS)
                {
                  builder->levels[i] = builder->levels[i - 1];
                  builder->levels[i - 1] = NULL;
                  if (i == builder->n_levels)
                    builder->n_levels++;
                }

              break;
            }
          else
            {
              cairo_region_union (builder->levels[i], builder->levels[i - 1]);
              cairo_region_destroy (builder->levels[i - 1]);
              builder->levels[i - 1] = NULL;
            }
        }
    }
}

cairo_region_t *
meta_region_builder_finish (MetaRegionBuilder *builder)
{
  cairo_region_t *result = NULL;
  int i;

  for (i = 0; i < builder->n_levels; i++)
    {
      if (builder->levels[i])
        {
          if (result == NULL)
            result = builder->levels[i];
          else
            {
              cairo_region_union(result, builder->levels[i]);
              cairo_region_destroy (builder->levels[i]);
            }
        }
    }

  if (result == NULL)
    result = cairo_region_create ();

  return result;
}


/* MetaRegionIterator */

void
meta_region_iterator_init (MetaRegionIterator *iter,
                           cairo_region_t     *region)
{
  iter->region = region;
  iter->i = 0;
  iter->n_rectangles = cairo_region_num_rectangles (region);
  iter->line_start = TRUE;

  if (iter->n_rectangles > 1)
    {
      cairo_region_get_rectangle (region, 0, &iter->rectangle);
      cairo_region_get_rectangle (region, 1, &iter->next_rectangle);

      iter->line_end = iter->next_rectangle.y != iter->rectangle.y;
    }
  else if (iter->n_rectangles > 0)
    {
      cairo_region_get_rectangle (region, 0, &iter->rectangle);
      iter->line_end = TRUE;
    }
}

gboolean
meta_region_iterator_at_end (MetaRegionIterator *iter)
{
  return iter->i >= iter->n_rectangles;
}

void
meta_region_iterator_next (MetaRegionIterator *iter)
{
  iter->i++;
  iter->rectangle = iter->next_rectangle;
  iter->line_start = iter->line_end;

  if (iter->i + 1 < iter->n_rectangles)
    {
      cairo_region_get_rectangle (iter->region, iter->i + 1, &iter->next_rectangle);
      iter->line_end = iter->next_rectangle.y != iter->rectangle.y;
    }
  else
    {
      iter->line_end = TRUE;
    }
}

cairo_region_t *
meta_region_scale_double (cairo_region_t       *region,
                          double                scale,
                          MetaRoundingStrategy  rounding_strategy)
{
  int n_rects, i;
  cairo_rectangle_int_t *rects;
  cairo_region_t *scaled_region;

  g_return_val_if_fail (scale > 0.0, NULL);

  if (G_APPROX_VALUE (scale, 1.f, FLT_EPSILON))
    return cairo_region_copy (region);

  n_rects = cairo_region_num_rectangles (region);
  META_REGION_CREATE_RECTANGLE_ARRAY_SCOPED (n_rects, rects);
  for (i = 0; i < n_rects; i++)
    {
      cairo_region_get_rectangle (region, i, &rects[i]);

      meta_rectangle_scale_double (&rects[i], scale, rounding_strategy,
                                   &rects[i]);
    }

  scaled_region = cairo_region_create_rectangles (rects, n_rects);

  return scaled_region;
}

cairo_region_t *
meta_region_scale (cairo_region_t *region, int scale)
{
  int n_rects, i;
  cairo_rectangle_int_t *rects;
  cairo_region_t *scaled_region;

  if (scale == 1)
    return cairo_region_copy (region);

  n_rects = cairo_region_num_rectangles (region);
  META_REGION_CREATE_RECTANGLE_ARRAY_SCOPED (n_rects, rects);
  for (i = 0; i < n_rects; i++)
    {
      cairo_region_get_rectangle (region, i, &rects[i]);
      rects[i].x *= scale;
      rects[i].y *= scale;
      rects[i].width *= scale;
      rects[i].height *= scale;
    }

  scaled_region = cairo_region_create_rectangles (rects, n_rects);

  return scaled_region;
}

static void
add_expanded_rect (MetaRegionBuilder  *builder,
                   int                 x,
                   int                 y,
                   int                 width,
                   int                 height,
                   int                 x_amount,
                   int                 y_amount,
                   gboolean            flip)
{
  if (flip)
    meta_region_builder_add_rectangle (builder,
                                       y - y_amount, x - x_amount,
                                       height + 2 * y_amount, width + 2 * x_amount);
  else
    meta_region_builder_add_rectangle (builder,
                                       x - x_amount, y - y_amount,
                                       width + 2 * x_amount, height + 2 * y_amount);
}

static cairo_region_t *
expand_region (cairo_region_t *region,
               int             x_amount,
               int             y_amount,
               gboolean        flip)
{
  MetaRegionBuilder builder;
  int n;
  int i;

  meta_region_builder_init (&builder);

  n = cairo_region_num_rectangles (region);
  for (i = 0; i < n; i++)
    {
      cairo_rectangle_int_t rect;

      cairo_region_get_rectangle (region, i, &rect);
      add_expanded_rect (&builder,
                         rect.x, rect.y, rect.width, rect.height,
                         x_amount, y_amount, flip);
    }

  return meta_region_builder_finish (&builder);
}

/* This computes a (clipped version) of the inverse of the region
 * and expands it by the given amount */
static cairo_region_t *
expand_region_inverse (cairo_region_t *region,
                       int             x_amount,
                       int             y_amount,
                       gboolean        flip)
{
  MetaRegionBuilder builder;
  MetaRegionIterator iter;
  cairo_rectangle_int_t extents;

  int last_x;

  meta_region_builder_init (&builder);

  cairo_region_get_extents (region, &extents);
  add_expanded_rect (&builder,
                     extents.x, extents.y - 1, extents.width, 1,
                     x_amount, y_amount, flip);
  add_expanded_rect (&builder,
                     extents.x - 1, extents.y, 1, extents.height,
                     x_amount, y_amount, flip);
  add_expanded_rect (&builder,
                     extents.x + extents.width, extents.y, 1, extents.height,
                     x_amount, y_amount, flip);
  add_expanded_rect (&builder,
                     extents.x, extents.y + extents.height, extents.width, 1,
                     x_amount, y_amount, flip);

  last_x = extents.x;
  for (meta_region_iterator_init (&iter, region);
       !meta_region_iterator_at_end (&iter);
       meta_region_iterator_next (&iter))
    {
      if (iter.rectangle.x > last_x)
        add_expanded_rect (&builder,
                           last_x, iter.rectangle.y,
                           iter.rectangle.x - last_x, iter.rectangle.height,
                           x_amount, y_amount, flip);

      if (iter.line_end)
        {
          if (extents.x + extents.width > iter.rectangle.x + iter.rectangle.width)
            add_expanded_rect (&builder,
                               iter.rectangle.x + iter.rectangle.width, iter.rectangle.y,
                               (extents.x + extents.width) - (iter.rectangle.x + iter.rectangle.width), iter.rectangle.height,
                               x_amount, y_amount, flip);
          last_x = extents.x;
        }
      else
        last_x = iter.rectangle.x + iter.rectangle.width;
    }

  return meta_region_builder_finish (&builder);
}

/**
 * meta_make_border_region:
 * @region: a #cairo_region_t
 * @x_amount: distance from the border to extend horizontally
 * @y_amount: distance from the border to extend vertically
 * @flip: if true, the result is computed with x and y interchanged
 *
 * Computes the "border region" of a given region, which is roughly
 * speaking the set of points near the boundary of the region.  If we
 * define the operation of growing a region as computing the set of
 * points within a given manhattan distance of the region, then the
 * border is 'grow(region) intersect grow(inverse(region))'.
 *
 * If we create an image by filling the region with a solid color,
 * the border is the region affected by blurring the region.
 *
 * Return value: a new region which is the border of the given region
 */
cairo_region_t *
meta_make_border_region (cairo_region_t *region,
                         int             x_amount,
                         int             y_amount,
                         gboolean        flip)
{
  cairo_region_t *border_region;
  cairo_region_t *inverse_region;

  border_region = expand_region (region, x_amount, y_amount, flip);
  inverse_region = expand_region_inverse (region, x_amount, y_amount, flip);
  cairo_region_intersect (border_region, inverse_region);
  cairo_region_destroy (inverse_region);

  return border_region;
}

cairo_region_t *
meta_region_transform (cairo_region_t       *region,
                       MetaMonitorTransform  transform,
                       int                   width,
                       int                   height)
{
  int n_rects, i;
  cairo_rectangle_int_t *rects;
  cairo_region_t *transformed_region;

  if (transform == META_MONITOR_TRANSFORM_NORMAL)
    return cairo_region_copy (region);

  n_rects = cairo_region_num_rectangles (region);
  META_REGION_CREATE_RECTANGLE_ARRAY_SCOPED (n_rects, rects);
  for (i = 0; i < n_rects; i++)
    {
      cairo_region_get_rectangle (region, i, &rects[i]);

      meta_rectangle_transform (&rects[i],
                                transform,
                                width,
                                height,
                                &rects[i]);
    }

  transformed_region = cairo_region_create_rectangles (rects, n_rects);

  return transformed_region;
}

cairo_region_t *
meta_region_crop_and_scale (cairo_region_t  *region,
                            graphene_rect_t *src_rect,
                            int              dst_width,
                            int              dst_height)
{
  int n_rects, i;
  cairo_rectangle_int_t *rects;
  cairo_region_t *viewport_region;

  if (G_APPROX_VALUE (src_rect->size.width, dst_width, FLT_EPSILON) &&
      G_APPROX_VALUE (src_rect->size.height, dst_height, FLT_EPSILON) &&
      G_APPROX_VALUE (roundf (src_rect->origin.x),
                      src_rect->origin.x, FLT_EPSILON) &&
      G_APPROX_VALUE (roundf (src_rect->origin.y),
                      src_rect->origin.y, FLT_EPSILON))
    {
      viewport_region = cairo_region_copy (region);

      if (G_APPROX_VALUE (src_rect->origin.x, 0, FLT_EPSILON) ||
          G_APPROX_VALUE (src_rect->origin.y, 0, FLT_EPSILON))
        {
          cairo_region_translate (viewport_region,
                                  (int) src_rect->origin.x,
                                  (int) src_rect->origin.y);
        }

      return viewport_region;
    }

  n_rects = cairo_region_num_rectangles (region);
  META_REGION_CREATE_RECTANGLE_ARRAY_SCOPED (n_rects, rects);
  for (i = 0; i < n_rects; i++)
    {
      cairo_region_get_rectangle (region, i, &rects[i]);

      meta_rectangle_crop_and_scale (&rects[i],
                                     src_rect,
                                     dst_width,
                                     dst_height,
                                     &rects[i]);
    }

  viewport_region = cairo_region_create_rectangles (rects, n_rects);

  return viewport_region;
}
