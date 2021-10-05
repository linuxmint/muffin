/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat, Inc.
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

#include "tests/monitor-unit-tests.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-crtc.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor.h"
#include "backends/meta-monitor-config-migration.h"
#include "backends/meta-monitor-config-store.h"
#include "backends/meta-output.h"
#include "core/window-private.h"
#include "meta-backend-test.h"
#include "tests/meta-monitor-manager-test.h"
#include "tests/monitor-test-utils.h"
#include "tests/test-utils.h"
#include "x11/meta-x11-display-private.h"

#define ALL_TRANSFORMS ((1 << (META_MONITOR_TRANSFORM_FLIPPED_270 + 1)) - 1)

#define MAX_N_MODES 10
#define MAX_N_OUTPUTS 10
#define MAX_N_CRTCS 10
#define MAX_N_MONITORS 10
#define MAX_N_LOGICAL_MONITORS 10

/*
 * The following structures are used to define test cases.
 *
 * Each test case consists of a test case setup and a test case expectaction.
 * and a expected result, consisting
 * of an array of monitors, logical monitors and a screen size.
 *
 * TEST CASE SETUP:
 *
 * A test case setup consists of an array of modes, an array of outputs and an
 * array of CRTCs.
 *
 * A mode has a width and height in pixels, and a refresh rate in updates per
 * second.
 *
 * An output has an array of available modes, and a preferred mode. Modes are
 * defined as indices into the modes array of the test case setup.
 *
 * It also has CRTc and an array of possible CRTCs. Crtcs are defined as indices
 * into the CRTC array. The CRTC value -1 means no CRTC.
 *
 * It also has various meta data, such as physical dimension, tile info and
 * scale.
 *
 * A CRTC only has a current mode. A mode is defined as an index into the modes
 * array.
 *
 *
 * TEST CASE EXPECTS:
 *
 * A test case expects consists of an array of monitors, an array of logical
 * monitors, a output and crtc count, and a screen width.
 *
 * A monitor represents a physical monitor (such as an external monitor, or a
 * laptop panel etc). A monitor consists of an array of outputs, defined by
 * indices into the setup output array, an array of monitor modes, and the
 * current mode, defined by an index into the monitor modes array, and the
 * physical dimensions.
 *
 * A logical monitor represents a region of the total screen area. It contains
 * the expected layout and a scale.
 */

typedef enum _MonitorTestFlag
{
  MONITOR_TEST_FLAG_NONE,
  MONITOR_TEST_FLAG_NO_STORED
} MonitorTestFlag;

typedef struct _MonitorTestCaseMode
{
  int width;
  int height;
  float refresh_rate;
  MetaCrtcModeFlag flags;
} MonitorTestCaseMode;

typedef struct _MonitorTestCaseOutput
{
  int crtc;
  int modes[MAX_N_MODES];
  int n_modes;
  int preferred_mode;
  int possible_crtcs[MAX_N_CRTCS];
  int n_possible_crtcs;
  int width_mm;
  int height_mm;
  MetaTileInfo tile_info;
  float scale;
  gboolean is_laptop_panel;
  gboolean is_underscanning;
  const char *serial;
  MetaMonitorTransform panel_orientation_transform;
} MonitorTestCaseOutput;

typedef struct _MonitorTestCaseCrtc
{
  int current_mode;
} MonitorTestCaseCrtc;

typedef struct _MonitorTestCaseSetup
{
  MonitorTestCaseMode modes[MAX_N_MODES];
  int n_modes;

  MonitorTestCaseOutput outputs[MAX_N_OUTPUTS];
  int n_outputs;

  MonitorTestCaseCrtc crtcs[MAX_N_CRTCS];
  int n_crtcs;
} MonitorTestCaseSetup;

typedef struct _MonitorTestCaseMonitorCrtcMode
{
  uint64_t output;
  int crtc_mode;
} MetaTestCaseMonitorCrtcMode;

typedef struct _MonitorTestCaseMonitorMode
{
  int width;
  int height;
  float refresh_rate;
  MetaCrtcModeFlag flags;
  MetaTestCaseMonitorCrtcMode crtc_modes[MAX_N_CRTCS];
} MetaMonitorTestCaseMonitorMode;

typedef struct _MonitorTestCaseMonitor
{
  uint64_t outputs[MAX_N_OUTPUTS];
  int n_outputs;
  MetaMonitorTestCaseMonitorMode modes[MAX_N_MODES];
  int n_modes;
  int current_mode;
  int width_mm;
  int height_mm;
  gboolean is_underscanning;
} MonitorTestCaseMonitor;

typedef struct _MonitorTestCaseLogicalMonitor
{
  MetaRectangle layout;
  float scale;
  int monitors[MAX_N_MONITORS];
  int n_monitors;
  MetaMonitorTransform transform;
} MonitorTestCaseLogicalMonitor;

typedef struct _MonitorTestCaseCrtcExpect
{
  MetaMonitorTransform transform;
  int current_mode;
  float x;
  float y;
} MonitorTestCaseCrtcExpect;

typedef struct _MonitorTestCaseExpect
{
  MonitorTestCaseMonitor monitors[MAX_N_MONITORS];
  int n_monitors;
  MonitorTestCaseLogicalMonitor logical_monitors[MAX_N_LOGICAL_MONITORS];
  int n_logical_monitors;
  int primary_logical_monitor;
  int n_outputs;
  MonitorTestCaseCrtcExpect crtcs[MAX_N_CRTCS];
  int n_crtcs;
  int n_tiled_monitors;
  int screen_width;
  int screen_height;
} MonitorTestCaseExpect;

typedef struct _MonitorTestCase
{
  MonitorTestCaseSetup setup;
  MonitorTestCaseExpect expect;
} MonitorTestCase;

static MonitorTestCase initial_test_case = {
  .setup = {
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
  },

  .expect = {
    .monitors = {
      {
        .outputs = { 0 },
        .n_outputs = 1,
        .modes = {
          {
            .width = 1024,
            .height = 768,
            .refresh_rate = 60.0,
            .crtc_modes = {
              {
                .output = 0,
                .crtc_mode = 0
              }
            }
          }
        },
        .n_modes = 1,
        .current_mode = 0,
        .width_mm = 222,
        .height_mm = 125
      },
      {
        .outputs = { 1 },
        .n_outputs = 1,
        .modes = {
          {
            .width = 1024,
            .height = 768,
            .refresh_rate = 60.0,
            .crtc_modes = {
              {
                .output = 1,
                .crtc_mode = 0
              }
            }
          }
        },
        .n_modes = 1,
        .current_mode = 0,
        .width_mm = 220,
        .height_mm = 124
      }
    },
    .n_monitors = 2,
    .logical_monitors = {
      {
        .monitors = { 0 },
        .n_monitors = 1,
        .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
        .scale = 1
      },
      {
        .monitors = { 1 },
        .n_monitors = 1,
        .layout = { .x = 1024, .y = 0, .width = 1024, .height = 768 },
        .scale = 1
      }
    },
    .n_logical_monitors = 2,
    .primary_logical_monitor = 0,
    .n_outputs = 2,
    .crtcs = {
      {
        .current_mode = 0,
      },
      {
        .current_mode = 0,
        .x = 1024,
      }
    },
    .n_crtcs = 2,
    .screen_width = 1024 * 2,
    .screen_height = 768
  }
};

static TestClient *wayland_monitor_test_client = NULL;
static TestClient *x11_monitor_test_client = NULL;

#define WAYLAND_TEST_CLIENT_NAME "wayland_monitor_test_client"
#define WAYLAND_TEST_CLIENT_WINDOW "window1"
#define X11_TEST_CLIENT_NAME "x11_monitor_test_client"
#define X11_TEST_CLIENT_WINDOW "window1"

static gboolean
monitor_tests_alarm_filter (MetaX11Display        *x11_display,
                            XSyncAlarmNotifyEvent *event,
                            gpointer               data)
{
  return test_client_alarm_filter (x11_display, event, x11_monitor_test_client);
}

static void
create_monitor_test_clients (void)
{
  GError *error = NULL;

  test_wait_for_x11_display ();

  meta_x11_display_set_alarm_filter (meta_get_display ()->x11_display,
                                     monitor_tests_alarm_filter, NULL);

  wayland_monitor_test_client = test_client_new (WAYLAND_TEST_CLIENT_NAME,
                                                 META_WINDOW_CLIENT_TYPE_WAYLAND,
                                                 &error);
  if (!wayland_monitor_test_client)
    g_error ("Failed to launch Wayland test client: %s", error->message);

  x11_monitor_test_client = test_client_new (X11_TEST_CLIENT_NAME,
                                             META_WINDOW_CLIENT_TYPE_X11,
                                             &error);
  if (!x11_monitor_test_client)
    g_error ("Failed to launch X11 test client: %s", error->message);

  if (!test_client_do (wayland_monitor_test_client, &error,
                       "create", WAYLAND_TEST_CLIENT_WINDOW,
                       NULL))
    g_error ("Failed to create Wayland window: %s", error->message);

  if (!test_client_do (x11_monitor_test_client, &error,
                       "create", X11_TEST_CLIENT_WINDOW,
                       NULL))
    g_error ("Failed to create X11 window: %s", error->message);

  if (!test_client_do (wayland_monitor_test_client, &error,
                       "show", WAYLAND_TEST_CLIENT_WINDOW,
                       NULL))
    g_error ("Failed to show the window: %s", error->message);

  if (!test_client_do (x11_monitor_test_client, &error,
                       "show", X11_TEST_CLIENT_WINDOW,
                       NULL))
    g_error ("Failed to show the window: %s", error->message);
}

static void
check_test_client_state (TestClient *test_client)
{
  GError *error = NULL;

  if (!test_client_wait (test_client, &error))
    {
      g_error ("Failed to sync test client '%s': %s",
               test_client_get_id (test_client), error->message);
    }
}

static void
check_monitor_test_clients_state (void)
{
  check_test_client_state (wayland_monitor_test_client);
  check_test_client_state (x11_monitor_test_client);
}

