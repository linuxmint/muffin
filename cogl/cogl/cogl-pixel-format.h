/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2008,2009 Intel Corporation.
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

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#ifndef __COGL_PIXEL_FORMAT_H__
#define __COGL_PIXEL_FORMAT_H__

#include <stdint.h>
#include <stddef.h>

#include <cogl/cogl-defines.h>
#include <cogl/cogl-macros.h>

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

/**
 * SECTION:cogl-pixel-format
 * @short_description: Pixel formats supported by Cogl
 *
 * The pixel format of an image descrbes how the bits of each pixel are
 * represented in memory. For example: an image can be laid out as one long
 * sequence of pixels, where each pixel is a sequence of 8 bits of Red, Green
 * and Blue. The amount of bits that are used can be different for each pixel
 * format, as well as the components (for example an Alpha layer to include
 * transparency, or non_RGBA).
 *
 * Other examples of factors that can influence the layout in memory are the
 * system's endianness.
 */

#define COGL_A_BIT              (1 << 4)
#define COGL_BGR_BIT            (1 << 5)
#define COGL_AFIRST_BIT         (1 << 6)
#define COGL_PREMULT_BIT        (1 << 7)
#define COGL_DEPTH_BIT          (1 << 8)
#define COGL_STENCIL_BIT        (1 << 9)

/* XXX: Notes to those adding new formats here...
 *
 * First this diagram outlines how we allocate the 32bits of a
 * CoglPixelFormat currently...
 *
 *                            6 bits for flags
 *                          |-----|
 *  enum        unused             4 bits for the bytes-per-pixel
 *                                 and component alignment info
 *  |------| |-------------|       |--|
 *  00000000 xxxxxxxx xxxxxxSD PFBA0000
 *                          ^ stencil
 *                           ^ depth
 *                             ^ premult
 *                              ^ alpha first
 *                               ^ bgr order
 *                                ^ has alpha
 *
 * The most awkward part about the formats is how we use the last 4
 * bits to encode the bytes per pixel and component alignment
 * information. Ideally we should have had 3 bits for the bpp and a
 * flag for alignment but we didn't plan for that in advance so we
 * instead use a small lookup table to query the bpp and whether the
 * components are byte aligned or not.
 *
 * The mapping is the following (see discussion on bug #660188):
 *
 * 0     = undefined
 * 1, 8  = 1 bpp (e.g. A_8, G_8)
 * 2     = 3 bpp, aligned (e.g. 888)
 * 3     = 4 bpp, aligned (e.g. 8888)
 * 4-6   = 2 bpp, not aligned (e.g. 565, 4444, 5551)
 * 7     = YUV: undefined bpp, undefined alignment
 * 9     = 2 bpp, aligned
 * 10    = depth, aligned (8, 16, 24, 32, 32f)
 * 11    = 8 bpp fp16
 * 12    = 3 bpp, not aligned
 * 13    = 4 bpp, not aligned (e.g. 2101010)
 * 14-15 = undefined
 *
 * Note: the gap at 10-11 is just because we wanted to maintain that
 * all non-aligned formats have the third bit set in case that's
 * useful later.
 *
 * Since we don't want to waste bits adding more and more flags, we'd
 * like to see most new pixel formats that can't be represented
 * uniquely with the existing flags in the least significant byte
 * simply be enumerated with sequential values in the most significant
 * enum byte.
 *
 * Note: Cogl avoids exposing any padded XRGB or RGBX formats and
 * instead we leave it up to applications to decided whether they
 * consider the A component as padding or valid data. We shouldn't
 * change this policy without good reasoning.
 *
 * So to add a new format:
 * 1) Use the mapping table above to figure out what to but in
 *    the lowest nibble.
 * 2) OR in the COGL_PREMULT_BIT, COGL_AFIRST_BIT, COGL_A_BIT and
 *    COGL_BGR_BIT flags as appropriate.
 * 3) If the result is not yet unique then also combine with an
 *    increment of the last sequence number in the most significant
 *    byte.
 *
 * The last sequence number used was 0 (i.e. no formats currently need
 *                                      a sequence number)
 * Update this note whenever a new sequence number is used.
 */
