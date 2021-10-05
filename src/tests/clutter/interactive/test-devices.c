#include <stdlib.h>
#include <gmodule.h>
#include <clutter/clutter.h>

#ifdef CLUTTER_WINDOWING_X11
#include <clutter/x11/clutter-x11.h>
#endif

#include "test-utils.h"

typedef struct {
  ClutterActor *stage;

  GHashTable *devices;
} TestDevicesApp;

int
test_devices_main (int argc, char **argv);

static const gchar *
device_type_name (ClutterInputDevice *device)
{
  ClutterInputDeviceType d_type;

  d_type = clutter_input_device_get_device_type (device);
  switch (d_type)
    {
    case CLUTTER_POINTER_DEVICE:
      return "Pointer";

    case CLUTTER_KEYBOARD_DEVICE:
      return "Keyboard";

    case CLUTTER_EXTENSION_DEVICE:
      return "Extension";

    case CLUTTER_PEN_DEVICE:
      return "Pen";

    case CLUTTER_ERASER_DEVICE:
      return "Eraser";

    case CLUTTER_CURSOR_DEVICE:
      return "Cursor";

    default:
      return "Unknown";
    }
}

static const gchar *
axis_type_name (ClutterInputAxis axis)
{
  switch (axis)
    {
    case CLUTTER_INPUT_AXIS_X:
      return "Absolute X";

    case CLUTTER_INPUT_AXIS_Y:
      return "Absolute Y";

    case CLUTTER_INPUT_AXIS_PRESSURE:
      return "Pressure";

    case CLUTTER_INPUT_AXIS_XTILT:
      return "X Tilt";

    case CLUTTER_INPUT_AXIS_YTILT:
      return "Y Tilt";

    case CLUTTER_INPUT_AXIS_WHEEL:
      return "Wheel";

    default:
      return "Unknown";
    }
}

static gboolean
stage_button_event_cb (ClutterActor   *actor,
                       ClutterEvent   *event,
                       TestDevicesApp *app)
{
  ClutterInputDevice *device;
  ClutterInputDevice *source_device;
  ClutterActor *hand = NULL;
  gdouble *axes;
  guint n_axes, i;

  device = clutter_event_get_device (event);
  source_device = clutter_event_get_source_device (event);

  hand = g_hash_table_lookup (app->devices, device);

  g_print ("Device: '%s' (id:%d, type: %s, source: '%s', axes: %d)\n",
           clutter_input_device_get_device_name (device),
           clutter_input_device_get_device_id (device),
           device_type_name (device),
           source_device != device
             ? clutter_input_device_get_device_name (source_device)
             : "<same>",
           clutter_input_device_get_n_axes (device));

  if (hand != NULL)
    {
      gfloat event_x, event_y;

      clutter_event_get_coords (event, &event_x, &event_y);
      clutter_actor_set_position (hand, event_x, event_y);
    }

  axes = clutter_event_get_axes (event, &n_axes);
  for (i = 0; i < n_axes; i++)
    {
      ClutterInputAxis axis;

      axis = clutter_input_device_get_axis (device, i);
      if (axis == CLUTTER_INPUT_AXIS_IGNORE)
        continue;

      g_print ("\tAxis[%2d][%s].value: %.2f\n",
               i,
               axis_type_name (axis),
               axes[i]);
    }

  return FALSE;
}

static gboolean
stage_motion_event_cb (ClutterActor   *actor,
                       ClutterEvent   *event,
                       TestDevicesApp *app)
{
  ClutterInputDevice *device;
  ClutterActor *hand = NULL;

  device = clutter_event_get_device (event);

  hand = g_hash_table_lookup (app->devices, device);
  if (hand != NULL)
    {
      gfloat event_x, event_y;

      clutter_event_get_coords (event, &event_x, &event_y);
      clutter_actor_set_position (hand, event_x, event_y);

      return TRUE;
    }

  return FALSE;
}

