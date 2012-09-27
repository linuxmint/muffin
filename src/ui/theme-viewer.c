/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Metacity theme viewer and test app main() */

/*
 * Copyright (C) 2002 Havoc Pennington
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 */

#include <config.h>
#include <meta/util.h>
#include <meta/theme.h>
#include "theme-private.h"
#include <meta/preview-widget.h>
#include <gtk/gtk.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include <libintl.h>
#define _(x) dgettext (GETTEXT_PACKAGE, x)
#define N_(x) x

/* We need to compute all different button arrangements
 * in terms of button location. We don't care about
 * different arrangements in terms of button function.
 *
 * So if dups are allowed, from 0-4 buttons on the left, from 0-4 on
 * the right, 5x5=25 combinations.
 *
 * If no dups, 0-4 on left determines the number on the right plus
 * we have a special case for the "no buttons on either side" case.
 */
#ifndef ALLOW_DUPLICATE_BUTTONS
#define BUTTON_LAYOUT_COMBINATIONS (MAX_BUTTONS_PER_CORNER + 1 + 1)
#else
#define BUTTON_LAYOUT_COMBINATIONS ((MAX_BUTTONS_PER_CORNER+1)*(MAX_BUTTONS_PER_CORNER+1))
#endif

enum
{
  FONT_SIZE_SMALL,
  FONT_SIZE_NORMAL,
  FONT_SIZE_LARGE,
  FONT_SIZE_LAST
};

static MetaTheme *global_theme = NULL;
static GtkWidget *previews[META_FRAME_TYPE_LAST*FONT_SIZE_LAST + BUTTON_LAYOUT_COMBINATIONS] = { NULL, };
static double milliseconds_to_draw_frame = 0.0;

static void run_position_expression_tests (void);
#if 0
static void run_position_expression_timings (void);
#endif
static void run_theme_benchmark (void);


static const gchar *menu_item_string =
  "<ui>\n"
    "<menubar>\n"
      "<menu name='Windows' action='Windows'>\n"
        "<menuitem name='Dialog' action='Dialog'/>\n"
        "<menuitem name='Modal dialog' action='Modal dialog'/>\n"
        "<menuitem name='Utility' action='Utility'/>\n"
        "<menuitem name='Splashscreen' action='Splashscreen'/>\n"
        "<menuitem name='Top dock' action='Top dock'/>\n"
        "<menuitem name='Bottom dock' action='Bottom dock'/>\n"
        "<menuitem name='Left dock' action='Left dock'/>\n"
        "<menuitem name='Right dock' action='Right dock'/>\n"
        "<menuitem name='Desktop' action='Desktop'/>\n"
      "</menu>\n"
    "</menubar>\n"
    "<toolbar>\n"
      "<separator/>\n"
      "<toolitem name='New' action='New'/>\n"
      "<toolitem name='Open' action='Open'/>\n"
      "<toolitem name='Quit' action='Quit'/>\n"
      "<separator/>\n"
    "</toolbar>\n"
  "</ui>\n";

static GtkActionEntry menu_items[] =
{
  { "Windows",		NULL, N_("_Windows"),		NULL,		NULL, NULL },
  { "Dialog",		NULL, N_("_Dialog"),		"<control>d",	NULL, NULL },
  { "Modal dialog",	NULL, N_("_Modal dialog"),	NULL,		NULL, NULL },
  { "Utility",		NULL, N_("_Utility"),		"<control>u",	NULL, NULL },
  { "Splashscreen",	NULL, N_("_Splashscreen"),	"<control>s",	NULL, NULL },
  { "Top dock",		NULL, N_("_Top dock"),		NULL,		NULL, NULL },
  { "Bottom dock",	NULL, N_("_Bottom dock"),	NULL,		NULL, NULL },
  { "Left dock",	NULL, N_("_Left dock"),		NULL,		NULL, NULL },
  { "Right dock",	NULL, N_("_Right dock"),	NULL,		NULL, NULL },
  { "All docks",	NULL, N_("_All docks"),		NULL,		NULL, NULL },
  { "Desktop",		NULL, N_("Des_ktop"),		NULL,		NULL, NULL }
};

static GtkActionEntry tool_items[] =
{
  { "New",	GTK_STOCK_NEW,	NULL,	NULL,
    N_("Open another one of these windows"),		NULL },
  { "Open",	GTK_STOCK_OPEN,	NULL,	NULL,
    N_("This is a demo button with an 'open' icon"),	NULL },
  { "Quit",	GTK_STOCK_QUIT,	NULL,	NULL,
    N_("This is a demo button with a 'quit' icon"),	NULL }
};

