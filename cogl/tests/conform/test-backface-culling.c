#define COGL_VERSION_MIN_REQUIRED COGL_VERSION_1_0

#include <cogl/cogl.h>

#include <string.h>

#include "test-declarations.h"
#include "test-utils.h"

/* Size the texture so that it is just off a power of two to encourage
   it so use software tiling when NPOTs aren't available */
#define TEXTURE_SIZE        257

/* Amount of pixels to skip off the top, bottom, left and right of the
   texture when reading back the stage */
#define TEST_INSET          2

/* Size to actually render the texture at */
#define TEXTURE_RENDER_SIZE 8

typedef struct _TestState
{
  CoglTexture *texture;
  CoglFramebuffer *offscreen;
  CoglTexture *offscreen_tex;
  int width, height;
} TestState;

static void
validate_part (CoglFramebuffer *framebuffer,
               int xnum, int ynum, gboolean shown)
{
  test_utils_check_region (framebuffer,
                           xnum * TEXTURE_RENDER_SIZE + TEST_INSET,
                           ynum * TEXTURE_RENDER_SIZE + TEST_INSET,
                           TEXTURE_RENDER_SIZE - TEST_INSET * 2,
                           TEXTURE_RENDER_SIZE - TEST_INSET * 2,
                           shown ? 0xff0000ff : 0x000000ff);
}

/* We draw everything 8 times. The draw number is used as a bitmask
   to test all of the combinations of enabling both winding orders and all four
   culling modes */

#define FRONT_WINDING(draw_num)    (((draw_num) & 0x01) >> 1)
#define CULL_FACE_MODE(draw_num)   (((draw_num) & 0x06) >> 2)

static void
paint_test_backface_culling (TestState *state,
                             CoglFramebuffer *framebuffer)
{
  int draw_num;
  CoglPipeline *base_pipeline = cogl_pipeline_new (test_ctx);

  cogl_framebuffer_orthographic (framebuffer,
                                 0, 0,
                                 state->width,
                                 state->height,
                                 -1,
                                 100);

  cogl_framebuffer_clear4f (framebuffer,
                            COGL_BUFFER_BIT_COLOR | COGL_BUFFER_BIT_STENCIL,
                            0, 0, 0, 1);

  cogl_pipeline_set_layer_texture (base_pipeline, 0, state->texture);

  cogl_pipeline_set_layer_filters (base_pipeline, 0,
                                   COGL_PIPELINE_FILTER_NEAREST,
                                   COGL_PIPELINE_FILTER_NEAREST);

  /* Render the scene sixteen times to test all of the combinations of
     cull face mode, legacy state and winding orders */
  for (draw_num = 0; draw_num < 8; draw_num++)
    {
      float x1 = 0, x2, y1 = 0, y2 = (float)(TEXTURE_RENDER_SIZE);
      CoglVertexP3T2 verts[4];
      CoglPrimitive *primitive;
      CoglPipeline *pipeline;

      cogl_framebuffer_push_matrix (framebuffer);
      cogl_framebuffer_translate (framebuffer,
                                  0,
                                  TEXTURE_RENDER_SIZE * draw_num,
                                  0);

      pipeline = cogl_pipeline_copy (base_pipeline);

      cogl_pipeline_set_front_face_winding (pipeline, FRONT_WINDING (draw_num));
      cogl_pipeline_set_cull_face_mode (pipeline, CULL_FACE_MODE (draw_num));

      memset (verts, 0, sizeof (verts));

      x2 = x1 + (float)(TEXTURE_RENDER_SIZE);

      /* Draw a front-facing texture */
      cogl_framebuffer_draw_rectangle (framebuffer, pipeline, x1, y1, x2, y2);

      x1 = x2;
      x2 = x1 + (float)(TEXTURE_RENDER_SIZE);

      /* Draw a front-facing texture with flipped texcoords */
      cogl_framebuffer_draw_textured_rectangle (framebuffer, pipeline,
                                                x1, y1, x2, y2,
                                                1.0, 0.0, 0.0, 1.0);

      x1 = x2;
      x2 = x1 + (float)(TEXTURE_RENDER_SIZE);

      /* Draw a back-facing texture */
      cogl_framebuffer_draw_rectangle (framebuffer, pipeline,
                                       x2, y1, x1, y2);

      x1 = x2;
      x2 = x1 + (float)(TEXTURE_RENDER_SIZE);

      /* If the texture is sliced then cogl_polygon doesn't work so
         we'll just use a solid color instead */
      if (cogl_texture_is_sliced (state->texture))
        cogl_pipeline_set_color4ub (pipeline, 255, 0, 0, 255);

      /* Draw a front-facing polygon */
      verts[0].x = x1;    verts[0].y = y2;
      verts[1].x = x2;    verts[1].y = y2;
      verts[2].x = x2;    verts[2].y = y1;
      verts[3].x = x1;    verts[3].y = y1;
      verts[0].s = 0;     verts[0].t = 0;
      verts[1].s = 1.0;   verts[1].t = 0;
      verts[2].s = 1.0;   verts[2].t = 1.0;
      verts[3].s = 0;     verts[3].t = 1.0;

      primitive = cogl_primitive_new_p3t2 (test_ctx,
                                           COGL_VERTICES_MODE_TRIANGLE_FAN,
                                           4,
                                           verts);
      cogl_primitive_draw (primitive, framebuffer, pipeline);
      cogl_object_unref (primitive);

      x1 = x2;
      x2 = x1 + (float)(TEXTURE_RENDER_SIZE);

      /* Draw a back-facing polygon */
      verts[0].x = x1;    verts[0].y = y1;
      verts[1].x = x2;    verts[1].y = y1;
      verts[2].x = x2;    verts[2].y = y2;
      verts[3].x = x1;    verts[3].y = y2;
      verts[0].s = 0;     verts[0].t = 0;
      verts[1].s = 1.0;   verts[1].t = 0;
      verts[2].s = 1.0;   verts[2].t = 1.0;
      verts[3].s = 0;     verts[3].t = 1.0;

      primitive = cogl_primitive_new_p3t2 (test_ctx,
                                           COGL_VERTICES_MODE_TRIANGLE_FAN,
                                           4,
                                           verts);
      cogl_primitive_draw (primitive, framebuffer, pipeline);
      cogl_object_unref (primitive);

      x1 = x2;
      x2 = x1 + (float)(TEXTURE_RENDER_SIZE);

      cogl_framebuffer_pop_matrix (framebuffer);

      cogl_object_unref (pipeline);
    }

  cogl_object_unref (base_pipeline);
}

