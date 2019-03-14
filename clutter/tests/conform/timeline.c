#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <clutter/clutter.h>

#include "test-conform-common.h"

/* This test runs three timelines at 30 fps with 10 frames. Some of
   the timelines have markers. Once the timelines are run it then
   checks that all of the frames were hit, all of the markers were hit
   and that the completed signal was fired. The timelines are then run
   again but this time with a timeout source that introduces a
   delay. This should cause some frames to be skipped. The test is run
   again but only the markers and the completed signal is checked
   for. */

#define FRAME_COUNT 10
#define FPS         30

typedef struct _TimelineData TimelineData;

struct _TimelineData
{
  int timeline_num;

  guint frame_hit_count[FRAME_COUNT + 1];
  GSList *markers_hit;
  guint completed_count;
};

static void
timeline_data_init (TimelineData *data, int timeline_num)
{
  memset (data, 0, sizeof (TimelineData));
  data->timeline_num = timeline_num;
}

static void
timeline_data_destroy (TimelineData *data)
{
  g_slist_foreach (data->markers_hit, (GFunc) free, NULL);
  g_slist_free (data->markers_hit);
}

static void
timeline_complete_cb (ClutterTimeline *timeline,
                      TimelineData    *data)
{
  if (g_test_verbose ())
    g_print ("%i: Completed\n", data->timeline_num);

  data->completed_count++;
}

static void
timeline_new_frame_cb (ClutterTimeline *timeline,
                       gint             msec,
                       TimelineData    *data)
{
  /* Calculate an approximate frame number from the duration with
     rounding */
  int frame_no = ((msec * FRAME_COUNT + (FRAME_COUNT * 1000 / FPS) / 2)
                  / (FRAME_COUNT * 1000 / FPS));

  if (g_test_verbose ())
    g_print ("%i: Doing frame %d, delta = %i\n",
             data->timeline_num, frame_no,
             clutter_timeline_get_delta (timeline));

  g_assert (frame_no >= 0 && frame_no <= FRAME_COUNT);

  data->frame_hit_count[frame_no]++;
}

static void
timeline_marker_reached_cb (ClutterTimeline *timeline,
                            const gchar     *marker_name,
                            guint            frame_num,
                            TimelineData    *data)
{
  if (g_test_verbose ())
    g_print ("%i: Marker '%s' (%d) reached, delta = %i\n",
             data->timeline_num, marker_name, frame_num,
             clutter_timeline_get_delta (timeline));
  data->markers_hit = g_slist_prepend (data->markers_hit,
                                       g_strdup (marker_name));
}

static gboolean
check_timeline (ClutterTimeline *timeline,
                TimelineData    *data,
                gboolean         check_missed_frames)
{
  gchar **markers;
  gsize n_markers;
  guint *marker_reached_count;
  gboolean succeeded = TRUE;
  GSList *node;
  int i;
  int missed_frame_count = 0;
  int frame_offset;

  if (clutter_timeline_get_direction (timeline) == CLUTTER_TIMELINE_BACKWARD)
    frame_offset = 0;
  else
    frame_offset = 1;

  markers = clutter_timeline_list_markers (timeline, -1, &n_markers);
  marker_reached_count = g_new0 (guint, n_markers);

  for (node = data->markers_hit; node; node = node->next)
    {
      for (i = 0; i < n_markers; i++)
        if (!strcmp (node->data, markers[i]))
          break;

      if (i < n_markers)
        marker_reached_count[i]++;
      else
        {
          if (g_test_verbose ())
            g_print ("FAIL: unknown marker '%s' hit for timeline %i\n",
                     (char *) node->data, data->timeline_num);
          succeeded = FALSE;
        }
    }

  for (i = 0; i < n_markers; i++)
    if (marker_reached_count[i] != 1)
      {
        if (g_test_verbose ())
          g_print ("FAIL: marker '%s' hit %i times for timeline %i\n",
                   markers[i], marker_reached_count[i], data->timeline_num);
        succeeded = FALSE;
      }

  if (check_missed_frames)
    {
      for (i = 0; i < FRAME_COUNT; i++)
        if (data->frame_hit_count[i + frame_offset] < 1)
          missed_frame_count++;

      if (missed_frame_count)
        {
          if (g_test_verbose ())
            g_print ("FAIL: missed %i frame%s for timeline %i\n",
                     missed_frame_count, missed_frame_count == 1 ? "" : "s",
                     data->timeline_num);
          succeeded = FALSE;
        }
    }

  if (data->completed_count != 1)
    {
      if (g_test_verbose ())
        g_print ("FAIL: timeline %i completed %i times\n",
                 data->timeline_num, data->completed_count);
      succeeded = FALSE;
    }

  g_strfreev (markers);
  free (marker_reached_count);

  return succeeded;
}