static GtkWidget *
normal_contents (void)
{
  GtkWidget *grid;
  GtkWidget *statusbar;
  GtkWidget *contents;
  GtkWidget *sw;
  GtkActionGroup *action_group;
  GtkUIManager *ui_manager;
      
  grid = gtk_grid_new ();

  /* Create the menubar
   */

  action_group = gtk_action_group_new ("mainmenu");
  gtk_action_group_add_actions (action_group,
                                menu_items,
                                G_N_ELEMENTS (menu_items),
                                NULL);
  gtk_action_group_add_actions (action_group,
                                tool_items,
                                G_N_ELEMENTS (tool_items),
                                NULL);

  ui_manager = gtk_ui_manager_new ();

  gtk_ui_manager_insert_action_group (ui_manager, action_group, 0);

  /* create menu items */
  gtk_ui_manager_add_ui_from_string (ui_manager, menu_item_string, -1, NULL);

  gtk_grid_attach (GTK_GRID (grid),
                   gtk_ui_manager_get_widget (ui_manager, "/ui/menubar"),
                   0, 0, 1, 1);

  gtk_widget_set_hexpand (gtk_ui_manager_get_widget (ui_manager, "/ui/menubar"),
                          TRUE);

  /* Create the toolbar
   */
  gtk_grid_attach (GTK_GRID (grid),
                   gtk_ui_manager_get_widget (ui_manager, "/ui/toolbar"),
                   0, 1, 1, 1);

  gtk_widget_set_hexpand (gtk_ui_manager_get_widget (ui_manager, "/ui/toolbar"),
                          TRUE);

  /* Create document
   */

  sw = gtk_scrolled_window_new (NULL, NULL);

  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);

  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw),
                                       GTK_SHADOW_IN);
      
  gtk_grid_attach (GTK_GRID (grid),
                   sw,
                   0, 2, 1, 1);

  gtk_widget_set_hexpand (sw, TRUE);
  gtk_widget_set_vexpand (sw, TRUE);
      
  contents = gtk_text_view_new ();
  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (contents),
                               PANGO_WRAP_WORD);
      
  gtk_container_add (GTK_CONTAINER (sw),
                     contents);

  /* Create statusbar */

  statusbar = gtk_statusbar_new ();
  gtk_grid_attach (GTK_GRID (grid),
                   statusbar,
                   0, 3, 1, 1);

  gtk_widget_set_hexpand (statusbar, TRUE);

  gtk_widget_show_all (grid);

  g_object_unref (ui_manager);

  return grid;
}

static void
update_spacings (GtkWidget *vbox,
                 GtkWidget *action_area)
{
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 2);
  gtk_box_set_spacing (GTK_BOX (action_area), 10);
  gtk_container_set_border_width (GTK_CONTAINER (action_area), 5);
}

static GtkWidget*
dialog_contents (void)
{
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *action_area;
  GtkWidget *label;
  GtkWidget *image;
  GtkWidget *button;
  
  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  action_area = gtk_button_box_new (GTK_ORIENTATION_HORIZONTAL);

  gtk_button_box_set_layout (GTK_BUTTON_BOX (action_area),
                             GTK_BUTTONBOX_END);  

  button = gtk_button_new_from_stock (GTK_STOCK_OK);
  gtk_box_pack_end (GTK_BOX (action_area),
                    button,
                    FALSE, TRUE, 0);
  
  gtk_box_pack_end (GTK_BOX (vbox), action_area,
                    FALSE, TRUE, 0);

  update_spacings (vbox, action_area);

  label = gtk_label_new (_("This is a sample message in a sample dialog"));
  image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_INFO,
                                    GTK_ICON_SIZE_DIALOG);
  gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0.0);
  
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_selectable (GTK_LABEL (label), TRUE);
  
  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

  gtk_box_pack_start (GTK_BOX (hbox), image,
                      FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (hbox), label,
                      TRUE, TRUE, 0);

  gtk_box_pack_start (GTK_BOX (vbox),
                      hbox,
                      FALSE, FALSE, 0);

  gtk_widget_show_all (vbox);

  return vbox;
}

static GtkWidget*
utility_contents (void)
{
  GtkWidget *grid;
  GtkWidget *button;
  int i, j;

  grid = gtk_grid_new ();

  i = 0;
  while (i < 3)
    {
      j = 0;
      while (j < 4)
        {
          char *str;

          str = g_strdup_printf ("_%c", (char) ('A' + 4*i + j));
          
          button = gtk_button_new_with_mnemonic (str);

          g_free (str);
          
          gtk_grid_attach (GTK_GRID (grid),
                           button,
                           i, j, 1, 1);

          ++j;
        }

      ++i;
    }

  gtk_widget_show_all (grid);
  
  return grid;
}

static GtkWidget*
menu_contents (void)
{
  GtkWidget *vbox;
  GtkWidget *mi;  
  int i;
  GtkWidget *frame;

  frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (frame),
                             GTK_SHADOW_OUT);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  i = 0;
  while (i < 10)
    {
      char *str = g_strdup_printf (_("Fake menu item %d\n"), i + 1);
      mi = gtk_label_new (str);
      gtk_misc_set_alignment (GTK_MISC (mi), 0.0, 0.5);
      g_free (str);
      gtk_box_pack_start (GTK_BOX (vbox), mi, FALSE, FALSE, 0);
      
      ++i;
    }

  gtk_container_add (GTK_CONTAINER (frame), vbox);
  
  gtk_widget_show_all (frame);
  
  return frame;
}

