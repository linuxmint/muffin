/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2010 Intel Corporation.
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

#ifndef __COGL_TEXTURE_PIXMAP_X11_H
#define __COGL_TEXTURE_PIXMAP_X11_H

/* NB: this is a top-level header that can be included directly but we
 * want to be careful not to define __COGL_H_INSIDE__ when this is
 * included internally while building Cogl itself since
 * __COGL_H_INSIDE__ is used in headers to guard public vs private api
 * definitions
 */
#ifndef COGL_COMPILATION

/* Note: When building Cogl .gir we explicitly define
 * __COGL_H_INSIDE__ */
#ifndef __COGL_H_INSIDE__
#define __COGL_H_INSIDE__
#define __COGL_MUST_UNDEF_COGL_H_INSIDE_COGL_TEXTURE_PIXMAP_X11_
#endif

#endif /* COGL_COMPILATION */

#include <cogl/cogl-context.h>

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * SECTION:cogl-texture-pixmap-x11
 * @short_description: Functions for creating and manipulating 2D meta
 *                     textures derived from X11 pixmaps.
 *
 * These functions allow high-level meta textures (See the
 * #CoglMetaTexture interface) that derive their contents from an X11
 * pixmap.
 */

typedef struct _CoglTexturePixmapX11 CoglTexturePixmapX11;

#define COGL_TEXTURE_PIXMAP_X11(X) ((CoglTexturePixmapX11 *)X)

/**
 * cogl_texture_pixmap_x11_get_gtype:
 *
 * Returns: a #GType that can be used with the GLib type system.
 */
COGL_EXPORT
GType cogl_texture_pixmap_x11_get_gtype (void);

typedef enum
{
  COGL_TEXTURE_PIXMAP_X11_DAMAGE_RAW_RECTANGLES,
  COGL_TEXTURE_PIXMAP_X11_DAMAGE_DELTA_RECTANGLES,
  COGL_TEXTURE_PIXMAP_X11_DAMAGE_BOUNDING_BOX,
  COGL_TEXTURE_PIXMAP_X11_DAMAGE_NON_EMPTY
} CoglTexturePixmapX11ReportLevel;

/**
 * COGL_TEXTURE_PIXMAP_X11_ERROR:
 *
 * #GError domain for texture-pixmap-x11 errors.
 *
 * Since: 1.10
 */
#define COGL_TEXTURE_PIXMAP_X11_ERROR (cogl_texture_pixmap_x11_error_quark ())

/**
 * CoglTexturePixmapX11Error:
 * @COGL_TEXTURE_PIXMAP_X11_ERROR_X11: An X11 protocol error
 *
 * Error codes that can be thrown when performing texture-pixmap-x11
 * operations.
 *
 * Since: 1.10
 */
typedef enum
{
  COGL_TEXTURE_PIXMAP_X11_ERROR_X11,
} CoglTexturePixmapX11Error;

COGL_EXPORT
uint32_t cogl_texture_pixmap_x11_error_quark (void);

/**
 * cogl_texture_pixmap_x11_new:
 * @context: A #CoglContext
 * @pixmap: A X11 pixmap ID
 * @automatic_updates: Whether to automatically copy the contents of
 * the pixmap to the texture.
 * @error: A #GError for exceptions
 *
 * Creates a texture that contains the contents of @pixmap. If
 * @automatic_updates is %TRUE then Cogl will attempt to listen for
 * damage events on the pixmap and automatically update the texture
 * when it changes.
 *
 * Return value: a new #CoglTexturePixmapX11 instance
 *
 * Since: 1.10
 * Stability: Unstable
 */
COGL_EXPORT CoglTexturePixmapX11 *
cogl_texture_pixmap_x11_new (CoglContext *context,
                             uint32_t pixmap,
                             gboolean automatic_updates,
                             GError **error);

/**
 * cogl_texture_pixmap_x11_new_left:
 * @context: A #CoglContext
 * @pixmap: A X11 pixmap ID
 * @automatic_updates: Whether to automatically copy the contents of
 * the pixmap to the texture.
 * @error: A #GError for exceptions
 *
 * Creates one of a pair of textures to contain the contents of @pixmap,
 * which has stereo content. (Different images for the right and left eyes.)
 * The left image is drawn using this texture; the right image is drawn
 * using a texture created by calling
 * cogl_texture_pixmap_x11_new_right() and passing in this texture as an
 * argument.
 *
 * In general, you should not use this function unless you have
 * queried the %GLX_STEREO_TREE_EXT attribute of the corresponding
 * window using glXQueryDrawable() and determined that the window is
 * stereo. Note that this attribute can change over time and
 * notification is also provided through events defined in the
 * EXT_stereo_tree GLX extension. As long as the system has support for
 * stereo content, drawing using the left and right pixmaps will not
 * produce an error even if the window doesn't have stereo
 * content any more, but drawing with the right pixmap will produce
 * undefined output, so you need to listen for these events and
 * re-render to avoid race conditions. (Recreating a non-stereo
 * pixmap is not necessary, but may save resources.)
 *
 * Return value: a new #CoglTexturePixmapX11 instance
 *
 * Since: 1.20
 * Stability: Unstable
 */
COGL_EXPORT CoglTexturePixmapX11 *
cogl_texture_pixmap_x11_new_left (CoglContext *context,
                                  uint32_t pixmap,
                                  gboolean automatic_updates,
                                  GError **error);

/**
 * cogl_texture_pixmap_x11_new_right:
 * @left_texture: A #CoglTexturePixmapX11 instance created with
 *                cogl_texture_pixmap_x11_new_left().
 *
 * Creates a texture object that corresponds to the right-eye image
 * of a pixmap with stereo content. @left_texture must have been
 * created using cogl_texture_pixmap_x11_new_left().
 *
 * Return value: a new #CoglTexturePixmapX11 instance
 *
 * Since: 1.20
 * Stability: Unstable
 */
COGL_EXPORT CoglTexturePixmapX11 *
cogl_texture_pixmap_x11_new_right (CoglTexturePixmapX11 *left_texture);

/**
 * cogl_texture_pixmap_x11_update_area:
 * @texture: A #CoglTexturePixmapX11 instance
 * @x: x coordinate of the area to update
 * @y: y coordinate of the area to update
 * @width: width of the area to update
 * @height: height of the area to update
 *
 * Forces an update of the given @texture so that it is refreshed with
 * the contents of the pixmap that was given to
 * cogl_texture_pixmap_x11_new().
 *
 * Since: 1.4
 * Stability: Unstable
 */
COGL_EXPORT void
cogl_texture_pixmap_x11_update_area (CoglTexturePixmapX11 *texture,
                                     int x,
                                     int y,
                                     int width,
                                     int height);

/**
 * cogl_texture_pixmap_x11_is_using_tfp_extension:
 * @texture: A #CoglTexturePixmapX11 instance
 *
 * Checks whether the given @texture is using the
 * GLX_EXT_texture_from_pixmap or similar extension to copy the
 * contents of the pixmap to the texture.  This extension is usually
 * implemented as zero-copy operation so it implies the updates are
 * working efficiently.
 *
 * Return value: %TRUE if the texture is using an efficient extension
 *   and %FALSE otherwise
 *
 * Since: 1.4
 * Stability: Unstable
 */
COGL_EXPORT gboolean
cogl_texture_pixmap_x11_is_using_tfp_extension (CoglTexturePixmapX11 *texture);

/**
 * cogl_is_texture_pixmap_x11:
 * @object: A pointer to a #CoglObject
 *
 * Checks whether @object points to a #CoglTexturePixmapX11 instance.
 *
 * Return value: %TRUE if the object is a #CoglTexturePixmapX11, and
 *   %FALSE otherwise
 *
 * Since: 1.4
 * Stability: Unstable
 */
COGL_EXPORT gboolean
cogl_is_texture_pixmap_x11 (void *object);

G_END_DECLS

/* The gobject introspection scanner seems to parse public headers in
 * isolation which means we need to be extra careful about how we
 * define and undefine __COGL_H_INSIDE__ used to detect when internal
 * headers are incorrectly included by developers. In the gobject
 * introspection case we have to manually define __COGL_H_INSIDE__ as
 * a commandline argument for the scanner which means we must be
 * careful not to undefine it in a header...
 */
#ifdef __COGL_MUST_UNDEF_COGL_H_INSIDE_COGL_TEXTURE_PIXMAP_X11_
#undef __COGL_H_INSIDE__
#undef __COGL_MUST_UNDEF_COGL_H_INSIDE_COGL_TEXTURE_PIXMAP_X11_
#endif

#endif /* __COGL_TEXTURE_PIXMAP_X11_H */
