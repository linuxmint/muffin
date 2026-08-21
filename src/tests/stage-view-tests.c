/*
 * Copyright (C) 2020 Jonas Dreßler
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "compositor/meta-plugin-manager.h"
#include "core/main-private.h"
#include "meta/main.h"
#include "tests/meta-backend-test.h"
#include "tests/monitor-test-utils.h"
#include "tests/test-utils.h"

#define FRAME_WARNING "Frame has assigned frame counter but no frame drawn time"

static gboolean
run_tests (gpointer data)
{
  MetaBackend *backend = meta_get_backend ();
  MetaSettings *settings = meta_backend_get_settings (backend);
  gboolean ret;

  g_test_log_set_fatal_handler (NULL, NULL);

  meta_settings_override_experimental_features (settings);

  meta_settings_enable_experimental_feature (
    settings,
    META_EXPERIMENTAL_FEATURE_SCALE_MONITOR_FRAMEBUFFER);

  ret = g_test_run ();

  meta_quit (ret != 0);

  return G_SOURCE_REMOVE;
}

static gboolean
ignore_frame_counter_warning (const gchar    *log_domain,
                              GLogLevelFlags  log_level,
                              const gchar    *message,
                              gpointer        user_data)
{
  if ((log_level & G_LOG_LEVEL_WARNING) &&
      g_strcmp0 (log_domain, "mutter") == 0 &&
      g_str_has_suffix (message, FRAME_WARNING))
    return FALSE;

  return TRUE;
}

static MonitorTestCaseSetup initial_test_case_setup = {
  .modes = {
    {
      .width = 1024,
      .height = 768,
      .refresh_rate = 60.0
    }
  },
  .n_modes = 1,
  .outputs = {
     {
      .crtc = 0,
      .modes = { 0 },
      .n_modes = 1,
      .preferred_mode = 0,
      .possible_crtcs = { 0 },
      .n_possible_crtcs = 1,
      .width_mm = 222,
      .height_mm = 125
    },
    {
      .crtc = 1,
      .modes = { 0 },
      .n_modes = 1,
      .preferred_mode = 0,
      .possible_crtcs = { 1 },
      .n_possible_crtcs = 1,
      .width_mm = 220,
      .height_mm = 124
    }
  },
  .n_outputs = 2,
  .crtcs = {
    {
      .current_mode = 0
    },
    {
      .current_mode = 0
    }
  },
  .n_crtcs = 2
};

static void
meta_test_stage_views_exist (void)
{
  MetaBackend *backend = meta_get_backend ();
  ClutterActor *stage;
  GList *stage_views;

  stage = meta_backend_get_stage (backend);
  g_assert_cmpint (clutter_actor_get_width (stage), ==, 1024 * 2);
  g_assert_cmpint (clutter_actor_get_height (stage), ==, 768);

  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));
  g_assert_cmpint (g_list_length (stage_views), ==, 2);
}

static void
on_after_paint (ClutterStage *stage,
                gboolean     *was_painted)
{
  *was_painted = TRUE;
}

static void
wait_for_paint (ClutterActor *stage)
{
  gboolean was_painted = FALSE;
  gulong was_painted_id;

  was_painted_id = g_signal_connect (CLUTTER_STAGE (stage),
                                     "after-paint",
                                     G_CALLBACK (on_after_paint),
                                     &was_painted);

  while (!was_painted)
    g_main_context_iteration (NULL, FALSE);

  g_signal_handler_disconnect (stage, was_painted_id);
}

static void
on_stage_views_changed (ClutterActor *actor,
                        gboolean     *stage_views_changed)
{
  *stage_views_changed = TRUE;
}

static void
is_on_stage_views (ClutterActor *actor,
                   unsigned int  n_views,
                   ...)
{
  va_list valist;
  int i = 0;
  GList *stage_views = clutter_actor_peek_stage_views (actor);

  va_start (valist, n_views);
  for (i = 0; i < n_views; i++)
    {
      ClutterStageView *view = va_arg (valist, ClutterStageView*);
      g_assert_nonnull (g_list_find (stage_views, view));
    }

  va_end (valist);
  g_assert (g_list_length (stage_views) == n_views);
}

static void
meta_test_actor_stage_views (void)
{
  MetaBackend *backend = meta_get_backend ();
  ClutterActor *stage, *container, *test_actor;
  GList *stage_views;
  gboolean stage_views_changed_container = FALSE;
  gboolean stage_views_changed_test_actor = FALSE;
  gboolean *stage_views_changed_container_ptr =
    &stage_views_changed_container;
  gboolean *stage_views_changed_test_actor_ptr =
    &stage_views_changed_test_actor;

  stage = meta_backend_get_stage (backend);
  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));

  container = clutter_actor_new ();
  clutter_actor_set_size (container, 100, 100);
  clutter_actor_add_child (stage, container);

  test_actor = clutter_actor_new ();
  clutter_actor_set_size (test_actor, 50, 50);
  clutter_actor_add_child (container, test_actor);

  g_signal_connect (container, "stage-views-changed",
                    G_CALLBACK (on_stage_views_changed),
                    stage_views_changed_container_ptr);
  g_signal_connect (test_actor, "stage-views-changed",
                    G_CALLBACK (on_stage_views_changed),
                    stage_views_changed_test_actor_ptr);

  clutter_actor_show (stage);

  wait_for_paint (stage);

  is_on_stage_views (container, 1, stage_views->data);
  is_on_stage_views (test_actor, 1, stage_views->data);

  /* The signal was emitted for the initial change */
  g_assert (stage_views_changed_container);
  g_assert (stage_views_changed_test_actor);
  stage_views_changed_container = FALSE;
  stage_views_changed_test_actor = FALSE;

  /* Move the container to the second stage view */
  clutter_actor_set_x (container, 1040);

  wait_for_paint (stage);

  is_on_stage_views (container, 1, stage_views->next->data);
  is_on_stage_views (test_actor, 1, stage_views->next->data);

  /* The signal was emitted again */
  g_assert (stage_views_changed_container);
  g_assert (stage_views_changed_test_actor);
  stage_views_changed_container = FALSE;
  stage_views_changed_test_actor = FALSE;

  /* Move the container so it's on both stage views while the test_actor
   * is only on the first one.
   */
  clutter_actor_set_x (container, 940);

  wait_for_paint (stage);

  is_on_stage_views (container, 2, stage_views->data, stage_views->next->data);
  is_on_stage_views (test_actor, 1, stage_views->data);

  /* The signal was emitted again */
  g_assert (stage_views_changed_container);
  g_assert (stage_views_changed_test_actor);

  g_signal_handlers_disconnect_by_func (container, on_stage_views_changed,
                                        stage_views_changed_container_ptr);
  g_signal_handlers_disconnect_by_func (test_actor, on_stage_views_changed,
                                        stage_views_changed_test_actor_ptr);
  clutter_actor_destroy (container);
}

