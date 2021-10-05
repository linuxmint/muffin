#define COGL_VERSION_MIN_REQUIRED COGL_VERSION_1_0

#include <cogl/cogl.h>
#include <string.h>

#include "test-declarations.h"
#include "test-utils.h"

#define TEX_SIZE 4

typedef struct _TestState
{
  int width;
  int height;
  CoglTexture *texture;
} TestState;

static CoglTexture *
create_texture (TestUtilsTextureFlags flags)
{
  uint8_t *data = g_malloc (TEX_SIZE * TEX_SIZE * 4), *p = data;
  CoglTexture *tex;
  int x, y;

  for (y = 0; y < TEX_SIZE; y++)
    for (x = 0; x < TEX_SIZE; x++)
      {
        *(p++) = 0;
        *(p++) = (x & 1) * 255;
        *(p++) = (y & 1) * 255;
        *(p++) = 255;
      }

  tex = test_utils_texture_new_from_data (test_ctx,
                                          TEX_SIZE, TEX_SIZE, flags,
                                          COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                                          TEX_SIZE * 4,
                                          data);
  g_free (data);

  return tex;
}

static CoglPipeline *
create_pipeline (TestState *state,
                 CoglPipelineWrapMode wrap_mode_s,
                 CoglPipelineWrapMode wrap_mode_t)
{
  CoglPipeline *pipeline;

  pipeline = cogl_pipeline_new (test_ctx);
  cogl_pipeline_set_layer_texture (pipeline, 0, state->texture);
  cogl_pipeline_set_layer_filters (pipeline, 0,
                                   COGL_PIPELINE_FILTER_NEAREST,
                                   COGL_PIPELINE_FILTER_NEAREST);
  cogl_pipeline_set_layer_wrap_mode_s (pipeline, 0, wrap_mode_s);
  cogl_pipeline_set_layer_wrap_mode_t (pipeline, 0, wrap_mode_t);

  return pipeline;
}

static CoglPipelineWrapMode
wrap_modes[] =
  {
    COGL_PIPELINE_WRAP_MODE_REPEAT,
    COGL_PIPELINE_WRAP_MODE_REPEAT,

    COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE,
    COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE,

    COGL_PIPELINE_WRAP_MODE_REPEAT,
    COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE,

    COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE,
    COGL_PIPELINE_WRAP_MODE_REPEAT,
  };

static void
draw_tests (TestState *state)
{
  int i;

  for (i = 0; i < G_N_ELEMENTS (wrap_modes); i += 2)
    {
      CoglPipelineWrapMode wrap_mode_s, wrap_mode_t;
      CoglPipeline *pipeline;

      /* Create a separate pipeline for each pair of wrap modes so
         that we can verify whether the batch splitting works */
      wrap_mode_s = wrap_modes[i];
      wrap_mode_t = wrap_modes[i + 1];
      pipeline = create_pipeline (state, wrap_mode_s, wrap_mode_t);
      /* Render the pipeline at four times the size of the texture */
      cogl_framebuffer_draw_textured_rectangle (test_fb,
                                                pipeline,
                                                i * TEX_SIZE,
                                                0,
                                                (i + 2) * TEX_SIZE,
                                                TEX_SIZE * 2,
                                                0, 0, 2, 2);
      cogl_object_unref (pipeline);
    }
}

static const CoglVertexP3T2 vertices[4] =
  {
    { .x = 0.0f, .y = 0.0f, .z = 0.0f, .s = 0.0f, .t = 0.0f },
    { .x = 0.0f, .y = TEX_SIZE * 2, .z = 0.0f, .s = 0.0f, .t = 2.0f },
    { .x = TEX_SIZE * 2, .y = TEX_SIZE * 2, .z = 0.0f, .s = 2.0f, .t = 2.0f },
    { .x = TEX_SIZE * 2, .y = 0.0f, .z = 0.0f, .s = 2.0f, .t = 0.0f }
  };

