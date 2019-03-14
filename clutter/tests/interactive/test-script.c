#include <stdlib.h>
#include <stdio.h>

#include <math.h>

#include <glib.h>
#include <gmodule.h>
#include <clutter/clutter.h>

static ClutterScript *script = NULL;
static guint merge_id = 0;

static const gchar *test_unmerge =
"["
"  {"
"    \"id\" : \"main-stage\","
"    \"type\" : \"ClutterStage\","
"    \"children\" : [ \"blue-button\" ]"
"  },"
"  {"
"    \"id\" : \"blue-button\","
"    \"type\" : \"ClutterRectangle\","
"    \"color\" : \"#0000ffff\","
"    \"x\" : 350,"
"    \"y\" : 50,"
"    \"width\" : 100,"
"    \"height\" : 100,"
"    \"visible\" : true,"
"    \"reactive\" : true"
"  }"
"]";

static const gchar *test_behaviour =
"["
"  {"
"    \"id\" : \"main-timeline\","
"    \"type\" : \"ClutterTimeline\","
"    \"duration\" : 5000,"
"    \"loop\" : true"
"  },"
"  {"
"    \"id\" : \"sine-alpha\","
"    \"type\" : \"ClutterAlpha\","
"    \"function\" : \"sine_alpha\","
"    \"timeline\" : \"main-timeline\""
"  },"
"  {"
"    \"id\"          : \"path-behaviour\","
"    \"type\"        : \"ClutterBehaviourPath\","
"    \"path\"        : \"M 50 50 L 100 100\","
"    \"alpha\"       : {"
"      \"timeline\" : \"main-timeline\","
"      \"function\" : \"double_ramp_alpha\""
"    }"
"  },"
"  {"
"    \"id\"          : \"rotate-behaviour\","
"    \"type\"        : \"ClutterBehaviourRotate\","
"    \"angle-start\" : 0.0,"
"    \"angle-end\"   : 360.0,"
"    \"axis\"        : \"y-axis\","
"    \"alpha\"       : \"sine-alpha\""
"  },"
"  {"
"    \"id\"            : \"fade-behaviour\","
"    \"type\"          : \"ClutterBehaviourOpacity\","
"    \"opacity-start\" : 255,"
"    \"opacity-end\"   : 0,"
"    \"alpha\"         : {"
"      \"id\"       : \"fade-alpha\","
"      \"type\"     : \"ClutterAlpha\","
"      \"timeline\" : \"main-timeline\","
"      \"mode\"     : \"linear\""
"    }"
"  }"
"]";

gdouble
sine_alpha (ClutterAlpha *alpha,
            gpointer      dummy G_GNUC_UNUSED)
{
  ClutterTimeline *timeline = clutter_alpha_get_timeline (alpha);

  return sin (clutter_timeline_get_progress (timeline) * G_PI);
}

gdouble
double_ramp_alpha (ClutterAlpha *alpha,
                   gpointer      dummy G_GNUC_UNUSED)
{
  ClutterTimeline *timeline = clutter_alpha_get_timeline (alpha);
  gdouble progress = clutter_timeline_get_progress (timeline);

  if (progress >= 0.5)
    return 1.0 - progress;

  return progress;
}

static gboolean
blue_button_press (ClutterActor       *actor,
                   ClutterButtonEvent *event,
                   gpointer            data)
{
  g_print ("[*] Pressed '%s'\n", clutter_get_script_id (G_OBJECT (actor)));
  g_print ("[*] Unmerging objects with merge id: %d\n", merge_id);

  clutter_script_unmerge_objects (script, merge_id);

  return TRUE;
}

static gboolean
red_button_press (ClutterActor *actor,
                  ClutterButtonEvent *event,
                  gpointer            data)
{
  GObject *timeline;

  g_print ("[*] Pressed '%s'\n", clutter_get_script_id (G_OBJECT (actor)));

  timeline = clutter_script_get_object (script, "main-timeline");
  g_assert (CLUTTER_IS_TIMELINE (timeline));

  if (!clutter_timeline_is_playing (CLUTTER_TIMELINE (timeline)))
    clutter_timeline_start (CLUTTER_TIMELINE (timeline));
  else
    clutter_timeline_pause (CLUTTER_TIMELINE (timeline));

  return TRUE;
}

G_MODULE_EXPORT int
test_script_main (int argc, char *argv[])
{
  GObject *stage, *blue_button, *red_button;
  GError *error = NULL;
  gchar *file;
  gint res;

  if (clutter_init (&argc, &argv) != CLUTTER_INIT_SUCCESS)
    return 1;

  script = clutter_script_new ();
  g_assert (CLUTTER_IS_SCRIPT (script));

  clutter_script_load_from_data (script, test_behaviour, -1, &error);
  if (error)
    {
      g_print ("*** Error:\n"
               "***   %s\n", error->message);
      g_error_free (error);
      g_object_unref (script);
      return EXIT_FAILURE;
    }

  file = g_build_filename (TESTS_DATADIR, "test-script.json", NULL);
  clutter_script_load_from_file (script, file, &error);
  if (error)
    {
      g_print ("*** Error:\n"
               "***   %s\n", error->message);
      g_error_free (error);
      g_object_unref (script);
      free (file);
      return EXIT_FAILURE;
    }

  free (file);

  merge_id = clutter_script_load_from_data (script, test_unmerge, -1, &error);
  if (error)
    {
      g_print ("*** Error:\n"
               "***   %s\n", error->message);
      g_error_free (error);
      g_object_unref (script);
      return EXIT_FAILURE;
    }

  clutter_script_connect_signals (script, NULL);

  res = clutter_script_get_objects (script,
                                    "main-stage", &stage,
                                    "red-button", &red_button,
                                    "blue-button", &blue_button,
                                    NULL);
  g_assert (res == 3);

  clutter_actor_show (CLUTTER_ACTOR (stage));

  g_signal_connect (red_button,
                    "button-press-event",
                    G_CALLBACK (red_button_press),
                    NULL);

  g_signal_connect (blue_button,
                    "button-press-event",
                    G_CALLBACK (blue_button_press),
                    NULL);

  clutter_main ();

  g_object_unref (script);

  return EXIT_SUCCESS;
}
