/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2008  Intel Corporation.
 *
 * Authored By: Øyvind Kolås <pippin@o-hand.com>
 *              Emmanuele Bassi <ebassi@linux.intel.com>
 *
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
 */

#ifndef __CLUTTER_TEXT_H__
#define __CLUTTER_TEXT_H__

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <clutter/clutter-actor.h>
#include <clutter/clutter-text-buffer.h>
#include <pango/pango.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_TEXT               (clutter_text_get_type ())
#define CLUTTER_TEXT(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_TEXT, ClutterText))
#define CLUTTER_TEXT_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass), CLUTTER_TYPE_TEXT, ClutterTextClass))
#define CLUTTER_IS_TEXT(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_TEXT))
#define CLUTTER_IS_TEXT_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass), CLUTTER_TYPE_TEXT))
#define CLUTTER_TEXT_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj), CLUTTER_TYPE_TEXT, ClutterTextClass))

typedef struct _ClutterText        ClutterText;
typedef struct _ClutterTextPrivate ClutterTextPrivate;
typedef struct _ClutterTextClass   ClutterTextClass;

/**
 * ClutterText:
 *
 * The #ClutterText struct contains only private data.
 *
 * Since: 1.0
 */
struct _ClutterText
{
  /*< private >*/
  ClutterActor parent_instance;

  ClutterTextPrivate *priv;
};

/**
 * ClutterTextClass:
 * @text_changed: class handler for the #ClutterText::text-changed signal
 * @activate: class handler for the #ClutterText::activate signal
 * @cursor_event: class handler for the #ClutterText::cursor-event signal
 * @cursor_changed: class handler for the #ClutterText::cursor-changed signal
 *
 * The #ClutterTextClass struct contains only private data.
 *
 * Since: 1.0
 */
struct _ClutterTextClass
{
  /*< private >*/
  ClutterActorClass parent_class;

  /*< public >*/
  /* signals, not vfuncs */
  void (* text_changed)   (ClutterText           *self);
  void (* activate)       (ClutterText           *self);
  void (* cursor_event)   (ClutterText           *self,
                           const graphene_rect_t *rect);
  void (* cursor_changed) (ClutterText           *self);

  /*< private >*/
  /* padding for future expansion */
  void (* _clutter_reserved1) (void);
  void (* _clutter_reserved2) (void);
  void (* _clutter_reserved3) (void);
  void (* _clutter_reserved4) (void);
  void (* _clutter_reserved5) (void);
  void (* _clutter_reserved6) (void);
  void (* _clutter_reserved7) (void);
};

CLUTTER_EXPORT
GType clutter_text_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterActor *        clutter_text_new                  (void);
CLUTTER_EXPORT
ClutterActor *        clutter_text_new_full             (const gchar          *font_name,
                                                         const gchar          *text,
                                                         const ClutterColor   *color);
CLUTTER_EXPORT
ClutterActor *        clutter_text_new_with_text        (const gchar          *font_name,
                                                         const gchar          *text);
CLUTTER_EXPORT
ClutterActor *        clutter_text_new_with_buffer      (ClutterTextBuffer    *buffer);
CLUTTER_EXPORT
ClutterTextBuffer *   clutter_text_get_buffer           (ClutterText          *self);
CLUTTER_EXPORT
void                  clutter_text_set_buffer           (ClutterText          *self,
                                                         ClutterTextBuffer    *buffer);
CLUTTER_EXPORT
const gchar *         clutter_text_get_text             (ClutterText          *self);
CLUTTER_EXPORT
void                  clutter_text_set_text             (ClutterText          *self,
                                                         const gchar          *text);
CLUTTER_EXPORT
void                  clutter_text_set_markup           (ClutterText          *self,
                                                         const gchar          *markup);
CLUTTER_EXPORT
void                  clutter_text_set_color            (ClutterText          *self,
                                                         const ClutterColor   *color);
CLUTTER_EXPORT
void                  clutter_text_get_color            (ClutterText          *self,
                                                         ClutterColor         *color);
CLUTTER_EXPORT
void                  clutter_text_set_font_name        (ClutterText          *self,
                                                         const gchar          *font_name);