static gboolean
timeout_cb (gpointer data G_GNUC_UNUSED)
{
  clutter_main_quit ();

  return FALSE;
}

static gboolean
delay_cb (gpointer data)
{
  /* Waste a bit of time so that it will skip frames */
  g_usleep (G_USEC_PER_SEC * 66 / 1000);

  return TRUE;
}

void
timeline_base (TestConformSimpleFixture *fixture,
	       gconstpointer data)
{
  ClutterTimeline *timeline_1;
  TimelineData data_1;
  ClutterTimeline *timeline_2;
  TimelineData data_2;
  ClutterTimeline *timeline_3;
  TimelineData data_3;
  gchar **markers;
  gsize n_markers;
  guint delay_tag;

  /* NB: We have to ensure a stage is instantiated else the master
   * clock wont run... */
  ClutterActor *stage = clutter_stage_new ();

  timeline_data_init (&data_1, 1);
  timeline_1 = clutter_timeline_new (FRAME_COUNT * 1000 / FPS);
  clutter_timeline_add_marker_at_time (timeline_1, "start-marker",
                                       0 * 1000 / FPS);
  clutter_timeline_add_marker_at_time (timeline_1, "foo", 5 * 1000 / FPS);
  clutter_timeline_add_marker_at_time (timeline_1, "bar", 5 * 1000 / FPS);
  clutter_timeline_add_marker_at_time (timeline_1, "baz", 5 * 1000 / FPS);
  clutter_timeline_add_marker_at_time (timeline_1, "near-end-marker",
                                       9 * 1000 / FPS);
  clutter_timeline_add_marker_at_time (timeline_1, "end-marker",
                                       10 * 1000 / FPS);
  markers = clutter_timeline_list_markers (timeline_1, 5 * 1000 / FPS,
                                           &n_markers);
  g_assert (markers != NULL);
  g_assert (n_markers == 3);
  g_strfreev (markers);

  timeline_data_init (&data_2, 2);
  timeline_2 = clutter_timeline_clone (timeline_1);
  clutter_timeline_add_marker_at_time (timeline_2, "bar", 2 * 1000 / FPS);
  markers = clutter_timeline_list_markers (timeline_2, -1, &n_markers);
  g_assert (markers != NULL);
  g_assert (n_markers == 1);
  g_assert (strcmp (markers[0], "bar") == 0);
  g_strfreev (markers);

  timeline_data_init (&data_3, 3);
  timeline_3 = clutter_timeline_clone (timeline_1);
  clutter_timeline_set_direction (timeline_3, CLUTTER_TIMELINE_BACKWARD);
  clutter_timeline_add_marker_at_time (timeline_3, "start-marker",
                                       FRAME_COUNT * 1000 / FPS);
  clutter_timeline_add_marker_at_time (timeline_3, "foo", 5 * 1000 / FPS);
  clutter_timeline_add_marker_at_time (timeline_3, "baz", 8 * 1000 / FPS);
  clutter_timeline_add_marker_at_time (timeline_3, "near-end-marker",
                                       1 * 1000 / FPS);
  clutter_timeline_add_marker_at_time (timeline_3, "end-marker",
                                       0 * 1000 / FPS);

  g_signal_connect (timeline_1,
                    "marker-reached", G_CALLBACK (timeline_marker_reached_cb),
                    &data_1);
  g_signal_connect (timeline_1,
                    "new-frame", G_CALLBACK (timeline_new_frame_cb),
                    &data_1);
  g_signal_connect (timeline_1,
                    "completed", G_CALLBACK (timeline_complete_cb),
                    &data_1);

  g_signal_connect (timeline_2,
                    "marker-reached::bar",
                    G_CALLBACK (timeline_marker_reached_cb),
                    &data_2);
  g_signal_connect (timeline_2,
                    "new-frame", G_CALLBACK (timeline_new_frame_cb),
                    &data_2);
  g_signal_connect (timeline_2,
                    "completed", G_CALLBACK (timeline_complete_cb),
                    &data_2);

  g_signal_connect (timeline_3,
                    "marker-reached", G_CALLBACK (timeline_marker_reached_cb),
                    &data_3);
  g_signal_connect (timeline_3,
                    "new-frame", G_CALLBACK (timeline_new_frame_cb),
                    &data_3);
  g_signal_connect (timeline_3,
                    "completed", G_CALLBACK (timeline_complete_cb),
                    &data_3);

  if (g_test_verbose ())
    g_print ("Without delay...\n");

  clutter_timeline_start (timeline_1);
  clutter_timeline_start (timeline_2);
  clutter_timeline_start (timeline_3);

  clutter_threads_add_timeout (2000, timeout_cb, NULL);

  clutter_main ();

  g_assert (check_timeline (timeline_1, &data_1, TRUE));
  g_assert (check_timeline (timeline_2, &data_2, TRUE));
  g_assert (check_timeline (timeline_3, &data_3, TRUE));

  if (g_test_verbose ())
    g_print ("With delay...\n");

  timeline_data_destroy (&data_1);
  timeline_data_init (&data_1, 1);
  timeline_data_destroy (&data_2);
  timeline_data_init (&data_2, 2);
  timeline_data_destroy (&data_3);
  timeline_data_init (&data_3, 3);

  clutter_timeline_start (timeline_1);
  clutter_timeline_start (timeline_2);
  clutter_timeline_start (timeline_3);

  clutter_threads_add_timeout (2000, timeout_cb, NULL);
  delay_tag = clutter_threads_add_timeout (99, delay_cb, NULL);

  clutter_main ();

  g_assert (check_timeline (timeline_1, &data_1, FALSE));
  g_assert (check_timeline (timeline_2, &data_2, FALSE));
  g_assert (check_timeline (timeline_3, &data_3, FALSE));

  g_object_unref (timeline_1);
  g_object_unref (timeline_2);
  g_object_unref (timeline_3);

  timeline_data_destroy (&data_1);
  timeline_data_destroy (&data_2);
  timeline_data_destroy (&data_3);

  g_source_remove (delay_tag);

  clutter_actor_destroy (stage);
}