/**
 * CoglPixelFormat:
 * @COGL_PIXEL_FORMAT_ANY: Any format
 * @COGL_PIXEL_FORMAT_A_8: 8 bits alpha mask
 * @COGL_PIXEL_FORMAT_RG_88: RG, 16 bits. Note that red-green textures
 *   are only available if %COGL_FEATURE_ID_TEXTURE_RG is advertised.
 *   See cogl_texture_set_components() for details.
 * @COGL_PIXEL_FORMAT_RGB_565: RGB, 16 bits
 * @COGL_PIXEL_FORMAT_RGBA_4444: RGBA, 16 bits
 * @COGL_PIXEL_FORMAT_RGBA_5551: RGBA, 16 bits
 * @COGL_PIXEL_FORMAT_YUV: Not currently supported
 * @COGL_PIXEL_FORMAT_G_8: Single luminance component
 * @COGL_PIXEL_FORMAT_RGB_888: RGB, 24 bits
 * @COGL_PIXEL_FORMAT_BGR_888: BGR, 24 bits
 * @COGL_PIXEL_FORMAT_RGBA_8888: RGBA, 32 bits
 * @COGL_PIXEL_FORMAT_BGRA_8888: BGRA, 32 bits
 * @COGL_PIXEL_FORMAT_ARGB_8888: ARGB, 32 bits
 * @COGL_PIXEL_FORMAT_ABGR_8888: ABGR, 32 bits
 * @COGL_PIXEL_FORMAT_RGBA_1010102 : RGBA, 32 bits, 10 bpc
 * @COGL_PIXEL_FORMAT_BGRA_1010102 : BGRA, 32 bits, 10 bpc
 * @COGL_PIXEL_FORMAT_ARGB_2101010 : ARGB, 32 bits, 10 bpc
 * @COGL_PIXEL_FORMAT_ABGR_2101010 : ABGR, 32 bits, 10 bpc
 * @COGL_PIXEL_FORMAT_RGBA_8888_PRE: Premultiplied RGBA, 32 bits
 * @COGL_PIXEL_FORMAT_BGRA_8888_PRE: Premultiplied BGRA, 32 bits
 * @COGL_PIXEL_FORMAT_ARGB_8888_PRE: Premultiplied ARGB, 32 bits
 * @COGL_PIXEL_FORMAT_ABGR_8888_PRE: Premultiplied ABGR, 32 bits
 * @COGL_PIXEL_FORMAT_RGBA_4444_PRE: Premultiplied RGBA, 16 bits
 * @COGL_PIXEL_FORMAT_RGBA_5551_PRE: Premultiplied RGBA, 16 bits
 * @COGL_PIXEL_FORMAT_RGBA_1010102_PRE: Premultiplied RGBA, 32 bits, 10 bpc
 * @COGL_PIXEL_FORMAT_BGRA_1010102_PRE: Premultiplied BGRA, 32 bits, 10 bpc
 * @COGL_PIXEL_FORMAT_ARGB_2101010_PRE: Premultiplied ARGB, 32 bits, 10 bpc
 * @COGL_PIXEL_FORMAT_ABGR_2101010_PRE: Premultiplied ABGR, 32 bits, 10 bpc
 * @COGL_PIXEL_FORMAT_RGBA_FP_16161616: RGBA half floating point, 64 bit
 * @COGL_PIXEL_FORMAT_BGRA_FP_16161616: BGRA half floating point, 64 bit
 * @COGL_PIXEL_FORMAT_ARGB_FP_16161616: ARGB half floating point, 64 bit
 * @COGL_PIXEL_FORMAT_ABGR_FP_16161616: ABGR half floating point, 64 bit
 * @COGL_PIXEL_FORMAT_RGBA_FP_16161616_PRE: Premultiplied RGBA half floating point, 64 bit
 * @COGL_PIXEL_FORMAT_BGRA_FP_16161616_PRE: Premultiplied BGRA half floating point, 64 bit
 * @COGL_PIXEL_FORMAT_ARGB_FP_16161616_PRE: Premultiplied ARGB half floating point, 64 bit
 * @COGL_PIXEL_FORMAT_ABGR_FP_16161616_PRE: Premultiplied ABGR half floating point, 64 bit
 *
 * Pixel formats used by Cogl. For the formats with a byte per
 * component, the order of the components specify the order in
 * increasing memory addresses. So for example
 * %COGL_PIXEL_FORMAT_RGB_888 would have the red component in the
 * lowest address, green in the next address and blue after that
 * regardless of the endianness of the system.
 *
 * For the formats with non byte aligned components the component
 * order specifies the order within a 16-bit or 32-bit number from
 * most significant bit to least significant. So for
 * %COGL_PIXEL_FORMAT_RGB_565, the red component would be in bits
 * 11-15, the green component would be in 6-11 and the blue component
 * would be in 1-5. Therefore the order in memory depends on the
 * endianness of the system.
 *
 * When uploading a texture %COGL_PIXEL_FORMAT_ANY can be used as the
 * internal format. Cogl will try to pick the best format to use
 * internally and convert the texture data if necessary.
 *
 * Since: 0.8
 */