CLUTTER_EXPORT
const gchar *         clutter_text_get_font_name        (ClutterText          *self);
CLUTTER_EXPORT
void                  clutter_text_set_font_description (ClutterText          *self,
                                                         PangoFontDescription *font_desc);
CLUTTER_EXPORT
PangoFontDescription *clutter_text_get_font_description (ClutterText          *self);

CLUTTER_EXPORT
void                  clutter_text_set_ellipsize        (ClutterText          *self,
                                                         PangoEllipsizeMode    mode);
CLUTTER_EXPORT
PangoEllipsizeMode    clutter_text_get_ellipsize        (ClutterText          *self);
CLUTTER_EXPORT
void                  clutter_text_set_line_wrap        (ClutterText          *self,
                                                         gboolean              line_wrap);
CLUTTER_EXPORT
gboolean              clutter_text_get_line_wrap        (ClutterText          *self);
CLUTTER_EXPORT
void                  clutter_text_set_line_wrap_mode   (ClutterText          *self,
                                                         PangoWrapMode         wrap_mode);
CLUTTER_EXPORT
PangoWrapMode         clutter_text_get_line_wrap_mode   (ClutterText          *self);
CLUTTER_EXPORT
PangoLayout *         clutter_text_get_layout           (ClutterText          *self);
CLUTTER_EXPORT
void                  clutter_text_set_attributes       (ClutterText          *self,
                                                         PangoAttrList        *attrs);
CLUTTER_EXPORT
PangoAttrList *       clutter_text_get_attributes       (ClutterText          *self);
CLUTTER_EXPORT
void                  clutter_text_set_use_markup       (ClutterText          *self,
                                                         gboolean              setting);
CLUTTER_EXPORT
gboolean              clutter_text_get_use_markup       (ClutterText          *self);
CLUTTER_EXPORT
void                  clutter_text_set_line_alignment   (ClutterText          *self,
                                                         PangoAlignment        alignment);
CLUTTER_EXPORT
PangoAlignment        clutter_text_get_line_alignment   (ClutterText          *self);
CLUTTER_EXPORT
void                  clutter_text_set_justify          (ClutterText          *self,
                                                         gboolean              justify);
CLUTTER_EXPORT
gboolean              clutter_text_get_justify          (ClutterText          *self);

CLUTTER_EXPORT
void                  clutter_text_insert_unichar       (ClutterText          *self,
                                                         gunichar              wc);
CLUTTER_EXPORT
void                  clutter_text_delete_chars         (ClutterText          *self,
                                                         guint                 n_chars);
CLUTTER_EXPORT
void                  clutter_text_insert_text          (ClutterText          *self,
                                                         const gchar          *text,
                                                         gssize                position);
CLUTTER_EXPORT
void                  clutter_text_delete_text          (ClutterText          *self,
                                                         gssize                start_pos,
                                                         gssize                end_pos);
CLUTTER_EXPORT
gchar *               clutter_text_get_chars            (ClutterText          *self,
                                                         gssize                start_pos,
                                                         gssize                end_pos);
CLUTTER_EXPORT
void                  clutter_text_set_editable         (ClutterText          *self,
                                                         gboolean              editable);
CLUTTER_EXPORT
gboolean              clutter_text_get_editable         (ClutterText          *self);
CLUTTER_EXPORT
void                  clutter_text_set_activatable      (ClutterText          *self,
                                                         gboolean              activatable);
CLUTTER_EXPORT
gboolean              clutter_text_get_activatable      (ClutterText          *self);

CLUTTER_EXPORT
gint                  clutter_text_get_cursor_position  (ClutterText          *self);
CLUTTER_EXPORT
void                  clutter_text_set_cursor_position  (ClutterText          *self,
                                                         gint                  position);
CLUTTER_EXPORT
void                  clutter_text_set_cursor_visible   (ClutterText          *self,
                                                         gboolean              cursor_visible);
CLUTTER_EXPORT
gboolean              clutter_text_get_cursor_visible   (ClutterText          *self);
CLUTTER_EXPORT
void                  clutter_text_set_cursor_color     (ClutterText          *self,
                                                         const ClutterColor   *color);
CLUTTER_EXPORT
void                  clutter_text_get_cursor_color     (ClutterText          *self,
                                                         ClutterColor         *color);
