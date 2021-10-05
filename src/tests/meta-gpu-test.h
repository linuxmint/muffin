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

#ifndef META_GPU_TEST_H
#define META_GPU_TEST_H

#include "backends/meta-gpu.h"

#define META_TYPE_GPU_TEST (meta_gpu_test_get_type ())
G_DECLARE_FINAL_TYPE (MetaGpuTest, meta_gpu_test, META, GPU_TEST, MetaGpu)

#endif /* META_GPU_TEST_H */