static GtkWidget*
border_only_contents (void)
{
  GtkWidget *event_box;
  GtkWidget *vbox;
  GtkWidget *w;
  GdkRGBA color;

  event_box = gtk_event_box_new ();

  color.red = 0.6;
  color.green = 0;
  color.blue = 0.6;
  color.alpha = 1.0;
  gtk_widget_override_background_color (event_box, 0, &color);
  
  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 3);
  
  w = gtk_label_new (_("Border-only window"));
  gtk_box_pack_start (GTK_BOX (vbox), w, FALSE, FALSE, 0);
  w = gtk_button_new_with_label (_("Bar"));
  gtk_box_pack_start (GTK_BOX (vbox), w, FALSE, FALSE, 0);

  gtk_container_add (GTK_CONTAINER (event_box), vbox);
  
  gtk_widget_show_all (event_box);
  
  return event_box;
}

static GtkWidget*
get_window_contents (MetaFrameType  type,
                     const char   **title)
{
  switch (type)
    {
    case META_FRAME_TYPE_NORMAL:
      *title = _("Normal Application Window");
      return normal_contents ();

    case META_FRAME_TYPE_DIALOG:
      *title = _("Dialog Box");
      return dialog_contents ();

    case META_FRAME_TYPE_MODAL_DIALOG:
      *title = _("Modal Dialog Box");
      return dialog_contents ();

    case META_FRAME_TYPE_UTILITY:
      *title = _("Utility Palette");
      return utility_contents ();

    case META_FRAME_TYPE_MENU:
      *title = _("Torn-off Menu");
      return menu_contents ();

    case META_FRAME_TYPE_BORDER:
      *title = _("Border");
      return border_only_contents ();

    case META_FRAME_TYPE_ATTACHED:
      *title = _("Attached Modal Dialog");
      return dialog_contents ();
      
    case META_FRAME_TYPE_LAST:
      g_assert_not_reached ();
      break;
    }

  return NULL;
}

static MetaFrameFlags
get_window_flags (MetaFrameType type)
{
  MetaFrameFlags flags;

  flags = META_FRAME_ALLOWS_DELETE |
    META_FRAME_ALLOWS_MENU |
    META_FRAME_ALLOWS_MINIMIZE |
    META_FRAME_ALLOWS_MAXIMIZE |
    META_FRAME_ALLOWS_VERTICAL_RESIZE |
    META_FRAME_ALLOWS_HORIZONTAL_RESIZE |
    META_FRAME_HAS_FOCUS |
    META_FRAME_ALLOWS_SHADE |
    META_FRAME_ALLOWS_MOVE;
  
  switch (type)
    {
    case META_FRAME_TYPE_NORMAL:
      break;

    case META_FRAME_TYPE_DIALOG:
    case META_FRAME_TYPE_MODAL_DIALOG:
      flags &= ~(META_FRAME_ALLOWS_MINIMIZE |
                 META_FRAME_ALLOWS_MAXIMIZE);
      break;

    case META_FRAME_TYPE_UTILITY:
      flags &= ~(META_FRAME_ALLOWS_MINIMIZE |
                 META_FRAME_ALLOWS_MAXIMIZE);
      break;

    case META_FRAME_TYPE_MENU:
      flags &= ~(META_FRAME_ALLOWS_MINIMIZE |
                 META_FRAME_ALLOWS_MAXIMIZE);
      break;

    case META_FRAME_TYPE_BORDER:
      break;

    case META_FRAME_TYPE_ATTACHED:
      break;
      
    case META_FRAME_TYPE_LAST:
      g_assert_not_reached ();
      break;
    }  
  
  return flags;
}

static GtkWidget*
preview_collection (int font_size,
                    const PangoFontDescription *base_desc)
{
  GtkWidget *box;
  GtkWidget *sw;
  GdkRGBA desktop_color;
  int i;
  GtkWidget *eventbox;

  sw = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_set_spacing (GTK_BOX (box), 20);
  gtk_container_set_border_width (GTK_CONTAINER (box), 20);

  eventbox = gtk_event_box_new ();
  gtk_container_add (GTK_CONTAINER (eventbox), box);
  
  gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (sw), eventbox);

  desktop_color.red = 0.32;
  desktop_color.green = 0.46;
  desktop_color.blue = 0.65;
  desktop_color.alpha = 1.0;

  gtk_widget_override_background_color (eventbox, 0, &desktop_color);

  i = 0;
  while (i < META_FRAME_TYPE_LAST)
    {
      const char *title = NULL;
      GtkWidget *contents;
      GtkWidget *align;
      double xalign, yalign;
      GtkWidget *eventbox2;
      GtkWidget *preview;
      PangoFontDescription *font_desc;
      double scale;
      
      eventbox2 = gtk_event_box_new ();
      
      preview = meta_preview_new ();
      
      gtk_container_add (GTK_CONTAINER (eventbox2), preview);
      
      meta_preview_set_frame_type (META_PREVIEW (preview), i);
      meta_preview_set_frame_flags (META_PREVIEW (preview),
                                    get_window_flags (i));
            
      meta_preview_set_theme (META_PREVIEW (preview), global_theme);
      
      contents = get_window_contents (i, &title);
      
      meta_preview_set_title (META_PREVIEW (preview), title);
      
      gtk_container_add (GTK_CONTAINER (preview), contents);

      if (i == META_FRAME_TYPE_MENU)
        {
          xalign = 0.0;
          yalign = 0.0;
        }
      else
        {
          xalign = 0.5;
          yalign = 0.5;
        }
      
      align = gtk_alignment_new (0.0, 0.0, xalign, yalign);
      gtk_container_add (GTK_CONTAINER (align), eventbox2);
      
      gtk_box_pack_start (GTK_BOX (box), align, TRUE, TRUE, 0);

      switch (font_size)
        {
        case FONT_SIZE_SMALL:
          scale = PANGO_SCALE_XX_SMALL;
          break;
        case FONT_SIZE_LARGE:
          scale = PANGO_SCALE_XX_LARGE;
          break;
        default:
          scale = 1.0;
          break;
        }

      if (scale != 1.0)
        {
          font_desc = pango_font_description_new ();
          
          pango_font_description_set_size (font_desc,
                                           MAX (pango_font_description_get_size (base_desc) * scale, 1));
          
          gtk_widget_modify_font (preview, font_desc);

          pango_font_description_free (font_desc);
        }
      
      previews[font_size*META_FRAME_TYPE_LAST + i] = preview;
      
      ++i;
    }

  return sw;
}

