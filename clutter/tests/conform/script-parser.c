#include <stdlib.h>
#include <string.h>

#define CLUTTER_DISABLE_DEPRECATION_WARNINGS
#include <clutter/clutter.h>

#define TEST_TYPE_GROUP                 (test_group_get_type ())
#define TEST_TYPE_GROUP_META            (test_group_meta_get_type ())

#define TEST_GROUP(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), TEST_TYPE_GROUP, TestGroup))
#define TEST_IS_GROUP(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TEST_TYPE_GROUP))

#define TEST_GROUP_META(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), TEST_TYPE_GROUP_META, TestGroupMeta))
#define TEST_IS_GROUP_META(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TEST_TYPE_GROUP_META))

typedef struct _ClutterActor            TestGroup;
typedef struct _ClutterActorClass       TestGroupClass;

typedef struct _TestGroupMeta {
  ClutterChildMeta parent_instance;

  guint is_focus : 1;
} TestGroupMeta;

typedef struct _ClutterChildMetaClass   TestGroupMetaClass;

GType test_group_meta_get_type (void);

G_DEFINE_TYPE (TestGroupMeta, test_group_meta, CLUTTER_TYPE_CHILD_META)

enum
{
  PROP_META_0,

  PROP_META_FOCUS
};

static void
test_group_meta_set_property (GObject      *gobject,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  TestGroupMeta *self = TEST_GROUP_META (gobject);

  switch (prop_id)
    {
    case PROP_META_FOCUS:
      self->is_focus = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
test_group_meta_get_property (GObject    *gobject,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  TestGroupMeta *self = TEST_GROUP_META (gobject);

  switch (prop_id)
    {
    case PROP_META_FOCUS:
      g_value_set_boolean (value, self->is_focus);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
test_group_meta_class_init (TestGroupMetaClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GParamSpec *pspec;

  gobject_class->set_property = test_group_meta_set_property;
  gobject_class->get_property = test_group_meta_get_property;

  pspec = g_param_spec_boolean ("focus", "Focus", "Focus",
                                FALSE,
                                G_PARAM_READWRITE);
  g_object_class_install_property (gobject_class, PROP_META_FOCUS, pspec);
}

static void
test_group_meta_init (TestGroupMeta *meta)
{
  meta->is_focus = FALSE;
}

static void
clutter_container_iface_init (ClutterContainerIface *iface)
{
  iface->child_meta_type = TEST_TYPE_GROUP_META;
}

GType test_group_get_type (void);

G_DEFINE_TYPE_WITH_CODE (TestGroup, test_group, CLUTTER_TYPE_ACTOR,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_CONTAINER,
                                                clutter_container_iface_init))

static void
test_group_class_init (TestGroupClass *klass)
{
}

static void
test_group_init (TestGroup *self)
{
}

static void
script_child (void)
{
  ClutterScript *script = clutter_script_new ();
  GObject *container, *actor;
  GError *error = NULL;
  gboolean focus_ret;
  gchar *test_file;

  test_file = g_test_build_filename (G_TEST_DIST, "scripts", "test-script-child.json", NULL);
  clutter_script_load_from_file (script, test_file, &error);
  if (g_test_verbose () && error)
    g_print ("Error: %s", error->message);

  g_assert_no_error (error);

  container = actor = NULL;
  clutter_script_get_objects (script,
                              "test-group", &container,
                              "test-rect-1", &actor,
                              NULL);
  g_assert (TEST_IS_GROUP (container));
  g_assert (CLUTTER_IS_RECTANGLE (actor));

  focus_ret = FALSE;
  clutter_container_child_get (CLUTTER_CONTAINER (container),
                               CLUTTER_ACTOR (actor),
                               "focus", &focus_ret,
                               NULL);
  g_assert (focus_ret);

  actor = clutter_script_get_object (script, "test-rect-2");
  g_assert (CLUTTER_IS_RECTANGLE (actor));

  focus_ret = FALSE;
  clutter_container_child_get (CLUTTER_CONTAINER (container),
                               CLUTTER_ACTOR (actor),
                               "focus", &focus_ret,
                               NULL);
  g_assert (!focus_ret);

  g_object_unref (script);
  free (test_file);
}

static void
script_single (void)
{
  ClutterScript *script = clutter_script_new ();
  ClutterColor color = { 0, };
  GObject *actor = NULL;
  GError *error = NULL;
  ClutterActor *rect;
  gchar *test_file;

  test_file = g_test_build_filename (G_TEST_DIST, "scripts", "test-script-single.json", NULL);
  clutter_script_load_from_file (script, test_file, &error);
  if (g_test_verbose () && error)
    g_print ("Error: %s", error->message);

  g_assert_no_error (error);

  actor = clutter_script_get_object (script, "test");
  g_assert (CLUTTER_IS_RECTANGLE (actor));

  rect = CLUTTER_ACTOR (actor);
  g_assert_cmpfloat (clutter_actor_get_width (rect), ==, 50.0);
  g_assert_cmpfloat (clutter_actor_get_y (rect), ==, 100.0);

  clutter_rectangle_get_color (CLUTTER_RECTANGLE (rect), &color);
  g_assert_cmpint (color.red, ==, 255);
  g_assert_cmpint (color.green, ==, 0xcc);
  g_assert_cmpint (color.alpha, ==, 0xff);

  g_object_unref (script);
  free (test_file);
}

static void
script_implicit_alpha (void)
{
  ClutterScript *script = clutter_script_new ();
  ClutterTimeline *timeline;
  GObject *behaviour = NULL;
  GError *error = NULL;
  ClutterAlpha *alpha;
  gchar *test_file;

  test_file = g_test_build_filename (G_TEST_DIST, "scripts", "test-script-implicit-alpha.json", NULL);
  clutter_script_load_from_file (script, test_file, &error);
  if (g_test_verbose () && error)
    g_print ("Error: %s", error->message);

#if GLIB_CHECK_VERSION (2, 20, 0)
  g_assert_no_error (error);
#else
  g_assert (error == NULL);
#endif

  behaviour = clutter_script_get_object (script, "test");
  g_assert (CLUTTER_IS_BEHAVIOUR (behaviour));

  alpha = clutter_behaviour_get_alpha (CLUTTER_BEHAVIOUR (behaviour));
  g_assert (CLUTTER_IS_ALPHA (alpha));

  g_assert_cmpint (clutter_alpha_get_mode (alpha), ==, CLUTTER_EASE_OUT_CIRC);

  timeline = clutter_alpha_get_timeline (alpha);
  g_assert (CLUTTER_IS_TIMELINE (timeline));

  g_assert_cmpint (clutter_timeline_get_duration (timeline), ==, 500);

  g_object_unref (script);
  free (test_file);
}

static void
script_object_property (void)
{
  ClutterScript *script = clutter_script_new ();
  ClutterLayoutManager *manager;
  GObject *actor = NULL;
  GError *error = NULL;
  gchar *test_file;

  test_file = g_test_build_filename (G_TEST_DIST, "scripts", "test-script-object-property.json", NULL);
  clutter_script_load_from_file (script, test_file, &error);
  if (g_test_verbose () && error)
    g_print ("Error: %s", error->message);

  g_assert_no_error (error);

  actor = clutter_script_get_object (script, "test");
  g_assert (CLUTTER_IS_BOX (actor));

  manager = clutter_box_get_layout_manager (CLUTTER_BOX (actor));
  g_assert (CLUTTER_IS_BIN_LAYOUT (manager));

  g_object_unref (script);
  free (test_file);
}

static void
script_named_object (void)
{
  ClutterScript *script = clutter_script_new ();
  ClutterLayoutManager *manager;
  GObject *actor = NULL;
  GError *error = NULL;
  gchar *test_file;

  test_file = g_test_build_filename (G_TEST_DIST, "scripts", "test-script-named-object.json", NULL);
  clutter_script_load_from_file (script, test_file, &error);
  if (g_test_verbose () && error)
    g_print ("Error: %s", error->message);

  g_assert_no_error (error);

  actor = clutter_script_get_object (script, "test");
  g_assert (CLUTTER_IS_BOX (actor));

  manager = clutter_box_get_layout_manager (CLUTTER_BOX (actor));
  g_assert (CLUTTER_IS_BOX_LAYOUT (manager));
  g_assert (clutter_box_layout_get_vertical (CLUTTER_BOX_LAYOUT (manager)));

  g_object_unref (script);
  free (test_file);
}

static void
script_animation (void)
{
  ClutterScript *script = clutter_script_new ();
  GObject *animation = NULL;
  GError *error = NULL;
  gchar *test_file;

  test_file = g_test_build_filename (G_TEST_DIST, "scripts", "test-script-animation.json", NULL);
  clutter_script_load_from_file (script, test_file, &error);
  if (g_test_verbose () && error)
    g_print ("Error: %s", error->message);

  g_assert_no_error (error);

  animation = clutter_script_get_object (script, "test");
  g_assert (CLUTTER_IS_ANIMATION (animation));

  g_object_unref (script);
  free (test_file);
}

static void
script_layout_property (void)
{
  ClutterScript *script = clutter_script_new ();
  GObject *manager, *container, *actor1, *actor2;
  GError *error = NULL;
  gchar *test_file;
  gboolean x_fill, expand;
  ClutterBoxAlignment y_align;

  test_file = g_test_build_filename (G_TEST_DIST, "scripts", "test-script-layout-property.json", NULL);
  clutter_script_load_from_file (script, test_file, &error);
  if (g_test_verbose () && error)
    g_print ("Error: %s", error->message);

  g_assert_no_error (error);

  manager = container = actor1 = actor2 = NULL;
  clutter_script_get_objects (script,
                              "manager", &manager,
                              "container", &container,
                              "actor-1", &actor1,
                              "actor-2", &actor2,
                              NULL);

  g_assert (CLUTTER_IS_LAYOUT_MANAGER (manager));
  g_assert (CLUTTER_IS_CONTAINER (container));
  g_assert (CLUTTER_IS_ACTOR (actor1));
  g_assert (CLUTTER_IS_ACTOR (actor2));

  x_fill = FALSE;
  y_align = CLUTTER_BOX_ALIGNMENT_START;
  expand = FALSE;
  clutter_layout_manager_child_get (CLUTTER_LAYOUT_MANAGER (manager),
                                    CLUTTER_CONTAINER (container),
                                    CLUTTER_ACTOR (actor1),
                                    "x-fill", &x_fill,
                                    "y-align", &y_align,
                                    "expand", &expand,
                                    NULL);

  g_assert (x_fill);
  g_assert (y_align == CLUTTER_BOX_ALIGNMENT_CENTER);
  g_assert (expand);

  x_fill = TRUE;
  y_align = CLUTTER_BOX_ALIGNMENT_START;
  expand = TRUE;
  clutter_layout_manager_child_get (CLUTTER_LAYOUT_MANAGER (manager),
                                    CLUTTER_CONTAINER (container),
                                    CLUTTER_ACTOR (actor2),
                                    "x-fill", &x_fill,
                                    "y-align", &y_align,
                                    "expand", &expand,
                                    NULL);

  g_assert (x_fill == FALSE);
  g_assert (y_align == CLUTTER_BOX_ALIGNMENT_END);
  g_assert (expand == FALSE);

  g_object_unref (script);
}

static void
script_margin (void)
{
  ClutterScript *script = clutter_script_new ();
  ClutterActor *actor;
  gchar *test_file;
  GError *error = NULL;

  test_file = g_test_build_filename (G_TEST_DIST, "scripts", "test-script-margin.json", NULL);
  clutter_script_load_from_file (script, test_file, &error);
  if (g_test_verbose () && error)
    g_print ("Error: %s", error->message);

  g_assert_no_error (error);

  actor = CLUTTER_ACTOR (clutter_script_get_object (script, "actor-1"));
  g_assert_cmpfloat (clutter_actor_get_margin_top (actor), ==, 10.0f);
  g_assert_cmpfloat (clutter_actor_get_margin_right (actor), ==, 10.0f);
  g_assert_cmpfloat (clutter_actor_get_margin_bottom (actor), ==, 10.0f);
  g_assert_cmpfloat (clutter_actor_get_margin_left (actor), ==, 10.0f);

  actor = CLUTTER_ACTOR (clutter_script_get_object (script, "actor-2"));
  g_assert_cmpfloat (clutter_actor_get_margin_top (actor), ==, 10.0f);
  g_assert_cmpfloat (clutter_actor_get_margin_right (actor), ==, 20.0f);
  g_assert_cmpfloat (clutter_actor_get_margin_bottom (actor), ==, 10.0f);
  g_assert_cmpfloat (clutter_actor_get_margin_left (actor), ==, 20.0f);

  actor = CLUTTER_ACTOR (clutter_script_get_object (script, "actor-3"));
  g_assert_cmpfloat (clutter_actor_get_margin_top (actor), ==, 10.0f);
  g_assert_cmpfloat (clutter_actor_get_margin_right (actor), ==, 20.0f);
  g_assert_cmpfloat (clutter_actor_get_margin_bottom (actor), ==, 30.0f);
  g_assert_cmpfloat (clutter_actor_get_margin_left (actor), ==, 20.0f);

  actor = CLUTTER_ACTOR (clutter_script_get_object (script, "actor-4"));
  g_assert_cmpfloat (clutter_actor_get_margin_top (actor), ==, 10.0f);
  g_assert_cmpfloat (clutter_actor_get_margin_right (actor), ==, 20.0f);
  g_assert_cmpfloat (clutter_actor_get_margin_bottom (actor), ==, 30.0f);
  g_assert_cmpfloat (clutter_actor_get_margin_left (actor), ==, 40.0f);

  g_object_unref (script);
  free (test_file);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/script/single-object", script_single)
  CLUTTER_TEST_UNIT ("/script/container-child", script_child)
  CLUTTER_TEST_UNIT ("/script/named-object", script_named_object)
  CLUTTER_TEST_UNIT ("/script/animation", script_animation)
  CLUTTER_TEST_UNIT ("/script/implicit-alpha", script_implicit_alpha)
  CLUTTER_TEST_UNIT ("/script/object-property", script_object_property)
  CLUTTER_TEST_UNIT ("/script/layout-property", script_layout_property)
  CLUTTER_TEST_UNIT ("/script/actor-margin", script_margin)
)
