/*
 * Copyright (C) 2016-2018 Red Hat, Inc.
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

#include "tests/meta-gpu-test.h"

#include "backends/meta-backend-private.h"
#include "tests/meta-monitor-manager-test.h"

struct _MetaGpuTest
{
  MetaGpu parent;
};

G_DEFINE_TYPE (MetaGpuTest, meta_gpu_test, META_TYPE_GPU)

static gboolean
meta_gpu_test_read_current (MetaGpu  *gpu,
                            GError  **error)
{
  MetaBackend *backend = meta_gpu_get_backend (gpu);
  MetaMonitorManager *manager = meta_backend_get_monitor_manager (backend);

  meta_monitor_manager_test_read_current (manager);

  return TRUE;
}

static void
meta_gpu_test_init (MetaGpuTest *gpu_test)
{
}

static void
meta_gpu_test_class_init (MetaGpuTestClass *klass)
{
  MetaGpuClass *gpu_class = META_GPU_CLASS (klass);

  gpu_class->read_current = meta_gpu_test_read_current;
}