static MetaButtonLayout different_layouts[BUTTON_LAYOUT_COMBINATIONS];

static void
init_layouts (void)
{
  int i;

  /* Blank out all the layouts */
  i = 0;
  while (i < (int) G_N_ELEMENTS (different_layouts))
    {
      int j;

      j = 0;
      while (j < MAX_BUTTONS_PER_CORNER)
        {
          different_layouts[i].left_buttons[j] = META_BUTTON_FUNCTION_LAST;
          different_layouts[i].right_buttons[j] = META_BUTTON_FUNCTION_LAST;
          ++j;
        }
      ++i;
    }
  
#ifndef ALLOW_DUPLICATE_BUTTONS
  i = 0;
  while (i <= MAX_BUTTONS_PER_CORNER)
    {
      int j;
      
      j = 0;
      while (j < i)
        {
          different_layouts[i].right_buttons[j] = (MetaButtonFunction) j;
          ++j;
        }
      while (j < MAX_BUTTONS_PER_CORNER)
        {
          different_layouts[i].left_buttons[j-i] = (MetaButtonFunction) j;
          ++j;
        }
      
      ++i;
    }

  /* Special extra case for no buttons on either side */
  different_layouts[i].left_buttons[0] = META_BUTTON_FUNCTION_LAST;
  different_layouts[i].right_buttons[0] = META_BUTTON_FUNCTION_LAST;
  
#else
  /* FIXME this code is if we allow duplicate buttons,
   * which we currently do not
   */
  int left;
  int i;
  
  left = 0;
  i = 0;

  while (left < MAX_BUTTONS_PER_CORNER)
    {
      int right;
      
      right = 0;
      
      while (right < MAX_BUTTONS_PER_CORNER)
        {
          int j;
          
          static MetaButtonFunction left_functions[MAX_BUTTONS_PER_CORNER] = {
            META_BUTTON_FUNCTION_MENU,
            META_BUTTON_FUNCTION_MINIMIZE,
            META_BUTTON_FUNCTION_MAXIMIZE,
            META_BUTTON_FUNCTION_CLOSE
          };
          static MetaButtonFunction right_functions[MAX_BUTTONS_PER_CORNER] = {
            META_BUTTON_FUNCTION_MINIMIZE,
            META_BUTTON_FUNCTION_MAXIMIZE,
            META_BUTTON_FUNCTION_CLOSE,
            META_BUTTON_FUNCTION_MENU
          };

          g_assert (i < BUTTON_LAYOUT_COMBINATIONS);
          
          j = 0;
          while (j <= left)
            {
              different_layouts[i].left_buttons[j] = left_functions[j];
              ++j;
            }

          j = 0;
          while (j <= right)
            {
              different_layouts[i].right_buttons[j] = right_functions[j];
              ++j;
            }
          
          ++i;
          
          ++right;
        }
      
      ++left;
    }
#endif
}


static GtkWidget*
previews_of_button_layouts (void)
{
  static gboolean initted = FALSE;
  GtkWidget *box;
  GtkWidget *sw;
  GdkRGBA desktop_color;
  int i;
  GtkWidget *eventbox;
  
  if (!initted)
    {
      init_layouts ();
      initted = TRUE;
    }
  
  sw = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_set_spacing (GTK_BOX (box), 20);
  gtk_container_set_border_width (GTK_CONTAINER (box), 20);

  eventbox = gtk_event_box_new ();
  gtk_container_add (GTK_CONTAINER (eventbox), box);
  
  gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (sw), eventbox);

  desktop_color.red = 0.32;
  desktop_color.green = 0.46;
  desktop_color.blue = 0.65;
  desktop_color.alpha = 1.0;

  gtk_widget_override_background_color (eventbox, 0, &desktop_color);

  i = 0;
  while (i < BUTTON_LAYOUT_COMBINATIONS)
    {
      GtkWidget *align;
      double xalign, yalign;
      GtkWidget *eventbox2;
      GtkWidget *preview;
      char *title;

      eventbox2 = gtk_event_box_new ();
  
      preview = meta_preview_new ();
  
      gtk_container_add (GTK_CONTAINER (eventbox2), preview);  
  
      meta_preview_set_theme (META_PREVIEW (preview), global_theme);

      title = g_strdup_printf (_("Button layout test %d"), i+1);
      meta_preview_set_title (META_PREVIEW (preview), title);
      g_free (title);

      meta_preview_set_button_layout (META_PREVIEW (preview),
                                      &different_layouts[i]);
  
      xalign = 0.5;
      yalign = 0.5;
      
      align = gtk_alignment_new (0.0, 0.0, xalign, yalign);
      gtk_container_add (GTK_CONTAINER (align), eventbox2);
  
      gtk_box_pack_start (GTK_BOX (box), align, TRUE, TRUE, 0);

      previews[META_FRAME_TYPE_LAST*FONT_SIZE_LAST + i] = preview;
      
      ++i;
    }
  
  return sw;
}