static void
destroy_monitor_test_clients (void)
{
  GError *error = NULL;

  if (!test_client_quit (wayland_monitor_test_client, &error))
    g_error ("Failed to quit Wayland test client: %s", error->message);

  if (!test_client_quit (x11_monitor_test_client, &error))
    g_error ("Failed to quit X11 test client: %s", error->message);

  test_client_destroy (wayland_monitor_test_client);
  test_client_destroy (x11_monitor_test_client);

  meta_x11_display_set_alarm_filter (meta_get_display ()->x11_display,
                                     NULL, NULL);
}

static MetaOutput *
output_from_winsys_id (MetaBackend *backend,
                       uint64_t     winsys_id)
{
  MetaGpu *gpu = meta_backend_test_get_gpu (META_BACKEND_TEST (backend));
  GList *l;

  for (l = meta_gpu_get_outputs (gpu); l; l = l->next)
    {
      MetaOutput *output = l->data;

      if (output->winsys_id == winsys_id)
        return output;
    }

  return NULL;
}

typedef struct _CheckMonitorModeData
{
  MetaBackend *backend;
  MetaTestCaseMonitorCrtcMode *expect_crtc_mode_iter;
} CheckMonitorModeData;

static gboolean
check_monitor_mode (MetaMonitor         *monitor,
                    MetaMonitorMode     *mode,
                    MetaMonitorCrtcMode *monitor_crtc_mode,
                    gpointer             user_data,
                    GError             **error)
{
  CheckMonitorModeData *data = user_data;
  MetaBackend *backend = data->backend;
  MetaOutput *output;
  MetaCrtcMode *crtc_mode;
  int expect_crtc_mode_index;

  output = output_from_winsys_id (backend,
                                  data->expect_crtc_mode_iter->output);
  g_assert (monitor_crtc_mode->output == output);

  expect_crtc_mode_index = data->expect_crtc_mode_iter->crtc_mode;
  if (expect_crtc_mode_index == -1)
    {
      crtc_mode = NULL;
    }
  else
    {
      MetaGpu *gpu = meta_output_get_gpu (output);

      crtc_mode = g_list_nth_data (meta_gpu_get_modes (gpu),
                                   expect_crtc_mode_index);
    }
  g_assert (monitor_crtc_mode->crtc_mode == crtc_mode);

  if (crtc_mode)
    {
      float refresh_rate;
      MetaCrtcModeFlag flags;

      refresh_rate = meta_monitor_mode_get_refresh_rate (mode);
      flags = meta_monitor_mode_get_flags (mode);

      g_assert_cmpfloat (refresh_rate, ==, crtc_mode->refresh_rate);
      g_assert_cmpint (flags, ==, (crtc_mode->flags & HANDLED_CRTC_MODE_FLAGS));
    }

  data->expect_crtc_mode_iter++;

  return TRUE;
}

static gboolean
check_current_monitor_mode (MetaMonitor         *monitor,
                            MetaMonitorMode     *mode,
                            MetaMonitorCrtcMode *monitor_crtc_mode,
                            gpointer             user_data,
                            GError             **error)
{
  CheckMonitorModeData *data = user_data;
  MetaBackend *backend = data->backend;
  MetaOutput *output;
  MetaCrtc *crtc;

  output = output_from_winsys_id (backend,
                                  data->expect_crtc_mode_iter->output);
  crtc = meta_output_get_assigned_crtc (output);

  if (data->expect_crtc_mode_iter->crtc_mode == -1)
    {
      g_assert_null (crtc);
    }
  else
    {
      MetaCrtcConfig *crtc_config;
      MetaLogicalMonitor *logical_monitor;

      g_assert_nonnull (crtc);

      crtc_config  = crtc->config;
      g_assert_nonnull (crtc_config);

      g_assert (monitor_crtc_mode->crtc_mode == crtc_config->mode);

      logical_monitor = meta_monitor_get_logical_monitor (monitor);
      g_assert_nonnull (logical_monitor);
    }


  data->expect_crtc_mode_iter++;

  return TRUE;
}

static MetaLogicalMonitor *
logical_monitor_from_layout (MetaMonitorManager *monitor_manager,
                             MetaRectangle      *layout)
{
  GList *l;

  for (l = monitor_manager->logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;

      if (meta_rectangle_equal (layout, &logical_monitor->rect))
        return logical_monitor;
    }

  return NULL;
}

static void
check_logical_monitor (MonitorTestCase               *test_case,
                       MetaMonitorManager            *monitor_manager,
                       MonitorTestCaseLogicalMonitor *test_logical_monitor)
{
  MetaLogicalMonitor *logical_monitor;
  MetaOutput *primary_output;
  GList *monitors;
  GList *l;
  int i;

  logical_monitor = logical_monitor_from_layout (monitor_manager,
                                                 &test_logical_monitor->layout);
  g_assert_nonnull (logical_monitor);

  g_assert_cmpint (logical_monitor->rect.x,
                   ==,
                   test_logical_monitor->layout.x);
  g_assert_cmpint (logical_monitor->rect.y,
                   ==,
                   test_logical_monitor->layout.y);
  g_assert_cmpint (logical_monitor->rect.width,
                   ==,
                   test_logical_monitor->layout.width);
  g_assert_cmpint (logical_monitor->rect.height,
                   ==,
                   test_logical_monitor->layout.height);
  g_assert_cmpfloat (logical_monitor->scale,
                     ==,
                     test_logical_monitor->scale);
  g_assert_cmpuint (logical_monitor->transform,
                    ==,
                    test_logical_monitor->transform);

  if (logical_monitor == monitor_manager->primary_logical_monitor)
    g_assert (meta_logical_monitor_is_primary (logical_monitor));

  primary_output = NULL;
  monitors = meta_logical_monitor_get_monitors (logical_monitor);
  g_assert_cmpint ((int) g_list_length (monitors),
                   ==,
                   test_logical_monitor->n_monitors);

  for (i = 0; i < test_logical_monitor->n_monitors; i++)
    {
      MetaMonitor *monitor =
        g_list_nth (monitor_manager->monitors,
                    test_logical_monitor->monitors[i])->data;

      g_assert_nonnull (g_list_find (monitors, monitor));
    }

  for (l = monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;
      GList *outputs;
      GList *l_output;

      outputs = meta_monitor_get_outputs (monitor);
      for (l_output = outputs; l_output; l_output = l_output->next)
        {
          MetaOutput *output = l_output->data;
          MetaCrtc *crtc;

          if (output->is_primary)
            {
              g_assert_null (primary_output);
              primary_output = output;
            }

          crtc = meta_output_get_assigned_crtc (output);
          g_assert (!crtc ||
                    meta_monitor_get_logical_monitor (monitor) == logical_monitor);
          g_assert_cmpint (logical_monitor->is_presentation,
                           ==,
                           output->is_presentation);
        }
    }

  if (logical_monitor == monitor_manager->primary_logical_monitor)
    g_assert_nonnull (primary_output);
}

