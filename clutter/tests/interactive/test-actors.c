#include <clutter/clutter.h>

#include <math.h>
#include <errno.h>
#include <stdlib.h>
#include <glib.h>
#include <gmodule.h>

#define NHANDS  6

typedef struct SuperOH
{
  ClutterActor **hand;
  ClutterActor  *bgtex;
  ClutterActor  *real_hand;
  ClutterActor  *group;
  ClutterActor  *stage;

  gint stage_width;
  gint stage_height;
  gfloat radius;

  ClutterBehaviour *scaler_1;
  ClutterBehaviour *scaler_2;
  ClutterTimeline *timeline;
} SuperOH;

static gint n_hands = NHANDS;

static GOptionEntry super_oh_entries[] = {
  {
    "num-hands", 'n',
    0,
    G_OPTION_ARG_INT, &n_hands,
    "Number of hands", "HANDS"
  },
  { NULL }
};

static void
on_group_destroy (ClutterActor *actor,
                  SuperOH      *oh)
{
  oh->group = NULL;
}

static void
on_hand_destroy (ClutterActor *actor,
                 SuperOH      *oh)
{
  int i;

  for (i = 0; i < n_hands; i++)
    {
      if (oh->hand[i] == actor)
        oh->hand[i] = NULL;
    }
}

static gboolean
on_button_press_event (ClutterActor *actor,
                       ClutterEvent *event,
                       SuperOH      *oh)
{
  gfloat x, y;

  clutter_event_get_coords (event, &x, &y);

  g_print ("*** button press event (button:%d) at %.2f, %.2f on %s ***\n",
           clutter_event_get_button (event),
           x, y,
           clutter_actor_get_name (actor));

  clutter_actor_hide (actor);

  return TRUE;
}

static gboolean
input_cb (ClutterActor *stage,
	  ClutterEvent *event,
	  gpointer      data)
{
  SuperOH *oh = data;

  if (event->type == CLUTTER_KEY_RELEASE)
    {
      g_print ("*** key press event (key:%c) ***\n",
	       clutter_event_get_key_symbol (event));

      if (clutter_event_get_key_symbol (event) == CLUTTER_KEY_q)
        {
	  clutter_main_quit ();

          return TRUE;
        }
      else if (clutter_event_get_key_symbol (event) == CLUTTER_KEY_r)
        {
          gint i;

          for (i = 0; i < n_hands; i++)
            {
              if (oh->hand[i] != NULL)
                clutter_actor_show (oh->hand[i]);
            }

          return TRUE;
        }
    }

  return FALSE;
}

/* Timeline handler */
static void
frame_cb (ClutterTimeline *timeline,
	  gint             msecs,
	  gpointer         data)
{
  SuperOH *oh = data;
  gint i;
  float rotation = clutter_timeline_get_progress (timeline) * 360.0f;

  /* Rotate everything clockwise about stage center*/
  if (oh->group != NULL)
    clutter_actor_set_rotation (oh->group,
                                CLUTTER_Z_AXIS,
                                rotation,
                                oh->stage_width / 2,
                                oh->stage_height / 2,
                                0);

  for (i = 0; i < n_hands; i++)
    {
      /* Rotate each hand around there centers - to get this we need
       * to take into account any scaling.
       */
      if (oh->hand[i] != NULL)
        clutter_actor_set_rotation (oh->hand[i],
                                    CLUTTER_Z_AXIS,
                                    -6.0 * rotation,
                                    0, 0, 0);
    }
}

static void
stop_and_quit (ClutterActor *stage,
               SuperOH      *data)
{
  clutter_timeline_stop (data->timeline);

  clutter_main_quit ();
}

static gdouble
my_sine_wave (ClutterAlpha *alpha,
              gpointer      dummy G_GNUC_UNUSED)
{
  ClutterTimeline *timeline = clutter_alpha_get_timeline (alpha);
  gdouble progress = clutter_timeline_get_progress (timeline);

  return sin (progress * G_PI);
}

