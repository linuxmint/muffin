/*
 * Copyright (C) 2018 Robert Mader
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

MetaMonitorTransform
meta_monitor_transform_invert (MetaMonitorTransform transform)
{
  switch (transform)
    {
    case META_MONITOR_TRANSFORM_90:
      return META_MONITOR_TRANSFORM_270;
    case META_MONITOR_TRANSFORM_270:
      return META_MONITOR_TRANSFORM_90;
    case META_MONITOR_TRANSFORM_NORMAL:
    case META_MONITOR_TRANSFORM_180:
    case META_MONITOR_TRANSFORM_FLIPPED:
    case META_MONITOR_TRANSFORM_FLIPPED_90:
    case META_MONITOR_TRANSFORM_FLIPPED_180:
    case META_MONITOR_TRANSFORM_FLIPPED_270:
      return transform;
    }
  g_assert_not_reached ();
  return 0;
}

MetaMonitorTransform
meta_monitor_transform_transform (MetaMonitorTransform transform,
                                  MetaMonitorTransform other)
{
  MetaMonitorTransform new_transform;

  new_transform = (transform + other) % META_MONITOR_TRANSFORM_FLIPPED;
  if (meta_monitor_transform_is_flipped (transform) !=
      meta_monitor_transform_is_flipped (other))
    new_transform += META_MONITOR_TRANSFORM_FLIPPED;

  return new_transform;
}

/**
 * meta_monitor_transform_relative_transform:
 * @transform: The transform to start from
 * @other: The transform to go to
 *
 * Return value: a transform to get from @transform to @other
 */
MetaMonitorTransform
meta_monitor_transform_relative_transform (MetaMonitorTransform transform,
                                           MetaMonitorTransform other)
{
  MetaMonitorTransform relative_transform;

  relative_transform = ((other % META_MONITOR_TRANSFORM_FLIPPED -
                         transform % META_MONITOR_TRANSFORM_FLIPPED) %
                        META_MONITOR_TRANSFORM_FLIPPED);

  if (meta_monitor_transform_is_flipped (transform) !=
      meta_monitor_transform_is_flipped (other))
    {
      relative_transform = (meta_monitor_transform_invert (relative_transform) +
                            META_MONITOR_TRANSFORM_FLIPPED);
    }

  return relative_transform;
}

void
meta_monitor_transform_transform_point (MetaMonitorTransform  transform,
                                        int                   area_width,
                                        int                   area_height,
                                        int                   x,
                                        int                   y,
                                        int                  *out_x,
                                        int                  *out_y)
{
  switch (transform)
    {
    case META_MONITOR_TRANSFORM_NORMAL:
      *out_x = x;
      *out_y = y;
      break;
    case META_MONITOR_TRANSFORM_90:
      *out_x = area_width - y;
      *out_y = x;
      break;
    case META_MONITOR_TRANSFORM_180:
      *out_x = area_width - x;
      *out_y = area_height - y;
      break;
    case META_MONITOR_TRANSFORM_270:
      *out_x = y,
      *out_y = area_height - x;
      break;
    case META_MONITOR_TRANSFORM_FLIPPED:
      *out_x = area_width - x;
      *out_y = y;
      break;
    case META_MONITOR_TRANSFORM_FLIPPED_90:
      *out_x = area_width - y;
      *out_y = area_height - x;
      break;
    case META_MONITOR_TRANSFORM_FLIPPED_180:
      *out_x = x;
      *out_y = area_height - y;
      break;
    case META_MONITOR_TRANSFORM_FLIPPED_270:
      *out_x = y;
      *out_y = x;
      break;
    }
}