static void
check_monitor_configuration (MonitorTestCase *test_case)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MetaGpu *gpu = meta_backend_test_get_gpu (META_BACKEND_TEST (backend));
  int tiled_monitor_count;
  GList *monitors;
  GList *crtcs;
  int n_logical_monitors;
  GList *l;
  int i;

  g_assert_cmpint (monitor_manager->screen_width,
                   ==,
                   test_case->expect.screen_width);
  g_assert_cmpint (monitor_manager->screen_height,
                   ==,
                   test_case->expect.screen_height);
  g_assert_cmpint ((int) g_list_length (meta_gpu_get_outputs (gpu)),
                   ==,
                   test_case->expect.n_outputs);
  g_assert_cmpint ((int) g_list_length (meta_gpu_get_crtcs (gpu)),
                   ==,
                   test_case->expect.n_crtcs);

  tiled_monitor_count =
    meta_monitor_manager_test_get_tiled_monitor_count (monitor_manager_test);
  g_assert_cmpint (tiled_monitor_count,
                   ==,
                   test_case->expect.n_tiled_monitors);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpint ((int) g_list_length (monitors),
                   ==,
                   test_case->expect.n_monitors);
  for (l = monitors, i = 0; l; l = l->next, i++)
    {
      MetaMonitor *monitor = l->data;
      GList *outputs;
      GList *l_output;
      int j;
      int width_mm, height_mm;
      GList *modes;
      GList *l_mode;
      MetaMonitorMode *current_mode;
      int expected_current_mode_index;
      MetaMonitorMode *expected_current_mode;

      outputs = meta_monitor_get_outputs (monitor);

      g_assert_cmpint ((int) g_list_length (outputs),
                       ==,
                       test_case->expect.monitors[i].n_outputs);

      for (l_output = outputs, j = 0; l_output; l_output = l_output->next, j++)
        {
          MetaOutput *output = l_output->data;
          uint64_t winsys_id = test_case->expect.monitors[i].outputs[j];

          g_assert (output == output_from_winsys_id (backend, winsys_id));
          g_assert_cmpint (test_case->expect.monitors[i].is_underscanning,
                           ==,
                           output->is_underscanning);
        }

      meta_monitor_get_physical_dimensions (monitor, &width_mm, &height_mm);
      g_assert_cmpint (width_mm,
                       ==,
                       test_case->expect.monitors[i].width_mm);
      g_assert_cmpint (height_mm,
                       ==,
                       test_case->expect.monitors[i].height_mm);

      modes = meta_monitor_get_modes (monitor);
      g_assert_cmpint (g_list_length (modes),
                       ==,
                       test_case->expect.monitors[i].n_modes);

      for (l_mode = modes, j = 0; l_mode; l_mode = l_mode->next, j++)
        {
          MetaMonitorMode *mode = l_mode->data;
          int width;
          int height;
          float refresh_rate;
          MetaCrtcModeFlag flags;
          CheckMonitorModeData data;

          meta_monitor_mode_get_resolution (mode, &width, &height);
          refresh_rate = meta_monitor_mode_get_refresh_rate (mode);
          flags = meta_monitor_mode_get_flags (mode);

          g_assert_cmpint (width,
                           ==,
                           test_case->expect.monitors[i].modes[j].width);
          g_assert_cmpint (height,
                           ==,
                           test_case->expect.monitors[i].modes[j].height);
          g_assert_cmpfloat (refresh_rate,
                             ==,
                             test_case->expect.monitors[i].modes[j].refresh_rate);
          g_assert_cmpint (flags,
                           ==,
                           test_case->expect.monitors[i].modes[j].flags);

          data = (CheckMonitorModeData) {
            .backend = backend,
            .expect_crtc_mode_iter =
              test_case->expect.monitors[i].modes[j].crtc_modes
          };
          meta_monitor_mode_foreach_output (monitor, mode,
                                            check_monitor_mode,
                                            &data,
                                            NULL);
        }

      current_mode = meta_monitor_get_current_mode (monitor);
      expected_current_mode_index = test_case->expect.monitors[i].current_mode;
      if (expected_current_mode_index == -1)
        expected_current_mode = NULL;
      else
        expected_current_mode = g_list_nth (modes,
                                            expected_current_mode_index)->data;

      g_assert (current_mode == expected_current_mode);
      if (current_mode)
        g_assert (meta_monitor_is_active (monitor));
      else
        g_assert (!meta_monitor_is_active (monitor));

      if (current_mode)
        {
          CheckMonitorModeData data;

          data = (CheckMonitorModeData) {
            .backend = backend,
            .expect_crtc_mode_iter =
              test_case->expect.monitors[i].modes[expected_current_mode_index].crtc_modes
          };
          meta_monitor_mode_foreach_output (monitor, expected_current_mode,
                                            check_current_monitor_mode,
                                            &data,
                                            NULL);
        }

      meta_monitor_derive_current_mode (monitor);
      g_assert (current_mode == meta_monitor_get_current_mode (monitor));
    }

  n_logical_monitors =
    meta_monitor_manager_get_num_logical_monitors (monitor_manager);
  g_assert_cmpint (n_logical_monitors,
                   ==,
                   test_case->expect.n_logical_monitors);

  /*
   * Check that we have a primary logical monitor (except for headless),
   * and that the main output of the first monitor is the only output
   * that is marked as primary (further below). Note: outputs being primary or
   * not only matters on X11.
   */
  if (test_case->expect.primary_logical_monitor == -1)
    {
      g_assert_null (monitor_manager->primary_logical_monitor);
      g_assert_null (monitor_manager->logical_monitors);
    }
  else
    {
      MonitorTestCaseLogicalMonitor *test_logical_monitor =
        &test_case->expect.logical_monitors[test_case->expect.primary_logical_monitor];
      MetaLogicalMonitor *logical_monitor;

      logical_monitor =
        logical_monitor_from_layout (monitor_manager,
                                     &test_logical_monitor->layout);
      g_assert (logical_monitor == monitor_manager->primary_logical_monitor);
    }

  for (i = 0; i < test_case->expect.n_logical_monitors; i++)
    {
      MonitorTestCaseLogicalMonitor *test_logical_monitor =
        &test_case->expect.logical_monitors[i];

      check_logical_monitor (test_case, monitor_manager, test_logical_monitor);
    }
  g_assert_cmpint (n_logical_monitors, ==, i);

  crtcs = meta_gpu_get_crtcs (gpu);
  for (l = crtcs, i = 0; l; l = l->next, i++)
    {
      MetaCrtc *crtc = l->data;
      MetaCrtcConfig *crtc_config = crtc->config;

      if (test_case->expect.crtcs[i].current_mode == -1)
        {
          g_assert_null (crtc_config);
        }
      else
        {
          MetaCrtcMode *expected_current_mode;

          g_assert_nonnull (crtc_config);

          expected_current_mode =
            g_list_nth_data (meta_gpu_get_modes (gpu),
                             test_case->expect.crtcs[i].current_mode);
          g_assert (crtc_config->mode == expected_current_mode);

          g_assert_cmpuint (crtc_config->transform,
                            ==,
                            test_case->expect.crtcs[i].transform);

          g_assert_cmpfloat_with_epsilon (crtc_config->layout.origin.x,
                                          test_case->expect.crtcs[i].x,
                                          FLT_EPSILON);
          g_assert_cmpfloat_with_epsilon (crtc_config->layout.origin.y,
                                          test_case->expect.crtcs[i].y,
                                          FLT_EPSILON);
        }
    }

  check_monitor_test_clients_state ();
}

static void
meta_output_test_destroy_notify (MetaOutput *output)
{
  g_clear_pointer (&output->driver_private, g_free);
}

static MetaMonitorTestSetup *
create_monitor_test_setup (MonitorTestCase *test_case,
                           MonitorTestFlag  flags)
{
  MetaMonitorTestSetup *test_setup;
  int i;
  int n_laptop_panels = 0;
  int n_normal_panels = 0;
  gboolean hotplug_mode_update;

  if (flags & MONITOR_TEST_FLAG_NO_STORED)
    hotplug_mode_update = TRUE;
  else
    hotplug_mode_update = FALSE;

  test_setup = g_new0 (MetaMonitorTestSetup, 1);

  test_setup->modes = NULL;
  for (i = 0; i < test_case->setup.n_modes; i++)
    {
      MetaCrtcMode *mode;

      mode = g_object_new (META_TYPE_CRTC_MODE, NULL);
      mode->mode_id = i;
      mode->width = test_case->setup.modes[i].width;
      mode->height = test_case->setup.modes[i].height;
      mode->refresh_rate = test_case->setup.modes[i].refresh_rate;
      mode->flags = test_case->setup.modes[i].flags;

      test_setup->modes = g_list_append (test_setup->modes, mode);
    }

  test_setup->crtcs = NULL;
  for (i = 0; i < test_case->setup.n_crtcs; i++)
    {
      MetaCrtc *crtc;

      crtc = g_object_new (META_TYPE_CRTC, NULL);
      crtc->crtc_id = i + 1;
      crtc->all_transforms = ALL_TRANSFORMS;

      test_setup->crtcs = g_list_append (test_setup->crtcs, crtc);
    }

  test_setup->outputs = NULL;
  for (i = 0; i < test_case->setup.n_outputs; i++)
    {
      MetaOutput *output;
      MetaOutputTest *output_test;
      int crtc_index;
      MetaCrtc *crtc;
      int preferred_mode_index;
      MetaCrtcMode *preferred_mode;
      MetaCrtcMode **modes;
      int n_modes;
      int j;
      MetaCrtc **possible_crtcs;
      int n_possible_crtcs;
      int scale;
      gboolean is_laptop_panel;
      const char *serial;

      crtc_index = test_case->setup.outputs[i].crtc;
      if (crtc_index == -1)
        crtc = NULL;
      else
        crtc = g_list_nth_data (test_setup->crtcs, crtc_index);

      preferred_mode_index = test_case->setup.outputs[i].preferred_mode;
      if (preferred_mode_index == -1)
        preferred_mode = NULL;
      else
        preferred_mode = g_list_nth_data (test_setup->modes,
                                          preferred_mode_index);

      n_modes = test_case->setup.outputs[i].n_modes;
      modes = g_new0 (MetaCrtcMode *, n_modes);
      for (j = 0; j < n_modes; j++)
        {
          int mode_index;

          mode_index = test_case->setup.outputs[i].modes[j];
          modes[j] = g_list_nth_data (test_setup->modes, mode_index);
        }

      n_possible_crtcs = test_case->setup.outputs[i].n_possible_crtcs;
      possible_crtcs = g_new0 (MetaCrtc *, n_possible_crtcs);
      for (j = 0; j < n_possible_crtcs; j++)
        {
          int possible_crtc_index;

          possible_crtc_index = test_case->setup.outputs[i].possible_crtcs[j];
          possible_crtcs[j] = g_list_nth_data (test_setup->crtcs,
                                               possible_crtc_index);
        }

      output_test = g_new0 (MetaOutputTest, 1);

      scale = test_case->setup.outputs[i].scale;
      if (scale < 1)
        scale = 1;

      *output_test = (MetaOutputTest) {
        .scale = scale
      };

      is_laptop_panel = test_case->setup.outputs[i].is_laptop_panel;

      serial = test_case->setup.outputs[i].serial;
      if (!serial)
        serial = "0x123456";

      output = g_object_new (META_TYPE_OUTPUT, NULL);

      if (crtc)
        meta_output_assign_crtc (output, crtc);
      output->winsys_id = i;
      output->name = (is_laptop_panel ? g_strdup_printf ("eDP-%d",
                                                  ++n_laptop_panels)
                               : g_strdup_printf ("DP-%d",
                                                  ++n_normal_panels));
      output->vendor = g_strdup ("MetaProduct's Inc.");
      output->product = g_strdup ("MetaMonitor");
      output->serial = g_strdup (serial);
      output->suggested_x = -1;
      output->suggested_y = -1;
      output->hotplug_mode_update = hotplug_mode_update;
      output->width_mm = test_case->setup.outputs[i].width_mm;
      output->height_mm = test_case->setup.outputs[i].height_mm;
      output->subpixel_order = COGL_SUBPIXEL_ORDER_UNKNOWN;
      output->preferred_mode = preferred_mode;
      output->n_modes = n_modes;
      output->modes = modes;
      output->n_possible_crtcs = n_possible_crtcs;
      output->possible_crtcs = possible_crtcs;
      output->n_possible_clones = 0;
      output->possible_clones = NULL;
      output->backlight = -1;
      output->connector_type = (is_laptop_panel ? META_CONNECTOR_TYPE_eDP
                                         : META_CONNECTOR_TYPE_DisplayPort);
      output->tile_info = test_case->setup.outputs[i].tile_info;
      output->is_underscanning = test_case->setup.outputs[i].is_underscanning;
      output->panel_orientation_transform =
        test_case->setup.outputs[i].panel_orientation_transform;
      output->driver_private = output_test;
      output->driver_notify = (GDestroyNotify) meta_output_test_destroy_notify;

      test_setup->outputs = g_list_append (test_setup->outputs, output);
    }

  return test_setup;
}