static void
meta_test_actor_stage_views_reparent (void)
{
  MetaBackend *backend = meta_get_backend ();
  ClutterActor *stage, *container, *test_actor;
  GList *stage_views;
  gboolean stage_views_changed_container = FALSE;
  gboolean stage_views_changed_test_actor = FALSE;
  gboolean *stage_views_changed_container_ptr =
    &stage_views_changed_container;
  gboolean *stage_views_changed_test_actor_ptr =
    &stage_views_changed_test_actor;

  stage = meta_backend_get_stage (backend);
  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));

  container = clutter_actor_new ();
  clutter_actor_set_size (container, 100, 100);
  clutter_actor_set_x (container, 1020);
  clutter_actor_add_child (stage, container);

  test_actor = clutter_actor_new ();
  clutter_actor_set_size (test_actor, 20, 20);
  clutter_actor_add_child (container, test_actor);

  g_signal_connect (container, "stage-views-changed",
                    G_CALLBACK (on_stage_views_changed),
                    stage_views_changed_container_ptr);
  g_signal_connect (test_actor, "stage-views-changed",
                    G_CALLBACK (on_stage_views_changed),
                    stage_views_changed_test_actor_ptr);

  clutter_actor_show (stage);

  wait_for_paint (stage);

  is_on_stage_views (container, 2, stage_views->data, stage_views->next->data);
  is_on_stage_views (test_actor, 2, stage_views->data, stage_views->next->data);

  /* The signal was emitted for both actors */
  g_assert (stage_views_changed_container);
  g_assert (stage_views_changed_test_actor);
  stage_views_changed_container = FALSE;
  stage_views_changed_test_actor = FALSE;

  /* Remove the test_actor from the scene-graph */
  g_object_ref (test_actor);
  clutter_actor_remove_child (container, test_actor);

  /* While the test_actor is not on stage, it must be on no stage views */
  is_on_stage_views (test_actor, 0);

  /* When the test_actor left the stage, the signal was emitted */
  g_assert (!stage_views_changed_container);
  g_assert (stage_views_changed_test_actor);
  stage_views_changed_test_actor = FALSE;

  /* Add the test_actor again as a child of the stage */
  clutter_actor_add_child (stage, test_actor);
  g_object_unref (test_actor);

  wait_for_paint (stage);

  /* The container is still on both stage views... */
  is_on_stage_views (container, 2, stage_views->data, stage_views->next->data);

  /* ...while the test_actor is only on the first one now */
  is_on_stage_views (test_actor, 1, stage_views->data);

  /* The signal was emitted for the test_actor again */
  g_assert (!stage_views_changed_container);
  g_assert (stage_views_changed_test_actor);
  stage_views_changed_test_actor = FALSE;

  /* Move the container out of the stage... */
  clutter_actor_set_y (container, 2000);
  g_object_ref (test_actor);
  clutter_actor_remove_child (stage, test_actor);

  /* When the test_actor left the stage, the signal was emitted */
  g_assert (!stage_views_changed_container);
  g_assert (stage_views_changed_test_actor);
  stage_views_changed_test_actor = FALSE;

  /* ...and reparent the test_actor to the container again */
  clutter_actor_add_child (container, test_actor);
  g_object_unref (test_actor);

  wait_for_paint (stage);

  /* Now both actors are on no stage views */
  is_on_stage_views (container, 0);
  is_on_stage_views (test_actor, 0);

  /* The signal was emitted only for the container, the test_actor already
   * has no stage-views.
   */
  g_assert (stage_views_changed_container);
  g_assert (!stage_views_changed_test_actor);

  g_signal_handlers_disconnect_by_func (container, on_stage_views_changed,
                                        stage_views_changed_container_ptr);
  g_signal_handlers_disconnect_by_func (test_actor, on_stage_views_changed,
                                        stage_views_changed_test_actor_ptr);
  clutter_actor_destroy (container);
}