G_MODULE_EXPORT int
test_actors_main (int argc, char *argv[])
{
  ClutterAlpha *alpha;
  SuperOH      *oh;
  gint          i;
  GError       *error;
  ClutterActor *real_hand;
  gchar        *file;

  error = NULL;

  if (clutter_init_with_args (&argc, &argv,
                              NULL,
                              super_oh_entries,
                              NULL,
                              &error) != CLUTTER_INIT_SUCCESS)
    {
      g_warning ("Unable to initialise Clutter:\n%s",
                 error->message);
      g_error_free (error);

      return EXIT_FAILURE;
    }

  oh = g_new (SuperOH, 1);

  oh->stage = clutter_stage_new ();
  clutter_actor_set_size (oh->stage, 800, 600);
  clutter_actor_set_name (oh->stage, "Default Stage");
  clutter_actor_set_background_color (oh->stage, CLUTTER_COLOR_LightSkyBlue);
  g_signal_connect (oh->stage, "destroy", G_CALLBACK (stop_and_quit), oh);

  clutter_stage_set_title (CLUTTER_STAGE (oh->stage), "Actors");
  clutter_stage_set_user_resizable (CLUTTER_STAGE (oh->stage), TRUE);

  /* Create a timeline to manage animation */
  oh->timeline = clutter_timeline_new (6000);
  clutter_timeline_set_repeat_count (oh->timeline, -1);

  /* fire a callback for frame change */
  g_signal_connect (oh->timeline, "new-frame", G_CALLBACK (frame_cb), oh);

  /* Set up some behaviours to handle scaling  */
  alpha = clutter_alpha_new_with_func (oh->timeline, my_sine_wave, NULL, NULL);

  oh->scaler_1 = clutter_behaviour_scale_new (alpha, 0.5, 0.5, 1.0, 1.0);
  oh->scaler_2 = clutter_behaviour_scale_new (alpha, 1.0, 1.0, 0.5, 0.5);

  file = g_build_filename (TESTS_DATADIR, "redhand.png", NULL);
  real_hand = clutter_texture_new_from_file (file, &error);
  if (real_hand == NULL)
    g_error ("image load failed: %s", error->message);

  free (file);

  /* create a new actor to hold other actors */
  oh->group = clutter_actor_new ();
  clutter_actor_set_layout_manager (oh->group, clutter_fixed_layout_new ());
  clutter_actor_set_name (oh->group, "Group");
  g_signal_connect (oh->group, "destroy", G_CALLBACK (on_group_destroy), oh);
  clutter_actor_add_constraint (oh->group, clutter_align_constraint_new (oh->stage, CLUTTER_ALIGN_BOTH, 0.5));
  clutter_actor_add_constraint (oh->group, clutter_bind_constraint_new (oh->stage, CLUTTER_BIND_SIZE, 0.0f));

  oh->hand = g_new (ClutterActor*, n_hands);

  oh->stage_width = clutter_actor_get_width (oh->stage);
  oh->stage_height = clutter_actor_get_height (oh->stage);
  oh->radius = (oh->stage_width + oh->stage_height)
             / n_hands;

  for (i = 0; i < n_hands; i++)
    {
      gint x, y, w, h;

      if (i == 0)
        {
          oh->hand[i] = real_hand;
          clutter_actor_set_name (oh->hand[i], "Real Hand");
        }
      else
        {
          oh->hand[i] = clutter_clone_new (real_hand);
          clutter_actor_set_name (oh->hand[i], "Clone Hand");
        }

      clutter_actor_set_reactive (oh->hand[i], TRUE);

      clutter_actor_set_size (oh->hand[i], 200, 213);

      /* Place around a circle */
      w = clutter_actor_get_width (oh->hand[i]);
      h = clutter_actor_get_height (oh->hand[i]);

      x = oh->stage_width / 2
	+ oh->radius
	* cos (i * G_PI / (n_hands / 2))
	- w / 2;

      y = oh->stage_height / 2
	+ oh->radius
	* sin (i * G_PI / (n_hands / 2))
	- h / 2;

      clutter_actor_set_position (oh->hand[i], x, y);

      clutter_actor_move_anchor_point_from_gravity (oh->hand[i],
						   CLUTTER_GRAVITY_CENTER);

      /* Add to our group group */
      clutter_container_add_actor (CLUTTER_CONTAINER (oh->group), oh->hand[i]);

      g_signal_connect (oh->hand[i], "button-press-event",
                        G_CALLBACK (on_button_press_event),
                        oh);

      g_signal_connect (oh->hand[i], "destroy",
                        G_CALLBACK (on_hand_destroy),
                        oh);

      if (i % 2)
	clutter_behaviour_apply (oh->scaler_1, oh->hand[i]);
      else
	clutter_behaviour_apply (oh->scaler_2, oh->hand[i]);
    }

  /* Add the group to the stage */
  clutter_container_add_actor (CLUTTER_CONTAINER (oh->stage), oh->group);

  /* Show everying */
  clutter_actor_show (oh->stage);

  g_signal_connect (oh->stage, "key-release-event",
		    G_CALLBACK (input_cb),
		    oh);

  /* and start it */
  clutter_timeline_start (oh->timeline);

  clutter_main ();

  clutter_timeline_stop (oh->timeline);

  /* clean up */
  g_object_unref (oh->scaler_1);
  g_object_unref (oh->scaler_2);
  g_object_unref (oh->timeline);
  free (oh->hand);
  free (oh);

  return EXIT_SUCCESS;
}
