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

#include "clutter-build-config.h"

#include "clutter-private.h"
#include "clutter/clutter-input-device-private.h"
#include "clutter/clutter-input-method.h"
#include "clutter/clutter-input-method-private.h"
#include "clutter/clutter-input-focus-private.h"

typedef struct _ClutterInputMethodPrivate ClutterInputMethodPrivate;

struct _ClutterInputMethodPrivate
{
  ClutterInputFocus *focus;
  ClutterInputContentHintFlags content_hints;
  ClutterInputContentPurpose content_purpose;
  gboolean can_show_preedit;
};

enum
{
  COMMIT,
  DELETE_SURROUNDING,
  REQUEST_SURROUNDING,
  INPUT_PANEL_STATE,
  CURSOR_LOCATION_CHANGED,
  N_SIGNALS,
};

enum
{
  PROP_0,
  PROP_CONTENT_HINTS,
  PROP_CONTENT_PURPOSE,
  PROP_CAN_SHOW_PREEDIT,
  N_PROPS
};

static guint signals[N_SIGNALS] = { 0 };
static GParamSpec *pspecs[N_PROPS] = { 0 };

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (ClutterInputMethod, clutter_input_method, G_TYPE_OBJECT)

static void
set_content_hints (ClutterInputMethod           *im,
                   ClutterInputContentHintFlags  content_hints)
{
  ClutterInputMethodPrivate *priv;

  priv = clutter_input_method_get_instance_private (im);
  priv->content_hints = content_hints;
  CLUTTER_INPUT_METHOD_GET_CLASS (im)->update_content_hints (im, content_hints);
}

static void
set_content_purpose (ClutterInputMethod         *im,
                     ClutterInputContentPurpose  content_purpose)
{
  ClutterInputMethodPrivate *priv;

  priv = clutter_input_method_get_instance_private (im);
  priv->content_purpose = content_purpose;
  CLUTTER_INPUT_METHOD_GET_CLASS (im)->update_content_purpose (im,
                                                               content_purpose);
}

static void
set_can_show_preedit (ClutterInputMethod *im,
                      gboolean            can_show_preedit)
{
  ClutterInputMethodPrivate *priv;

  priv = clutter_input_method_get_instance_private (im);
  priv->can_show_preedit = can_show_preedit;
}

