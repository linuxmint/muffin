#include <clutter-build-config.h>
#include <glib.h>
#include <gmodule.h>
#include <stdlib.h>
#include <clutter/clutter.h>
#include <cogl/cogl.h>
#include <math.h>

#define STAGE_WIDTH 800
#define STAGE_HEIGHT 600

gboolean run_all = FALSE;

static GOptionEntry entries[] = {
  {
    "run-all", 'a',
    0,
    G_OPTION_ARG_NONE, &run_all,
    "Run all tests", ""
  },
  { NULL }
};

typedef struct _TestState
{
  ClutterActor *stage;
  int current_test;
} TestState;

typedef void (*TestCallback) (TestState           *state,
                              ClutterPaintContext *paint_context);

static void
test_rectangles (TestState           *state,
                 ClutterPaintContext *paint_context)
{
#define RECT_WIDTH 5
#define RECT_HEIGHT 5
  CoglFramebuffer *framebuffer =
    clutter_paint_context_get_framebuffer (paint_context);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  int x;
  int y;
  CoglPipeline *pipeline;

  /* Should the rectangles be randomly positioned/colored/rotated?
   *
   * It could be good to develop equivalent GL and Cairo tests so we can
   * have a sanity check for our Cogl performance.
   *
   * The color should vary to check that we correctly batch color changes
   * The use of alpha should vary so we have a variation of which rectangles
   * require blending.
   *  Should this be a random variation?
   *  It could be good to experiment with focibly enabling blending for
   *  rectangles that don't technically need it for the sake of extending
   *  batching. E.g. if you a long run of interleved rectangles with every
   *  other rectangle needing blending then it may be worth enabling blending
   *  for all the rectangles to avoid the state changes.
   * The modelview should change between rectangles to check the software
   * transform codepath.
   *  Should we group some rectangles under the same modelview? Potentially
   *  we could avoid software transform for long runs of rectangles with the
   *  same modelview.
   *
   */

  pipeline = cogl_pipeline_new (ctx);

  for (y = 0; y < STAGE_HEIGHT; y += RECT_HEIGHT)
    {
      for (x = 0; x < STAGE_WIDTH; x += RECT_WIDTH)
        {
          cogl_framebuffer_push_matrix (framebuffer);
          cogl_framebuffer_translate (framebuffer, x, y, 0);
          cogl_framebuffer_rotate (framebuffer, 45, 0, 0, 1);
          cogl_pipeline_set_color4f (pipeline,
                                     1,
                                     (1.0f / STAGE_WIDTH) * y,
                                     (1.0f / STAGE_HEIGHT) * x,
                                     1);
          cogl_framebuffer_draw_rectangle (framebuffer, pipeline,
                                           0, 0, RECT_WIDTH, RECT_HEIGHT);
          cogl_framebuffer_pop_matrix (framebuffer);
        }
    }

  for (y = 0; y < STAGE_HEIGHT; y += RECT_HEIGHT)
    {
      for (x = 0; x < STAGE_WIDTH; x += RECT_WIDTH)
        {
          cogl_framebuffer_push_matrix (framebuffer);
          cogl_framebuffer_translate (framebuffer, x, y, 0);
          cogl_framebuffer_rotate (framebuffer, 0, 0, 0, 1);
          cogl_pipeline_set_color4f (pipeline,
                                     1,
                                     (1.0f / STAGE_WIDTH) * x,
                                     (1.0f / STAGE_HEIGHT) * y,
                                     (1.0f / STAGE_WIDTH) * x);
          cogl_framebuffer_draw_rectangle (framebuffer, pipeline,
                                           0, 0, RECT_WIDTH, RECT_HEIGHT);
          cogl_framebuffer_pop_matrix (framebuffer);
        }
    }
}

TestCallback tests[] =
{
  test_rectangles
};

static void
on_paint (ClutterActor        *actor,
          ClutterPaintContext *paint_context,
          TestState           *state)
{
  tests[state->current_test] (state, paint_context);
}

static gboolean
queue_redraw (gpointer stage)
{
  clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));

  return TRUE;
}

int
main (int argc, char *argv[])
{
  TestState state;
  ClutterActor *stage;
  GError *error = NULL;

  g_setenv ("CLUTTER_VBLANK", "none", FALSE);
  g_setenv ("CLUTTER_SHOW_FPS", "1", FALSE);

  if (clutter_init_with_args (&argc, &argv,
                              NULL,
                              entries,
                              NULL,
                              &error) != CLUTTER_INIT_SUCCESS)
    {
      g_warning ("Unable to initialise Clutter:\n%s",
                 error->message);
      g_error_free (error);

      return EXIT_FAILURE;
    }

  state.current_test = 0;

  state.stage = stage = clutter_stage_new ();

  clutter_actor_set_size (stage, STAGE_WIDTH, STAGE_HEIGHT);
  clutter_stage_set_color (CLUTTER_STAGE (stage), CLUTTER_COLOR_White);
  clutter_stage_set_title (CLUTTER_STAGE (stage), "Cogl Performance Test");

  /* We want continuous redrawing of the stage... */
  clutter_threads_add_idle (queue_redraw, stage);

  g_signal_connect_after (stage, "paint", G_CALLBACK (on_paint), &state);

  clutter_actor_show (stage);

  clutter_main ();

  clutter_actor_destroy (stage);

  return 0;
}

