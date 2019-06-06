#include <cogl/cogl.h>

#include <string.h>

#include "test-utils.h"

#define QUAD_WIDTH 20

#define RED 0
#define GREEN 1
#define BLUE 2
#define ALPHA 3

#define MASK_RED(COLOR)   ((COLOR & 0xff000000) >> 24)
#define MASK_GREEN(COLOR) ((COLOR & 0xff0000) >> 16)
#define MASK_BLUE(COLOR)  ((COLOR & 0xff00) >> 8)
#define MASK_ALPHA(COLOR) (COLOR & 0xff)

#define BLEND_CONSTANT_UNUSED 0xDEADBEEF
#define TEX_CONSTANT_UNUSED   0xDEADBEEF

typedef struct _TestState
{
  CoglContext *ctx;
} TestState;


static void
test_blend (TestState *state,
            int x,
            int y,
            uint32_t src_color,
            uint32_t dst_color,
            const char *blend_string,
            uint32_t blend_constant,
            uint32_t expected_result)
{
  /* src color */
  uint8_t Sr = MASK_RED (src_color);
  uint8_t Sg = MASK_GREEN (src_color);
  uint8_t Sb = MASK_BLUE (src_color);
  uint8_t Sa = MASK_ALPHA (src_color);
  /* dest color */
  uint8_t Dr = MASK_RED (dst_color);
  uint8_t Dg = MASK_GREEN (dst_color);
  uint8_t Db = MASK_BLUE (dst_color);
  uint8_t Da = MASK_ALPHA (dst_color);
  /* blend constant - when applicable */
  uint8_t Br = MASK_RED (blend_constant);
  uint8_t Bg = MASK_GREEN (blend_constant);
  uint8_t Bb = MASK_BLUE (blend_constant);
  uint8_t Ba = MASK_ALPHA (blend_constant);
  CoglColor blend_const_color;

  CoglHandle material;
  CoglPipeline *pipeline;
  CoglBool status;
  CoglError *error = NULL;
  int y_off;
  int x_off;

  /* First write out the destination color without any blending... */
  pipeline = cogl_pipeline_new (test_ctx);
  cogl_pipeline_set_color4ub (pipeline, Dr, Dg, Db, Da);
  cogl_pipeline_set_blend (pipeline, "RGBA = ADD (SRC_COLOR, 0)", NULL);
  cogl_set_source (pipeline);
  cogl_rectangle (x * QUAD_WIDTH,
                  y * QUAD_WIDTH,
                  x * QUAD_WIDTH + QUAD_WIDTH,
                  y * QUAD_WIDTH + QUAD_WIDTH);
  cogl_object_unref (pipeline);

  /*
   * Now blend a rectangle over our well defined destination:
   */

  pipeline = cogl_pipeline_new (test_ctx);
  cogl_pipeline_set_color4ub (pipeline, Sr, Sg, Sb, Sa);

  status = cogl_pipeline_set_blend (pipeline, blend_string, &error);
  if (!status)
    {
      /* It's not strictly a test failure; you need a more capable GPU or
       * driver to test this blend string. */
      if (cogl_test_verbose ())
	{
	  g_debug ("Failed to test blend string %s: %s",
		   blend_string, error->message);
	  g_print ("Skipping\n");
	}
      return;
    }

  cogl_color_init_from_4ub (&blend_const_color, Br, Bg, Bb, Ba);
  cogl_pipeline_set_blend_constant (pipeline, &blend_const_color);

  cogl_set_source (pipeline);
  cogl_rectangle (x * QUAD_WIDTH,
                  y * QUAD_WIDTH,
                  x * QUAD_WIDTH + QUAD_WIDTH,
                  y * QUAD_WIDTH + QUAD_WIDTH);
  cogl_object_unref (pipeline);

  /* See what we got... */

  y_off = y * QUAD_WIDTH + (QUAD_WIDTH / 2);
  x_off = x * QUAD_WIDTH + (QUAD_WIDTH / 2);

  if (cogl_test_verbose ())
    {
      g_print ("test_blend (%d, %d):\n%s\n", x, y, blend_string);
      g_print ("  src color = %02x, %02x, %02x, %02x\n", Sr, Sg, Sb, Sa);
      g_print ("  dst color = %02x, %02x, %02x, %02x\n", Dr, Dg, Db, Da);
      if (blend_constant != BLEND_CONSTANT_UNUSED)
        g_print ("  blend constant = %02x, %02x, %02x, %02x\n",
                 Br, Bg, Bb, Ba);
      else
        g_print ("  blend constant = UNUSED\n");
    }

  test_utils_check_pixel (test_fb, x_off, y_off, expected_result);


  /*
   * Test with legacy API
   */

  /* Clear previous work */
  cogl_set_source_color4ub (0, 0, 0, 0xff);
  cogl_rectangle (x * QUAD_WIDTH,
                  y * QUAD_WIDTH,
                  x * QUAD_WIDTH + QUAD_WIDTH,
                  y * QUAD_WIDTH + QUAD_WIDTH);

  /* First write out the destination color without any blending... */
  material = cogl_material_new ();
  cogl_material_set_color4ub (material, Dr, Dg, Db, Da);
  cogl_material_set_blend (material, "RGBA = ADD (SRC_COLOR, 0)", NULL);
  cogl_set_source (material);
  cogl_rectangle (x * QUAD_WIDTH,
                  y * QUAD_WIDTH,
                  x * QUAD_WIDTH + QUAD_WIDTH,
                  y * QUAD_WIDTH + QUAD_WIDTH);
  cogl_handle_unref (material);

  /*
   * Now blend a rectangle over our well defined destination:
   */

  material = cogl_material_new ();
  cogl_material_set_color4ub (material, Sr, Sg, Sb, Sa);

  status = cogl_material_set_blend (material, blend_string, &error);
  if (!status)
    {
      /* This is a failure as it must be equivalent to the new API */
      g_warning ("Error setting blend string %s: %s",
		 blend_string, error->message);
      g_assert_not_reached ();
    }

  cogl_color_init_from_4ub (&blend_const_color, Br, Bg, Bb, Ba);
  cogl_material_set_blend_constant (material, &blend_const_color);

  cogl_set_source (material);
  cogl_rectangle (x * QUAD_WIDTH,
                  y * QUAD_WIDTH,
                  x * QUAD_WIDTH + QUAD_WIDTH,
                  y * QUAD_WIDTH + QUAD_WIDTH);
  cogl_handle_unref (material);

  /* See what we got... */

  test_utils_check_pixel (test_fb, x_off, y_off, expected_result);
}

