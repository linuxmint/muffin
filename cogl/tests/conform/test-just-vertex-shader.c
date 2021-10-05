#define COGL_VERSION_MIN_REQUIRED COGL_VERSION_1_0

#include <cogl/cogl.h>

#include <string.h>

#include "test-declarations.h"
#include "test-utils.h"

typedef struct _TestState
{
  int paddiing;
} TestState;

static CoglTexture *
create_dummy_texture (void)
{
  /* Create a dummy 1x1 green texture to replace the color from the
     vertex shader */
  static const uint8_t data[4] = { 0x00, 0xff, 0x00, 0xff };

  return test_utils_texture_new_from_data (test_ctx,
                                           1, 1, /* size */
                                           TEST_UTILS_TEXTURE_NONE,
                                           COGL_PIXEL_FORMAT_RGB_888,
                                           4, /* rowstride */
                                           data);
}

static void
paint (TestState *state)
{
  CoglPipeline *pipeline = cogl_pipeline_new (test_ctx);
  CoglTexture *tex;
  CoglColor color;
  GError *error = NULL;
  CoglHandle shader, program;

  cogl_color_init_from_4ub (&color, 0, 0, 0, 255);
  cogl_framebuffer_clear (test_fb, COGL_BUFFER_BIT_COLOR, &color);

  /* Set the primary vertex color as red */
  cogl_color_init_from_4ub (&color, 0xff, 0x00, 0x00, 0xff);
  cogl_pipeline_set_color (pipeline, &color);

  /* Override the vertex color in the texture environment with a
     constant green color provided by a texture */
  tex = create_dummy_texture ();
  cogl_pipeline_set_layer_texture (pipeline, 0, tex);
  cogl_object_unref (tex);
  if (!cogl_pipeline_set_layer_combine (pipeline, 0,
                                        "RGBA=REPLACE(TEXTURE)",
                                        &error))
    {
      g_warning ("Error setting layer combine: %s", error->message);
      g_assert_not_reached ();
    }

  /* Set up a dummy vertex shader that does nothing but the usual
     fixed function transform */
  shader = cogl_create_shader (COGL_SHADER_TYPE_VERTEX);
  cogl_shader_source (shader,
                      "void\n"
                      "main ()\n"
                      "{\n"
                      "  cogl_position_out = "
                      "cogl_modelview_projection_matrix * "
                      "cogl_position_in;\n"
                      "  cogl_color_out = cogl_color_in;\n"
                      "  cogl_tex_coord_out[0] = cogl_tex_coord_in;\n"
                      "}\n");

  program = cogl_create_program ();
  cogl_program_attach_shader (program, shader);
  cogl_program_link (program);

  cogl_object_unref (shader);

  /* Draw something without the program */
  cogl_framebuffer_draw_rectangle (test_fb, pipeline,
                                   0, 0, 50, 50);

  /* Draw it again using the program. It should look exactly the same */
  cogl_pipeline_set_user_program (pipeline, program);
  cogl_object_unref (program);

  cogl_framebuffer_draw_rectangle (test_fb, pipeline,
                                   50, 0, 100, 50);
  cogl_pipeline_set_user_program (pipeline, NULL);

  cogl_object_unref (pipeline);
}

static void
validate_result (CoglFramebuffer *framebuffer)
{
  /* Non-shader version */
  test_utils_check_pixel (framebuffer, 25, 25, 0x00ff0000);
  /* Shader version */
  test_utils_check_pixel (framebuffer, 75, 25, 0x00ff0000);
}

void
test_just_vertex_shader (void)
{
  TestState state;

  cogl_framebuffer_orthographic (test_fb,
                                 0, 0,
                                 cogl_framebuffer_get_width (test_fb),
                                 cogl_framebuffer_get_height (test_fb),
                                 -1,
                                 100);

  paint (&state);
  validate_result (test_fb);

  if (cogl_test_verbose ())
    g_print ("OK\n");
}