static void
meta_test_actor_stage_views_hide_parent (void)
{
  MetaBackend *backend = meta_get_backend ();
  ClutterActor *stage, *outer_container, *inner_container, *test_actor;
  GList *stage_views;
  gboolean stage_views_changed_outer_container = FALSE;
  gboolean stage_views_changed_inner_container = FALSE;
  gboolean stage_views_changed_test_actor = FALSE;
  gboolean *stage_views_changed_outer_container_ptr =
    &stage_views_changed_outer_container;
  gboolean *stage_views_changed_inner_container_ptr =
    &stage_views_changed_inner_container;
  gboolean *stage_views_changed_test_actor_ptr =
    &stage_views_changed_test_actor;

  stage = meta_backend_get_stage (backend);
  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));

  outer_container = clutter_actor_new ();
  clutter_actor_add_child (stage, outer_container);

  inner_container = clutter_actor_new ();
  clutter_actor_add_child (outer_container, inner_container);

  test_actor = clutter_actor_new ();
  clutter_actor_set_size (test_actor, 20, 20);
  clutter_actor_add_child (inner_container, test_actor);

  g_signal_connect (outer_container, "stage-views-changed",
                    G_CALLBACK (on_stage_views_changed),
                    stage_views_changed_outer_container_ptr);
  g_signal_connect (inner_container, "stage-views-changed",
                    G_CALLBACK (on_stage_views_changed),
                    stage_views_changed_inner_container_ptr);
  g_signal_connect (test_actor, "stage-views-changed",
                    G_CALLBACK (on_stage_views_changed),
                    stage_views_changed_test_actor_ptr);

  clutter_actor_show (stage);

  wait_for_paint (stage);

  /* The containers and the test_actor are on all on the first view */
  is_on_stage_views (outer_container, 1, stage_views->data);
  is_on_stage_views (inner_container, 1, stage_views->data);
  is_on_stage_views (test_actor, 1, stage_views->data);

  /* The signal was emitted for all three */
  g_assert (stage_views_changed_outer_container);
  g_assert (stage_views_changed_inner_container);
  g_assert (stage_views_changed_test_actor);
  stage_views_changed_outer_container = FALSE;
  stage_views_changed_inner_container = FALSE;
  stage_views_changed_test_actor = FALSE;

  /* Hide the inner_container */
  clutter_actor_hide (inner_container);

  /* Move the outer_container so it's still on the first view */
  clutter_actor_set_x (outer_container, 1023);

  wait_for_paint (stage);

  /* The outer_container is still expanded so it should be on both views */
  is_on_stage_views (outer_container, 2,
                     stage_views->data, stage_views->next->data);

  /* The inner_container and test_actor aren't updated because they're hidden */
  is_on_stage_views (inner_container, 1, stage_views->data);
  is_on_stage_views (test_actor, 1, stage_views->data);

  /* The signal was emitted for the outer_container */
  g_assert (stage_views_changed_outer_container);
  g_assert (!stage_views_changed_inner_container);
  g_assert (!stage_views_changed_test_actor);
  stage_views_changed_outer_container = FALSE;

  /* Show the inner_container again */
  clutter_actor_show (inner_container);

  wait_for_paint (stage);

  /* All actors are on both views now */
  is_on_stage_views (outer_container, 2,
                     stage_views->data, stage_views->next->data);
  is_on_stage_views (inner_container, 2,
                     stage_views->data, stage_views->next->data);
  is_on_stage_views (test_actor, 2,
                     stage_views->data, stage_views->next->data);

  /* The signal was emitted for the inner_container and test_actor */
  g_assert (!stage_views_changed_outer_container);
  g_assert (stage_views_changed_inner_container);
  g_assert (stage_views_changed_test_actor);

  g_signal_handlers_disconnect_by_func (outer_container, on_stage_views_changed,
                                        stage_views_changed_outer_container_ptr);
  g_signal_handlers_disconnect_by_func (inner_container, on_stage_views_changed,
                                        stage_views_changed_inner_container_ptr);
  g_signal_handlers_disconnect_by_func (test_actor, on_stage_views_changed,
                                        stage_views_changed_test_actor_ptr);
  clutter_actor_destroy (outer_container);
}