static GtkWidget*
benchmark_summary (void)
{
  char *msg;
  GtkWidget *label;
  
  msg = g_strdup_printf (_("%g milliseconds to draw one window frame"),
                         milliseconds_to_draw_frame);
  label = gtk_label_new (msg);
  g_free (msg);

  return label;
}

int
main (int argc, char **argv)
{
  GtkStyleContext *style;
  const PangoFontDescription *font_desc;
  GtkWidget *window;
  GtkWidget *collection;
  GError *err;
  clock_t start, end;
  GtkWidget *notebook;
  int i;
  
  bindtextdomain (GETTEXT_PACKAGE, MUFFIN_LOCALEDIR);
  textdomain(GETTEXT_PACKAGE);
  bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");

  run_position_expression_tests ();
#if 0
  run_position_expression_timings ();
#endif

  gtk_init (&argc, &argv);

  if (g_getenv ("MUFFIN_DEBUG") != NULL)
    {
      meta_set_debugging (TRUE);
      meta_set_verbose (TRUE);
    }
  
  start = clock ();
  err = NULL;
  if (argc == 1)
    global_theme = meta_theme_load ("Atlanta", &err);
  else if (argc == 2)
    global_theme = meta_theme_load (argv[1], &err);
  else
    {
      g_printerr (_("Usage: metacity-theme-viewer [THEMENAME]\n"));
      exit (1);
    }
  end = clock ();

  if (global_theme == NULL)
    {
      g_printerr (_("Error loading theme: %s\n"),
                  err->message);
      g_error_free (err);
      exit (1);
    }

  g_print (_("Loaded theme \"%s\" in %g seconds\n"),
           global_theme->name,
           (end - start) / (double) CLOCKS_PER_SEC);

  run_theme_benchmark ();
  
  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_default_size (GTK_WINDOW (window), 350, 350);

  if (strcmp (global_theme->name, global_theme->readable_name)==0)
    gtk_window_set_title (GTK_WINDOW (window),
                          global_theme->readable_name);
  else
    {
      /* The theme directory name is different from the name the theme
       * gives itself within its file.  Display both, directory name first.
       */
      gchar *title =  g_strconcat (global_theme->name, " - ",
                                   global_theme->readable_name,
                                   NULL);

      gtk_window_set_title (GTK_WINDOW (window),
                            title);

      g_free (title);
    }       

  g_signal_connect (G_OBJECT (window), "destroy",
                    G_CALLBACK (gtk_main_quit), NULL);

  gtk_widget_realize (window);
  style = gtk_widget_get_style_context (window);
  font_desc = gtk_style_context_get_font (style, 0);

  g_assert (style);
  g_assert (font_desc);
  
  notebook = gtk_notebook_new ();
  gtk_container_add (GTK_CONTAINER (window), notebook);

  collection = preview_collection (FONT_SIZE_NORMAL,
                                   font_desc);
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
                            collection,
                            gtk_label_new (_("Normal Title Font")));
  
  collection = preview_collection (FONT_SIZE_SMALL,
                                   font_desc);
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
                            collection,
                            gtk_label_new (_("Small Title Font")));
  
  collection = preview_collection (FONT_SIZE_LARGE,
                                   font_desc);
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
                            collection,
                            gtk_label_new (_("Large Title Font")));

  collection = previews_of_button_layouts ();
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
                            collection,
                            gtk_label_new (_("Button Layouts")));

  collection = benchmark_summary ();
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
                            collection,
                            gtk_label_new (_("Benchmark")));
  
  i = 0;
  while (i < (int) G_N_ELEMENTS (previews))
    {
      /* preview widget likes to be realized before its size request.
       * it's lame that way.
       */
      gtk_widget_realize (previews[i]);

      ++i;
    }
  
  gtk_widget_show_all (window);

  gtk_main ();

  return 0;
}


static MetaFrameFlags
get_flags (GtkWidget *widget)
{
  return META_FRAME_ALLOWS_DELETE |
    META_FRAME_ALLOWS_MENU |
    META_FRAME_ALLOWS_MINIMIZE |
    META_FRAME_ALLOWS_MAXIMIZE |
    META_FRAME_ALLOWS_VERTICAL_RESIZE |
    META_FRAME_ALLOWS_HORIZONTAL_RESIZE |
    META_FRAME_HAS_FOCUS |
    META_FRAME_ALLOWS_SHADE |
    META_FRAME_ALLOWS_MOVE;
}