void
timeline_markers_from_script (TestConformSimpleFixture *fixture,
                              gconstpointer data)
{
  ClutterScript *script = clutter_script_new ();
  ClutterTimeline *timeline;
  GError *error = NULL;
  gchar *test_file;
  gchar **markers;
  gsize n_markers;

  test_file = clutter_test_get_data_file ("test-script-timeline-markers.json");
  clutter_script_load_from_file (script, test_file, &error);
  if (g_test_verbose () && error != NULL)
    g_print ("Error: %s", error->message);

  g_assert_no_error (error);

  timeline = CLUTTER_TIMELINE (clutter_script_get_object (script, "timeline0"));

  g_assert (clutter_timeline_has_marker (timeline, "marker0"));
  g_assert (clutter_timeline_has_marker (timeline, "marker1"));
  g_assert (!clutter_timeline_has_marker (timeline, "foo"));
  g_assert (clutter_timeline_has_marker (timeline, "marker2"));
  g_assert (clutter_timeline_has_marker (timeline, "marker3"));

  markers = clutter_timeline_list_markers (timeline, -1, &n_markers);
  g_assert_cmpint (n_markers, ==, 4);
  g_strfreev (markers);

  markers = clutter_timeline_list_markers (timeline, 500, &n_markers);
  g_assert_cmpint (n_markers, ==, 2);
  g_assert (markers != NULL);
  g_assert_cmpstr (markers[0], ==, "marker3");
  g_assert_cmpstr (markers[1], ==, "marker1");
  g_strfreev (markers);

  g_object_unref (script);

  free (test_file);
}
