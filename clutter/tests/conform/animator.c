#define CLUTTER_DISABLE_DEPRECATION_WARNINGS
#include <clutter/clutter.h>

static void
animator_multi_properties (void)
{
  ClutterScript *script = clutter_script_new ();
  GObject *animator = NULL, *foo = NULL;
  GError *error = NULL;
  gchar *test_file;
  GList *keys;
  ClutterAnimatorKey *key;
  GValue value = { 0, };

  test_file = g_test_build_filename (G_TEST_DIST,
                                     "scripts",
                                     "test-animator-3.json",
                                     NULL);
  clutter_script_load_from_file (script, test_file, &error);
  if (g_test_verbose () && error)
    g_print ("Error: %s", error->message);

  g_assert_no_error (error);

  foo = clutter_script_get_object (script, "foo");
  g_assert (G_IS_OBJECT (foo));

  animator = clutter_script_get_object (script, "animator");
  g_assert (CLUTTER_IS_ANIMATOR (animator));

  /* get all the keys for foo:x */
  keys = clutter_animator_get_keys (CLUTTER_ANIMATOR (animator),
                                    foo, "x",
                                    -1.0);
  g_assert_cmpint (g_list_length (keys), ==, 3);

  key = g_list_nth_data (keys, 1);
  g_assert (key != NULL);

  if (g_test_verbose ())
    {
      g_print ("(foo, x).keys[1] = \n"
               ".object = %s\n"
               ".progress = %.2f\n"
               ".name = '%s'\n"
               ".type = '%s'\n",
               clutter_get_script_id (clutter_animator_key_get_object (key)),
               clutter_animator_key_get_progress (key),
               clutter_animator_key_get_property_name (key),
               g_type_name (clutter_animator_key_get_property_type (key)));
    }

  g_assert (clutter_animator_key_get_object (key) != NULL);
  g_assert_cmpfloat (clutter_animator_key_get_progress (key), ==, 0.2);
  g_assert_cmpstr (clutter_animator_key_get_property_name (key), ==, "x");

  g_assert (clutter_animator_key_get_property_type (key) == G_TYPE_FLOAT);

  g_value_init (&value, G_TYPE_FLOAT);
  g_assert (clutter_animator_key_get_value (key, &value));
  g_assert_cmpfloat (g_value_get_float (&value), ==, 150.0);
  g_value_unset (&value);

  g_list_free (keys);

  /* get all the keys for foo:y */
  keys = clutter_animator_get_keys (CLUTTER_ANIMATOR (animator),
                                    foo, "y",
                                    -1.0);
  g_assert_cmpint (g_list_length (keys), ==, 3);

  key = g_list_nth_data (keys, 2);
  g_assert (key != NULL);

  if (g_test_verbose ())
    {
      g_print ("(foo, y).keys[2] = \n"
               ".object = %s\n"
               ".progress = %.2f\n"
               ".name = '%s'\n"
               ".type = '%s'\n",
               clutter_get_script_id (clutter_animator_key_get_object (key)),
               clutter_animator_key_get_progress (key),
               clutter_animator_key_get_property_name (key),
               g_type_name (clutter_animator_key_get_property_type (key)));
    }

  g_assert (clutter_animator_key_get_object (key) != NULL);
  g_assert_cmpfloat (clutter_animator_key_get_progress (key), ==, 0.8);
  g_assert_cmpstr (clutter_animator_key_get_property_name (key), ==, "y");

  g_assert (clutter_animator_key_get_property_type (key) == G_TYPE_FLOAT);

  g_value_init (&value, G_TYPE_FLOAT);
  g_assert (clutter_animator_key_get_value (key, &value));
  g_assert_cmpfloat (g_value_get_float (&value), ==, 200.0);
  g_value_unset (&value);

  g_list_free (keys);

  g_object_unref (script);
  free (test_file);
}

static void
animator_properties (void)
{
  ClutterScript *script = clutter_script_new ();
  GObject *animator = NULL;
  GError *error = NULL;
  gchar *test_file;
  GList *keys;
  ClutterAnimatorKey *key;
  GValue value = { 0, };

  test_file = g_test_build_filename (G_TEST_DIST,
                                     "scripts",
                                     "test-animator-2.json",
                                     NULL);
  clutter_script_load_from_file (script, test_file, &error);
  if (g_test_verbose () && error)
    g_print ("Error: %s", error->message);

  g_assert_no_error (error);

  animator = clutter_script_get_object (script, "animator");
  g_assert (CLUTTER_IS_ANIMATOR (animator));

  /* get all the keys */
  keys = clutter_animator_get_keys (CLUTTER_ANIMATOR (animator),
                                    NULL, NULL, -1.0);
  g_assert_cmpint (g_list_length (keys), ==, 3);

  key = g_list_nth_data (keys, 1);
  g_assert (key != NULL);

  if (g_test_verbose ())
    {
      g_print ("keys[1] = \n"
               ".object = %s\n"
               ".progress = %.2f\n"
               ".name = '%s'\n"
               ".type = '%s'\n",
               clutter_get_script_id (clutter_animator_key_get_object (key)),
               clutter_animator_key_get_progress (key),
               clutter_animator_key_get_property_name (key),
               g_type_name (clutter_animator_key_get_property_type (key)));
    }

  g_assert (clutter_animator_key_get_object (key) != NULL);
  g_assert_cmpfloat (clutter_animator_key_get_progress (key), ==, 0.2);
  g_assert_cmpstr (clutter_animator_key_get_property_name (key), ==, "x");

  g_assert (clutter_animator_key_get_property_type (key) == G_TYPE_FLOAT);

  g_value_init (&value, G_TYPE_FLOAT);
  g_assert (clutter_animator_key_get_value (key, &value));
  g_assert_cmpfloat (g_value_get_float (&value), ==, 150.0);
  g_value_unset (&value);

  g_list_free (keys);
  g_object_unref (script);
  free (test_file);
}

static void
animator_base (void)
{
  ClutterScript *script = clutter_script_new ();
  GObject *animator = NULL;
  GError *error = NULL;
  guint duration = 0;
  gchar *test_file;

  test_file = g_test_build_filename (G_TEST_DIST,
                                     "scripts",
                                     "test-animator-1.json",
                                     NULL);
  clutter_script_load_from_file (script, test_file, &error);
  if (g_test_verbose () && error)
    g_print ("Error: %s", error->message);

  g_assert_no_error (error);

  animator = clutter_script_get_object (script, "animator");
  g_assert (CLUTTER_IS_ANIMATOR (animator));

  duration = clutter_animator_get_duration (CLUTTER_ANIMATOR (animator));
  g_assert_cmpint (duration, ==, 1000);

  g_object_unref (script);
  free (test_file);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/script/animator/base", animator_base)
  CLUTTER_TEST_UNIT ("/script/animator/properties", animator_properties)
  CLUTTER_TEST_UNIT ("/script/animator/multi-properties", animator_multi_properties)
)
