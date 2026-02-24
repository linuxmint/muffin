#define CLUTTER_DISABLE_DEPRECATION_WARNINGS
#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

#define STAGE_WIDTH  640
#define STAGE_HEIGHT 480
#define ACTORS_X 12
#define ACTORS_Y 16

typedef struct _State State;

struct _State
{
  ClutterActor *stage;
  int y, x;
  ClutterActor *actors[ACTORS_X * ACTORS_Y];
  guint actor_width, actor_height;
  guint failed_pass;
  guint failed_idx;
  gboolean pass;
};

static const char *test_passes[] = {
  "No covering actor",
  "Invisible covering actor",
  "Clipped covering actor",
  "Blur effect",
};

static gboolean
on_timeout (gpointer data)
{
  State *state = data;
  int test_num = 0;
  int y, x;
  ClutterActor *over_actor = NULL;

  /* This will cause an unclipped pick redraw that will get buffered.
     We'll check below that this buffer is discarded because we also need
     to pick non-reactive actors */
  clutter_stage_get_actor_at_pos (CLUTTER_STAGE (state->stage),
                                  CLUTTER_PICK_REACTIVE, 10, 10);

  clutter_stage_get_actor_at_pos (CLUTTER_STAGE (state->stage),
                                  CLUTTER_PICK_REACTIVE, 10, 10);

  for (test_num = 0; test_num < G_N_ELEMENTS (test_passes); test_num++)
    {
      if (test_num == 0)
        {
          if (g_test_verbose ())
            g_print ("No covering actor:\n");
        }
      if (test_num == 1)
        {
          static const ClutterColor red = { 0xff, 0x00, 0x00, 0xff };
          /* Create an actor that covers the whole stage but that
             isn't visible so it shouldn't affect the picking */
          over_actor = clutter_rectangle_new_with_color (&red);
          clutter_actor_set_size (over_actor, STAGE_WIDTH, STAGE_HEIGHT);
          clutter_actor_add_child (state->stage, over_actor);
          clutter_actor_hide (over_actor);

          if (g_test_verbose ())
            g_print ("Invisible covering actor:\n");
        }
      else if (test_num == 2)
        {
          ClutterActorBox over_actor_box =
            CLUTTER_ACTOR_BOX_INIT (0, 0, STAGE_WIDTH, STAGE_HEIGHT);

          /* Make the actor visible but set a clip so that only some
             of the actors are accessible */
          clutter_actor_show (over_actor);
          clutter_actor_set_clip (over_actor,
                                  state->actor_width * 2,
                                  state->actor_height * 2,
                                  state->actor_width * (ACTORS_X - 4),
                                  state->actor_height * (ACTORS_Y - 4));

          /* Only allocated actors can be picked, so force an allocation
           * of the overlay actor here.
           */
          clutter_actor_allocate (over_actor, &over_actor_box);

          if (g_test_verbose ())
            g_print ("Clipped covering actor:\n");
        }
      else if (test_num == 3)
        {
          if (!clutter_feature_available (CLUTTER_FEATURE_SHADERS_GLSL))
            continue;

          clutter_actor_hide (over_actor);

          clutter_actor_add_effect_with_name (CLUTTER_ACTOR (state->stage),
                                              "blur",
                                              clutter_blur_effect_new ());

          if (g_test_verbose ())
            g_print ("With blur effect:\n");
        }

      for (y = 0; y < ACTORS_Y; y++)
        {
          x = 0;

          for (; x < ACTORS_X; x++)
            {
              gboolean pass = FALSE;
              gfloat pick_x;
              ClutterActor *actor;

              pick_x = x * state->actor_width + state->actor_width / 2;

              actor =
                clutter_stage_get_actor_at_pos (CLUTTER_STAGE (state->stage),
                                                CLUTTER_PICK_ALL,
                                                pick_x,
                                                y * state->actor_height
                                                + state->actor_height / 2);

              if (g_test_verbose ())
                g_print ("% 3i,% 3i / %p -> ",
                         x, y, state->actors[y * ACTORS_X + x]);

              if (actor == NULL)
                {
                  if (g_test_verbose ())
                    g_print ("NULL:       FAIL\n");
                }
              else if (actor == over_actor)
                {
                  if (test_num == 2
                      && x >= 2 && x < ACTORS_X - 2
                      && y >= 2 && y < ACTORS_Y - 2)
                    pass = TRUE;

                  if (g_test_verbose ())
                    g_print ("over_actor: %s\n", pass ? "pass" : "FAIL");
                }
              else
                {
                  if (actor == state->actors[y * ACTORS_X + x]
                      && (test_num != 2
                          || x < 2 || x >= ACTORS_X - 2
                          || y < 2 || y >= ACTORS_Y - 2))
                    pass = TRUE;

                  if (g_test_verbose ())
                    g_print ("%p: %s\n", actor, pass ? "pass" : "FAIL");
                }

              if (!pass)
                {
                  state->failed_pass = test_num;
                  state->failed_idx = y * ACTORS_X + x;
                  state->pass = FALSE;
                }
            }
        }
    }

  clutter_main_quit ();

  return G_SOURCE_REMOVE;
}

static void
actor_pick (void)
{
  int y, x;
  State state;
  
  state.pass = TRUE;

  state.stage = clutter_test_get_stage ();

  state.actor_width = STAGE_WIDTH / ACTORS_X;
  state.actor_height = STAGE_HEIGHT / ACTORS_Y;

  for (y = 0; y < ACTORS_Y; y++)
    for (x = 0; x < ACTORS_X; x++)
      {
        ClutterColor color = { x * 255 / (ACTORS_X - 1),
                               y * 255 / (ACTORS_Y - 1),
                               128, 255 };
        ClutterActor *rect = clutter_rectangle_new_with_color (&color);

        clutter_actor_set_position (rect,
                                    x * state.actor_width,
                                    y * state.actor_height);
        clutter_actor_set_size (rect,
                                state.actor_width,
                                state.actor_height);

        clutter_actor_add_child (state.stage, rect);

        state.actors[y * ACTORS_X + x] = rect;
      }

  clutter_actor_show (state.stage);

  clutter_threads_add_idle (on_timeout, &state);

  clutter_main ();

  if (g_test_verbose ())
    {
      if (!state.pass)
        g_test_message ("Failed pass: %s[%d], actor index: %d [%p]\n",
                        test_passes[state.failed_pass],
                        state.failed_pass,
                        state.failed_idx,
                        state.actors[state.failed_idx]);
    }

  g_assert (state.pass);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/actor/pick", actor_pick)
)