static CoglTexture *
make_texture (uint32_t color)
{
  guchar *tex_data, *p;
  uint8_t r = MASK_RED (color);
  uint8_t g = MASK_GREEN (color);
  uint8_t b = MASK_BLUE (color);
  uint8_t a = MASK_ALPHA (color);
  CoglTexture *tex;

  tex_data = malloc (QUAD_WIDTH * QUAD_WIDTH * 4);

  for (p = tex_data + QUAD_WIDTH * QUAD_WIDTH * 4; p > tex_data;)
    {
      *(--p) = a;
      *(--p) = b;
      *(--p) = g;
      *(--p) = r;
    }

  /* Note: we claim that the data is premultiplied so that Cogl won't
   * premultiply the data on upload */
  tex = test_utils_texture_new_from_data (test_ctx,
                                          QUAD_WIDTH,
                                          QUAD_WIDTH,
                                          TEST_UTILS_TEXTURE_NONE,
                                          COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                                          QUAD_WIDTH * 4,
                                          tex_data);

  free (tex_data);

  return tex;
}

static void
test_tex_combine (TestState *state,
                  int x,
                  int y,
                  uint32_t tex0_color,
                  uint32_t tex1_color,
                  uint32_t combine_constant,
                  const char *combine_string,
                  uint32_t expected_result)
{
  CoglTexture *tex0, *tex1;

  /* combine constant - when applicable */
  uint8_t Cr = MASK_RED (combine_constant);
  uint8_t Cg = MASK_GREEN (combine_constant);
  uint8_t Cb = MASK_BLUE (combine_constant);
  uint8_t Ca = MASK_ALPHA (combine_constant);
  CoglColor combine_const_color;

  CoglHandle material;
  CoglBool status;
  CoglError *error = NULL;
  int y_off;
  int x_off;


  tex0 = make_texture (tex0_color);
  tex1 = make_texture (tex1_color);

  material = cogl_material_new ();

  cogl_material_set_color4ub (material, 0x80, 0x80, 0x80, 0x80);
  cogl_material_set_blend (material, "RGBA = ADD (SRC_COLOR, 0)", NULL);

  cogl_material_set_layer (material, 0, tex0);
  cogl_material_set_layer_combine (material, 0,
                                   "RGBA = REPLACE (TEXTURE)", NULL);

  cogl_material_set_layer (material, 1, tex1);
  status = cogl_material_set_layer_combine (material, 1,
                                            combine_string, &error);
  if (!status)
    {
      /* It's not strictly a test failure; you need a more capable GPU or
       * driver to test this texture combine string. */
      g_debug ("Failed to test texture combine string %s: %s",
               combine_string, error->message);
    }

  cogl_color_init_from_4ub (&combine_const_color, Cr, Cg, Cb, Ca);
  cogl_material_set_layer_combine_constant (material, 1, &combine_const_color);

  cogl_set_source (material);
  cogl_rectangle (x * QUAD_WIDTH,
                  y * QUAD_WIDTH,
                  x * QUAD_WIDTH + QUAD_WIDTH,
                  y * QUAD_WIDTH + QUAD_WIDTH);

  cogl_handle_unref (material);
  cogl_object_unref (tex0);
  cogl_object_unref (tex1);

  /* See what we got... */

  y_off = y * QUAD_WIDTH + (QUAD_WIDTH / 2);
  x_off = x * QUAD_WIDTH + (QUAD_WIDTH / 2);

  if (cogl_test_verbose ())
    {
      g_print ("test_tex_combine (%d, %d):\n%s\n", x, y, combine_string);
      g_print ("  texture 0 color = 0x%08lX\n", (unsigned long)tex0_color);
      g_print ("  texture 1 color = 0x%08lX\n", (unsigned long)tex1_color);
      if (combine_constant != TEX_CONSTANT_UNUSED)
        g_print ("  combine constant = %02x, %02x, %02x, %02x\n",
                 Cr, Cg, Cb, Ca);
      else
        g_print ("  combine constant = UNUSED\n");
    }

  test_utils_check_pixel (test_fb, x_off, y_off, expected_result);
}