static int
get_text_height (GtkWidget *widget)
{
  GtkStyleContext *style;
  const PangoFontDescription *font_desc;

  style = gtk_widget_get_style_context (widget);
  font_desc = gtk_style_context_get_font (style, 0);
  return meta_pango_font_desc_get_text_height (font_desc,
                                               gtk_widget_get_pango_context (widget));
}

static PangoLayout*
create_title_layout (GtkWidget *widget)
{
  PangoLayout *layout;

  layout = gtk_widget_create_pango_layout (widget, _("Window Title Goes Here"));

  return layout;
}

static void
run_theme_benchmark (void)
{
  GtkWidget* widget;
  cairo_surface_t *pixmap;
  MetaFrameBorders borders;
  MetaButtonState button_states[META_BUTTON_TYPE_LAST] =
  {
    META_BUTTON_STATE_NORMAL,
    META_BUTTON_STATE_NORMAL,
    META_BUTTON_STATE_NORMAL,
    META_BUTTON_STATE_NORMAL
  };
  PangoLayout *layout;
  clock_t start;
  clock_t end;
  GTimer *timer;
  int i;
  MetaButtonLayout button_layout;
#define ITERATIONS 100
  int client_width;
  int client_height;
  cairo_t *cr;
  int inc;
  
  widget = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_widget_realize (widget);
  
  meta_theme_get_frame_borders (global_theme,
                                META_FRAME_TYPE_NORMAL,
                                get_text_height (widget),
                                get_flags (widget),
                                &borders);
  
  layout = create_title_layout (widget);
  
  i = 0;
  while (i < MAX_BUTTONS_PER_CORNER)
    {
      button_layout.left_buttons[i] = META_BUTTON_FUNCTION_LAST;
      button_layout.right_buttons[i] = META_BUTTON_FUNCTION_LAST;
      ++i;
    }
  
  button_layout.left_buttons[0] = META_BUTTON_FUNCTION_MENU;

  button_layout.right_buttons[0] = META_BUTTON_FUNCTION_MINIMIZE;
  button_layout.right_buttons[1] = META_BUTTON_FUNCTION_MAXIMIZE;
  button_layout.right_buttons[2] = META_BUTTON_FUNCTION_CLOSE;

  timer = g_timer_new ();
  start = clock ();

  client_width = 50;
  client_height = 50;
  inc = 1000 / ITERATIONS; /* Increment to grow width/height,
                            * eliminates caching effects.
                            */
  
  i = 0;
  while (i < ITERATIONS)
    {
      /* Creating the pixmap in the loop is right, since
       * GDK does the same with its double buffering.
       */
      pixmap = gdk_window_create_similar_surface (gtk_widget_get_window (widget),
                                                  CAIRO_CONTENT_COLOR,
                                                  client_width + borders.total.left + borders.total.right,
                                                  client_height + borders.total.top + borders.total.bottom);

      cr = cairo_create (pixmap);

      meta_theme_draw_frame (global_theme,
                             widget,
                             cr,
                             META_FRAME_TYPE_NORMAL,
                             get_flags (widget),
                             client_width, client_height,
                             layout,
                             get_text_height (widget),
                             &button_layout,
                             button_states,
                             meta_preview_get_mini_icon (),
                             meta_preview_get_icon ());

      cairo_destroy (cr);
      cairo_surface_destroy (pixmap);
      
      ++i;
      client_width += inc;
      client_height += inc;
    }

  end = clock ();
  g_timer_stop (timer);

  milliseconds_to_draw_frame = (g_timer_elapsed (timer, NULL) / (double) ITERATIONS) * 1000;
  
  g_print (_("Drew %d frames in %g client-side seconds (%g milliseconds per frame) and %g seconds wall clock time including X server resources (%g milliseconds per frame)\n"),
           ITERATIONS,
           ((double)end - (double)start) / CLOCKS_PER_SEC,
           (((double)end - (double)start) / CLOCKS_PER_SEC / (double) ITERATIONS) * 1000,
           g_timer_elapsed (timer, NULL),
           milliseconds_to_draw_frame);

  g_timer_destroy (timer);
  g_object_unref (G_OBJECT (layout));
  gtk_widget_destroy (widget);

#undef ITERATIONS
}

typedef struct
{
  GdkRectangle rect;
  const char *expr;
  int expected_x;
  int expected_y;
  MetaThemeError expected_error;
} PositionExpressionTest;

#define NO_ERROR -1