static void
meta_test_monitor_initial_linear_config (void)
{
  check_monitor_configuration (&initial_test_case);
}

static void
emulate_hotplug (MetaMonitorTestSetup *test_setup)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);

  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);
  g_usleep (G_USEC_PER_SEC / 100);
}

static void
meta_test_monitor_one_disconnected_linear_config (void)
{
  MonitorTestCase test_case = initial_test_case;
  MetaMonitorTestSetup *test_setup;

  test_case.setup.n_outputs = 1;

  test_case.expect = (MonitorTestCaseExpect) {
    .monitors = {
      {
        .outputs = { 0 },
        .n_outputs = 1,
        .modes = {
          {
            .width = 1024,
            .height = 768,
            .refresh_rate = 60.0,
            .crtc_modes = {
              {
                .output = 0,
                .crtc_mode = 0
              }
            }
          }
        },
        .n_modes = 1,
        .current_mode = 0,
        .width_mm = 222,
        .height_mm = 125
      }
    },
    .n_monitors = 1,
    .logical_monitors = {
      {
        .monitors = { 0 },
        .n_monitors = 1,
        .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
        .scale = 1
      },
    },
    .n_logical_monitors = 1,
    .primary_logical_monitor = 0,
    .n_outputs = 1,
    .crtcs = {
      {
        .current_mode = 0,
      },
      {
        .current_mode = -1,
      }
    },
    .n_crtcs = 2,
    .screen_width = 1024,
    .screen_height = 768
  };

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_one_off_linear_config (void)
{
  MonitorTestCase test_case;
  MetaMonitorTestSetup *test_setup;
  MonitorTestCaseOutput outputs[] = {
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
      .crtc = -1,
      .modes = { 0 },
      .n_modes = 1,
      .preferred_mode = 0,
      .possible_crtcs = { 1 },
      .n_possible_crtcs = 1,
      .width_mm = 224,
      .height_mm = 126
    }
  };

  test_case = initial_test_case;

  memcpy (&test_case.setup.outputs, &outputs, sizeof (outputs));
  test_case.setup.n_outputs = G_N_ELEMENTS (outputs);

  test_case.setup.crtcs[1].current_mode = -1;

  test_case.expect = (MonitorTestCaseExpect) {
    .monitors = {
      {
        .outputs = { 0 },
        .n_outputs = 1,
        .modes = {
          {
            .width = 1024,
            .height = 768,
            .refresh_rate = 60.0,
            .crtc_modes = {
              {
                .output = 0,
                .crtc_mode = 0
              }
            }
          }
        },
        .n_modes = 1,
        .current_mode = 0,
        .width_mm = 222,
        .height_mm = 125
      },
      {
        .outputs = { 1 },
        .n_outputs = 1,
        .modes = {
          {
            .width = 1024,
            .height = 768,
            .refresh_rate = 60.0,
            .crtc_modes = {
              {
                .output = 1,
                .crtc_mode = 0
              }
            }
          }
        },
        .n_modes = 1,
        .current_mode = 0,
        .width_mm = 224,
        .height_mm = 126
      }
    },
    .n_monitors = 2,
    .logical_monitors = {
      {
        .monitors = { 0 },
        .n_monitors = 1,
        .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
        .scale = 1
      },
      {
        .monitors = { 1 },
        .n_monitors = 1,
        .layout = { .x = 1024, .y = 0, .width = 1024, .height = 768 },
        .scale = 1
      },
    },
    .n_logical_monitors = 2,
    .primary_logical_monitor = 0,
    .n_outputs = 2,
    .crtcs = {
      {
        .current_mode = 0,
      },
      {
        .current_mode = 0,
        .x = 1024,
      }
    },
    .n_crtcs = 2,
    .screen_width = 1024 * 2,
    .screen_height = 768
  };

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_preferred_linear_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0
        },
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.0
        },
        {
          .width = 1280,
          .height = 720,
          .refresh_rate = 60.0
        }
      },
      .n_modes = 3,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0, 1, 2 },
          .n_modes = 3,
          .preferred_mode = 1,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            },
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 1
                }
              }
            },
            {
              .width = 1280,
              .height = 720,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 2
                }
              }
            }
          },
          .n_modes = 3,
          .current_mode = 1,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        },
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 1,
        }
      },
      .n_crtcs = 1,
      .screen_width = 1024,
      .screen_height = 768,
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_tiled_linear_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 400,
          .height = 600,
          .refresh_rate = 60.0
        },
      },
      .n_modes = 1,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 0,
            .loc_v_tile = 0,
            .tile_w = 400,
            .tile_h = 600
          }
        },
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 1,
            .loc_v_tile = 0,
            .tile_w = 400,
            .tile_h = 600
          }
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0, 1 },
          .n_outputs = 2,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                },
                {
                  .output = 1,
                  .crtc_mode = 0,
                }
              }
            },
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 800, .height = 600 },
          .scale = 1
        },
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = 0,
          .x = 400,
          .y = 0
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 1,
      .screen_width = 800,
      .screen_height = 600,
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_tiled_non_preferred_linear_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 640,
          .height = 480,
          .refresh_rate = 60.0
        },
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0
        },
        {
          .width = 512,
          .height = 768,
          .refresh_rate = 120.0
        },
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.0
        },
      },
      .n_modes = 4,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0, 2 },
          .n_modes = 2,
          .preferred_mode = 1,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 0,
            .loc_v_tile = 0,
            .tile_w = 512,
            .tile_h = 768
          }
        },
        {
          .crtc = -1,
          .modes = { 1, 2, 3 },
          .n_modes = 3,
          .preferred_mode = 0,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 1,
            .loc_v_tile = 0,
            .tile_w = 512,
            .tile_h = 768
          }
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0, 1 },
          .n_outputs = 2,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 120.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 2
                },
                {
                  .output = 1,
                  .crtc_mode = 2,
                }
              }
            },
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = -1
                },
                {
                  .output = 1,
                  .crtc_mode = 1,
                }
              }
            },
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = -1
                },
                {
                  .output = 1,
                  .crtc_mode = 3,
                }
              }
            },
          },
          .n_modes = 3,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        },
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 2,
        },
        {
          .current_mode = 2,
          .x = 512
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 1,
      .screen_width = 1024,
      .screen_height = 768,
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_tiled_non_main_origin_linear_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 400,
          .height = 600,
          .refresh_rate = 60.0
        },
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 30.0
        },
      },
      .n_modes = 2,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0, 1 },
          .n_modes = 2,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 1,
            .loc_v_tile = 0,
            .tile_w = 400,
            .tile_h = 600
          }
        },
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 0,
            .loc_v_tile = 0,
            .tile_w = 400,
            .tile_h = 600
          }
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0, 1 },
          .n_outputs = 2,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0,
                },
                {
                  .output = 1,
                  .crtc_mode = 0,
                }
              }
            },
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 30.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 1
                },
                {
                  .output = 1,
                  .crtc_mode = -1,
                }
              }
            },
          },
          .n_modes = 2,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 800, .height = 600 },
          .scale = 1
        },
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
          .x = 400,
          .y = 0
        },
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 1,
      .screen_width = 800,
      .screen_height = 600,
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_hidpi_linear_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1280,
          .height = 720,
          .refresh_rate = 60.0
        },
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.0
        }
      },
      .n_modes = 2,
      .outputs = {
        {
          .crtc = 0,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          /* These will result in DPI of about 216" */
          .width_mm = 150,
          .height_mm = 85,
          .scale = 2,
        },
        {
          .crtc = 1,
          .modes = { 1 },
          .n_modes = 1,
          .preferred_mode = 1,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .scale = 1,
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1280,
              .height = 720,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            },
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 150,
          .height_mm = 85
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 1
                }
              }
            },
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 640, .height = 360 },
          .scale = 2
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 640, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = 1,
          .x = 640,
        }
      },
      .n_crtcs = 2,
      .screen_width = 640 + 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;

  if (!meta_is_stage_views_enabled ())
    {
      g_test_skip ("Not using stage views");
      return;
    }

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);
}

static void
set_suggested_output_position (MetaOutput *output,
                               int         x,
                               int         y)
{
  output->suggested_x = x;
  output->suggested_y = y;
}