typedef enum /*< prefix=COGL_PIXEL_FORMAT >*/
{
  COGL_PIXEL_FORMAT_ANY           = 0,
  COGL_PIXEL_FORMAT_A_8           = 1 | COGL_A_BIT,

  COGL_PIXEL_FORMAT_RGB_565       = 4,
  COGL_PIXEL_FORMAT_RGBA_4444     = 5 | COGL_A_BIT,
  COGL_PIXEL_FORMAT_RGBA_5551     = 6 | COGL_A_BIT,
  COGL_PIXEL_FORMAT_YUV           = 7,
  COGL_PIXEL_FORMAT_G_8           = 8,

  COGL_PIXEL_FORMAT_RG_88         = 9,

  COGL_PIXEL_FORMAT_RGB_888       = 2,
  COGL_PIXEL_FORMAT_BGR_888       = (2 | COGL_BGR_BIT),

  COGL_PIXEL_FORMAT_RGBA_8888     = (3 | COGL_A_BIT),
  COGL_PIXEL_FORMAT_BGRA_8888     = (3 | COGL_A_BIT | COGL_BGR_BIT),
  COGL_PIXEL_FORMAT_ARGB_8888     = (3 | COGL_A_BIT | COGL_AFIRST_BIT),
  COGL_PIXEL_FORMAT_ABGR_8888     = (3 | COGL_A_BIT | COGL_BGR_BIT | COGL_AFIRST_BIT),

  COGL_PIXEL_FORMAT_RGBA_1010102  = (13 | COGL_A_BIT),
  COGL_PIXEL_FORMAT_BGRA_1010102  = (13 | COGL_A_BIT | COGL_BGR_BIT),
  COGL_PIXEL_FORMAT_ARGB_2101010  = (13 | COGL_A_BIT | COGL_AFIRST_BIT),
  COGL_PIXEL_FORMAT_ABGR_2101010  = (13 | COGL_A_BIT | COGL_BGR_BIT | COGL_AFIRST_BIT),

  COGL_PIXEL_FORMAT_RGBA_FP_16161616 = (11 | COGL_A_BIT),
  COGL_PIXEL_FORMAT_BGRA_FP_16161616 = (11 | COGL_A_BIT | COGL_BGR_BIT),
  COGL_PIXEL_FORMAT_ARGB_FP_16161616 = (11 | COGL_A_BIT | COGL_AFIRST_BIT),
  COGL_PIXEL_FORMAT_ABGR_FP_16161616 = (11 | COGL_A_BIT | COGL_BGR_BIT | COGL_AFIRST_BIT),

  COGL_PIXEL_FORMAT_RGBA_8888_PRE = (3 | COGL_A_BIT | COGL_PREMULT_BIT),
  COGL_PIXEL_FORMAT_BGRA_8888_PRE = (3 | COGL_A_BIT | COGL_PREMULT_BIT | COGL_BGR_BIT),
  COGL_PIXEL_FORMAT_ARGB_8888_PRE = (3 | COGL_A_BIT | COGL_PREMULT_BIT | COGL_AFIRST_BIT),
  COGL_PIXEL_FORMAT_ABGR_8888_PRE = (3 | COGL_A_BIT | COGL_PREMULT_BIT | COGL_BGR_BIT | COGL_AFIRST_BIT),
  COGL_PIXEL_FORMAT_RGBA_4444_PRE = (COGL_PIXEL_FORMAT_RGBA_4444 | COGL_A_BIT | COGL_PREMULT_BIT),
  COGL_PIXEL_FORMAT_RGBA_5551_PRE = (COGL_PIXEL_FORMAT_RGBA_5551 | COGL_A_BIT | COGL_PREMULT_BIT),

  COGL_PIXEL_FORMAT_RGBA_1010102_PRE = (COGL_PIXEL_FORMAT_RGBA_1010102 | COGL_PREMULT_BIT),
  COGL_PIXEL_FORMAT_BGRA_1010102_PRE = (COGL_PIXEL_FORMAT_BGRA_1010102 | COGL_PREMULT_BIT),
  COGL_PIXEL_FORMAT_ARGB_2101010_PRE = (COGL_PIXEL_FORMAT_ARGB_2101010 | COGL_PREMULT_BIT),
  COGL_PIXEL_FORMAT_ABGR_2101010_PRE = (COGL_PIXEL_FORMAT_ABGR_2101010 | COGL_PREMULT_BIT),

  COGL_PIXEL_FORMAT_RGBA_FP_16161616_PRE = (11 | COGL_A_BIT | COGL_PREMULT_BIT),
  COGL_PIXEL_FORMAT_BGRA_FP_16161616_PRE = (11 | COGL_A_BIT | COGL_PREMULT_BIT | COGL_BGR_BIT),
  COGL_PIXEL_FORMAT_ARGB_FP_16161616_PRE = (11 | COGL_A_BIT | COGL_PREMULT_BIT | COGL_AFIRST_BIT),
  COGL_PIXEL_FORMAT_ABGR_FP_16161616_PRE = (11 | COGL_A_BIT | COGL_PREMULT_BIT | COGL_BGR_BIT | COGL_AFIRST_BIT),

  COGL_PIXEL_FORMAT_DEPTH_16  = (9 | COGL_DEPTH_BIT),
  COGL_PIXEL_FORMAT_DEPTH_32  = (3 | COGL_DEPTH_BIT),

  COGL_PIXEL_FORMAT_DEPTH_24_STENCIL_8 = (3 | COGL_DEPTH_BIT | COGL_STENCIL_BIT)
} CoglPixelFormat;

