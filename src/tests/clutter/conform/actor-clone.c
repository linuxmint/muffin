#include <stdlib.h>
#include <string.h>

#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

static void
on_presented (ClutterStage     *stage,
              CoglFrameEvent   *frame_event,
              ClutterFrameInfo *frame_info,
              gboolean         *was_presented)
{
  *was_presented = TRUE;
}

static void
actor_clone_unmapped (void)
{
  ClutterActor *container;
  ClutterActor *actor;
  ClutterActor *clone;
  ClutterActor *stage;
  gboolean was_presented;

  stage = clutter_test_get_stage ();

  container = clutter_actor_new ();
  g_object_ref_sink (container);
  g_object_add_weak_pointer (G_OBJECT (container), (gpointer *) &container);

  actor = clutter_actor_new ();
  g_object_ref_sink (actor);
  g_object_add_weak_pointer (G_OBJECT (actor), (gpointer *) &actor);

  clone = clutter_clone_new (actor);
  g_object_ref_sink (clone);
  g_object_add_weak_pointer (G_OBJECT (clone), (gpointer *) &clone);

  clutter_actor_hide (container);
  clutter_actor_hide (actor);

  clutter_actor_add_child (stage, container);
  clutter_actor_add_child (container, actor);
  clutter_actor_add_child (stage, clone);

  clutter_actor_set_offscreen_redirect (actor, CLUTTER_OFFSCREEN_REDIRECT_ALWAYS);

  g_signal_connect (stage, "presented", G_CALLBACK (on_presented),
                    &was_presented);

  clutter_actor_show (stage);

  was_presented = FALSE;
  while (!was_presented)
    g_main_context_iteration (NULL, FALSE);

  clutter_actor_destroy (clone);
  clutter_actor_destroy (actor);
  clutter_actor_destroy (container);
  g_assert_null (clone);
  g_assert_null (actor);
  g_assert_null (container);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/actor/clone/unmapped", actor_clone_unmapped)
)