static void
meta_test_monitor_suggested_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0
        },
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.0
        }
      },
      .n_modes = 2,
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
          .modes = { 1 },
          .n_modes = 1,
          .preferred_mode = 1,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 1
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_monitors = 2,
      /*
       * Logical monitors expectations altered to correspond to the
       * "suggested_x/y" changed further below.
       */
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 758, .width = 800, .height = 600 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 1,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
          .x = 1024,
          .y = 758,
        },
        {
          .current_mode = 1,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 0,
      .screen_width = 1024 + 800,
      .screen_height = 1358
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);

  set_suggested_output_position (g_list_nth_data (test_setup->outputs, 0),
                                 1024, 758);
  set_suggested_output_position (g_list_nth_data (test_setup->outputs, 1),
                                 0, 0);

  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_limited_crtcs (void)
{
  MonitorTestCase test_case = {
    .setup = {
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
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0
        }
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = -1,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        },
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
                         "Failed to use linear *");

  emulate_hotplug (test_setup);
  g_test_assert_expected_messages ();

  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_lid_switch_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
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
          .height_mm = 125,
          .is_laptop_panel = TRUE
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
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = 0,
          .x = 1024,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 0,
      .screen_width = 1024 * 2,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);

  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);
  meta_monitor_manager_lid_is_closed_changed (monitor_manager);

  test_case.expect.logical_monitors[0] = (MonitorTestCaseLogicalMonitor) {
    .monitors = { 1 },
    .n_monitors = 1,
    .layout = {.x = 0, .y = 0, .width = 1024, .height = 768 },
    .scale = 1
  };
  test_case.expect.n_logical_monitors = 1;
  test_case.expect.screen_width = 1024;
  test_case.expect.monitors[0].current_mode = -1;
  test_case.expect.crtcs[0].current_mode = -1;
  test_case.expect.crtcs[1].x = 0;

  check_monitor_configuration (&test_case);

  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);
  meta_monitor_manager_lid_is_closed_changed (monitor_manager);

  test_case.expect.logical_monitors[0] = (MonitorTestCaseLogicalMonitor) {
    .monitors = { 0 },
    .n_monitors = 1,
    .layout = {.x = 0, .y = 0, .width = 1024, .height = 768 },
    .scale = 1
  };
  test_case.expect.n_logical_monitors = 2;
  test_case.expect.screen_width = 1024 * 2;
  test_case.expect.monitors[0].current_mode = 0;
  test_case.expect.primary_logical_monitor = 0;

  test_case.expect.crtcs[0].current_mode = 0;
  test_case.expect.crtcs[1].current_mode = 0;
  test_case.expect.crtcs[1].x = 1024;

  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_lid_opened_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
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
          .height_mm = 125,
          .is_laptop_panel = TRUE
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
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = -1,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        },
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1, /* Second one checked after lid opened. */
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = -1,
        },
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 0,
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);

  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);

  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);
  meta_monitor_manager_lid_is_closed_changed (monitor_manager);

  test_case.expect.n_logical_monitors = 2;
  test_case.expect.screen_width = 1024 * 2;
  test_case.expect.monitors[0].current_mode = 0;
  test_case.expect.crtcs[0].current_mode = 0;
  test_case.expect.crtcs[0].x = 1024;
  test_case.expect.crtcs[1].current_mode = 0;
  test_case.expect.crtcs[1].x = 0;

  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_lid_closed_no_external (void)
{
  MonitorTestCase test_case = {
    .setup = {
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
          .height_mm = 125,
          .is_laptop_panel = TRUE
        }
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        }
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        },
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_get_backend ();

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);

  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_lid_closed_with_hotplugged_external (void)
{
  MonitorTestCase test_case = {
    .setup = {
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
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .is_laptop_panel = TRUE
        },
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_outputs = 1, /* Second is hotplugged later */
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_monitors = 1, /* Second is hotplugged later */
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1, /* Second is hotplugged later */
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = -1,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 0,
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_get_backend ();

  /*
   * The first part of this test emulate the following:
   *  1) Start with the lid open
   *  2) Connect external monitor
   *  3) Close lid
   */

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);

  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);

  /* External monitor connected */

  test_case.setup.n_outputs = 2;
  test_case.expect.n_outputs = 2;
  test_case.expect.n_monitors = 2;
  test_case.expect.n_logical_monitors = 2;
  test_case.expect.crtcs[1].current_mode = 0;
  test_case.expect.crtcs[1].x = 1024;
  test_case.expect.screen_width = 1024 * 2;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);

  /* Lid closed */

  test_case.expect.monitors[0].current_mode = -1;
  test_case.expect.logical_monitors[0].monitors[0] = 1,
  test_case.expect.n_logical_monitors = 1;
  test_case.expect.crtcs[0].current_mode = -1;
  test_case.expect.crtcs[1].x = 0;
  test_case.expect.screen_width = 1024;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);

  /*
   * The second part of this test emulate the following:
   *  1) Open lid
   *  2) Disconnect external monitor
   *  3) Close lid
   *  4) Open lid
   */

  /* Lid opened */

  test_case.expect.monitors[0].current_mode = 0;
  test_case.expect.logical_monitors[0].monitors[0] = 0,
  test_case.expect.logical_monitors[1].monitors[0] = 1,
  test_case.expect.n_logical_monitors = 2;
  test_case.expect.crtcs[0].current_mode = 0;
  test_case.expect.crtcs[1].x = 1024;
  test_case.expect.screen_width = 1024 * 2;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);

  /* External monitor disconnected */

  test_case.setup.n_outputs = 1;
  test_case.expect.n_outputs = 1;
  test_case.expect.n_monitors = 1;
  test_case.expect.n_logical_monitors = 1;
  test_case.expect.crtcs[1].current_mode = -1;
  test_case.expect.screen_width = 1024;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);

  /* Lid closed */

  test_case.expect.logical_monitors[0].monitors[0] = 0,
  test_case.expect.n_logical_monitors = 1;
  test_case.expect.screen_width = 1024;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);

  /* Lid opened */

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_lid_scaled_closed_opened (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1920,
          .height = 1080,
          .refresh_rate = 60.000495910644531
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
          .height_mm = 125,
          .is_laptop_panel = TRUE
        },
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        },
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1920,
              .height = 1080,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 960, .height = 540 },
          .scale = 2
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 960,
      .screen_height = 540
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  if (!meta_is_stage_views_enabled ())
    {
      g_test_skip ("Not using stage views");
      return;
    }

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("lid-scale.xml");
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);

  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);
  meta_monitor_manager_lid_is_closed_changed (monitor_manager);

  check_monitor_configuration (&test_case);

  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);
  meta_monitor_manager_lid_is_closed_changed (monitor_manager);

  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_no_outputs (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .n_modes = 0,
      .n_outputs = 0,
      .n_crtcs = 0
    },

    .expect = {
      .n_monitors = 0,
      .n_logical_monitors = 0,
      .primary_logical_monitor = -1,
      .n_outputs = 0,
      .n_crtcs = 0,
      .n_tiled_monitors = 0,
      .screen_width = META_MONITOR_MANAGER_MIN_SCREEN_WIDTH,
      .screen_height = META_MONITOR_MANAGER_MIN_SCREEN_HEIGHT
    }
  };
  MetaMonitorTestSetup *test_setup;
  GError *error = NULL;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);

  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);

  if (!test_client_do (x11_monitor_test_client, &error,
                       "resize", X11_TEST_CLIENT_WINDOW,
                       "123", "210",
                       NULL))
    g_error ("Failed to resize X11 window: %s", error->message);

  if (!test_client_do (wayland_monitor_test_client, &error,
                       "resize", WAYLAND_TEST_CLIENT_WINDOW,
                       "123", "210",
                       NULL))
    g_error ("Failed to resize Wayland window: %s", error->message);

  check_monitor_test_clients_state ();

  /* Also check that we handle going headless -> headless */
  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);

  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_underscanning_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
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
          .height_mm = 125,
          .is_underscanning = TRUE,
        }
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        }
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
          .is_underscanning = TRUE,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_preferred_non_first_mode (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0,
          .flags = META_CRTC_MODE_FLAG_NHSYNC,
        },
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0,
          .flags = META_CRTC_MODE_FLAG_PHSYNC,
        },
      },
      .n_modes = 2,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0, 1 },
          .n_modes = 2,
          .preferred_mode = 1,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 1
                }
              }
            },
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 800, .height = 600 },
          .scale = 1
        },
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 1,
        }
      },
      .n_crtcs = 1,
      .screen_width = 800,
      .screen_height = 600,
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_non_upright_panel (void)
{
  MonitorTestCase test_case = initial_test_case;
  MetaMonitorTestSetup *test_setup;

  test_case.setup.modes[1] = (MonitorTestCaseMode) {
    .width = 768,
    .height = 1024,
    .refresh_rate = 60.0,
  };
  test_case.setup.n_modes = 2;  
  test_case.setup.outputs[0].modes[0] = 1;
  test_case.setup.outputs[0].preferred_mode = 1;
  test_case.setup.outputs[0].panel_orientation_transform =
    META_MONITOR_TRANSFORM_90;
  /*
   * Note we do not swap outputs[0].width_mm and height_mm, because these get
   * swapped for rotated panels inside the xrandr / kms code and we directly
   * create a dummy output here, skipping this code.
   */
  test_case.setup.crtcs[0].current_mode = 1;

  test_case.expect.monitors[0].modes[0].crtc_modes[0].crtc_mode = 1;
  test_case.expect.crtcs[0].current_mode = 1;
  test_case.expect.crtcs[0].transform = META_MONITOR_TRANSFORM_90;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_custom_vertical_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
        },
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.000495910644531
        }
      },
      .n_modes = 2,
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
          .modes = { 1 },
          .n_modes = 1,
          .preferred_mode = 1,
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
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 1
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 768, .width = 800, .height = 600 },
          .scale = 1
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = 1,
          .y = 768,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 0,
      .screen_width = 1024,
      .screen_height = 768 + 600
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("vertical.xml");
  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_custom_primary_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
        },
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.000495910644531
        }
      },
      .n_modes = 2,
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
          .modes = { 1 },
          .n_modes = 1,
          .preferred_mode = 1,
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
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 1
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 0, .width = 800, .height = 600 },
          .scale = 1
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 1,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = 1,
          .x = 1024,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 0,
      .screen_width = 1024 + 800,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("primary.xml");
  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_custom_underscanning_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
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
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        },
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
          .is_underscanning = TRUE,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("underscanning.xml");
  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_custom_scale_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1920,
          .height = 1080,
          .refresh_rate = 60.000495910644531
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
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        },
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1920,
              .height = 1080,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 960, .height = 540 },
          .scale = 2
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 960,
      .screen_height = 540
    }
  };
  MetaMonitorTestSetup *test_setup;

  if (!meta_is_stage_views_enabled ())
    {
      g_test_skip ("Not using stage views");
      return;
    }

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("scale.xml");
  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_custom_fractional_scale_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1200,
          .height = 900,
          .refresh_rate = 60.000495910644531
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
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        },
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1200,
              .height = 900,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 800, .height = 600 },
          .scale = 1.5
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 800,
      .screen_height = 600
    }
  };
  MetaMonitorTestSetup *test_setup;

  if (!meta_is_stage_views_enabled ())
    {
      g_test_skip ("Not using stage views");
      return;
    }

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("fractional-scale.xml");
  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_custom_high_precision_fractional_scale_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
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
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        },
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 744, .height = 558 },
          .scale = 1024.0/744.0 /* 1.3763440847396851 */
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 744,
      .screen_height = 558
    }
  };
  MetaMonitorTestSetup *test_setup;

  if (!meta_is_stage_views_enabled ())
    {
      g_test_skip ("Not using stage views");
      return;
    }

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("high-precision-fractional-scale.xml");
  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_custom_tiled_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 400,
          .height = 600,
          .refresh_rate = 60.000495910644531
        }
      },
      .n_modes = 1,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0, 1 },
          .n_possible_crtcs = 2,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 0,
            .loc_v_tile = 0,
            .tile_w = 400,
            .tile_h = 600
          }
        },
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0, 1 },
          .n_possible_crtcs = 2,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 1,
            .loc_v_tile = 0,
            .tile_w = 400,
            .tile_h = 600
          }
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0, 1 },
          .n_outputs = 2,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0,
                },
                {
                  .output = 1,
                  .crtc_mode = 0,
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 400, .height = 300 },
          .scale = 2
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = 0,
          .x = 200,
          .y = 0
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 1,
      .screen_width = 400,
      .screen_height = 300
    }
  };
  MetaMonitorTestSetup *test_setup;

  if (!meta_is_stage_views_enabled ())
    {
      g_test_skip ("Not using stage views");
      return;
    }

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("tiled.xml");
  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_custom_tiled_custom_resolution_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 400,
          .height = 600,
          .refresh_rate = 60.000495910644531
        },
        {
          .width = 640,
          .height = 480,
          .refresh_rate = 60.000495910644531
        }
      },
      .n_modes = 2,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0, 1 },
          .n_modes = 2,
          .preferred_mode = 0,
          .possible_crtcs = { 0, 1 },
          .n_possible_crtcs = 2,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 0,
            .loc_v_tile = 0,
            .tile_w = 400,
            .tile_h = 600
          }
        },
        {
          .crtc = -1,
          .modes = { 0, 1 },
          .n_modes = 2,
          .preferred_mode = 0,
          .possible_crtcs = { 0, 1 },
          .n_possible_crtcs = 2,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 1,
            .loc_v_tile = 0,
            .tile_w = 400,
            .tile_h = 600
          }
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0, 1 },
          .n_outputs = 2,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0,
                },
                {
                  .output = 1,
                  .crtc_mode = 0,
                }
              }
            },
            {
              .width = 640,
              .height = 480,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 1,
                },
                {
                  .output = 1,
                  .crtc_mode = -1,
                }
              }
            }
          },
          .n_modes = 2,
          .current_mode = 1,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 320, .height = 240 },
          .scale = 2
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 1,
        },
        {
          .current_mode = -1,
          .x = 400,
          .y = 0,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 1,
      .screen_width = 320,
      .screen_height = 240
    }
  };
  MetaMonitorTestSetup *test_setup;

  if (!meta_is_stage_views_enabled ())
    {
      g_test_skip ("Not using stage views");
      return;
    }

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("tiled-custom-resolution.xml");
  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_custom_tiled_non_preferred_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 640,
          .height = 480,
          .refresh_rate = 60.0
        },
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0
        },
        {
          .width = 512,
          .height = 768,
          .refresh_rate = 120.0
        },
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.0
        },
      },
      .n_modes = 4,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0, 2 },
          .n_modes = 2,
          .preferred_mode = 1,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 0,
            .loc_v_tile = 0,
            .tile_w = 512,
            .tile_h = 768
          }
        },
        {
          .crtc = -1,
          .modes = { 1, 2, 3 },
          .n_modes = 3,
          .preferred_mode = 0,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 1,
            .loc_v_tile = 0,
            .tile_w = 512,
            .tile_h = 768
          }
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0, 1 },
          .n_outputs = 2,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 120.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 2
                },
                {
                  .output = 1,
                  .crtc_mode = 2,
                }
              }
            },
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = -1
                },
                {
                  .output = 1,
                  .crtc_mode = 1,
                }
              }
            },
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = -1
                },
                {
                  .output = 1,
                  .crtc_mode = 3,
                }
              }
            },
          },
          .n_modes = 3,
          .current_mode = 1,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 800, .height = 600 },
          .scale = 1
        },
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = -1,
        },
        {
          .current_mode = 1,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 1,
      .screen_width = 800,
      .screen_height = 600,
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("non-preferred-tiled-custom-resolution.xml");
  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_custom_mirrored_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.000495910644531
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
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0, 1 },
          .n_monitors = 2,
          .layout = { .x = 0, .y = 0, .width = 800, .height = 600 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 0,
      .screen_width = 800,
      .screen_height = 600
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("mirrored.xml");
  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_custom_first_rotated_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
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
          .height_mm = 125,
        },
        {
          .crtc = 1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = 0
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 768, .height = 1024 },
          .scale = 1,
          .transform = META_MONITOR_TRANSFORM_270
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 768, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
          .transform = META_MONITOR_TRANSFORM_270
        },
        {
          .current_mode = 0,
          .x = 768,
        }
      },
      .n_crtcs = 2,
      .screen_width = 768 + 1024,
      .screen_height = 1024
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("first-rotated.xml");
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_custom_second_rotated_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
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
          .height_mm = 125,
        },
        {
          .crtc = 1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
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
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 256, .width = 1024, .height = 768 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 0, .width = 768, .height = 1024 },
          .scale = 1,
          .transform = META_MONITOR_TRANSFORM_90
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
          .y = 256,
        },
        {
          .current_mode = 0,
          .transform = META_MONITOR_TRANSFORM_90,
          .x = 1024,
        }
      },
      .n_crtcs = 2,
      .screen_width = 768 + 1024,
      .screen_height = 1024
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("second-rotated.xml");
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_custom_second_rotated_tiled_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
        },
        {
          .width = 400,
          .height = 600,
          .refresh_rate = 60.000495910644531
        }
      },
      .n_modes = 2,
      .outputs = {
        {
          .crtc = 0,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
        },
        {
          .crtc = -1,
          .modes = { 1 },
          .n_modes = 1,
          .preferred_mode = 1,
          .possible_crtcs = { 1, 2 },
          .n_possible_crtcs = 2,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 0,
            .loc_v_tile = 0,
            .tile_w = 400,
            .tile_h = 600
          }
        },
        {
          .crtc = -1,
          .modes = { 1 },
          .n_modes = 1,
          .preferred_mode = 1,
          .possible_crtcs = { 1, 2 },
          .n_possible_crtcs = 2,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 1,
            .loc_v_tile = 0,
            .tile_w = 400,
            .tile_h = 600
          }
        }
      },
      .n_outputs = 3,
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 3
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        },
        {
          .outputs = { 1, 2 },
          .n_outputs = 2,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 1,
                },
                {
                  .output = 2,
                  .crtc_mode = 1,
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 256, .width = 1024, .height = 768 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 0, .width = 600, .height = 800 },
          .scale = 1,
          .transform = META_MONITOR_TRANSFORM_90
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 0,
      .n_outputs = 3,
      .crtcs = {
        {
          .current_mode = 0,
          .y = 256,
        },
        {
          .current_mode = 1,
          .transform = META_MONITOR_TRANSFORM_90,
          .x = 1024,
          .y = 0,
        },
        {
          .current_mode = 1,
          .transform = META_MONITOR_TRANSFORM_90,
          .x = 1024,
          .y = 400,
        }
      },
      .n_crtcs = 3,
      .n_tiled_monitors = 1,
      .screen_width = 1024 + 600,
      .screen_height = 1024
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);

  meta_monitor_manager_test_set_handles_transforms (monitor_manager_test,
                                                    TRUE);

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("second-rotated-tiled.xml");
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_custom_second_rotated_nonnative_tiled_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
        },
        {
          .width = 400,
          .height = 600,
          .refresh_rate = 60.000495910644531
        }
      },
      .n_modes = 2,
      .outputs = {
        {
          .crtc = 0,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
        },
        {
          .crtc = -1,
          .modes = { 1 },
          .n_modes = 1,
          .preferred_mode = 1,
          .possible_crtcs = { 1, 2 },
          .n_possible_crtcs = 2,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 0,
            .loc_v_tile = 0,
            .tile_w = 400,
            .tile_h = 600
          }
        },
        {
          .crtc = -1,
          .modes = { 1 },
          .n_modes = 1,
          .preferred_mode = 1,
          .possible_crtcs = { 1, 2 },
          .n_possible_crtcs = 2,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 1,
            .loc_v_tile = 0,
            .tile_w = 400,
            .tile_h = 600
          }
        }
      },
      .n_outputs = 3,
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 3
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        },
        {
          .outputs = { 1, 2 },
          .n_outputs = 2,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 1,
                },
                {
                  .output = 2,
                  .crtc_mode = 1,
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 256, .width = 1024, .height = 768 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 0, .width = 600, .height = 800 },
          .scale = 1,
          .transform = META_MONITOR_TRANSFORM_90
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 0,
      .n_outputs = 3,
      .crtcs = {
        {
          .current_mode = 0,
          .y = 256,
        },
        {
          .current_mode = 1,
          .transform = META_MONITOR_TRANSFORM_NORMAL,
          .x = 1024,
          .y = 0,
        },
        {
          .current_mode = 1,
          .transform = META_MONITOR_TRANSFORM_NORMAL,
          .x = 1024,
          .y = 400,
        }
      },
      .n_crtcs = 3,
      .n_tiled_monitors = 1,
      .screen_width = 1024 + 600,
      .screen_height = 1024
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);

  meta_monitor_manager_test_set_handles_transforms (monitor_manager_test,
                                                    FALSE);

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("second-rotated-tiled.xml");
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_custom_second_rotated_nonnative_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
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
          .height_mm = 125,
        },
        {
          .crtc = 1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
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
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 256, .width = 1024, .height = 768 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 0, .width = 768, .height = 1024 },
          .scale = 1,
          .transform = META_MONITOR_TRANSFORM_90
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
          .y = 256,
        },
        {
          .current_mode = 0,
          .transform = META_MONITOR_TRANSFORM_NORMAL,
          .x = 1024,
        }
      },
      .n_crtcs = 2,
      .screen_width = 768 + 1024,
      .screen_height = 1024
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);

  if (!meta_is_stage_views_enabled ())
    {
      g_test_skip ("Not using stage views");
      return;
    }

  meta_monitor_manager_test_set_handles_transforms (monitor_manager_test,
                                                    FALSE);

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("second-rotated.xml");
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_custom_interlaced_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
        },
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531,
          .flags = META_CRTC_MODE_FLAG_INTERLACE,
        }
      },
      .n_modes = 2,
      .outputs = {
        {
          .crtc = 0,
          .modes = { 0, 1 },
          .n_modes = 2,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125
        },
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        },
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .flags = META_CRTC_MODE_FLAG_NONE,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0,
                },
              }
            },
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .flags = META_CRTC_MODE_FLAG_INTERLACE,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 1,
                }
              }
            }
          },
          .n_modes = 2,
          .current_mode = 1,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 1,
        }
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("interlaced.xml");
  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_custom_oneoff (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0
        }
      },
      .n_modes = 1,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0, 1 },
          .n_possible_crtcs = 2,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0, 1 },
          .n_possible_crtcs = 2,
          .width_mm = 222,
          .height_mm = 125,
          .serial = "0x654321"
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = -1,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 800, .height = 600 },
          .scale = 1,
          .transform = META_MONITOR_TRANSFORM_NORMAL
        },
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = -1,
        }
      },
      .n_crtcs = 2,
      .screen_width = 800,
      .screen_height = 600,
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("oneoff.xml");
  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_custom_lid_switch_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
        }
      },
      .n_modes = 1,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .is_laptop_panel = TRUE
        },
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_outputs = 1, /* Second one hot plugged later */
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = 0
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1, /* Second one hot plugged later */
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 768, .height = 1024 },
          .scale = 1,
          .transform = META_MONITOR_TRANSFORM_270
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 0, .width = 768, .height = 1024 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1, /* Second one hot plugged later */
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
          .transform = META_MONITOR_TRANSFORM_270
        },
        {
          .current_mode = -1,
        }
      },
      .n_crtcs = 2,
      .screen_width = 768,
      .screen_height = 1024
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_get_backend ();

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("lid-switch.xml");
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);

  /* External monitor connected */

  test_case.setup.n_outputs = 2;
  test_case.expect.n_monitors = 2;
  test_case.expect.n_outputs = 2;
  test_case.expect.crtcs[0].transform = META_MONITOR_TRANSFORM_NORMAL;
  test_case.expect.crtcs[1].current_mode = 0;
  test_case.expect.crtcs[1].x = 1024;
  test_case.expect.crtcs[1].transform = META_MONITOR_TRANSFORM_270;
  test_case.expect.logical_monitors[0].layout =
    (MetaRectangle) { .width = 1024, .height = 768 };
  test_case.expect.logical_monitors[0].transform = META_MONITOR_TRANSFORM_NORMAL;
  test_case.expect.logical_monitors[1].transform = META_MONITOR_TRANSFORM_270;
  test_case.expect.n_logical_monitors = 2;
  test_case.expect.screen_width = 1024 + 768;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);

  /* Lid was closed */

  test_case.expect.crtcs[0].current_mode = -1;
  test_case.expect.crtcs[1].transform = META_MONITOR_TRANSFORM_90;
  test_case.expect.crtcs[1].x = 0;
  test_case.expect.monitors[0].current_mode = -1;
  test_case.expect.logical_monitors[0].layout =
    (MetaRectangle) { .width = 768, .height = 1024 };
  test_case.expect.logical_monitors[0].monitors[0] = 1;
  test_case.expect.logical_monitors[0].transform = META_MONITOR_TRANSFORM_90;
  test_case.expect.n_logical_monitors = 1;
  test_case.expect.screen_width = 768;
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);

  /* Lid was opened */

  test_case.expect.crtcs[0].current_mode = 0;
  test_case.expect.crtcs[0].transform = META_MONITOR_TRANSFORM_NORMAL;
  test_case.expect.crtcs[1].current_mode = 0;
  test_case.expect.crtcs[1].transform = META_MONITOR_TRANSFORM_270;
  test_case.expect.crtcs[1].x = 1024;
  test_case.expect.monitors[0].current_mode = 0;
  test_case.expect.logical_monitors[0].layout =
    (MetaRectangle) { .width = 1024, .height = 768 };
  test_case.expect.logical_monitors[0].monitors[0] = 0;
  test_case.expect.logical_monitors[0].transform = META_MONITOR_TRANSFORM_NORMAL;
  test_case.expect.logical_monitors[1].transform = META_MONITOR_TRANSFORM_270;
  test_case.expect.n_logical_monitors = 2;
  test_case.expect.screen_width = 1024 + 768;
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case);
}