static void
paint (TestState *state)
{
  test_blend (state, 0, 0, /* position */
              0xff0000ff, /* src */
              0xffffffff, /* dst */
              "RGBA = ADD (SRC_COLOR, 0)",
              BLEND_CONSTANT_UNUSED,
              0xff0000ff); /* expected */

  test_blend (state, 1, 0, /* position */
              0x11223344, /* src */
              0x11223344, /* dst */
              "RGBA = ADD (SRC_COLOR, DST_COLOR)",
              BLEND_CONSTANT_UNUSED,
              0x22446688); /* expected */

  test_blend (state, 2, 0, /* position */
              0x80808080, /* src */
              0xffffffff, /* dst */
              "RGBA = ADD (SRC_COLOR * (CONSTANT), 0)",
              0x80808080, /* constant (RGBA all = 0.5 when normalized) */
              0x40404040); /* expected */

  test_blend (state, 3, 0, /* position */
              0x80000080, /* src (alpha = 0.5 when normalized) */
              0x40000000, /* dst */
              "RGBA = ADD (SRC_COLOR * (SRC_COLOR[A]),"
              "            DST_COLOR * (1-SRC_COLOR[A]))",
              BLEND_CONSTANT_UNUSED,
              0x60000040); /* expected */

  /* XXX:
   * For all texture combine tests tex0 will use a combine mode of
   * "RGBA = REPLACE (TEXTURE)"
   */

  test_tex_combine (state, 4, 0, /* position */
                    0x11111111, /* texture 0 color */
                    0x22222222, /* texture 1 color */
                    TEX_CONSTANT_UNUSED,
                    "RGBA = ADD (PREVIOUS, TEXTURE)", /* tex combine */
                    0x33333333); /* expected */

  test_tex_combine (state, 5, 0, /* position */
                    0x40404040, /* texture 0 color */
                    0x80808080, /* texture 1 color (RGBA all = 0.5) */
                    TEX_CONSTANT_UNUSED,
                    "RGBA = MODULATE (PREVIOUS, TEXTURE)", /* tex combine */
                    0x20202020); /* expected */

  test_tex_combine (state, 6, 0, /* position */
                    0xffffff80, /* texture 0 color (alpha = 0.5) */
                    0xDEADBE40, /* texture 1 color */
                    TEX_CONSTANT_UNUSED,
                    "RGB = REPLACE (PREVIOUS)"
                    "A = MODULATE (PREVIOUS, TEXTURE)", /* tex combine */
                    0xffffff20); /* expected */

  /* XXX: we are assuming test_tex_combine creates a material with
   * a color of 0x80808080 (i.e. the "PRIMARY" color) */
  test_tex_combine (state, 7, 0, /* position */
                    0xffffff80, /* texture 0 color (alpha = 0.5) */
                    0xDEADBE20, /* texture 1 color */
                    TEX_CONSTANT_UNUSED,
                    "RGB = REPLACE (PREVIOUS)"
                    "A = MODULATE (PRIMARY, TEXTURE)", /* tex combine */
                    0xffffff10); /* expected */

  test_tex_combine (state, 8, 0, /* position */
                    0x11111111, /* texture 0 color */
                    0x22222222, /* texture 1 color */
                    TEX_CONSTANT_UNUSED,
                    "RGBA = ADD (PREVIOUS, 1-TEXTURE)", /* tex combine */
                    0xeeeeeeee); /* expected */

  /* this is again assuming a primary color of 0x80808080 */
  test_tex_combine (state, 9, 0, /* position */
                    0x10101010, /* texture 0 color */
                    0x20202020, /* texture 1 color */
                    TEX_CONSTANT_UNUSED,
                    "RGBA = INTERPOLATE (PREVIOUS, TEXTURE, PRIMARY)",
                    0x18181818); /* expected */

#if 0 /* using TEXTURE_N appears to be broken in cogl-blend-string.c */
  test_tex_combine (state, 0, 1, /* position */
                    0xDEADBEEF, /* texture 0 color (not used) */
                    0x11223344, /* texture 1 color */
                    TEX_CONSTANT_UNUSED,
                    "RGBA = ADD (TEXTURE_1, TEXTURE)", /* tex combine */
                    0x22446688); /* expected */
#endif

  test_tex_combine (state, 1, 1, /* position */
                    0x21314151, /* texture 0 color */
                    0x99999999, /* texture 1 color */
                    TEX_CONSTANT_UNUSED,
                    "RGBA = ADD_SIGNED (PREVIOUS, TEXTURE)", /* tex combine */
                    0x3a4a5a6a); /* expected */

  test_tex_combine (state, 2, 1, /* position */
                    0xfedcba98, /* texture 0 color */
                    0x11111111, /* texture 1 color */
                    TEX_CONSTANT_UNUSED,
                    "RGBA = SUBTRACT (PREVIOUS, TEXTURE)", /* tex combine */
                    0xedcba987); /* expected */

  test_tex_combine (state, 3, 1, /* position */
                    0x8899aabb, /* texture 0 color */
                    0xbbaa9988, /* texture 1 color */
                    TEX_CONSTANT_UNUSED,
                    "RGB = DOT3_RGBA (PREVIOUS, TEXTURE)"
                    "A = REPLACE (PREVIOUS)",
                    0x2a2a2abb); /* expected */
}

void
test_blend_strings (void)
{
  TestState state;

  cogl_framebuffer_orthographic (test_fb, 0, 0,
                                 cogl_framebuffer_get_width (test_fb),
                                 cogl_framebuffer_get_height (test_fb),
                                 -1,
                                 100);

  /* XXX: we have to push/pop a framebuffer since this test currently
   * uses the legacy cogl_rectangle() api. */
  cogl_push_framebuffer (test_fb);
  paint (&state);
  cogl_pop_framebuffer ();

  if (cogl_test_verbose ())
    g_print ("OK\n");
}