static void
validate_result (CoglFramebuffer *framebuffer, int y_offset)
{
  int draw_num;

  for (draw_num = 0; draw_num < 8; draw_num++)
    {
      gboolean cull_front, cull_back;
      CoglPipelineCullFaceMode cull_mode;

      cull_mode = CULL_FACE_MODE (draw_num);

      switch (cull_mode)
        {
        case COGL_PIPELINE_CULL_FACE_MODE_NONE:
          cull_front = FALSE;
          cull_back = FALSE;
          break;

        case COGL_PIPELINE_CULL_FACE_MODE_FRONT:
          cull_front = TRUE;
          cull_back = FALSE;
          break;

        case COGL_PIPELINE_CULL_FACE_MODE_BACK:
          cull_front = FALSE;
          cull_back = TRUE;
          break;

        case COGL_PIPELINE_CULL_FACE_MODE_BOTH:
          cull_front = TRUE;
          cull_back = TRUE;
          break;
        }

      if (FRONT_WINDING (draw_num) == COGL_WINDING_CLOCKWISE)
        {
          gboolean tmp = cull_front;
          cull_front = cull_back;
          cull_back = tmp;
        }

      /* Front-facing texture */
      validate_part (framebuffer,
                     0, y_offset + draw_num, !cull_front);
      /* Front-facing texture with flipped tex coords */
      validate_part (framebuffer,
                     1, y_offset + draw_num, !cull_front);
      /* Back-facing texture */
      validate_part (framebuffer,
                     2, y_offset + draw_num, !cull_back);
      /* Front-facing texture polygon */
      validate_part (framebuffer,
                     3, y_offset + draw_num, !cull_front);
      /* Back-facing texture polygon */
      validate_part (framebuffer,
                     4, y_offset + draw_num, !cull_back);
    }
}

static void
paint (TestState *state)
{
  CoglPipeline *pipeline;

  paint_test_backface_culling (state, test_fb);

  /*
   * Now repeat the test but rendered to an offscreen
   * framebuffer. Note that by default the conformance tests are
   * always run to an offscreen buffer but we might as well have this
   * check anyway in case it is being run with COGL_TEST_ONSCREEN=1
   */
  paint_test_backface_culling (state, state->offscreen);

  /* Copy the result of the offscreen rendering for validation and
   * also so we can have visual feedback. */
  pipeline = cogl_pipeline_new (test_ctx);
  cogl_pipeline_set_layer_texture (pipeline, 0, state->offscreen_tex);
  cogl_framebuffer_draw_rectangle (test_fb,
                                   pipeline,
                                   0, TEXTURE_RENDER_SIZE * 8,
                                   state->width,
                                   state->height + TEXTURE_RENDER_SIZE * 8);
  cogl_object_unref (pipeline);

  validate_result (test_fb, 0);
  validate_result (test_fb, 8);
}

static CoglTexture *
make_texture (void)
{
  guchar *tex_data, *p;
  CoglTexture *tex;

  tex_data = g_malloc (TEXTURE_SIZE * TEXTURE_SIZE * 4);

  for (p = tex_data + TEXTURE_SIZE * TEXTURE_SIZE * 4; p > tex_data;)
    {
      *(--p) = 255;
      *(--p) = 0;
      *(--p) = 0;
      *(--p) = 255;
    }

  tex = test_utils_texture_new_from_data (test_ctx,
                                          TEXTURE_SIZE,
                                          TEXTURE_SIZE,
                                          TEST_UTILS_TEXTURE_NO_ATLAS,
                                          COGL_PIXEL_FORMAT_RGBA_8888,
                                          TEXTURE_SIZE * 4,
                                          tex_data);

  g_free (tex_data);

  return tex;
}

void
test_backface_culling (void)
{
  TestState state;
  CoglTexture *tex;

  state.width = cogl_framebuffer_get_width (test_fb);
  state.height = cogl_framebuffer_get_height (test_fb);

  state.offscreen = NULL;

  state.texture = make_texture ();

  tex = test_utils_texture_new_with_size (test_ctx,
                                          state.width, state.height,
                                          TEST_UTILS_TEXTURE_NO_SLICING,
                                          COGL_TEXTURE_COMPONENTS_RGBA);
  state.offscreen = cogl_offscreen_new_with_texture (tex);
  state.offscreen_tex = tex;

  paint (&state);

  cogl_object_unref (state.offscreen);
  cogl_object_unref (state.offscreen_tex);
  cogl_object_unref (state.texture);

  if (cogl_test_verbose ())
    g_print ("OK\n");
}