static void
meta_test_monitor_migrated_rotated (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0
        }
      },
      .n_modes = 1,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 600, .height = 800 },
          .scale = 1,
          .transform = META_MONITOR_TRANSFORM_270
        },
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
          .transform = META_MONITOR_TRANSFORM_270
        }
      },
      .n_crtcs = 1,
      .screen_width = 600,
      .screen_height = 800,
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorConfigStore *config_store =
    meta_monitor_config_manager_get_store (config_manager);
  g_autofree char *migrated_path = NULL;
  const char *old_config_path;
  g_autoptr (GFile) old_config_file = NULL;
  GError *error = NULL;
  const char *expected_path;
  g_autofree char *migrated_data = NULL;
  g_autofree char *expected_data = NULL;
  g_autoptr (GFile) migrated_file = NULL;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);

  migrated_path = g_build_filename (g_get_tmp_dir (),
                                    "test-finished-migrated-monitors.xml",
                                    NULL);
  if (!meta_monitor_config_store_set_custom (config_store,
                                             "/dev/null",
                                             migrated_path,
                                             &error))
    g_error ("Failed to set custom config store files: %s", error->message);

  old_config_path = g_test_get_filename (G_TEST_DIST,
                                         "tests", "migration",
                                         "rotated-old.xml",
                                         NULL);
  old_config_file = g_file_new_for_path (old_config_path);
  if (!meta_migrate_old_monitors_config (config_store,
                                         old_config_file,
                                         &error))
    g_error ("Failed to migrate config: %s", error->message);

  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case);

  expected_path = g_test_get_filename (G_TEST_DIST,
                                       "tests", "migration",
                                       "rotated-new-finished.xml",
                                       NULL);
  expected_data = read_file (expected_path);
  migrated_data = read_file (migrated_path);

  g_assert_nonnull (expected_data);
  g_assert_nonnull (migrated_data);

  g_assert (strcmp (expected_data, migrated_data) == 0);

  migrated_file = g_file_new_for_path (migrated_path);
  if (!g_file_delete (migrated_file, NULL, &error))
    g_error ("Failed to remove test data output file: %s", error->message);
}