static const PositionExpressionTest position_expression_tests[] = {
  /* Just numbers */
  { { 10, 20, 40, 50 },
    "10", 20, 30, NO_ERROR },
  { { 10, 20, 40, 50 },
    "14.37", 24, 34, NO_ERROR },
  /* Binary expressions with 2 ints */
  { { 10, 20, 40, 50 },
    "14 * 10", 150, 160, NO_ERROR },
  { { 10, 20, 40, 50 },
    "14 + 10", 34, 44, NO_ERROR },
  { { 10, 20, 40, 50 },
    "14 - 10", 14, 24, NO_ERROR },
  { { 10, 20, 40, 50 },
    "8 / 2", 14, 24, NO_ERROR },
  { { 10, 20, 40, 50 },
    "8 % 3", 12, 22, NO_ERROR },
  /* Binary expressions with floats and mixed float/ints */
  { { 10, 20, 40, 50 },
    "7.0 / 3.5", 12, 22, NO_ERROR },
  { { 10, 20, 40, 50 },
    "12.1 / 3", 14, 24, NO_ERROR },
  { { 10, 20, 40, 50 },
    "12 / 2.95", 14, 24, NO_ERROR },
  /* Binary expressions without whitespace after first number */
  { { 10, 20, 40, 50 },
    "14* 10", 150, 160, NO_ERROR },
  { { 10, 20, 40, 50 },
    "14+ 10", 34, 44, NO_ERROR },
  { { 10, 20, 40, 50 },
    "14- 10", 14, 24, NO_ERROR },
  { { 10, 20, 40, 50 },
    "8/ 2", 14, 24, NO_ERROR },
  { { 10, 20, 40, 50 },
    "7.0/ 3.5", 12, 22, NO_ERROR },
  { { 10, 20, 40, 50 },
    "12.1/ 3", 14, 24, NO_ERROR },
  { { 10, 20, 40, 50 },
    "12/ 2.95", 14, 24, NO_ERROR },
  /* Binary expressions without whitespace before second number */
  { { 10, 20, 40, 50 },
    "14 *10", 150, 160, NO_ERROR },
  { { 10, 20, 40, 50 },
    "14 +10", 34, 44, NO_ERROR },
  { { 10, 20, 40, 50 },
    "14 -10", 14, 24, NO_ERROR },
  { { 10, 20, 40, 50 },
    "8 /2", 14, 24, NO_ERROR },
  { { 10, 20, 40, 50 },
    "7.0 /3.5", 12, 22, NO_ERROR },
  { { 10, 20, 40, 50 },
    "12.1 /3", 14, 24, NO_ERROR },
  { { 10, 20, 40, 50 },
    "12 /2.95", 14, 24, NO_ERROR },
  /* Binary expressions without any whitespace */
  { { 10, 20, 40, 50 },
    "14*10", 150, 160, NO_ERROR },
  { { 10, 20, 40, 50 },
    "14+10", 34, 44, NO_ERROR },
  { { 10, 20, 40, 50 },
    "14-10", 14, 24, NO_ERROR },
  { { 10, 20, 40, 50 },
    "8/2", 14, 24, NO_ERROR },
  { { 10, 20, 40, 50 },
    "7.0/3.5", 12, 22, NO_ERROR },
  { { 10, 20, 40, 50 },
    "12.1/3", 14, 24, NO_ERROR },
  { { 10, 20, 40, 50 },
    "12/2.95", 14, 24, NO_ERROR },
  /* Binary expressions with parentheses */
  { { 10, 20, 40, 50 },
    "(14) * (10)", 150, 160, NO_ERROR },
  { { 10, 20, 40, 50 },
    "(14) + (10)", 34, 44, NO_ERROR },
  { { 10, 20, 40, 50 },
    "(14) - (10)", 14, 24, NO_ERROR },
  { { 10, 20, 40, 50 },
    "(8) / (2)", 14, 24, NO_ERROR },
  { { 10, 20, 40, 50 },
    "(7.0) / (3.5)", 12, 22, NO_ERROR },
  { { 10, 20, 40, 50 },
    "(12.1) / (3)", 14, 24, NO_ERROR },
  { { 10, 20, 40, 50 },
    "(12) / (2.95)", 14, 24, NO_ERROR },
  /* Lots of extra parentheses */
  { { 10, 20, 40, 50 },
    "(((14)) * ((10)))", 150, 160, NO_ERROR },
  { { 10, 20, 40, 50 },
    "((((14)))) + ((((((((10))))))))", 34, 44, NO_ERROR },
  { { 10, 20, 40, 50 },
    "((((((((((14 - 10))))))))))", 14, 24, NO_ERROR },
  /* Binary expressions with variables */
  { { 10, 20, 40, 50 },
    "2 * width", 90, 100, NO_ERROR },
  { { 10, 20, 40, 50 },
    "2 * height", 110, 120, NO_ERROR },
  { { 10, 20, 40, 50 },
    "width - 10", 40, 50, NO_ERROR },
  { { 10, 20, 40, 50 },
    "height / 2", 35, 45, NO_ERROR },
  /* More than two operands */
  { { 10, 20, 40, 50 },
    "8 / 2 + 5", 19, 29, NO_ERROR },
  { { 10, 20, 40, 50 },
    "8 * 2 + 5", 31, 41, NO_ERROR },
  { { 10, 20, 40, 50 },
    "8 + 2 * 5", 28, 38, NO_ERROR },
  { { 10, 20, 40, 50 },
    "8 + 8 / 2", 22, 32, NO_ERROR },
  { { 10, 20, 40, 50 },
    "14 / (2 + 5)", 12, 22, NO_ERROR },
  { { 10, 20, 40, 50 },
    "8 * (2 + 5)", 66, 76, NO_ERROR },
  { { 10, 20, 40, 50 },
    "(8 + 2) * 5", 60, 70, NO_ERROR },
  { { 10, 20, 40, 50 },
    "(8 + 8) / 2", 18, 28, NO_ERROR },
  /* Errors */
  { { 10, 20, 40, 50 },
    "2 * foo", 0, 0, META_THEME_ERROR_UNKNOWN_VARIABLE },
  { { 10, 20, 40, 50 },
    "2 *", 0, 0, META_THEME_ERROR_FAILED },
  { { 10, 20, 40, 50 },
    "- width", 0, 0, META_THEME_ERROR_FAILED },
  { { 10, 20, 40, 50 },
    "5 % 1.0", 0, 0, META_THEME_ERROR_MOD_ON_FLOAT },
  { { 10, 20, 40, 50 },
    "1.0 % 5", 0, 0, META_THEME_ERROR_MOD_ON_FLOAT },
  { { 10, 20, 40, 50 },
    "! * 2", 0, 0, META_THEME_ERROR_BAD_CHARACTER },
  { { 10, 20, 40, 50 },
    "   ", 0, 0, META_THEME_ERROR_FAILED },
  { { 10, 20, 40, 50 },
    "() () (( ) ()) ((()))", 0, 0, META_THEME_ERROR_FAILED },
  { { 10, 20, 40, 50 },
    "(*) () ((/) ()) ((()))", 0, 0, META_THEME_ERROR_FAILED },
  { { 10, 20, 40, 50 },
    "2 * 5 /", 0, 0, META_THEME_ERROR_FAILED },
  { { 10, 20, 40, 50 },
    "+ 2 * 5", 0, 0, META_THEME_ERROR_FAILED },
  { { 10, 20, 40, 50 },
    "+ 2 * 5", 0, 0, META_THEME_ERROR_FAILED }
};

