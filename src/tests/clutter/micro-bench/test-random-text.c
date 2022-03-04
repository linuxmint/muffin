#include <gmodule.h>
#include <clutter/clutter.h>
#include <stdlib.h>

#define MAX_TEXT_LEN  10
#define MIN_FONT_SIZE 10
#define MAX_FONT_SIZE 30

static const char * const font_names[] =
  {
    "Sans", "Sans Italic", "Serif", "Serif Bold", "Times", "Monospace"
  };
#define FONT_NAME_COUNT 6

static gboolean
on_idle (gpointer data)
{
  ClutterActor *stage = CLUTTER_ACTOR (data);
  int line_height = 0, xpos = 0, ypos = 0;
  int stage_width = clutter_actor_get_width (stage);
  int stage_height = clutter_actor_get_height (stage);
  char text[MAX_TEXT_LEN + 1];
  char font_name[64];
  int i;
  GList *children, *node;
  static GTimer *timer = NULL;
  static int frame_count = 0;

  /* Remove all of the children of the stage */
  children = clutter_container_get_children (CLUTTER_CONTAINER (stage));
  for (node = children; node; node = node->next)
    clutter_container_remove_actor (CLUTTER_CONTAINER (stage),
                                    CLUTTER_ACTOR (node->data));
  g_list_free (children);

  /* Fill the stage with new random labels */
  while (ypos < stage_height)
    {
      int text_len = rand () % MAX_TEXT_LEN + 1;
      ClutterActor *label;

      for (i = 0; i < text_len; i++)
        text[i] = rand () % (128 - 32) + 32;
      text[text_len] = '\0';

      sprintf (font_name, "%s %i",
               font_names[rand () % FONT_NAME_COUNT],
               rand () % (MAX_FONT_SIZE - MIN_FONT_SIZE) + MIN_FONT_SIZE);

      label = clutter_text_new_with_text (font_name, text);

      if (clutter_actor_get_height (label) > line_height)
        line_height = clutter_actor_get_height (label);

      if (xpos + clutter_actor_get_width (label) > stage_width)
        {
          xpos = 0;
          ypos += line_height;
          line_height = 0;
        }

      clutter_actor_set_position (label, xpos, ypos);

      clutter_container_add (CLUTTER_CONTAINER (stage), label, NULL);

      xpos += clutter_actor_get_width (label);
    }

  if (timer == NULL)
    timer = g_timer_new ();
  else
    {
      if (++frame_count >= 10)
        {
          printf ("10 frames in %f seconds\n",
                  g_timer_elapsed (timer, NULL));
          g_timer_start (timer);
          frame_count = 0;
        }
    }

  return TRUE;
}

int
main (int argc, char *argv[])
{
  ClutterActor *stage;

  if (clutter_init (&argc, &argv) != CLUTTER_INIT_SUCCESS)
    return 1;

  stage = clutter_stage_new ();
  clutter_stage_set_title (CLUTTER_STAGE (stage), "Random Text");

  clutter_actor_show (stage);

  clutter_threads_add_idle (on_idle, stage);

  clutter_main ();

  clutter_actor_destroy (stage);

  return 0;
}
