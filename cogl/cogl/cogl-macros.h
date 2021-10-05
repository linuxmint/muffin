/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2012 Intel Corporation
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
 */

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#ifndef __COGL_MACROS_H__
#define __COGL_MACROS_H__

#include <cogl/cogl-version.h>

/* These macros are used to mark deprecated functions, and thus have
 * to be exposed in a public header.
 *
 * They are only intended for internal use and should not be used by
 * other projects.
 */
#if defined(COGL_DISABLE_DEPRECATION_WARNINGS) || defined(COGL_COMPILATION)

#define COGL_DEPRECATED
#define COGL_DEPRECATED_FOR(f)
#define COGL_UNAVAILABLE(maj,min)

#else /* COGL_DISABLE_DEPRECATION_WARNINGS */

#if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1)
#define COGL_DEPRECATED __attribute__((__deprecated__))
#elif defined(_MSC_VER) && (_MSC_VER >= 1300)
#define COGL_DEPRECATED __declspec(deprecated)
#else
#define COGL_DEPRECATED
#endif

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 5)
#define COGL_DEPRECATED_FOR(f) __attribute__((__deprecated__("Use '" #f "' instead")))
#elif defined(_MSC_FULL_VER) && (_MSC_FULL_VER > 140050320)
#define COGL_DEPRECATED_FOR(f) __declspec(deprecated("is deprecated. Use '" #f "' instead"))
#else
#define COGL_DEPRECATED_FOR(f) G_DEPRECATED
#endif

#if    __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 5)
#define COGL_UNAVAILABLE(maj,min) __attribute__((deprecated("Not available before " #maj "." #min)))
#elif defined(_MSC_FULL_VER) && (_MSC_FULL_VER > 140050320)
#define COGL_UNAVAILABLE(maj,min) __declspec(deprecated("is not available before " #maj "." #min))
#else
#define COGL_UNAVAILABLE(maj,min)
#endif

#endif /* COGL_DISABLE_DEPRECATION_WARNINGS */

#define COGL_EXPORT __attribute__((visibility("default"))) extern

#endif /* __COGL_MACROS_H__ */