static void
init_tests (int argc, char **argv)
{
  MetaMonitorTestSetup *test_setup;

  test_setup = create_monitor_test_setup (&initial_test_case_setup,
                                          MONITOR_TEST_FLAG_NO_STORED);

  meta_monitor_manager_test_init_test_setup (test_setup);

  g_test_add_func ("/stage-view/stage-views-exist",
                   meta_test_stage_views_exist);
  g_test_add_func ("/stage-views/actor-stage-views",
                   meta_test_actor_stage_views);
  g_test_add_func ("/stage-views/actor-stage-views-reparent",
                   meta_test_actor_stage_views_reparent);
  g_test_add_func ("/stage-views/actor-stage-views-hide-parent",
                   meta_test_actor_stage_views_hide_parent);
}

int
main (int argc, char *argv[])
{
  test_init (&argc, &argv);
  init_tests (argc, argv);

  meta_plugin_manager_load (test_get_plugin_name ());

  meta_override_compositor_configuration (META_COMPOSITOR_TYPE_WAYLAND,
                                          META_TYPE_BACKEND_TEST);

  meta_init ();
  meta_register_with_session ();

  g_test_log_set_fatal_handler (ignore_frame_counter_warning, NULL);

  g_idle_add (run_tests, NULL);

  return meta_run ();
}