static void
draw_tests_polygon (TestState *state)
{
  int i;

  for (i = 0; i < G_N_ELEMENTS (wrap_modes); i += 2)
    {
      CoglPipelineWrapMode wrap_mode_s, wrap_mode_t;
      CoglPrimitive *primitive;
      CoglPipeline *pipeline;

      wrap_mode_s = wrap_modes[i];
      wrap_mode_t = wrap_modes[i + 1];
      pipeline = create_pipeline (state, wrap_mode_s, wrap_mode_t);
      cogl_framebuffer_push_matrix (test_fb);
      cogl_framebuffer_translate (test_fb, TEX_SIZE * i, 0.0f, 0.0f);
      /* Render the pipeline at four times the size of the texture */
      primitive = cogl_primitive_new_p3t2 (test_ctx,
                                           COGL_VERTICES_MODE_TRIANGLE_FAN,
                                           G_N_ELEMENTS (vertices),
                                           vertices);
      cogl_primitive_draw (primitive, test_fb, pipeline);
      cogl_object_unref (primitive);
      cogl_object_unref (pipeline);
      cogl_framebuffer_pop_matrix (test_fb);
    }
}

static void
validate_set (TestState *state, int offset)
{
  uint8_t data[TEX_SIZE * 2 * TEX_SIZE * 2 * 4], *p;
  int x, y, i;

  for (i = 0; i < G_N_ELEMENTS (wrap_modes); i += 2)
    {
      CoglPipelineWrapMode wrap_mode_s, wrap_mode_t;

      wrap_mode_s = wrap_modes[i];
      wrap_mode_t = wrap_modes[i + 1];

      cogl_framebuffer_read_pixels (test_fb, i * TEX_SIZE, offset * TEX_SIZE * 2,
                                    TEX_SIZE * 2, TEX_SIZE * 2,
                                    COGL_PIXEL_FORMAT_RGBA_8888,
                                    data);

      p = data;

      for (y = 0; y < TEX_SIZE * 2; y++)
        for (x = 0; x < TEX_SIZE * 2; x++)
          {
            uint8_t green, blue;

            if (x < TEX_SIZE ||
                wrap_mode_s == COGL_PIPELINE_WRAP_MODE_REPEAT ||
                wrap_mode_s == COGL_PIPELINE_WRAP_MODE_AUTOMATIC)
              green = (x & 1) * 255;
            else
              green = ((TEX_SIZE - 1) & 1) * 255;

            if (y < TEX_SIZE ||
                wrap_mode_t == COGL_PIPELINE_WRAP_MODE_REPEAT ||
                wrap_mode_t == COGL_PIPELINE_WRAP_MODE_AUTOMATIC)
              blue = (y & 1) * 255;
            else
              blue = ((TEX_SIZE - 1) & 1) * 255;

            g_assert_cmpint (p[0], ==, 0);
            g_assert_cmpint (p[1], ==, green);
            g_assert_cmpint (p[2], ==, blue);

            p += 4;
          }
    }
}

static void
validate_result (TestState *state)
{
  validate_set (state, 0); /* non-atlased rectangle */
#if 0 /* this doesn't currently work */
  validate_set (state, 1); /* atlased rectangle */
#endif
  validate_set (state, 2); /* CoglPrimitive */
}

static void
paint (TestState *state)
{
  /* Draw the tests first with a non atlased texture */
  state->texture = create_texture (TEST_UTILS_TEXTURE_NO_ATLAS);
  draw_tests (state);
  cogl_object_unref (state->texture);

  /* Draw the tests again with a possible atlased texture. This should
     end up testing software repeats */
  state->texture = create_texture (TEST_UTILS_TEXTURE_NONE);
  cogl_framebuffer_push_matrix (test_fb);
  cogl_framebuffer_translate (test_fb, 0.0f, TEX_SIZE * 2.0f, 0.0f);
  draw_tests (state);
  cogl_framebuffer_pop_matrix (test_fb);
  cogl_object_unref (state->texture);

  /* Draw the tests using CoglPrimitive */
  state->texture = create_texture (TEST_UTILS_TEXTURE_NO_ATLAS);
  cogl_framebuffer_push_matrix (test_fb);
  cogl_framebuffer_translate (test_fb,
                              0.0f, TEX_SIZE * 4.0f, 0.0f);
  draw_tests_polygon (state);
  cogl_framebuffer_pop_matrix (test_fb);
  cogl_object_unref (state->texture);

  validate_result (state);
}

void
test_wrap_modes (void)
{
  TestState state;

  state.width = cogl_framebuffer_get_width (test_fb);
  state.height = cogl_framebuffer_get_height (test_fb);

  cogl_framebuffer_orthographic (test_fb,
                                 0, 0,
                                 state.width,
                                 state.height,
                                 -1,
                                 100);

  paint (&state);

  if (cogl_test_verbose ())
    g_print ("OK\n");
}
