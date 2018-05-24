/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2012 Intel Corporation
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

#ifndef __CLUTTER_MACROS_H__
#define __CLUTTER_MACROS_H__

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

/**
 * CLUTTER_FLAVOUR:
 *
 * GL Windowing system used
 *
 * Since: 0.4
 *
 * Deprecated: 1.10: The macro evaluates to "deprecated" as Clutter can be
 *   compiled with multiple windowing system backends. Use the various
 *   CLUTTER_WINDOWING_* macros to detect the windowing system that Clutter
 *   is being compiled against, and the type check macros for the
 *   #ClutterBackend for a run-time check.
 */
#define CLUTTER_FLAVOUR         "deprecated"

/**
 * CLUTTER_COGL:
 *
 * Cogl (internal GL abstraction utility library) backend. Can be "gl" or
 * "gles" currently
 *
 * Since: 0.4
 *
 * Deprecated: 1.10: The macro evaluates to "deprecated" as Cogl can be
 *   compiled against multiple GL implementations.
 */
#define CLUTTER_COGL            "deprecated"

/**
 * CLUTTER_STAGE_TYPE:
 *
 * The default GObject type for the Clutter stage.
 *
 * Since: 0.8
 *
 * Deprecated: 1.10: The macro evaluates to "deprecated" as Clutter can
 *   be compiled against multiple windowing systems. You can use the
 *   CLUTTER_WINDOWING_* macros for compile-time checks, and the type
 *   check macros for run-time checks.
 */
#define CLUTTER_STAGE_TYPE      "deprecated"

/**
 * CLUTTER_NO_FPU:
 *
 * Set to 1 if Clutter was built without FPU (i.e fixed math), 0 otherwise
 *
 * Deprecated: 0.6: This macro is no longer defined (identical code is used
 *  regardless the presence of FPU).
 */
#define CLUTTER_NO_FPU          (0)

/* some structures are meant to be opaque and still be allocated on the stack;
 * in order to avoid people poking at their internals, we use this macro to
 * ensure that users don't accidentally access a struct private members.
 *
 * we use the CLUTTER_COMPILATION define to allow us easier access, though.
 */
#ifdef CLUTTER_COMPILATION
#define CLUTTER_PRIVATE_FIELD(x)        x
#else
#define CLUTTER_PRIVATE_FIELD(x)        clutter_private_ ## x
#endif

#define _CLUTTER_EXTERN __attribute__((visibility("default"))) extern

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) || \
  __clang_major__ > 3 || (__clang_major__ == 3 && __clang_minor__ >= 4)
#define _CLUTTER_GNUC_DO_PRAGMA(x) _Pragma(G_STRINGIFY (x))
#define _CLUTTER_DEPRECATED_MACRO _CLUTTER_GNUC_DO_PRAGMA(GCC warning "Deprecated macro")
#define _CLUTTER_DEPRECATED_MACRO_FOR(f) _CLUTTER_GNUC_DO_PRAGMA(GCC warning #f)
#else
#define _CLUTTER_DEPRECATED_MACRO
#define _CLUTTER_DEPRECATED_MACRO_FOR(f)
#endif

/* these macros are used to mark deprecated functions, and thus have to be
 * exposed in a public header.
 *
 * do *not* use them in other libraries depending on Clutter: use G_DEPRECATED
 * and G_DEPRECATED_FOR, or use your own wrappers around them.
 */
#ifdef CLUTTER_DISABLE_DEPRECATION_WARNINGS
#define CLUTTER_DEPRECATED _CLUTTER_EXTERN
#define CLUTTER_DEPRECATED_FOR(f) _CLUTTER_EXTERN
#define CLUTTER_DEPRECATED_MACRO
#define CLUTTER_DEPRECATED_MACRO_FOR(f)
#else
#define CLUTTER_DEPRECATED G_DEPRECATED _CLUTTER_EXTERN
#define CLUTTER_DEPRECATED_FOR(f) G_DEPRECATED_FOR(f) _CLUTTER_EXTERN
#define CLUTTER_DEPRECATED_MACRO _CLUTTER_DEPRECATED_MACRO
#define CLUTTER_DEPRECATED_MACRO_FOR(f) _CLUTTER_DEPRECATED_MACRO_FOR(f)
#endif

#define CLUTTER_MACRO_DEPRECATED CLUTTER_DEPRECATED_MACRO
#define CLUTTER_MACRO_DEPRECATED_FOR(f) CLUTTER_DEPRECATED_MACRO_FOR(f)

#define CLUTTER_EXPORT _CLUTTER_EXTERN

#endif /* __CLUTTER_MACROS_H__ */
