/*
 * Copyright (C) 2020 Red Hat Inc.
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

#include "tests/monitor-transform-tests.h"

#include "backends/meta-monitor-transform.h"

static void
test_transform (void)
{
  const struct
  {
    MetaMonitorTransform transform;
    MetaMonitorTransform other;
    MetaMonitorTransform expect;
  } tests[] = {
    {
      .transform = META_MONITOR_TRANSFORM_NORMAL,
      .other = META_MONITOR_TRANSFORM_90,
      .expect = META_MONITOR_TRANSFORM_90,
    },
    {
      .transform = META_MONITOR_TRANSFORM_NORMAL,
      .other = META_MONITOR_TRANSFORM_FLIPPED_90,
      .expect = META_MONITOR_TRANSFORM_FLIPPED_90,
    },
    {
      .transform = META_MONITOR_TRANSFORM_90,
      .other = META_MONITOR_TRANSFORM_90,
      .expect = META_MONITOR_TRANSFORM_180,
    },
    {
      .transform = META_MONITOR_TRANSFORM_FLIPPED_90,
      .other = META_MONITOR_TRANSFORM_90,
      .expect = META_MONITOR_TRANSFORM_FLIPPED_180,
    },
    {
      .transform = META_MONITOR_TRANSFORM_FLIPPED_90,
      .other = META_MONITOR_TRANSFORM_180,
      .expect = META_MONITOR_TRANSFORM_FLIPPED_270,
    },
    {
      .transform = META_MONITOR_TRANSFORM_FLIPPED_180,
      .other = META_MONITOR_TRANSFORM_FLIPPED_180,
      .expect = META_MONITOR_TRANSFORM_NORMAL,
    },
    {
      .transform = META_MONITOR_TRANSFORM_NORMAL,
      .other = -META_MONITOR_TRANSFORM_90,
      .expect = META_MONITOR_TRANSFORM_270,
    },
    {
      .transform = META_MONITOR_TRANSFORM_FLIPPED,
      .other = -META_MONITOR_TRANSFORM_90,
      .expect = META_MONITOR_TRANSFORM_FLIPPED_270,
    },
    {
      .transform = META_MONITOR_TRANSFORM_FLIPPED_180,
      .other = -META_MONITOR_TRANSFORM_270,
      .expect = META_MONITOR_TRANSFORM_FLIPPED_270,
    },
    {
      .transform = META_MONITOR_TRANSFORM_FLIPPED_180,
      .other = -META_MONITOR_TRANSFORM_FLIPPED_180,
      .expect = META_MONITOR_TRANSFORM_NORMAL,
    },
  };
  int i;

  for (i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      MetaMonitorTransform result;

      result = meta_monitor_transform_transform (tests[i].transform,
                                                 tests[i].other);
      g_assert_cmpint (result, ==, tests[i].expect);
    }
}

void
init_monitor_transform_tests (void)
{
  g_test_add_func ("/util/monitor-transform/transform", test_transform);
}