static void
meta_test_monitor_migrated_wiggle_discard (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 59.0
        }
      },
      .n_modes = 1,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 59.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 800, .height = 600 },
          .scale = 1,
          .transform = META_MONITOR_TRANSFORM_NORMAL
        },
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .screen_width = 800,
      .screen_height = 600,
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorConfigStore *config_store =
    meta_monitor_config_manager_get_store (config_manager);
  g_autofree char *migrated_path = NULL;
  const char *old_config_path;
  g_autoptr (GFile) old_config_file = NULL;
  GError *error = NULL;
  const char *expected_path;
  g_autofree char *migrated_data = NULL;
  g_autofree char *expected_data = NULL;
  g_autoptr (GFile) migrated_file = NULL;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);

  migrated_path = g_build_filename (g_get_tmp_dir (),
                                    "test-finished-migrated-monitors.xml",
                                    NULL);
  if (!meta_monitor_config_store_set_custom (config_store,
                                             "/dev/null",
                                             migrated_path,
                                             &error))
    g_error ("Failed to set custom config store files: %s", error->message);

  old_config_path = g_test_get_filename (G_TEST_DIST,
                                         "tests", "migration",
                                         "wiggle-old.xml",
                                         NULL);
  old_config_file = g_file_new_for_path (old_config_path);
  if (!meta_migrate_old_monitors_config (config_store,
                                         old_config_file,
                                         &error))
    g_error ("Failed to migrate config: %s", error->message);

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
                         "Failed to finish monitors config migration: "
                         "Mode not available on monitor");
  emulate_hotplug (test_setup);
  g_test_assert_expected_messages ();

  check_monitor_configuration (&test_case);

  expected_path = g_test_get_filename (G_TEST_DIST,
                                       "tests", "migration",
                                       "wiggle-new-discarded.xml",
                                       NULL);
  expected_data = read_file (expected_path);
  migrated_data = read_file (migrated_path);

  g_assert_nonnull (expected_data);
  g_assert_nonnull (migrated_data);

  g_assert (strcmp (expected_data, migrated_data) == 0);

  migrated_file = g_file_new_for_path (migrated_path);
  if (!g_file_delete (migrated_file, NULL, &error))
    g_error ("Failed to remove test data output file: %s", error->message);
}

static gboolean
quit_main_loop (gpointer data)
{
  GMainLoop *loop = data;

  g_main_loop_quit (loop);

  return G_SOURCE_REMOVE;
}

static void
dispatch (void)
{
  GMainLoop *loop;

  loop = g_main_loop_new (NULL, FALSE);
  meta_later_add (META_LATER_BEFORE_REDRAW,
                  quit_main_loop,
                  loop,
                  NULL);
  g_main_loop_run (loop);
}

static TestClient *
create_test_window (const char *window_name)
{
  TestClient *test_client;
  static int client_count = 0;
  g_autofree char *client_name = NULL;
  g_autoptr (GError) error = NULL;

  client_name = g_strdup_printf ("test_client_%d", client_count++);
  test_client = test_client_new (client_name, META_WINDOW_CLIENT_TYPE_WAYLAND,
                                 &error);
  if (!test_client)
    g_error ("Failed to launch test client: %s", error->message);

  if (!test_client_do (test_client, &error,
                       "create", window_name,
                       NULL))
    g_error ("Failed to create window: %s", error->message);

  return test_client;
}

