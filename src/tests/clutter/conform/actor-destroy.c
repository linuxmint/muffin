#define CLUTTER_DISABLE_DEPRECATION_WARNINGS
#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

#define TEST_TYPE_DESTROY               (test_destroy_get_type ())
#define TEST_DESTROY(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), TEST_TYPE_DESTROY, TestDestroy))
#define TEST_IS_DESTROY(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TEST_TYPE_DESTROY))

typedef struct _TestDestroy             TestDestroy;
typedef struct _TestDestroyClass        TestDestroyClass;

struct _TestDestroy
{
  ClutterActor parent_instance;

  ClutterActor *bg;
  ClutterActor *label;

  GList *children;
};

struct _TestDestroyClass
{
  ClutterActorClass parent_class;
};

static void clutter_container_init (ClutterContainerIface *iface);

GType test_destroy_get_type (void);

G_DEFINE_TYPE_WITH_CODE (TestDestroy, test_destroy, CLUTTER_TYPE_ACTOR,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_CONTAINER,
                                                clutter_container_init));

static void
test_destroy_add (ClutterContainer *container,
                  ClutterActor *actor)
{
  TestDestroy *self = TEST_DESTROY (container);

  if (g_test_verbose ())
    g_print ("Adding '%s' (type:%s)\n",
             clutter_actor_get_name (actor),
             G_OBJECT_TYPE_NAME (actor));

  self->children = g_list_prepend (self->children, actor);
  clutter_actor_set_parent (actor, CLUTTER_ACTOR (container));
}

static void
test_destroy_remove (ClutterContainer *container,
                     ClutterActor *actor)
{
  TestDestroy *self = TEST_DESTROY (container);

  if (g_test_verbose ())
    g_print ("Removing '%s' (type:%s)\n",
             clutter_actor_get_name (actor),
             G_OBJECT_TYPE_NAME (actor));

  g_assert_true (g_list_find (self->children, actor));
  self->children = g_list_remove (self->children, actor);

  clutter_actor_unparent (actor);
}

static void
clutter_container_init (ClutterContainerIface *iface)
{
  iface->add = test_destroy_add;
  iface->remove = test_destroy_remove;
}

static void
test_destroy_destroy (ClutterActor *self)
{
  TestDestroy *test = TEST_DESTROY (self);

  g_assert_cmpuint (g_list_length (test->children), ==, 3);

  if (test->bg != NULL)
    {
      if (g_test_verbose ())
        g_print ("Destroying '%s' (type:%s)\n",
                 clutter_actor_get_name (test->bg),
                 G_OBJECT_TYPE_NAME (test->bg));

      clutter_actor_destroy (test->bg);
      test->bg = NULL;
    }

  if (test->label != NULL)
    {
      if (g_test_verbose ())
        g_print ("Destroying '%s' (type:%s)\n",
                 clutter_actor_get_name (test->label),
                 G_OBJECT_TYPE_NAME (test->label));

      clutter_actor_destroy (test->label);
      test->label = NULL;
    }

  g_assert_cmpuint (g_list_length (test->children), ==, 1);

  if (CLUTTER_ACTOR_CLASS (test_destroy_parent_class)->destroy)
    CLUTTER_ACTOR_CLASS (test_destroy_parent_class)->destroy (self);

  g_assert_null (test->children);
}

static void
test_destroy_class_init (TestDestroyClass *klass)
{
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

  actor_class->destroy = test_destroy_destroy;
}

static void
test_destroy_init (TestDestroy *self)
{
  self->bg = clutter_rectangle_new ();
  clutter_container_add_actor (CLUTTER_CONTAINER (self), self->bg);
  clutter_actor_set_name (self->bg, "Background");

  self->label = clutter_text_new ();
  clutter_container_add_actor (CLUTTER_CONTAINER (self), self->label);
  clutter_actor_set_name (self->label, "Label");
}

static void
on_destroy (ClutterActor *actor,
            gpointer      data)
{
  gboolean *destroy_called = data;

  g_assert_true (CLUTTER_IS_ACTOR (clutter_actor_get_parent (actor)));

  *destroy_called = TRUE;
}

static void
on_parent_set (ClutterActor *actor,
               ClutterActor *old_parent,
               gpointer      data)
{
  gboolean *parent_set_called = data;

  *parent_set_called = TRUE;
}

static void
on_notify (ClutterActor *actor,
           ClutterActor *old_parent,
           gpointer      data)
{
  gboolean *property_changed = data;

  *property_changed = TRUE;
}

static void
actor_destruction (void)
{
  ClutterActor *test = g_object_new (TEST_TYPE_DESTROY, NULL);
  ClutterActor *child = clutter_rectangle_new ();
  gboolean destroy_called = FALSE;
  gboolean parent_set_called = FALSE;
  gboolean property_changed = FALSE;

  g_object_ref_sink (test);

  g_object_add_weak_pointer (G_OBJECT (test), (gpointer *) &test);
  g_object_add_weak_pointer (G_OBJECT (child), (gpointer *) &child);

  if (g_test_verbose ())
    g_print ("Adding external child...\n");

  clutter_actor_set_name (child, "Child");
  clutter_container_add_actor (CLUTTER_CONTAINER (test), child);
  g_signal_connect (child, "parent-set", G_CALLBACK (on_parent_set),
                    &parent_set_called);
  g_signal_connect (child, "notify", G_CALLBACK (on_notify), &property_changed);
  g_signal_connect (child, "destroy", G_CALLBACK (on_destroy), &destroy_called);

  if (g_test_verbose ())
    g_print ("Calling destroy()...\n");

  clutter_actor_destroy (test);
  g_assert (destroy_called);
  g_assert_false (parent_set_called);
  g_assert_false (property_changed);
  g_assert_null (child);
  g_assert_null (test);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/actor/destruction", actor_destruction)
)
