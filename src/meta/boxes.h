/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Simple box operations */

/*
 * Copyright (C) 2005, 2006 Elijah Newren
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

#ifndef META_BOXES_H
#define META_BOXES_H

#include <glib-object.h>
#include <meta/common.h>

#define META_TYPE_RECTANGLE            (meta_rectangle_get_type ())

/**
 * MetaRectangle:
 * @x: X coordinate of the top-left corner
 * @y: Y coordinate of the top-left corner
 * @width: Width of the rectangle
 * @height: Height of the rectangle
 */
#ifdef __GI_SCANNER__
/* The introspection scanner is currently unable to lookup how
 * cairo_rectangle_int_t is actually defined. This prevents
 * introspection data for the GdkRectangle type to include fields
 * descriptions. To workaround this issue, we define it with the same
 * content as cairo_rectangle_int_t, but only under the introspection
 * define.
 */
struct _MetaRectangle
{
  int x;
  int y;
  int width;
  int height;
};
typedef struct _MetaRectangle MetaRectangle;
#else
typedef cairo_rectangle_int_t MetaRectangle;
#endif

/**
 * MetaStrut:
 * @rect: #MetaRectangle the #MetaStrut is on
 * @side: #MetaSide the #MetaStrut is on
 */
typedef struct _MetaStrut MetaStrut;
struct _MetaStrut
{
  MetaRectangle rect;
  MetaSide side;
};

/**
 * MetaEdgeType:
 * @META_EDGE_WINDOW: Whether the edge belongs to a window
 * @META_EDGE_MONITOR: Whether the edge belongs to a monitor
 * @META_EDGE_SCREEN: Whether the edge belongs to a screen
 */
typedef enum
{
  META_EDGE_WINDOW,
  META_EDGE_MONITOR,
  META_EDGE_SCREEN
} MetaEdgeType;

/**
 * MetaEdge:
 * @rect: #MetaRectangle with the bounds of the edge
 * @side_type: Side
 * @edge_type: To what belongs the edge
 */
typedef struct _MetaEdge MetaEdge;
struct _MetaEdge
{
  MetaRectangle rect;      /* width or height should be 1 */
  MetaSide side_type;
  MetaEdgeType  edge_type;
};

META_EXPORT
GType meta_rectangle_get_type (void);

META_EXPORT
MetaRectangle *meta_rectangle_copy (const MetaRectangle *rect);

META_EXPORT
void           meta_rectangle_free (MetaRectangle       *rect);

/* Function to make initializing a rect with a single line of code easy */
META_EXPORT
MetaRectangle                 meta_rect (int x, int y, int width, int height);

/* Basic comparison functions */
META_EXPORT
int      meta_rectangle_area            (const MetaRectangle *rect);

META_EXPORT
gboolean meta_rectangle_intersect       (const MetaRectangle *src1,
                                         const MetaRectangle *src2,
                                         MetaRectangle       *dest);

META_EXPORT
gboolean meta_rectangle_equal           (const MetaRectangle *src1,
                                         const MetaRectangle *src2);

/* Find the bounding box of the union of two rectangles */
META_EXPORT
void     meta_rectangle_union           (const MetaRectangle *rect1,
                                         const MetaRectangle *rect2,
                                         MetaRectangle       *dest);

/* overlap is similar to intersect but doesn't provide location of
 * intersection information.
 */
META_EXPORT
gboolean meta_rectangle_overlap         (const MetaRectangle *rect1,
                                         const MetaRectangle *rect2);

/* vert_overlap means ignore the horizontal location and ask if the
 * vertical parts overlap.  An alternate way to think of it is "Does there
 * exist a way to shift either rect horizontally so that the two rects
 * overlap?"  horiz_overlap is similar.
 */
META_EXPORT
gboolean meta_rectangle_vert_overlap    (const MetaRectangle *rect1,
                                         const MetaRectangle *rect2);

META_EXPORT
gboolean meta_rectangle_horiz_overlap   (const MetaRectangle *rect1,
                                         const MetaRectangle *rect2);

/* could_fit_rect determines whether "outer_rect" is big enough to contain
 * inner_rect.  contains_rect checks whether it actually contains it.
 */
META_EXPORT
gboolean meta_rectangle_could_fit_rect  (const MetaRectangle *outer_rect,
                                         const MetaRectangle *inner_rect);

META_EXPORT
gboolean meta_rectangle_contains_rect   (const MetaRectangle *outer_rect,
                                         const MetaRectangle *inner_rect);

#endif /* META_BOXES_H */