static void
meta_test_monitor_wm_tiling (void)
{
  MonitorTestCase test_case = initial_test_case;
  MetaMonitorTestSetup *test_setup;
  g_autoptr (GError) error = NULL;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);

  /*
   * 1) Start with two monitors connected.
   * 2) Tile it on the second monitor.
   * 3) Unplug both monitors.
   * 4) Replug in first monitor.
   */

  const char *test_window_name= "window1";
  TestClient *test_client = create_test_window (test_window_name);

  if (!test_client_do (test_client, &error,
                       "show", test_window_name,
                       NULL))
    g_error ("Failed to show the window: %s", error->message);

  MetaWindow *test_window =
    test_client_find_window (test_client,
                             test_window_name,
                             &error);
  if (!test_window)
    g_error ("Failed to find the window: %s", error->message);
  test_client_wait_for_window_shown (test_client, test_window);

  meta_window_tile (test_window, META_TILE_MAXIMIZED);
  meta_window_move_to_monitor (test_window, 1);
  check_test_client_state (test_client);

  fprintf(stderr, ":::: %s:%d %s() - UNPLUGGING\n", __FILE__, __LINE__, __func__);

  test_case.setup.n_outputs = 0;
  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  test_case.setup.n_outputs = 1;
  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);

  dispatch ();

  /*
   * 1) Start with two monitors connected.
   * 2) Tile a window on the second monitor.
   * 3) Untile window.
   * 4) Unplug monitor.
   * 5) Tile window again.
   */

  test_case.setup.n_outputs = 2;
  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);

  meta_window_move_to_monitor (test_window, 1);
  meta_window_tile (test_window, META_TILE_NONE);

  test_case.setup.n_outputs = 1;
  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);

  meta_window_tile (test_window, META_TILE_MAXIMIZED);

  test_client_destroy (test_client);
}

static void
meta_test_monitor_migrated_wiggle (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0
        }
      },
      .n_modes = 1,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 600, .height = 800 },
          .scale = 1,
          .transform = META_MONITOR_TRANSFORM_90
        },
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
          .transform = META_MONITOR_TRANSFORM_90
        }
      },
      .n_crtcs = 1,
      .screen_width = 600,
      .screen_height = 800,
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorConfigStore *config_store =
    meta_monitor_config_manager_get_store (config_manager);
  g_autofree char *migrated_path = NULL;
  const char *old_config_path;
  g_autoptr (GFile) old_config_file = NULL;
  GError *error = NULL;
  const char *expected_path;
  g_autofree char *migrated_data = NULL;
  g_autofree char *expected_data = NULL;
  g_autoptr (GFile) migrated_file = NULL;

  test_setup = create_monitor_test_setup (&test_case,
                                          MONITOR_TEST_FLAG_NONE);

  migrated_path = g_build_filename (g_get_tmp_dir (),
                                    "test-finished-migrated-monitors.xml",
                                    NULL);
  if (!meta_monitor_config_store_set_custom (config_store,
                                             "/dev/null",
                                             migrated_path,
                                             &error))
    g_error ("Failed to set custom config store files: %s", error->message);

  old_config_path = g_test_get_filename (G_TEST_DIST,
                                         "tests", "migration",
                                         "wiggle-old.xml",
                                         NULL);
  old_config_file = g_file_new_for_path (old_config_path);
  if (!meta_migrate_old_monitors_config (config_store,
                                         old_config_file,
                                         &error))
    g_error ("Failed to migrate config: %s", error->message);

  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case);

  expected_path = g_test_get_filename (G_TEST_DIST,
                                       "tests", "migration",
                                       "wiggle-new-finished.xml",
                                       NULL);
  expected_data = read_file (expected_path);
  migrated_data = read_file (migrated_path);

  g_assert_nonnull (expected_data);
  g_assert_nonnull (migrated_data);

  g_assert (strcmp (expected_data, migrated_data) == 0);

  migrated_file = g_file_new_for_path (migrated_path);
  if (!g_file_delete (migrated_file, NULL, &error))
    g_error ("Failed to remove test data output file: %s", error->message);
}

static void
test_case_setup (void       **fixture,
                 const void   *data)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;

  meta_monitor_manager_test_set_handles_transforms (monitor_manager_test,
                                                    TRUE);
  meta_monitor_config_manager_set_current (config_manager, NULL);
  meta_monitor_config_manager_clear_history (config_manager);
}

static void
add_monitor_test (const char *test_path,
                  GTestFunc   test_func)
{
  g_test_add (test_path, gpointer, NULL,
              test_case_setup,
              (void (* ) (void **, const void *)) test_func,
              NULL);
}

void
init_monitor_tests (void)
{
  MetaMonitorTestSetup *initial_test_setup;

  initial_test_setup = create_monitor_test_setup (&initial_test_case,
                                                  MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_init_test_setup (initial_test_setup);

  add_monitor_test ("/backends/monitor/initial-linear-config",
                    meta_test_monitor_initial_linear_config);
  add_monitor_test ("/backends/monitor/one-disconnected-linear-config",
                    meta_test_monitor_one_disconnected_linear_config);
  add_monitor_test ("/backends/monitor/one-off-linear-config",
                    meta_test_monitor_one_off_linear_config);
  add_monitor_test ("/backends/monitor/preferred-linear-config",
                    meta_test_monitor_preferred_linear_config);
  add_monitor_test ("/backends/monitor/tiled-linear-config",
                    meta_test_monitor_tiled_linear_config);
  add_monitor_test ("/backends/monitor/tiled-non-preferred-linear-config",
                    meta_test_monitor_tiled_non_preferred_linear_config);
  add_monitor_test ("/backends/monitor/tiled-non-main-origin-linear-config",
                    meta_test_monitor_tiled_non_main_origin_linear_config);
  add_monitor_test ("/backends/monitor/hidpi-linear-config",
                    meta_test_monitor_hidpi_linear_config);
  add_monitor_test ("/backends/monitor/suggested-config",
                    meta_test_monitor_suggested_config);
  add_monitor_test ("/backends/monitor/limited-crtcs",
                    meta_test_monitor_limited_crtcs);
  add_monitor_test ("/backends/monitor/lid-switch-config",
                    meta_test_monitor_lid_switch_config);
  add_monitor_test ("/backends/monitor/lid-opened-config",
                    meta_test_monitor_lid_opened_config);
  add_monitor_test ("/backends/monitor/lid-closed-no-external",
                    meta_test_monitor_lid_closed_no_external);
  add_monitor_test ("/backends/monitor/lid-closed-with-hotplugged-external",
                    meta_test_monitor_lid_closed_with_hotplugged_external);
  add_monitor_test ("/backends/monitor/lid-scaled-closed-opened",
                    meta_test_monitor_lid_scaled_closed_opened);
  add_monitor_test ("/backends/monitor/no-outputs",
                    meta_test_monitor_no_outputs);
  add_monitor_test ("/backends/monitor/underscanning-config",
                    meta_test_monitor_underscanning_config);
  add_monitor_test ("/backends/monitor/preferred-non-first-mode",
                    meta_test_monitor_preferred_non_first_mode);
  add_monitor_test ("/backends/monitor/non-upright-panel",
                    meta_test_monitor_non_upright_panel);

  add_monitor_test ("/backends/monitor/custom/vertical-config",
                    meta_test_monitor_custom_vertical_config);
  add_monitor_test ("/backends/monitor/custom/primary-config",
                    meta_test_monitor_custom_primary_config);
  add_monitor_test ("/backends/monitor/custom/underscanning-config",
                    meta_test_monitor_custom_underscanning_config);
  add_monitor_test ("/backends/monitor/custom/scale-config",
                    meta_test_monitor_custom_scale_config);
  add_monitor_test ("/backends/monitor/custom/fractional-scale-config",
                    meta_test_monitor_custom_fractional_scale_config);
  add_monitor_test ("/backends/monitor/custom/high-precision-fractional-scale-config",
                    meta_test_monitor_custom_high_precision_fractional_scale_config);
  add_monitor_test ("/backends/monitor/custom/tiled-config",
                    meta_test_monitor_custom_tiled_config);
  add_monitor_test ("/backends/monitor/custom/tiled-custom-resolution-config",
                    meta_test_monitor_custom_tiled_custom_resolution_config);
  add_monitor_test ("/backends/monitor/custom/tiled-non-preferred-config",
                    meta_test_monitor_custom_tiled_non_preferred_config);
  add_monitor_test ("/backends/monitor/custom/mirrored-config",
                    meta_test_monitor_custom_mirrored_config);
  add_monitor_test ("/backends/monitor/custom/first-rotated-config",
                    meta_test_monitor_custom_first_rotated_config);
  add_monitor_test ("/backends/monitor/custom/second-rotated-config",
                    meta_test_monitor_custom_second_rotated_config);
  add_monitor_test ("/backends/monitor/custom/second-rotated-tiled-config",
                    meta_test_monitor_custom_second_rotated_tiled_config);
  add_monitor_test ("/backends/monitor/custom/second-rotated-nonnative-tiled-config",
                    meta_test_monitor_custom_second_rotated_nonnative_tiled_config);
  add_monitor_test ("/backends/monitor/custom/second-rotated-nonnative-config",
                    meta_test_monitor_custom_second_rotated_nonnative_config);
  add_monitor_test ("/backends/monitor/custom/interlaced-config",
                    meta_test_monitor_custom_interlaced_config);
  add_monitor_test ("/backends/monitor/custom/oneoff-config",
                    meta_test_monitor_custom_oneoff);
  add_monitor_test ("/backends/monitor/custom/lid-switch-config",
                    meta_test_monitor_custom_lid_switch_config);

  add_monitor_test ("/backends/monitor/migrated/rotated",
                    meta_test_monitor_migrated_rotated);
  add_monitor_test ("/backends/monitor/migrated/wiggle",
                    meta_test_monitor_migrated_wiggle);
  add_monitor_test ("/backends/monitor/migrated/wiggle-discard",
                    meta_test_monitor_migrated_wiggle_discard);

  add_monitor_test ("/backends/monitor/wm/tiling",
                    meta_test_monitor_wm_tiling);
}

void
pre_run_monitor_tests (void)
{
  create_monitor_test_clients ();
}

void
finish_monitor_tests (void)
{
  destroy_monitor_test_clients ();
}