CLUTTER_EXPORT
void                  clutter_text_set_cursor_size      (ClutterText          *self,
                                                         gint                  size);
CLUTTER_EXPORT
guint                 clutter_text_get_cursor_size      (ClutterText          *self);
CLUTTER_EXPORT
void                  clutter_text_get_cursor_rect      (ClutterText          *self,
                                                         graphene_rect_t      *rect);
CLUTTER_EXPORT
void                  clutter_text_set_selectable       (ClutterText          *self,
                                                         gboolean              selectable);
CLUTTER_EXPORT
gboolean              clutter_text_get_selectable       (ClutterText          *self);
CLUTTER_EXPORT
void                  clutter_text_set_selection_bound  (ClutterText          *self,
                                                         gint                  selection_bound);
CLUTTER_EXPORT
gint                  clutter_text_get_selection_bound  (ClutterText          *self);
CLUTTER_EXPORT
void                  clutter_text_set_selection        (ClutterText          *self,
                                                         gssize                start_pos,
                                                         gssize                end_pos);
CLUTTER_EXPORT
gchar *               clutter_text_get_selection        (ClutterText          *self);
CLUTTER_EXPORT
void                  clutter_text_set_selection_color  (ClutterText          *self,
                                                         const ClutterColor   *color);
CLUTTER_EXPORT
void                  clutter_text_get_selection_color  (ClutterText          *self,
                                                         ClutterColor         *color);
CLUTTER_EXPORT
gboolean              clutter_text_delete_selection     (ClutterText          *self);
CLUTTER_EXPORT
void                  clutter_text_set_password_char    (ClutterText          *self,
                                                         gunichar              wc);
CLUTTER_EXPORT
gunichar              clutter_text_get_password_char    (ClutterText          *self);
CLUTTER_EXPORT
void                  clutter_text_set_max_length       (ClutterText          *self,
                                                         gint                  max);
CLUTTER_EXPORT
gint                  clutter_text_get_max_length       (ClutterText          *self);
CLUTTER_EXPORT
void                  clutter_text_set_single_line_mode (ClutterText          *self,
                                                         gboolean              single_line);
CLUTTER_EXPORT
gboolean              clutter_text_get_single_line_mode (ClutterText          *self);

CLUTTER_EXPORT
void                  clutter_text_set_selected_text_color  (ClutterText          *self,
                                                             const ClutterColor   *color);
CLUTTER_EXPORT
void                  clutter_text_get_selected_text_color  (ClutterText          *self,
                                                             ClutterColor         *color);

CLUTTER_EXPORT
gboolean              clutter_text_activate             (ClutterText          *self);
CLUTTER_EXPORT
gint                  clutter_text_coords_to_position   (ClutterText          *self,
                                                         gfloat                x,
                                                         gfloat                y);
CLUTTER_EXPORT
gboolean              clutter_text_position_to_coords   (ClutterText          *self,
                                                         gint                  position,
                                                         gfloat               *x,
                                                         gfloat               *y,
                                                         gfloat               *line_height);

CLUTTER_EXPORT
void                  clutter_text_set_preedit_string   (ClutterText          *self,
                                                         const gchar          *preedit_str,
                                                         PangoAttrList        *preedit_attrs,
                                                         guint                 cursor_pos);

CLUTTER_EXPORT
void                  clutter_text_get_layout_offsets   (ClutterText           *self,
                                                         gint                  *x,
                                                         gint                  *y);

CLUTTER_EXPORT
void                  clutter_text_set_input_hints (ClutterText                  *self,
                                                    ClutterInputContentHintFlags  hints);
CLUTTER_EXPORT
void                  clutter_text_set_input_purpose (ClutterText                *self,
                                                      ClutterInputContentPurpose  purpose);
CLUTTER_EXPORT
ClutterInputContentHintFlags clutter_text_get_input_hints (ClutterText *self);
CLUTTER_EXPORT
ClutterInputContentPurpose clutter_text_get_input_purpose (ClutterText *self);

CLUTTER_EXPORT
gboolean              clutter_text_has_preedit (ClutterText *self);

G_END_DECLS

#endif /* __CLUTTER_TEXT_H__ */