/**
 * COGL_PIXEL_FORMAT_MAX_PLANES:
 *
 * The maximum number of planes of a pixel format (see also
 * cogl_pixel_format_get_planes()).
 */
#define COGL_PIXEL_FORMAT_MAX_PLANES (4)

/**
 * cogl_pixel_format_get_bytes_per_pixel:
 * @format: The pixel format
 * @plane: The index of the plane (should not be more than the number of planes
 *         in the given format).
 *
 * Queries the number of bytes per pixel for a given format in the given plane.
 *
 * Returns: The number of bytes per pixel in the given format's given plane.
 */
COGL_EXPORT int
cogl_pixel_format_get_bytes_per_pixel (CoglPixelFormat format,
                                       int             plane);

/*
 * _cogl_pixel_format_has_aligned_components:
 * @format: a #CoglPixelFormat
 *
 * Queries whether the ordering of the components for the given
 * @format depend on the endianness of the host CPU or if the
 * components can be accessed using bit shifting and bitmasking by
 * loading a whole pixel into a word.
 *
 * XXX: If we ever consider making something like this public we
 * should really try to think of a better name and come up with
 * much clearer documentation since it really depends on what
 * point of view you consider this from whether a format like
 * COGL_PIXEL_FORMAT_RGBA_8888 is endian dependent. E.g. If you
 * read an RGBA_8888 pixel into a uint32
 * it's endian dependent how you mask out the different channels.
 * But If you already have separate color components and you want
 * to write them to an RGBA_8888 pixel then the bytes can be
 * written sequentially regardless of the endianness.
 *
 * Return value: %TRUE if you need to consider the host CPU
 *               endianness when dealing with the given @format
 *               else %FALSE.
 */
gboolean
_cogl_pixel_format_is_endian_dependant (CoglPixelFormat format);

/*
 * COGL_PIXEL_FORMAT_CAN_HAVE_PREMULT(format):
 * @format: a #CoglPixelFormat
 *
 * Returns TRUE if the pixel format can take a premult bit. This is
 * currently true for all formats that have an alpha channel except
 * COGL_PIXEL_FORMAT_A_8 (because that doesn't have any other
 * components to multiply by the alpha).
 */
#define COGL_PIXEL_FORMAT_CAN_HAVE_PREMULT(format) \
  (((format) & COGL_A_BIT) && (format) != COGL_PIXEL_FORMAT_A_8)

/**
 * cogl_pixel_format_get_n_planes:
 * @format: The format for which to get the number of planes
 *
 * Returns the number of planes the given CoglPixelFormat specifies.
 *
 * Returns: The no. of planes of @format (at most %COGL_PIXEL_FORMAT_MAX_PLANES)
 */
COGL_EXPORT int
cogl_pixel_format_get_n_planes (CoglPixelFormat format);

/**
 * cogl_pixel_format_to_string:
 * @format: a #CoglPixelFormat
 *
 * Returns a string representation of @format, useful for debugging purposes.
 *
 * Returns: (transfer none): A string representation of @format.
 */
COGL_EXPORT const char *
cogl_pixel_format_to_string (CoglPixelFormat format);

G_END_DECLS

#endif /* __COGL_PIXEL_FORMAT_H__ */
