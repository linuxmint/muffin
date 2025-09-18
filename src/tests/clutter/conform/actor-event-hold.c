/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: José Expósito <jose.exposito89@gmail.com>
 */
#include <stdlib.h>
#include <string.h>

#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

#define EVENT_TIME 1000

typedef struct {
    ClutterTouchpadGesturePhase phase;
    guint n_fingers;
    gfloat x;
    gfloat y;
} HoldTestCase;

static const HoldTestCase test_cases[] = {
    {
        .phase = CLUTTER_TOUCHPAD_GESTURE_PHASE_BEGIN,
        .n_fingers = 1,
        .x = 100,
        .y = 150,
    },
    {
        .phase = CLUTTER_TOUCHPAD_GESTURE_PHASE_END,
        .n_fingers = 2,
        .x = 200,
        .y = 250,
    },
    {
        .phase = CLUTTER_TOUCHPAD_GESTURE_PHASE_CANCEL,
        .n_fingers = 3,
        .x = 300,
        .y = 350,
    },
};

static gboolean
on_stage_captured_event (ClutterActor  *stage,
                         ClutterEvent  *event,
                         ClutterEvent **captured_event)
{
    *captured_event = clutter_event_copy (event);
    return TRUE;
}

static void
actor_event_hold (void)
{
    ClutterActor *stage;
    ClutterBackend *backend;
    ClutterSeat *seat;
    ClutterInputDevice *device;
    ClutterEvent *event;
    ClutterEvent *captured_event;
    size_t n_test_case;

    /* Get the stage and listen for touchpad events */
    stage = clutter_test_get_stage ();
    g_signal_connect (stage, "captured-event::touchpad",
                      G_CALLBACK (on_stage_captured_event),
                      &captured_event);
    clutter_actor_show (stage);

    /* Get the input device*/
    backend = clutter_get_default_backend ();
    seat = clutter_backend_get_default_seat (backend);
    device = clutter_seat_get_pointer (seat);

    for (n_test_case = 0; n_test_case < G_N_ELEMENTS (test_cases); n_test_case++)
    {
        graphene_point_t actual_position;
        gdouble *actual_axes;
        ClutterTouchpadGesturePhase actual_phase;
        guint actual_n_fingers;
        gdouble dx, dy, udx, udy;

        const HoldTestCase *test_case = test_cases + n_test_case;

        /* Create a synthetic hold event */
        event = clutter_event_new (CLUTTER_TOUCHPAD_HOLD);
        event->touchpad_hold.phase = test_case->phase;
        event->touchpad_hold.time = EVENT_TIME;
        event->touchpad_hold.n_fingers = test_case->n_fingers;
        event->touchpad_hold.stage = (ClutterStage *) stage;
        event->touchpad_hold.source = stage;
        clutter_event_set_coords (event, test_case->x, test_case->y);
        clutter_event_set_device (event, device);

        clutter_event_put (event);
        clutter_event_free (event);

        /* Capture the event received by the stage */
        captured_event = NULL;
        while (captured_event == NULL)
            g_main_context_iteration (NULL, FALSE);

        /* Check that expected the event params match the actual values */
        clutter_event_get_position (captured_event, &actual_position);
        actual_axes = clutter_event_get_axes (captured_event, 0);
        actual_phase = clutter_event_get_gesture_phase (captured_event);
        actual_n_fingers = clutter_event_get_touchpad_gesture_finger_count (captured_event);
        clutter_event_get_gesture_motion_delta (captured_event, &dx, &dy);

        g_assert (actual_position.x == test_case->x);
        g_assert (actual_position.y == test_case->y);
        g_assert_null (actual_axes);
        g_assert (actual_phase == test_case->phase);
        g_assert (actual_n_fingers == test_case->n_fingers);
        g_assert (dx == 0);
        g_assert (dy == 0);
        g_assert (udx == 0);
        g_assert (udy == 0);

        clutter_event_free (captured_event);
    }
}

CLUTTER_TEST_SUITE (
    CLUTTER_TEST_UNIT ("/actor/event/hold", actor_event_hold)
)