static void
clutter_input_method_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  switch (prop_id)
    {
    case PROP_CONTENT_HINTS:
      set_content_hints (CLUTTER_INPUT_METHOD (object),
                         g_value_get_flags (value));
      break;
    case PROP_CONTENT_PURPOSE:
      set_content_purpose (CLUTTER_INPUT_METHOD (object),
                           g_value_get_enum (value));
      break;
    case PROP_CAN_SHOW_PREEDIT:
      set_can_show_preedit (CLUTTER_INPUT_METHOD (object),
                            g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_input_method_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  ClutterInputMethodPrivate *priv;
  ClutterInputMethod *im;

  im = CLUTTER_INPUT_METHOD (object);
  priv = clutter_input_method_get_instance_private (im);

  switch (prop_id)
    {
    case PROP_CONTENT_HINTS:
      g_value_set_flags (value, priv->content_hints);
      break;
    case PROP_CONTENT_PURPOSE:
      g_value_set_enum (value, priv->content_purpose);
      break;
    case PROP_CAN_SHOW_PREEDIT:
      g_value_set_boolean (value, priv->can_show_preedit);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_input_method_class_init (ClutterInputMethodClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = clutter_input_method_set_property;
  object_class->get_property = clutter_input_method_get_property;

  signals[COMMIT] =
    g_signal_new ("commit",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1, G_TYPE_STRING);
  signals[DELETE_SURROUNDING] =
    g_signal_new ("delete-surrounding",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_UINT);
  signals[REQUEST_SURROUNDING] =
    g_signal_new ("request-surrounding",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
  signals[INPUT_PANEL_STATE] =
    g_signal_new ("input-panel-state",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_INPUT_PANEL_STATE);
  signals[CURSOR_LOCATION_CHANGED] =
    g_signal_new ("cursor-location-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1, GRAPHENE_TYPE_RECT);

  pspecs[PROP_CONTENT_HINTS] =
    g_param_spec_flags ("content-hints",
                        P_("Content hints"),
                        P_("Content hints"),
                        CLUTTER_TYPE_INPUT_CONTENT_HINT_FLAGS, 0,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS);
  pspecs[PROP_CONTENT_PURPOSE] =
    g_param_spec_enum ("content-purpose",
                       P_("Content purpose"),
                       P_("Content purpose"),
                       CLUTTER_TYPE_INPUT_CONTENT_PURPOSE, 0,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS);
  pspecs[PROP_CAN_SHOW_PREEDIT] =
    g_param_spec_boolean ("can-show-preedit",
                          P_("Can show preedit"),
                          P_("Can show preedit"),
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, pspecs);
}

static void
clutter_input_method_init (ClutterInputMethod *im)
{
}

void
clutter_input_method_focus_in (ClutterInputMethod *im,
                               ClutterInputFocus  *focus)
{
  ClutterInputMethodPrivate *priv;
  ClutterInputMethodClass *klass;

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));
  g_return_if_fail (CLUTTER_IS_INPUT_FOCUS (focus));

  priv = clutter_input_method_get_instance_private (im);

  if (priv->focus == focus)
    return;

  if (priv->focus)
    clutter_input_method_focus_out (im);

  g_set_object (&priv->focus, focus);

  if (focus)
    {
      klass = CLUTTER_INPUT_METHOD_GET_CLASS (im);
      klass->focus_in (im, focus);

      clutter_input_focus_focus_in (priv->focus, im);
    }
}

void
clutter_input_method_focus_out (ClutterInputMethod *im)
{
  ClutterInputMethodPrivate *priv;
  ClutterInputMethodClass *klass;

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  priv = clutter_input_method_get_instance_private (im);

  if (!priv->focus)
    return;

  clutter_input_focus_focus_out (priv->focus);
  g_clear_object (&priv->focus);

  klass = CLUTTER_INPUT_METHOD_GET_CLASS (im);
  klass->focus_out (im);
}

ClutterInputFocus *
clutter_input_method_get_focus (ClutterInputMethod *im)
{
  ClutterInputMethodPrivate *priv;

  priv = clutter_input_method_get_instance_private (im);
  return priv->focus;
}

static void
clutter_input_method_put_im_event (ClutterInputMethod *im,
                                   ClutterEventType    event_type,
                                   const gchar        *text,
                                   int32_t             offset,
                                   uint32_t            len)
{
  ClutterInputDevice *keyboard;
  ClutterSeat *seat;
  ClutterStage *stage;
  ClutterEvent *event;

  seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  keyboard = clutter_seat_get_keyboard (seat);
  stage = _clutter_input_device_get_stage (keyboard);
  if (stage == NULL)
    return;

  event = clutter_event_new (event_type);
  event->im.text = g_strdup (text);
  event->im.offset = offset;
  event->im.len = len;
  clutter_event_set_device (event, keyboard);
  clutter_event_set_source_device (event, keyboard);
  clutter_event_set_flags (event, CLUTTER_EVENT_FLAG_INPUT_METHOD);

  clutter_event_set_stage (event, stage);

  clutter_event_put (event);
  clutter_event_free (event);
}

void
clutter_input_method_commit (ClutterInputMethod *im,
                             const gchar        *text)
{
  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  clutter_input_method_put_im_event (im, CLUTTER_IM_COMMIT, text, 0, 0);
}

void
clutter_input_method_delete_surrounding (ClutterInputMethod *im,
                                         int                 offset,
                                         guint               len)
{
  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  clutter_input_method_put_im_event (im, CLUTTER_IM_DELETE, NULL, offset, len);
}

void
clutter_input_method_request_surrounding (ClutterInputMethod *im)
{
  ClutterInputMethodPrivate *priv;

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  priv = clutter_input_method_get_instance_private (im);
  if (priv->focus)
    clutter_input_focus_request_surrounding (priv->focus);
}

/**
 * clutter_input_method_set_preedit_text:
 * @im: a #ClutterInputMethod
 * @preedit: (nullable): the preedit text, or %NULL
 * @cursor: the cursor
 *
 * Sets the preedit text on the current input focus.
 **/
void
clutter_input_method_set_preedit_text (ClutterInputMethod *im,
                                       const gchar        *preedit,
                                       guint               cursor)
{
  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  clutter_input_method_put_im_event (im, CLUTTER_IM_PREEDIT, preedit, cursor, 0);
}

void
clutter_input_method_notify_key_event (ClutterInputMethod *im,
                                       const ClutterEvent *event,
                                       gboolean            filtered)
{
  if (!filtered)
    {
      ClutterEvent *copy;

      /* XXX: we rely on the IM implementation to notify back of
       * key events in the exact same order they were given.
       */
      copy = clutter_event_copy (event);
      clutter_event_set_flags (copy, clutter_event_get_flags (event) |
                               CLUTTER_EVENT_FLAG_INPUT_METHOD);
      clutter_event_set_source_device (copy, clutter_event_get_device (copy));
      clutter_event_put (copy);
      clutter_event_free (copy);
    }
}

void
clutter_input_method_set_input_panel_state (ClutterInputMethod     *im,
                                            ClutterInputPanelState  state)
{
  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  g_signal_emit (im, signals[INPUT_PANEL_STATE], 0, state);
}

void
clutter_input_method_reset (ClutterInputMethod *im)
{
  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  CLUTTER_INPUT_METHOD_GET_CLASS (im)->reset (im);
}

void
clutter_input_method_set_cursor_location (ClutterInputMethod    *im,
                                          const graphene_rect_t *rect)
{
  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  CLUTTER_INPUT_METHOD_GET_CLASS (im)->set_cursor_location (im, rect);

  g_signal_emit (im, signals[CURSOR_LOCATION_CHANGED], 0, rect);
}

void
clutter_input_method_set_surrounding (ClutterInputMethod *im,
                                      const gchar        *text,
                                      guint               cursor,
                                      guint               anchor)
{
  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  CLUTTER_INPUT_METHOD_GET_CLASS (im)->set_surrounding (im, text,
                                                        cursor, anchor);
}

void
clutter_input_method_set_content_hints (ClutterInputMethod           *im,
                                        ClutterInputContentHintFlags  hints)
{
  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  g_object_set (G_OBJECT (im), "content-hints", hints, NULL);
}

void
clutter_input_method_set_content_purpose (ClutterInputMethod         *im,
                                          ClutterInputContentPurpose  purpose)
{
  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  g_object_set (G_OBJECT (im), "content-purpose", purpose, NULL);
}

void
clutter_input_method_set_can_show_preedit (ClutterInputMethod *im,
                                           gboolean            can_show_preedit)
{
  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  g_object_set (G_OBJECT (im), "can-show-preedit", can_show_preedit, NULL);
}

gboolean
clutter_input_method_filter_key_event (ClutterInputMethod    *im,
                                       const ClutterKeyEvent *key)
{
  ClutterInputMethodClass *im_class = CLUTTER_INPUT_METHOD_GET_CLASS (im);

  g_return_val_if_fail (CLUTTER_IS_INPUT_METHOD (im), FALSE);
  g_return_val_if_fail (key != NULL, FALSE);

  if (clutter_event_get_flags ((ClutterEvent *) key) & CLUTTER_EVENT_FLAG_INPUT_METHOD)
    return FALSE;
  if (!im_class->filter_key_event)
    return FALSE;

  return im_class->filter_key_event (im, (const ClutterEvent *) key);
}

void
clutter_input_method_forward_key (ClutterInputMethod *im,
                                  uint32_t            keyval,
                                  uint32_t            keycode,
                                  uint32_t            state,
                                  uint64_t            time_,
                                  gboolean            press)
{
  ClutterInputMethodPrivate *priv;
  ClutterInputDevice *keyboard;
  ClutterSeat *seat;
  ClutterStage *stage;
  ClutterEvent *event;

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  priv = clutter_input_method_get_instance_private (im);
  if (!priv->focus)
    return;

  seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  keyboard = clutter_seat_get_keyboard (seat);
  stage = _clutter_input_device_get_stage (keyboard);
  if (stage == NULL)
    return;

  event = clutter_event_new (press ? CLUTTER_KEY_PRESS : CLUTTER_KEY_RELEASE);
  event->key.time = time_;
  event->key.flags = CLUTTER_EVENT_FLAG_INPUT_METHOD;
  event->key.modifier_state = state;
  event->key.keyval = keyval;
  event->key.hardware_keycode = keycode;
  event->key.unicode_value = clutter_keysym_to_unicode (keyval);

  clutter_event_set_device (event, keyboard);
  clutter_event_set_source_device (event, keyboard);
  clutter_event_set_stage (event, stage);

  clutter_event_put (event);
  clutter_event_free (event);
}