static void
run_position_expression_tests (void)
{
#if 0
  int i;
  MetaPositionExprEnv env;

  i = 0;
  while (i < (int) G_N_ELEMENTS (position_expression_tests))
    {
      GError *err;
      gboolean retval;
      const PositionExpressionTest *test;
      PosToken *tokens;
      int n_tokens;
      int x, y;

      test = &position_expression_tests[i];

      if (g_getenv ("META_PRINT_TESTS") != NULL)
        g_print ("Test expression: \"%s\" expecting x = %d y = %d",
                 test->expr, test->expected_x, test->expected_y);

      err = NULL;
      
      env.rect = meta_rect (test->rect.x, test->rect.y,
                            test->rect.width, test->rect.height);
      env.object_width = -1;
      env.object_height = -1;
      env.left_width = 0;
      env.right_width = 0;
      env.top_height = 0;
      env.bottom_height = 0;
      env.title_width = 5;
      env.title_height = 5;
      env.icon_width = 32;
      env.icon_height = 32;
      env.mini_icon_width = 16;
      env.mini_icon_height = 16;
      env.theme = NULL;

      if (err == NULL)
        {
          retval = meta_parse_position_expression (tokens, n_tokens,
                                                   &env,
                                                   &x, &y,
                                                   &err);
        }

      if (retval && err)
        g_error (_("position expression test returned TRUE but set error"));
      if (!retval && err == NULL)
        g_error (_("position expression test returned FALSE but didn't set error"));
      if (((int) test->expected_error) != NO_ERROR)
        {
          if (err == NULL)
            g_error (_("Error was expected but none given"));
          if (err->code != (int) test->expected_error)
            g_error (_("Error %d was expected but %d given"),
                     test->expected_error, err->code);
        }
      else
        {
          if (err)
            g_error (_("Error not expected but one was returned: %s"),
                     err->message);

          if (x != test->expected_x)
            g_error (_("x value was %d, %d was expected"), x, test->expected_x);

          if (y != test->expected_y)
            g_error (_("y value was %d, %d was expected"), y, test->expected_y);
        }

      if (err)
        g_error_free (err);

      meta_pos_tokens_free (tokens, n_tokens);
      ++i;
    }
#endif
}

#if 0
static void
run_position_expression_timings (void)
{
  int i;
  int iters;
  clock_t start;
  clock_t end;
  MetaPositionExprEnv env;

#define ITERATIONS 100000

  start = clock ();

  iters = 0;
  i = 0;
  while (iters < ITERATIONS)
    {
      const PositionExpressionTest *test;
      int x, y;

      test = &position_expression_tests[i];

      env.x = test->rect.x;
      env.y = test->rect.y;
      env.width = test->rect.width;
      env.height = test->rect.height;
      env.object_width = -1;
      env.object_height = -1;
      env.left_width = 0;
      env.right_width = 0;
      env.top_height = 0;
      env.bottom_height = 0;
      env.title_width = 5;
      env.title_height = 5;
      env.icon_width = 32;
      env.icon_height = 32;
      env.mini_icon_width = 16;
      env.mini_icon_height = 16;
      env.theme = NULL;

      meta_parse_position_expression (test->expr,
                                      &env,
                                      &x, &y, NULL);

      ++iters;
      ++i;
      if (i == G_N_ELEMENTS (position_expression_tests))
        i = 0;
    }

  end = clock ();

  g_print (_("%d coordinate expressions parsed in %g seconds (%g seconds average)\n"),
           ITERATIONS,
           ((double)end - (double)start) / CLOCKS_PER_SEC,
           ((double)end - (double)start) / CLOCKS_PER_SEC / (double) ITERATIONS);

}
#endif