static void
seat_device_added_cb (ClutterSeat        *seat,
                      ClutterInputDevice *device,
                      TestDevicesApp     *app)
{
  ClutterInputDeviceType device_type;
  ClutterActor *hand = NULL;

  g_print ("got a %s device '%s' with id %d\n",
           device_type_name (device),
           clutter_input_device_get_device_name (device),
           clutter_input_device_get_device_id (device));

  device_type = clutter_input_device_get_device_type (device);
  if (device_type == CLUTTER_POINTER_DEVICE ||
      device_type == CLUTTER_PEN_DEVICE ||
      device_type == CLUTTER_POINTER_DEVICE)
    {
      g_print ("*** enabling device '%s' ***\n",
               clutter_input_device_get_device_name (device));

      clutter_input_device_set_enabled (device, TRUE);

      hand = clutter_test_utils_create_texture_from_file (TESTS_DATADIR
                                                          G_DIR_SEPARATOR_S
                                                          "redhand.png",
                                                          NULL);
      g_hash_table_insert (app->devices, device, hand);

      clutter_container_add_actor (CLUTTER_CONTAINER (app->stage), hand);
    }
}

static void
seat_device_removed_cb (ClutterSeat        *seat,
                        ClutterInputDevice *device,
                        TestDevicesApp     *app)
{
  ClutterInputDeviceType device_type;
  ClutterActor *hand = NULL;

  g_print ("removed a %s device '%s' with id %d\n",
           device_type_name (device),
           clutter_input_device_get_device_name (device),
           clutter_input_device_get_device_id (device));

  device_type = clutter_input_device_get_device_type (device);
  if (device_type == CLUTTER_POINTER_DEVICE ||
      device_type == CLUTTER_PEN_DEVICE ||
      device_type == CLUTTER_POINTER_DEVICE)
    {
      hand = g_hash_table_lookup (app->devices, device);
      if (hand != NULL)
        clutter_container_add_actor (CLUTTER_CONTAINER (app->stage), hand);

      g_hash_table_remove (app->devices, device);
    }
}

G_MODULE_EXPORT int
test_devices_main (int argc, char **argv)
{
  ClutterActor *stage;
  TestDevicesApp *app;
  ClutterSeat *seat;
  GList *stage_devices, *l;

  if (clutter_init (&argc, &argv) != CLUTTER_INIT_SUCCESS)
    return 1;

  app = g_new0 (TestDevicesApp, 1);
  app->devices = g_hash_table_new (g_direct_hash, g_direct_equal) ;

  stage = clutter_stage_new ();
  clutter_actor_set_background_color (stage, CLUTTER_COLOR_LightSkyBlue);
  clutter_stage_set_title (CLUTTER_STAGE (stage), "Devices");
  clutter_stage_hide_cursor (CLUTTER_STAGE (stage));
  g_signal_connect (stage,
                    "destroy", G_CALLBACK (clutter_main_quit),
                    NULL);
  g_signal_connect (stage, 
                    "motion-event", G_CALLBACK (stage_motion_event_cb),
                    app);
  g_signal_connect (stage,
                    "button-press-event", G_CALLBACK (stage_button_event_cb),
                    app);
  app->stage = stage;

  clutter_actor_show_all (stage);

  seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  g_signal_connect (seat,
                    "device-added", G_CALLBACK (seat_device_added_cb),
                    app);
  g_signal_connect (seat,
                    "device-removed", G_CALLBACK (seat_device_removed_cb),
                    app);

  stage_devices = clutter_seat_list_devices (seat);

  if (stage_devices == NULL)
    g_error ("No input devices found.");

  for (l = stage_devices; l != NULL; l = l->next)
    {
      ClutterInputDevice *device = l->data;
      ClutterInputDeviceType device_type;
      ClutterActor *hand = NULL;

      g_print ("got a %s device '%s' with id %d\n",
               device_type_name (device),
               clutter_input_device_get_device_name (device),
               clutter_input_device_get_device_id (device));

      device_type = clutter_input_device_get_device_type (device);
      if (device_type == CLUTTER_POINTER_DEVICE ||
          device_type == CLUTTER_PEN_DEVICE ||
          device_type == CLUTTER_POINTER_DEVICE)
        {
          g_print ("*** enabling device '%s' ***\n",
                   clutter_input_device_get_device_name (device));

          clutter_input_device_set_enabled (device, TRUE);

          hand = clutter_test_utils_create_texture_from_file (TESTS_DATADIR
                                                              G_DIR_SEPARATOR_S
                                                              "redhand.png",
                                                              NULL);
          g_hash_table_insert (app->devices, device, hand);

          clutter_container_add_actor (CLUTTER_CONTAINER (stage), hand);
        }
    }

  g_list_free (stage_devices);

  clutter_main ();

  return EXIT_SUCCESS;
} 
