
#include <clutter/clutter.h>
#include <cogl/cogl.h>

#include "test-conform-common.h"

#define RED 0
#define GREEN 1
#define BLUE 2

#define FRAMEBUFFER_WIDTH  640
#define FRAMEBUFFER_HEIGHT 480

static const ClutterColor stage_color = { 0x0, 0x0, 0x0, 0xff };


static void
on_paint (ClutterActor *actor, void *state)
{
  float saved_viewport[4];
  CoglMatrix saved_projection;
  CoglMatrix projection;
  CoglMatrix modelview;
  guchar *data;
  CoglHandle tex;
  CoglHandle offscreen;
  uint32_t *pixels;
  uint8_t *pixelsc;

  /* Save the Clutter viewport/matrices and load identity matrices */

  cogl_get_viewport (saved_viewport);
  cogl_get_projection_matrix (&saved_projection);
  cogl_push_matrix ();

  cogl_matrix_init_identity (&projection);
  cogl_matrix_init_identity (&modelview);

  cogl_set_projection_matrix (&projection);
  cogl_set_modelview_matrix (&modelview);

  /* All offscreen rendering is done upside down so the first thing we
   * verify is reading back grid of colors from a CoglOffscreen framebuffer
   */

  data = malloc (FRAMEBUFFER_WIDTH * 4 * FRAMEBUFFER_HEIGHT);
  tex = test_utils_texture_new_from_data (FRAMEBUFFER_WIDTH, FRAMEBUFFER_HEIGHT,
                                    TEST_UTILS_TEXTURE_NO_SLICING,
                                    COGL_PIXEL_FORMAT_RGBA_8888, /* data fmt */
                                    COGL_PIXEL_FORMAT_ANY, /* internal fmt */
                                    FRAMEBUFFER_WIDTH * 4, /* rowstride */
                                    data);
  free (data);
  offscreen = cogl_offscreen_new_with_texture (tex);

  cogl_push_framebuffer (offscreen);

  /* red, top left */
  cogl_set_source_color4ub (0xff, 0x00, 0x00, 0xff);
  cogl_rectangle (-1, 1, 0, 0);
  /* green, top right */
  cogl_set_source_color4ub (0x00, 0xff, 0x00, 0xff);
  cogl_rectangle (0, 1, 1, 0);
  /* blue, bottom left */
  cogl_set_source_color4ub (0x00, 0x00, 0xff, 0xff);
  cogl_rectangle (-1, 0, 0, -1);
  /* white, bottom right */
  cogl_set_source_color4ub (0xff, 0xff, 0xff, 0xff);
  cogl_rectangle (0, 0, 1, -1);

  pixels = calloc (1, FRAMEBUFFER_WIDTH * 4 * FRAMEBUFFER_HEIGHT);
  cogl_read_pixels (0, 0, FRAMEBUFFER_WIDTH, FRAMEBUFFER_HEIGHT,
                    COGL_READ_PIXELS_COLOR_BUFFER,
                    COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                    (guchar *)pixels);

  g_assert_cmpint (pixels[0], ==, 0xff0000ff);
  g_assert_cmpint (pixels[FRAMEBUFFER_WIDTH - 1], ==, 0xff00ff00);
  g_assert_cmpint (pixels[(FRAMEBUFFER_HEIGHT - 1) * FRAMEBUFFER_WIDTH], ==, 0xffff0000);
  g_assert_cmpint (pixels[(FRAMEBUFFER_HEIGHT - 1) * FRAMEBUFFER_WIDTH + FRAMEBUFFER_WIDTH - 1], ==, 0xffffffff);
  free (pixels);

  cogl_pop_framebuffer ();
  cogl_handle_unref (offscreen);

  /* Now verify reading back from an onscreen framebuffer...
   */

  cogl_set_source_texture (tex);
  cogl_rectangle (-1, 1, 1, -1);

  pixels = calloc (1, FRAMEBUFFER_WIDTH * 4 * FRAMEBUFFER_HEIGHT);
  cogl_read_pixels (0, 0, FRAMEBUFFER_WIDTH, FRAMEBUFFER_HEIGHT,
                    COGL_READ_PIXELS_COLOR_BUFFER,
                    COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                    (guchar *)pixels);

  g_assert_cmpint (pixels[0], ==, 0xff0000ff);
  g_assert_cmpint (pixels[FRAMEBUFFER_WIDTH - 1], ==, 0xff00ff00);
  g_assert_cmpint (pixels[(FRAMEBUFFER_HEIGHT - 1) * FRAMEBUFFER_WIDTH], ==, 0xffff0000);
  g_assert_cmpint (pixels[(FRAMEBUFFER_HEIGHT - 1) * FRAMEBUFFER_WIDTH + FRAMEBUFFER_WIDTH - 1], ==, 0xffffffff);
  free (pixels);

  /* Verify using BGR format */

  cogl_set_source_texture (tex);
  cogl_rectangle (-1, 1, 1, -1);

  pixelsc = calloc (1, FRAMEBUFFER_WIDTH * 3 * FRAMEBUFFER_HEIGHT);
  cogl_read_pixels (0, 0, FRAMEBUFFER_WIDTH, FRAMEBUFFER_HEIGHT,
                    COGL_READ_PIXELS_COLOR_BUFFER,
                    COGL_PIXEL_FORMAT_BGR_888,
                    (guchar *)pixelsc);

  g_assert_cmpint (pixelsc[0], ==, 0x00);
  g_assert_cmpint (pixelsc[1], ==, 0x00);
  g_assert_cmpint (pixelsc[2], ==, 0xff);

  g_assert_cmpint (pixelsc[(FRAMEBUFFER_WIDTH - 1) * 3 + 0], ==, 0x00);
  g_assert_cmpint (pixelsc[(FRAMEBUFFER_WIDTH - 1) * 3 + 1], ==, 0xff);
  g_assert_cmpint (pixelsc[(FRAMEBUFFER_WIDTH - 1) * 3 + 2], ==, 0x00);

  free (pixelsc);

  cogl_handle_unref (tex);

  /* Restore the viewport and matrices state */
  cogl_set_viewport (saved_viewport[0],
                     saved_viewport[1],
                     saved_viewport[2],
                     saved_viewport[3]);
  cogl_set_projection_matrix (&saved_projection);
  cogl_pop_matrix ();

  /* Comment this out if you want visual feedback of what this test
   * paints.
   */
  clutter_main_quit ();
}

static CoglBool
queue_redraw (void *stage)
{
  clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));

  return TRUE;
}

void
test_readpixels (TestUtilsGTestFixture *fixture,
                      void *data)
{
  unsigned int idle_source;
  ClutterActor *stage;

  stage = clutter_stage_get_default ();
  clutter_stage_set_color (CLUTTER_STAGE (stage), &stage_color);

  /* We force continuous redrawing of the stage, since we need to skip
   * the first few frames, and we wont be doing anything else that
   * will trigger redrawing. */
  idle_source = g_idle_add (queue_redraw, stage);
  g_signal_connect_after (stage, "paint", G_CALLBACK (on_paint), NULL);

  clutter_actor_show (stage);
  clutter_main ();

  g_source_remove (idle_source);

  /* Remove all of the actors from the stage */
  clutter_container_foreach (CLUTTER_CONTAINER (stage),
                             (ClutterCallback) clutter_actor_destroy,
                             NULL);

  if (cogl_test_verbose ())
    g_print ("OK\n");
}

