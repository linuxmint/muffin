/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2012,2013 Intel Corporation.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 */

#ifndef __COGL_VERSION_H__
#define __COGL_VERSION_H__

#include <cogl/cogl-defines.h>


/* Macros to handle compacting a 3-component version number into an
 * int for quick comparison. This assumes all of the components are <=
 * 1023 and that an int is >= 31 bits */
#define COGL_VERSION_COMPONENT_BITS 10
#define COGL_VERSION_MAX_COMPONENT_VALUE        \
  ((1 << COGL_VERSION_COMPONENT_BITS) - 1)

/**
 * COGL_VERSION_ENCODE:
 * @major: The major part of a version number
 * @minor: The minor part of a version number
 * @micro: The micro part of a version number
 *
 * Encodes a 3 part version number into a single integer. This can be
 * used to compare the Cogl version. For example if there is a known
 * bug in Cogl versions between 1.3.2 and 1.3.4 you could use the
 * following code to provide a workaround:
 *
 * |[
 * #if COGL_VERSION >= COGL_VERSION_ENCODE (1, 3, 2) && \
 *     COGL_VERSION <= COGL_VERSION_ENCODE (1, 3, 4)
 *   /<!-- -->* Do the workaround *<!-- -->/
 * #endif
 * ]|
 *
 * Since: 1.12.0
 */
#define COGL_VERSION_ENCODE(major, minor, micro)        \
  (((major) << (COGL_VERSION_COMPONENT_BITS * 2)) |     \
   ((minor) << COGL_VERSION_COMPONENT_BITS)             \
   | (micro))

/**
 * COGL_VERSION_GET_MAJOR:
 * @version: An encoded version number
 *
 * Extracts the major part of an encoded version number.
 *
 * Since: 1.12.0
 */
#define COGL_VERSION_GET_MAJOR(version)                 \
  (((version) >> (COGL_VERSION_COMPONENT_BITS * 2))     \
   & COGL_VERSION_MAX_COMPONENT_VALUE)

/**
 * COGL_VERSION_GET_MINOR:
 * @version: An encoded version number
 *
 * Extracts the minor part of an encoded version number.
 *
 * Since: 1.12.0
 */
#define COGL_VERSION_GET_MINOR(version)         \
  (((version) >> COGL_VERSION_COMPONENT_BITS) & \
   COGL_VERSION_MAX_COMPONENT_VALUE)

/**
 * COGL_VERSION_GET_MICRO:
 * @version: An encoded version number
 *
 * Extracts the micro part of an encoded version number.
 *
 * Since: 1.12.0
 */
#define COGL_VERSION_GET_MICRO(version) \
  ((version) & COGL_VERSION_MAX_COMPONENT_VALUE)

/**
 * COGL_VERSION_CHECK:
 * @major: The major part of a version number
 * @minor: The minor part of a version number
 * @micro: The micro part of a version number
 *
 * A convenient macro to check whether the Cogl version being compiled
 * against is at least the given version number. For example if the
 * function cogl_pipeline_frobnicate was added in version 2.0.1 and
 * you want to conditionally use that function when it is available,
 * you could write the following:
 *
 * |[
 * #if COGL_VERSION_CHECK (2, 0, 1)
 * cogl_pipeline_frobnicate (pipeline);
 * #else
 * /<!-- -->* Frobnication is not supported. Use a red color instead *<!-- -->/
 * cogl_pipeline_set_color_4f (pipeline, 1.0f, 0.0f, 0.0f, 1.0f);
 * #endif
 * ]|
 *
 * Return value: %TRUE if the Cogl version being compiled against is
 *   greater than or equal to the given three part version number.
 * Since: 1.12.0
 */
#define COGL_VERSION_CHECK(major, minor, micro) \
  (COGL_VERSION >= COGL_VERSION_ENCODE (major, minor, micro))

#endif /* __COGL_VERSION_H__ */
