/*
 * Copyright (C) 2017,2018 Red Hat
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#ifndef __CLUTTER_INPUT_FOCUS_H__
#define __CLUTTER_INPUT_FOCUS_H__

#include <clutter/clutter.h>

#define CLUTTER_TYPE_INPUT_FOCUS (clutter_input_focus_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterInputFocus, clutter_input_focus,
                          CLUTTER, INPUT_FOCUS, GObject)

struct _ClutterInputFocusClass
{
  GObjectClass parent_class;
  GTypeInterface iface;

  void (* focus_in)  (ClutterInputFocus  *focus,
                      ClutterInputMethod *input_method);
  void (* focus_out) (ClutterInputFocus  *focus);

  void (* request_surrounding) (ClutterInputFocus *focus);
  void (* delete_surrounding)  (ClutterInputFocus *focus,
                                int                offset,
                                guint              len);
  void (* commit_text) (ClutterInputFocus *focus,
                        const gchar       *text);

  void (* set_preedit_text) (ClutterInputFocus *focus,
                             const gchar       *preedit,
                             guint              cursor);
};

CLUTTER_EXPORT
gboolean clutter_input_focus_is_focused (ClutterInputFocus *focus);

CLUTTER_EXPORT
void clutter_input_focus_reset (ClutterInputFocus *focus);
CLUTTER_EXPORT
void clutter_input_focus_set_cursor_location (ClutterInputFocus     *focus,
                                              const graphene_rect_t *rect);

CLUTTER_EXPORT
void clutter_input_focus_set_surrounding (ClutterInputFocus *focus,
                                          const gchar       *text,
                                          guint              cursor,
                                          guint              anchor);
CLUTTER_EXPORT
void clutter_input_focus_set_content_hints (ClutterInputFocus            *focus,
                                            ClutterInputContentHintFlags  hint);
CLUTTER_EXPORT
void clutter_input_focus_set_content_purpose (ClutterInputFocus          *focus,
                                              ClutterInputContentPurpose  purpose);
CLUTTER_EXPORT
gboolean clutter_input_focus_filter_event (ClutterInputFocus  *focus,
                                           const ClutterEvent *event);
CLUTTER_EXPORT
void clutter_input_focus_set_can_show_preedit (ClutterInputFocus *focus,
                                               gboolean           can_show_preedit);
CLUTTER_EXPORT
void clutter_input_focus_set_input_panel_state (ClutterInputFocus      *focus,
                                                ClutterInputPanelState  state);

#endif /* __CLUTTER_INPUT_FOCUS_H__ */
