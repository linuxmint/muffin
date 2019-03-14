/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2006, 2007, 2008 OpenedHand Ltd
 * Copyright (C) 2009 Intel Corportation
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
 *
 * Original author:
 *
 *      Emmanuele Bassi <ebassi@linux.intel.com>
 */

#ifdef HAVE_CONFIG_H
#include "clutter-build-config.h"
#endif

#include <string.h>
#include <stdlib.h>

#include <glib.h>
#include <gmodule.h>

#define CLUTTER_DISABLE_DEPRECATION_WARNINGS
#include "deprecated/clutter-container.h"
#include "deprecated/clutter-alpha.h"

#include "clutter-actor.h"
#include "clutter-debug.h"
#include "clutter-enum-types.h"

#include "clutter-script.h"
#include "clutter-script-private.h"
#include "clutter-scriptable.h"

#include "clutter-stage-manager.h"

#include "clutter-private.h"

static void clutter_script_parser_object_end (JsonParser *parser,
                                              JsonObject *object);
static void clutter_script_parser_parse_end  (JsonParser *parser);

#define clutter_script_parser_get_type  _clutter_script_parser_get_type

G_DEFINE_TYPE (ClutterScriptParser, clutter_script_parser, JSON_TYPE_PARSER);

static void
clutter_script_parser_class_init (ClutterScriptParserClass *klass)
{
  JsonParserClass *parser_class = JSON_PARSER_CLASS (klass);

  parser_class->object_end = clutter_script_parser_object_end;
  parser_class->parse_end = clutter_script_parser_parse_end;
}

static void
clutter_script_parser_init (ClutterScriptParser *parser)
{
}

GType
_clutter_script_get_type_from_symbol (const gchar *symbol)
{
  static GModule *module = NULL;
  GTypeGetFunc func;
  GType gtype = G_TYPE_INVALID;

  if (!module)
    module = g_module_open (NULL, 0);

  if (g_module_symbol (module, symbol, (gpointer)&func))
    gtype = func ();

  return gtype;
}

GType
_clutter_script_get_type_from_class (const gchar *name)
{
  static GModule *module = NULL;
  GString *symbol_name = g_string_sized_new (64);
  GType gtype = G_TYPE_INVALID;
  GTypeGetFunc func;
  gchar *symbol;
  gint i;

  if (G_UNLIKELY (!module))
    module = g_module_open (NULL, 0);

  for (i = 0; name[i] != '\0'; i++)
    {
      gchar c = name[i];

      /* the standard naming policy for GObject-based libraries
       * is:
       *
       *   NAME := INITIAL_WORD WORD+
       *   INITIAL_WORD := [A-Z][a-z0-9]*
       *   WORD := [A-Z]{1,2}[a-z0-9]+ | [A-Z]{2,}
       *
       * for instance:
       *
       *   GString -> g_string
       *   GtkCTree -> gtk_ctree
       *   ClutterX11TexturePixmap -> clutter_x11_texture_pixmap
       *
       * see:
       *
       * http://mail.gnome.org/archives/gtk-devel-list/2007-June/msg00022.html
       *
       * and:
       *
       * http://git.gnome.org/cgit/gtk+/plain/gtk/gtkbuilderparser.c
       */

      if ((c == g_ascii_toupper (c) &&
           i > 0 && name[i - 1] != g_ascii_toupper (name[i - 1])) ||
          (i > 2 && name[i] == g_ascii_toupper (name[i]) &&
           name[i - 1] == g_ascii_toupper (name[i - 1]) &&
           name[i - 2] == g_ascii_toupper (name[i - 2])))
        g_string_append_c (symbol_name, '_');

      g_string_append_c (symbol_name, g_ascii_tolower (c));
    }

  g_string_append (symbol_name, "_get_type");

  symbol = g_string_free (symbol_name, FALSE);

  if (g_module_symbol (module, symbol, (gpointer)&func))
    {
      CLUTTER_NOTE (SCRIPT, "Type function: %s", symbol);
      gtype = func ();
    }

  free (symbol);

  return gtype;
}

/*
 * clutter_script_enum_from_string:
 * @type: a #GType for an enumeration type
 * @string: the enumeration value as a string
 * @enum_value: (out): return location for the enumeration value as an integer
 *
 * Converts an enumeration value inside @string into a numeric
 * value and places it into @enum_value.
 *
 * The enumeration value can be an integer, the enumeration nick
 * or the enumeration name, as part of the #GEnumValue structure.
 *
 * Return value: %TRUE if the conversion was successfull.
 */
gboolean
_clutter_script_enum_from_string (GType        type,
                                  const gchar *string,
                                  gint        *enum_value)
{
  GEnumClass *eclass;
  GEnumValue *ev;
  gchar *endptr;
  gint value;
  gboolean retval = TRUE;

  g_return_val_if_fail (G_TYPE_IS_ENUM (type), 0);
  g_return_val_if_fail (string != NULL, 0);

  value = strtoul (string, &endptr, 0);
  if (endptr != string) /* parsed a number */
    *enum_value = value;
  else
    {
      eclass = g_type_class_ref (type);
      ev = g_enum_get_value_by_name (eclass, string);
      if (!ev)
	ev = g_enum_get_value_by_nick (eclass, string);

      if (ev)
	*enum_value = ev->value;
      else
        retval = FALSE;

      g_type_class_unref (eclass);
    }

  return retval;
}

gboolean
_clutter_script_flags_from_string (GType        type,
                                   const gchar *string,
                                   gint        *flags_value)
{
  gchar *endptr, *prevptr;
  guint i, j, ret, value;
  gchar *flagstr;
  GFlagsValue *fv;
  const gchar *flag;

  g_return_val_if_fail (G_TYPE_IS_FLAGS (type), 0);
  g_return_val_if_fail (string != NULL, 0);

  ret = TRUE;

  value = strtoul (string, &endptr, 0);
  if (endptr != string) /* parsed a number */
    *flags_value = value;
  else
    {
      GFlagsClass *fclass;

      fclass = g_type_class_ref (type);

      flagstr = g_strdup (string);
      for (value = i = j = 0; ; i++)
	{
          gboolean eos = (flagstr[i] == '\0') ? TRUE : FALSE;

	  if (!eos && flagstr[i] != '|')
	    continue;

	  flag = &flagstr[j];
	  endptr = &flagstr[i];

	  if (!eos)
	    {
	      flagstr[i++] = '\0';
	      j = i;
	    }

	  /* trim spaces */
	  for (;;)
	    {
	      gunichar ch = g_utf8_get_char (flag);
	      if (!g_unichar_isspace (ch))
		break;

	      flag = g_utf8_next_char (flag);
	    }

	  while (endptr > flag)
	    {
              gunichar ch;

	      prevptr = g_utf8_prev_char (endptr);

	      ch = g_utf8_get_char (prevptr);
	      if (!g_unichar_isspace (ch))
		break;

	      endptr = prevptr;
	    }

	  if (endptr > flag)
	    {
	      *endptr = '\0';

	      fv = g_flags_get_value_by_name (fclass, flag);

	      if (!fv)
		fv = g_flags_get_value_by_nick (fclass, flag);

	      if (fv)
		value |= fv->value;
	      else
		{
		  ret = FALSE;
		  break;
		}
	    }

	  if (eos)
	    {
	      *flags_value = value;
	      break;
	    }
	}

      free (flagstr);

      g_type_class_unref (fclass);
    }

  return ret;
}

static gboolean
parse_knot_from_array (JsonArray   *array,
                       ClutterKnot *knot)
{
  if (json_array_get_length (array) != 2)
    return FALSE;

  knot->x = json_array_get_int_element (array, 0);
  knot->y = json_array_get_int_element (array, 1);

  return TRUE;
}

static gboolean
parse_knot_from_object (JsonObject  *object,
                        ClutterKnot *knot)
{
  if (json_object_has_member (object, "x"))
    knot->x = json_object_get_int_member (object, "x");
  else
    knot->x = 0;

  if (json_object_has_member (object, "y"))
    knot->y = json_object_get_int_member (object, "y");
  else
    knot->y = 0;

  return TRUE;
}

gboolean
_clutter_script_parse_knot (ClutterScript *script,
                            JsonNode      *node,
                            ClutterKnot   *knot)
{
  g_return_val_if_fail (CLUTTER_IS_SCRIPT (script), FALSE);
  g_return_val_if_fail (node != NULL, FALSE);
  g_return_val_if_fail (knot != NULL, FALSE);

  switch (JSON_NODE_TYPE (node))
    {
    case JSON_NODE_ARRAY:
      return parse_knot_from_array (json_node_get_array (node), knot);

    case JSON_NODE_OBJECT:
      return parse_knot_from_object (json_node_get_object (node), knot);

    default:
      break;
    }

  return FALSE;
}

static gboolean
parse_geometry_from_array (JsonArray       *array,
                           ClutterGeometry *geometry)
{
  if (json_array_get_length (array) != 4)
    return FALSE;

  geometry->x = json_array_get_int_element (array, 0);
  geometry->y = json_array_get_int_element (array, 1);
  geometry->width = json_array_get_int_element (array, 2);
  geometry->height = json_array_get_int_element (array, 3);

  return TRUE;
}

static gboolean
parse_geometry_from_object (JsonObject      *object,
                            ClutterGeometry *geometry)
{
  if (json_object_has_member (object, "x"))
    geometry->x = json_object_get_int_member (object, "x");
  else
    geometry->x = 0;

  if (json_object_has_member (object, "y"))
    geometry->y = json_object_get_int_member (object, "y");
  else
    geometry->y = 0;

  if (json_object_has_member (object, "width"))
    geometry->width = json_object_get_int_member (object, "width");
  else
    geometry->width = 0;

  if (json_object_has_member (object, "height"))
    geometry->height = json_object_get_int_member (object, "height");
  else
    geometry->height = 0;

  return TRUE;
}

gboolean
_clutter_script_parse_geometry (ClutterScript   *script,
                                JsonNode        *node,
                                ClutterGeometry *geometry)
{
  g_return_val_if_fail (CLUTTER_IS_SCRIPT (script), FALSE);
  g_return_val_if_fail (node != NULL, FALSE);
  g_return_val_if_fail (geometry != NULL, FALSE);

  switch (JSON_NODE_TYPE (node))
    {
    case JSON_NODE_ARRAY:
      return parse_geometry_from_array (json_node_get_array (node), geometry);

    case JSON_NODE_OBJECT:
      return parse_geometry_from_object (json_node_get_object (node), geometry);

    default:
      break;
    }

  return FALSE;
}

static gboolean
parse_color_from_array (JsonArray    *array,
                        ClutterColor *color)
{
  if (json_array_get_length (array) != 3 ||
      json_array_get_length (array) != 4)
    return FALSE;

  color->red   = CLAMP (json_array_get_int_element (array, 0), 0, 255);
  color->green = CLAMP (json_array_get_int_element (array, 1), 0, 255);
  color->blue  = CLAMP (json_array_get_int_element (array, 2), 0, 255);

  if (json_array_get_length (array) == 4)
    color->alpha = CLAMP (json_array_get_int_element (array, 3), 0, 255);
  else
    color->alpha = 255;

  return TRUE;
}

static gboolean
parse_color_from_object (JsonObject   *object,
                         ClutterColor *color)
{
  if (json_object_has_member (object, "red"))
    color->red = CLAMP (json_object_get_int_member (object, "red"), 0, 255);
  else
    color->red = 0;

  if (json_object_has_member (object, "green"))
    color->green = CLAMP (json_object_get_int_member (object, "green"), 0, 255);
  else
    color->green = 0;

  if (json_object_has_member (object, "blue"))
    color->blue = CLAMP (json_object_get_int_member (object, "blue"), 0, 255);
  else
    color->blue = 0;

  if (json_object_has_member (object, "alpha"))
    color->alpha = CLAMP (json_object_get_int_member (object, "alpha"), 0, 255);
  else
    color->alpha = 255;

  return TRUE;
}

gboolean
_clutter_script_parse_color (ClutterScript *script,
                             JsonNode      *node,
                             ClutterColor  *color)
{
  g_return_val_if_fail (CLUTTER_IS_SCRIPT (script), FALSE);
  g_return_val_if_fail (node != NULL, FALSE);
  g_return_val_if_fail (color != NULL, FALSE);

  switch (JSON_NODE_TYPE (node))
    {
    case JSON_NODE_ARRAY:
      return parse_color_from_array (json_node_get_array (node), color);

    case JSON_NODE_OBJECT:
      return parse_color_from_object (json_node_get_object (node), color);

    case JSON_NODE_VALUE:
      return clutter_color_from_string (color, json_node_get_string (node));

    default:
      break;
    }

  return FALSE;
}

static gboolean
parse_point_from_array (JsonArray    *array,
                        ClutterPoint *point)
{
  if (json_array_get_length (array) != 2)
    return FALSE;

  point->x = json_array_get_double_element (array, 0);
  point->y = json_array_get_double_element (array, 1);

  return TRUE;
}

static gboolean
parse_point_from_object (JsonObject   *object,
                         ClutterPoint *point)
{
  if (json_object_has_member (object, "x"))
    point->x = json_object_get_double_member (object, "x");
  else
    point->x = 0.f;

  if (json_object_has_member (object, "y"))
    point->y = json_object_get_double_member (object, "y");
  else
    point->y = 0.f;

  return TRUE;
}

gboolean
_clutter_script_parse_point (ClutterScript *script,
                             JsonNode      *node,
                             ClutterPoint  *point)
{
  g_return_val_if_fail (CLUTTER_IS_SCRIPT (script), FALSE);
  g_return_val_if_fail (node != NULL, FALSE);
  g_return_val_if_fail (point != NULL, FALSE);

  switch (JSON_NODE_TYPE (node))
    {
    case JSON_NODE_ARRAY:
      return parse_point_from_array (json_node_get_array (node), point);

    case JSON_NODE_OBJECT:
      return parse_point_from_object (json_node_get_object (node), point);

    default:
      break;
    }

  return FALSE;
}

static gboolean
parse_size_from_array (JsonArray   *array,
                       ClutterSize *size)
{
  if (json_array_get_length (array) != 2)
    return FALSE;

  size->width = json_array_get_double_element (array, 0);
  size->height = json_array_get_double_element (array, 1);

  return TRUE;
}

static gboolean
parse_size_from_object (JsonObject  *object,
                        ClutterSize *size)
{
  if (json_object_has_member (object, "width"))
    size->width = json_object_get_double_member (object, "width");
  else
    size->width = 0.f;

  if (json_object_has_member (object, "height"))
    size->height = json_object_get_double_member (object, "height");
  else
    size->height = 0.f;

  return TRUE;
}

gboolean
_clutter_script_parse_size (ClutterScript *script,
                            JsonNode      *node,
                            ClutterSize   *size)
{
  g_return_val_if_fail (CLUTTER_IS_SCRIPT (script), FALSE);
  g_return_val_if_fail (node != NULL, FALSE);
  g_return_val_if_fail (size != NULL, FALSE);

  switch (JSON_NODE_TYPE (node))
    {
    case JSON_NODE_ARRAY:
      return parse_size_from_array (json_node_get_array (node), size);

    case JSON_NODE_OBJECT:
      return parse_size_from_object (json_node_get_object (node), size);

    default:
      break;
    }

  return FALSE;
}

const gchar *
_clutter_script_get_id_from_node (JsonNode *node)
{
  JsonObject *object;

  switch (JSON_NODE_TYPE (node))
    {
    case JSON_NODE_OBJECT:
      object = json_node_get_object (node);
      if (json_object_has_member (object, "id"))
        return json_object_get_string_member (object, "id");
      break;

    case JSON_NODE_VALUE:
      return json_node_get_string (node);

    default:
      break;
    }

  return NULL;
}

static GList *
parse_children (ObjectInfo *oinfo,
                JsonNode   *node)
{
  JsonArray *array;
  GList *retval;
  guint array_len, i;

  if (JSON_NODE_TYPE (node) != JSON_NODE_ARRAY)
    return NULL;

  retval = oinfo->children;

  array = json_node_get_array (node);
  array_len = json_array_get_length (array);

  for (i = 0; i < array_len; i++)
    {
      JsonNode *child = json_array_get_element (array, i);
      const gchar *id_;

      id_ = _clutter_script_get_id_from_node (child);
      if (id_ != NULL)
        retval = g_list_prepend (retval, g_strdup (id_));
    }

  return g_list_reverse (retval);
}

static GList *
parse_signals (ClutterScript *script,
               ObjectInfo    *oinfo,
               JsonNode      *node)
{
  JsonArray *array;
  GList *retval;
  guint array_len, i;

  if (JSON_NODE_TYPE (node) != JSON_NODE_ARRAY)
    {
      _clutter_script_warn_invalid_value (script, "signals", "Array", node);
      return NULL;
    }

  retval = oinfo->signals;
  array = json_node_get_array (node);
  array_len = json_array_get_length (array);

  for (i = 0; i < array_len; i++)
    {
      JsonNode *val = json_array_get_element (array, i);
      SignalInfo *sinfo = NULL;
      JsonObject *object;
      const gchar *name;

      if (JSON_NODE_TYPE (val) != JSON_NODE_OBJECT)
        {
          _clutter_script_warn_invalid_value (script,
                                              "signals array", "Object",
                                              node);
          continue;
        }

      object = json_node_get_object (val);

      /* mandatory: "name" */
      if (!json_object_has_member (object, "name"))
        {
          _clutter_script_warn_missing_attribute (script, NULL, "name");
          continue;
        }
      else
        {
          name = json_object_get_string_member (object, "name");
          if (!name)
            {
              _clutter_script_warn_invalid_value (script,
                                                  "name", "string",
                                                  val);
              continue;
            }
        }

      /* mandatory: "target-state" or "handler" */
      if (json_object_has_member (object, "target-state"))
        {
          const gchar *state = NULL;
          const gchar *target = NULL;
          gboolean warp_to = FALSE;

          target = json_object_get_string_member (object, "target-state");
          if (target == NULL)
            {
              _clutter_script_warn_invalid_value (script,
                                                  "target-state", "string",
                                                  val);
              continue;
            }

          if (json_object_has_member (object, "states"))
            state = json_object_get_string_member (object, "states");

          if (json_object_has_member (object, "warp"))
            warp_to = json_object_get_boolean_member (object, "warp");

          CLUTTER_NOTE (SCRIPT,
                        "Added signal '%s' (states:%s, target-state:%s, warp:%s)",
                        name,
                        state != NULL ? state : "<default>", target,
                        warp_to ? "true" : "false");

          sinfo = g_slice_new0 (SignalInfo);
          sinfo->is_handler = FALSE;
          sinfo->name = g_strdup (name);
          sinfo->state = g_strdup (state);
          sinfo->target = g_strdup (target);
          sinfo->warp_to = warp_to;
        }
      else if (json_object_has_member (object, "handler"))
        {
          const gchar *handler;
          const gchar *connect;
          GConnectFlags flags = 0;

          handler = json_object_get_string_member (object, "handler");
          if (handler == NULL)
            {
              _clutter_script_warn_invalid_value (script,
                                                  "handler", "string",
                                                  val);
              continue;
            }

          /* optional: "object" */
          if (json_object_has_member (object, "object"))
            connect = json_object_get_string_member (object, "object");
          else
            connect = NULL;

          /* optional: "after" */
          if (json_object_has_member (object, "after"))
            {
              if (json_object_get_boolean_member (object, "after"))
                flags |= G_CONNECT_AFTER;
            }

          /* optional: "swapped" */
          if (json_object_has_member (object, "swapped"))
            {
              if (json_object_get_boolean_member (object, "swapped"))
                flags |= G_CONNECT_SWAPPED;
            }

          CLUTTER_NOTE (SCRIPT,
                        "Added signal '%s' (handler:%s, object:%s, flags:%d)",
                        name,
                        handler, connect, flags);

          sinfo = g_slice_new0 (SignalInfo);
          sinfo->is_handler = TRUE;
          sinfo->name = g_strdup (name);
          sinfo->handler = g_strdup (handler);
          sinfo->object = g_strdup (connect);
          sinfo->flags = flags;
        }
      else
        _clutter_script_warn_missing_attribute (script,
                                                NULL,
                                                "handler or state");
      if (sinfo != NULL)
        retval = g_list_prepend (retval, sinfo);
    }

  return retval;
}

static ClutterTimeline *
construct_timeline (ClutterScript *script,
                    JsonObject    *object)
{
  ClutterTimeline *retval = NULL;
  ObjectInfo *oinfo;
  GList *members, *l;

  /* we fake an ObjectInfo so we can reuse clutter_script_construct_object()
   * here; we do not save it inside the hash table, because if this had
   * been a named object then we wouldn't have ended up here in the first
   * place
   */
  oinfo = g_slice_new0 (ObjectInfo);
  oinfo->gtype = CLUTTER_TYPE_TIMELINE;
  oinfo->id = g_strdup ("dummy");

  members = json_object_get_members (object);
  for (l = members; l != NULL; l = l->next)
    {
      const gchar *name = l->data;
      JsonNode *node = json_object_get_member (object, name);
      PropertyInfo *pinfo = g_slice_new0 (PropertyInfo);

      pinfo->name = g_strdelimit (g_strdup (name), G_STR_DELIMITERS, '-');
      pinfo->node = json_node_copy (node);

      oinfo->properties = g_list_prepend (oinfo->properties, pinfo);
    }

  g_list_free (members);

  _clutter_script_construct_object (script, oinfo);
  _clutter_script_apply_properties (script, oinfo);
  retval = CLUTTER_TIMELINE (oinfo->object);

  /* we transfer ownership to the alpha function, so we ref before
   * destroying the ObjectInfo to avoid the timeline going away
   */
  g_object_ref (retval);
  object_info_free (oinfo);

  return retval;
}

/* define the names of the animation modes to match the ones
 * that developers might be more accustomed to
 */
static const struct
{
  const gchar *name;
  ClutterAnimationMode mode;
} animation_modes[] = {
  { "linear", CLUTTER_LINEAR },
  { "easeInQuad", CLUTTER_EASE_IN_QUAD },
  { "easeOutQuad", CLUTTER_EASE_OUT_QUAD },
  { "easeInOutQuad", CLUTTER_EASE_IN_OUT_QUAD },
  { "easeInCubic", CLUTTER_EASE_IN_CUBIC },
  { "easeOutCubic", CLUTTER_EASE_OUT_CUBIC },
  { "easeInOutCubic", CLUTTER_EASE_IN_OUT_CUBIC },
  { "easeInQuart", CLUTTER_EASE_IN_QUART },
  { "easeOutQuart", CLUTTER_EASE_OUT_QUART },
  { "easeInOutQuart", CLUTTER_EASE_IN_OUT_QUART },
  { "easeInQuint", CLUTTER_EASE_IN_QUINT },
  { "easeOutQuint", CLUTTER_EASE_OUT_QUINT },
  { "easeInOutQuint", CLUTTER_EASE_IN_OUT_QUINT },
  { "easeInSine", CLUTTER_EASE_IN_SINE },
  { "easeOutSine", CLUTTER_EASE_OUT_SINE },
  { "easeInOutSine", CLUTTER_EASE_IN_OUT_SINE },
  { "easeInExpo", CLUTTER_EASE_IN_EXPO },
  { "easeOutExpo", CLUTTER_EASE_OUT_EXPO },
  { "easeInOutExpo", CLUTTER_EASE_IN_OUT_EXPO },
  { "easeInCirc", CLUTTER_EASE_IN_CIRC },
  { "easeOutCirc", CLUTTER_EASE_OUT_CIRC },
  { "easeInOutCirc", CLUTTER_EASE_IN_OUT_CIRC },
  { "easeInElastic", CLUTTER_EASE_IN_ELASTIC },
  { "easeOutElastic", CLUTTER_EASE_OUT_ELASTIC },
  { "easeInOutElastic", CLUTTER_EASE_IN_OUT_ELASTIC },
  { "easeInBack", CLUTTER_EASE_IN_BACK },
  { "easeOutBack", CLUTTER_EASE_OUT_BACK },
  { "easeInOutBack", CLUTTER_EASE_IN_OUT_BACK },
  { "easeInBounce", CLUTTER_EASE_IN_BOUNCE },
  { "easeOutBounce", CLUTTER_EASE_OUT_BOUNCE },
  { "easeInOutBounce", CLUTTER_EASE_IN_OUT_BOUNCE },
};

static const gint n_animation_modes = G_N_ELEMENTS (animation_modes);

gulong
_clutter_script_resolve_animation_mode (JsonNode *node)
{
  gint i, res = CLUTTER_CUSTOM_MODE;

  if (JSON_NODE_TYPE (node) != JSON_NODE_VALUE)
    return CLUTTER_CUSTOM_MODE;

  if (json_node_get_value_type (node) == G_TYPE_INT64)
    return json_node_get_int (node);

  if (json_node_get_value_type (node) == G_TYPE_STRING)
    {
      const gchar *name = json_node_get_string (node);

      /* XXX - we might be able to optimize by changing the ordering
       * of the animation_modes array, e.g.
       *  - special casing linear
       *  - tokenizing ('ease', 'In', 'Sine') and matching on token
       *  - binary searching?
       */
      for (i = 0; i < n_animation_modes; i++)
        {
          if (strcmp (animation_modes[i].name, name) == 0)
            return animation_modes[i].mode;
        }

      if (_clutter_script_enum_from_string (CLUTTER_TYPE_ANIMATION_MODE,
                                            name,
                                            &res))
        return res;

      g_warning ("Unable to find the animation mode '%s'", name);
    }

  return CLUTTER_CUSTOM_MODE;
}

static ClutterAlphaFunc
resolve_alpha_func (const gchar *name)
{
  static GModule *module = NULL;
  ClutterAlphaFunc func;

  CLUTTER_NOTE (SCRIPT, "Looking up '%s' alpha function", name);

  if (G_UNLIKELY (!module))
    module = g_module_open (NULL, 0);

  if (g_module_symbol (module, name, (gpointer) &func))
    {
      CLUTTER_NOTE (SCRIPT, "Found '%s' alpha function in the symbols table",
                    name);
      return func;
    }

  return NULL;
}

GObject *
_clutter_script_parse_alpha (ClutterScript *script,
                             JsonNode      *node)
{
  GObject *retval = NULL;
  JsonObject *object;
  ClutterTimeline *timeline = NULL;
  ClutterAlphaFunc alpha_func = NULL;
  ClutterAnimationMode mode = CLUTTER_CUSTOM_MODE;
  JsonNode *val;
  gboolean unref_timeline = FALSE;

  if (JSON_NODE_TYPE (node) != JSON_NODE_OBJECT)
    return NULL;

  object = json_node_get_object (node);

  val = json_object_get_member (object, "timeline");
  if (val)
    {
      if (JSON_NODE_TYPE (val) == JSON_NODE_VALUE &&
          json_node_get_string (val) != NULL)
        {
          const gchar *id_ = json_node_get_string (val);

          timeline =
            CLUTTER_TIMELINE (clutter_script_get_object (script, id_));
        }
      else if (JSON_NODE_TYPE (val) == JSON_NODE_OBJECT)
        {
          timeline = construct_timeline (script, json_node_get_object (val));
          unref_timeline = TRUE;
        }
    }

  val = json_object_get_member (object, "mode");
  if (val != NULL)
    mode = _clutter_script_resolve_animation_mode (val);

  if (mode == CLUTTER_CUSTOM_MODE)
    {
      val = json_object_get_member (object, "function");
      if (val && json_node_get_string (val) != NULL)
        {
          alpha_func = resolve_alpha_func (json_node_get_string (val));
          if (!alpha_func)
            {
              g_warning ("Unable to find the function '%s' in the "
                         "Clutter alpha functions or the symbols table",
                         json_node_get_string (val));
            }
        }
    }

  CLUTTER_NOTE (SCRIPT, "Parsed alpha: %s timeline (%p) (mode:%d, func:%p)",
                unref_timeline ? "implicit" : "explicit",
                timeline ? timeline : 0x0,
                mode != CLUTTER_CUSTOM_MODE ? mode : 0,
                alpha_func ? alpha_func : 0x0);

  retval = g_object_new (CLUTTER_TYPE_ALPHA, NULL);

  if (mode != CLUTTER_CUSTOM_MODE)
    clutter_alpha_set_mode (CLUTTER_ALPHA (retval), mode);

  if (alpha_func != NULL)
    clutter_alpha_set_func (CLUTTER_ALPHA (retval), alpha_func, NULL, NULL);

  clutter_alpha_set_timeline (CLUTTER_ALPHA (retval), timeline);

  /* if we created an implicit timeline, the Alpha has full ownership
   * of it now, since it won't be accessible from ClutterScript
   */
  if (unref_timeline)
    g_object_unref (timeline);

  return retval;
}

static void
clutter_script_parser_object_end (JsonParser *json_parser,
                                  JsonObject *object)
{
  ClutterScriptParser *parser = CLUTTER_SCRIPT_PARSER (json_parser);
  ClutterScript *script = parser->script;
  ObjectInfo *oinfo;
  JsonNode *val;
  const gchar *id_;
  GList *members, *l;

  /* if the object definition does not have an 'id' field we'll
   * fake one for it...
   */
  if (!json_object_has_member (object, "id"))
    {
      gchar *fake;

      /* ... unless it doesn't even have a type - in which case
       * it is an internal object definition and we're not
       * supposed to touch it
       */
      if (!json_object_has_member (object, "type"))
        return;

      fake = _clutter_script_generate_fake_id (script);
      json_object_set_string_member (object, "id", fake);

      CLUTTER_NOTE (SCRIPT,
                    "Adding fake id '%s' to object of type '%s'",
                    json_object_get_string_member (object, "id"),
                    json_object_get_string_member (object, "type"));

      free (fake);
    }

  if (!json_object_has_member (object, "type"))
    {
      val = json_object_get_member (object, "id");

      _clutter_script_warn_missing_attribute (script,
                                              json_node_get_string (val),
                                              "type");
      return;
    }

  id_ = json_object_get_string_member (object, "id");
  CLUTTER_NOTE (SCRIPT, "Getting object info for object '%s'", id_);

  oinfo = _clutter_script_get_object_info (script, id_);
  if (oinfo == NULL)
    {
      const gchar *class_name;

      oinfo = g_slice_new0 (ObjectInfo);
      oinfo->merge_id = _clutter_script_get_last_merge_id (script);
      oinfo->id = g_strdup (id_);
      oinfo->has_unresolved = TRUE;

      class_name = json_object_get_string_member (object, "type");
      oinfo->class_name = g_strdup (class_name);

      if (json_object_has_member (object, "type_func"))
        {
          const gchar *type_func;

          type_func = json_object_get_string_member (object, "type_func");
          oinfo->type_func = g_strdup (type_func);

          /* remove the type_func member; we don't want it to
           * pollute the object members
           */
          json_object_remove_member (object, "type_func");
        }
    }

  if (json_object_has_member (object, "children"))
    {
      val = json_object_get_member (object, "children");
      oinfo->children = parse_children (oinfo, val);

      json_object_remove_member (object, "children");

      oinfo->has_unresolved = TRUE;
    }

  if (json_object_has_member (object, "signals"))
    {
      val = json_object_get_member (object, "signals");
      oinfo->signals = parse_signals (script, oinfo, val);

      json_object_remove_member (object, "signals");

      oinfo->has_unresolved = TRUE;
    }

  if (strcmp (oinfo->class_name, "ClutterStage") == 0 &&
      json_object_has_member (object, "is-default"))
    {
      oinfo->is_actor = TRUE;
      oinfo->is_stage = TRUE;
      oinfo->is_stage_default =
        json_object_get_boolean_member (object, "is-default");

      json_object_remove_member (object, "is-default");
    }
  else
    oinfo->is_stage_default = FALSE;

  members = json_object_get_members (object);
  for (l = members; l; l = l->next)
    {
      const gchar *name = l->data;
      PropertyInfo *pinfo;
      JsonNode *node;

      CLUTTER_NOTE (SCRIPT, "Object '%s' member '%s'",
                    oinfo->id,
                    name);

      /* we have already parsed these */
      if (strcmp (name, "id") == 0 || strcmp (name, "type") == 0)
        continue;

      node = json_object_get_member (object, name);

      /* this should not really happen; we're getting a list of
       * member names, and if one does not map a real member
       * value then it's likely that something has gone wrong
       */
      if (G_UNLIKELY (node == NULL))
        {
          CLUTTER_NOTE (SCRIPT,
                        "Empty node for member '%s' of object '%s' (type: %s)",
                        name,
                        oinfo->id,
                        oinfo->class_name);
          continue;
        }

      pinfo = g_slice_new (PropertyInfo);

      pinfo->name = g_strdup (name);
      pinfo->node = json_node_copy (node);
      pinfo->pspec = NULL;
      pinfo->is_child = g_str_has_prefix (name, "child::") ? TRUE : FALSE;
      pinfo->is_layout = g_str_has_prefix (name, "layout::") ? TRUE : FALSE;

      oinfo->properties = g_list_prepend (oinfo->properties, pinfo);
      oinfo->has_unresolved = TRUE;
    }

  g_list_free (members);

  CLUTTER_NOTE (SCRIPT,
                "Added object '%s' (type:%s, id:%d, props:%d, signals:%d)",
                oinfo->id,
                oinfo->class_name,
                oinfo->merge_id,
                g_list_length (oinfo->properties),
                g_list_length (oinfo->signals));

  _clutter_script_add_object_info (script, oinfo);
  _clutter_script_construct_object (script, oinfo);
}

static void
clutter_script_parser_parse_end (JsonParser *parser)
{
  clutter_script_ensure_objects (CLUTTER_SCRIPT_PARSER (parser)->script);
}

gboolean
_clutter_script_parse_translatable_string (ClutterScript *script,
                                           JsonNode      *node,
                                           char         **str)
{
  JsonObject *obj;
  const char *string, *domain, *context;
  const char *res;
  gboolean translatable;

  if (!JSON_NODE_HOLDS_OBJECT (node))
    return FALSE;

  obj = json_node_get_object (node);
  if (!(json_object_has_member (obj, "translatable") &&
        json_object_has_member (obj, "string")))
    return FALSE;

  translatable = json_object_get_boolean_member (obj, "translatable");

  string = json_object_get_string_member (obj, "string");
  if (string == NULL || *string == '\0')
    return FALSE;

  if (json_object_has_member (obj, "context"))
    context = json_object_get_string_member (obj, "context");
  else
    context = NULL;

  if (json_object_has_member (obj, "domain"))
    domain = json_object_get_string_member (obj, "domain");
  else
    domain = NULL;

  if (domain == NULL || *domain == '\0')
    domain = clutter_script_get_translation_domain (script);

  if (translatable)
    {
      if (context != NULL && *context != '\0')
        res = g_dpgettext2 (domain, context, string);
      else
        res = g_dgettext (domain, string);
    }
  else
    res = string;

  if (str != NULL)
    *str = g_strdup (res);

  return TRUE;
}

gboolean
_clutter_script_parse_node (ClutterScript *script,
                            GValue        *value,
                            const gchar   *name,
                            JsonNode      *node,
                            GParamSpec    *pspec)
{
  GValue node_value = G_VALUE_INIT;
  gboolean retval = FALSE;

  g_return_val_if_fail (CLUTTER_IS_SCRIPT (script), FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (node != NULL, FALSE);

  switch (JSON_NODE_TYPE (node))
    {
    case JSON_NODE_OBJECT:
      /* if we don't have a GParamSpec we can't infer the type
       * of the property; this usually means that this property
       * is a custom member that will be parsed by the Scriptable
       * interface implementantion
       */
      if (pspec == NULL && !G_IS_VALUE (value))
        return FALSE;
      else
        {
          GType p_type;
          ObjectInfo *oinfo;
          const gchar *id_;

          if (G_IS_VALUE (value))
            p_type = G_VALUE_TYPE (value);
          else
            {
              p_type = G_PARAM_SPEC_VALUE_TYPE (pspec);
              g_value_init (value, p_type);
            }

          if (g_type_is_a (p_type, G_TYPE_OBJECT))
            {
              /* default GObject handling: we get the id and
               * retrieve the ObjectInfo for it; since the object
               * definitions are parsed leaf-first we are guaranteed
               * to have a defined object at this point
               */
              id_ = _clutter_script_get_id_from_node (node);
              if (id_ == NULL || *id_ == '\0')
                return FALSE;

              oinfo = _clutter_script_get_object_info (script, id_);
              if (oinfo == NULL || oinfo->gtype == G_TYPE_INVALID )
                return FALSE;

              if (g_type_is_a (oinfo->gtype, p_type))
                {
                  /* force construction, even though it should
                   * not be necessary; we don't need the properties
                   * to be applied as well: they will when the
                   * ScriptParser finishes
                   */
                  _clutter_script_construct_object (script, oinfo);

                  g_value_set_object (value, oinfo->object);

                  return TRUE;
                }
            }
          else if (p_type == CLUTTER_TYPE_KNOT)
            {
              ClutterKnot knot = { 0, };

              /* knot := { "x" : (int), "y" : (int) } */

              if (_clutter_script_parse_knot (script, node, &knot))
                {
                  g_value_set_boxed (value, &knot);
                  return TRUE;
                }
            }
          else if (p_type == CLUTTER_TYPE_GEOMETRY)
            {
              ClutterGeometry geom = { 0, };

              /* geometry := {
               *        "x" : (int),
               *        "y" : (int),
               *        "width" : (int),
               *        "height" : (int)
               * }
               */

              if (_clutter_script_parse_geometry (script, node, &geom))
                {
                  g_value_set_boxed (value, &geom);
                  return TRUE;
                }
            }
          else if (p_type == CLUTTER_TYPE_COLOR)
            {
              ClutterColor color = { 0, };

              /* color := {
               *        "red" : (int),
               *        "green" : (int),
               *        "blue" : (int),
               *        "alpha" : (int)
               * }
               */

              if (_clutter_script_parse_color (script, node, &color))
                {
                  g_value_set_boxed (value, &color);
                  return TRUE;
                }
            }
          else if (p_type == CLUTTER_TYPE_POINT)
            {
              ClutterPoint point = CLUTTER_POINT_INIT_ZERO;

              if (_clutter_script_parse_point (script, node, &point))
                {
                  g_value_set_boxed (value, &point);
                  return TRUE;
                }
            }
          else if (p_type == CLUTTER_TYPE_SIZE)
            {
              ClutterSize size = CLUTTER_SIZE_INIT_ZERO;

              if (_clutter_script_parse_size (script, node, &size))
                {
                  g_value_set_boxed (value, &size);
                  return TRUE;
                }
            }
          else if (p_type == G_TYPE_STRING)
            {
              char *str = NULL;

              if (_clutter_script_parse_translatable_string (script, node, &str))
                {
                  g_value_take_string (value, str);
                  return TRUE;
                }
            }
         }
      return FALSE;

    case JSON_NODE_ARRAY:
      if (!pspec && !G_IS_VALUE (value))
        return FALSE;
      else
        {
          if (!G_IS_VALUE (value))
            g_value_init (value, G_PARAM_SPEC_VALUE_TYPE (pspec));

          if (G_VALUE_HOLDS (value, CLUTTER_TYPE_KNOT))
            {
              ClutterKnot knot = { 0, };

              /* knot := [ (int), (int) ] */

              if (_clutter_script_parse_knot (script, node, &knot))
                {
                  g_value_set_boxed (value, &knot);
                  return TRUE;
                }
            }
          else if (G_VALUE_HOLDS (value, CLUTTER_TYPE_GEOMETRY))
            {
              ClutterGeometry geom = { 0, };

              /* geometry := [ (int), (int), (int), (int) ] */

              if (_clutter_script_parse_geometry (script, node, &geom))
                {
                  g_value_set_boxed (value, &geom);
                  return TRUE;
                }
            }
          else if (CLUTTER_VALUE_HOLDS_COLOR (value))
            {
              ClutterColor color = { 0, };

              /* color := [ (int), (int), (int), (int) ] */

              if (_clutter_script_parse_color (script, node, &color))
                {
                  g_value_set_boxed (value, &color);
                  return TRUE;
                }
            }
          else if (G_VALUE_HOLDS (value, CLUTTER_TYPE_POINT))
            {
              ClutterPoint point = CLUTTER_POINT_INIT_ZERO;

              if (_clutter_script_parse_point (script, node, &point))
                {
                  g_value_set_boxed (value, &point);
                  return TRUE;
                }
            }
          else if (G_VALUE_HOLDS (value, CLUTTER_TYPE_SIZE))
            {
              ClutterSize size = CLUTTER_SIZE_INIT_ZERO;

              if (_clutter_script_parse_size (script, node, &size))
                {
                  g_value_set_boxed (value, &size);
                  return TRUE;
                }
            }
          else if (G_VALUE_HOLDS (value, G_TYPE_STRV))
            {
              JsonArray *array = json_node_get_array (node);
              guint i, array_len = json_array_get_length (array);
              GPtrArray *str_array = g_ptr_array_sized_new (array_len);

              /* strv := [ (str), (str), ... ] */

              for (i = 0; i < array_len; i++)
                {
                  JsonNode *val = json_array_get_element (array, i);

                  if (JSON_NODE_TYPE (val) != JSON_NODE_VALUE &&
                      json_node_get_string (val) == NULL)
                    continue;

                  g_ptr_array_add (str_array,
                                   (gpointer) json_node_get_string (val));
                }

              g_value_set_boxed (value, str_array->pdata);
              g_ptr_array_free (str_array, TRUE);

              return TRUE;
            }
        }
      return FALSE;

    case JSON_NODE_NULL:
      return FALSE;

    case JSON_NODE_VALUE:
      json_node_get_value (node, &node_value);

      if (!pspec && !G_IS_VALUE (value))
        g_value_init (value, G_VALUE_TYPE (&node_value));
      else if (pspec)
        g_value_init (value, G_PARAM_SPEC_VALUE_TYPE (pspec));

      switch (G_TYPE_FUNDAMENTAL (G_VALUE_TYPE (value)))
        {
        /* fundamental JSON types */
        case G_TYPE_INT64:
        case G_TYPE_DOUBLE:
        case G_TYPE_STRING:
        case G_TYPE_BOOLEAN:
          g_value_copy (&node_value, value);
          retval = TRUE;
          break;

        case G_TYPE_INT:
          g_value_set_int (value, g_value_get_int64 (&node_value));
          retval = TRUE;
          break;

        case G_TYPE_UINT:
          g_value_set_uint (value, (guint) g_value_get_int64 (&node_value));
          retval = TRUE;
          break;

        case G_TYPE_ULONG:
          g_value_set_ulong (value, (gulong) g_value_get_int64 (&node_value));
          retval = TRUE;
          break;

        case G_TYPE_UCHAR:
          g_value_set_uchar (value, (guchar) g_value_get_int64 (&node_value));
          retval = TRUE;
          break;

        case G_TYPE_FLOAT:
          if (G_VALUE_HOLDS (&node_value, G_TYPE_DOUBLE))
            {
              g_value_set_float (value, g_value_get_double (&node_value));
              retval = TRUE;
            }
          else if (G_VALUE_HOLDS (&node_value, G_TYPE_INT64))
            {
              g_value_set_float (value, g_value_get_int64 (&node_value));
              retval = TRUE;
            }
          break;

        case G_TYPE_ENUM:
          if (G_VALUE_HOLDS (&node_value, G_TYPE_INT64))
            {
              g_value_set_enum (value, g_value_get_int64 (&node_value));
              retval = TRUE;
            }
          else if (G_VALUE_HOLDS (&node_value, G_TYPE_STRING))
            {
              gint enum_value;

              retval = _clutter_script_enum_from_string (G_VALUE_TYPE (value),
                                                         g_value_get_string (&node_value),
                                                         &enum_value);
              if (retval)
                g_value_set_enum (value, enum_value);
            }
          break;

        case G_TYPE_FLAGS:
          if (G_VALUE_HOLDS (&node_value, G_TYPE_INT64))
            {
              g_value_set_flags (value, g_value_get_int64 (&node_value));
              retval = TRUE;
            }
          else if (G_VALUE_HOLDS (&node_value, G_TYPE_STRING))
            {
              gint flags_value;

              retval = _clutter_script_flags_from_string (G_VALUE_TYPE (value),
                                                          g_value_get_string (&node_value),
                                                          &flags_value);
              if (retval)
                g_value_set_flags (value, flags_value);
            }
          break;

        case G_TYPE_BOXED:
          if (G_VALUE_HOLDS (value, CLUTTER_TYPE_COLOR))
            {
              ClutterColor color = { 0, };

              retval = _clutter_script_parse_color (script, node, &color);
              if (retval)
                clutter_value_set_color (value, &color);
            }
          break;

        case G_TYPE_OBJECT:
          if (G_VALUE_HOLDS (&node_value, G_TYPE_STRING))
            {
              const gchar *str = g_value_get_string (&node_value);
              GObject *object = clutter_script_get_object (script, str);
              if (object)
                {
                  CLUTTER_NOTE (SCRIPT,
                                "Assigning '%s' (%s) to property '%s'",
                                str,
                                G_OBJECT_TYPE_NAME (object),
                                name);

                  g_value_set_object (value, object);
                  retval = TRUE;
                }
            }
          break;

        default:
          retval = FALSE;
          break;
        }

      if (G_VALUE_TYPE (value) == G_TYPE_GTYPE &&
          G_VALUE_HOLDS (&node_value, G_TYPE_STRING))
        {
          const gchar *str = g_value_get_string (&node_value);
          GType type = clutter_script_get_type_from_name (script, str);
          g_value_set_gtype (value, type);
          retval = TRUE;
        }

      g_value_unset (&node_value);
      break;
    }

  return retval;
}

static GList *
clutter_script_translate_parameters (ClutterScript  *script,
                                     GObject        *object,
                                     const gchar    *name,
                                     GList          *properties,
                                     GArray        **params)
{
  ClutterScriptable *scriptable = NULL;
  ClutterScriptableIface *iface = NULL;
  GList *l, *unparsed;
  gboolean parse_custom = FALSE;

  *params = g_array_new (FALSE, FALSE, sizeof (GParameter));

  if (CLUTTER_IS_SCRIPTABLE (object))
    {
      scriptable = CLUTTER_SCRIPTABLE (object);
      iface = CLUTTER_SCRIPTABLE_GET_IFACE (scriptable);

      if (iface->parse_custom_node)
        parse_custom = TRUE;
    }

  unparsed = NULL;

  for (l = properties; l != NULL; l = l->next)
    {
      PropertyInfo *pinfo = l->data;
      GParameter param = { NULL };
      gboolean res = FALSE;

      if (pinfo->is_child || pinfo->is_layout)
        {
          CLUTTER_NOTE (SCRIPT, "Skipping %s property '%s'",
                        pinfo->is_child ? "child" : "layout",
                        pinfo->name);
          unparsed = g_list_prepend (unparsed, pinfo);
          continue;
        }

      CLUTTER_NOTE (SCRIPT, "Parsing %s property (id:%s)",
                    pinfo->pspec ? "regular" : "custom",
                    pinfo->name);

      if (parse_custom)
        res = iface->parse_custom_node (scriptable, script, &param.value,
                                        pinfo->name,
                                        pinfo->node);

      if (!res)
        res = _clutter_script_parse_node (script, &param.value,
                                          pinfo->name,
                                          pinfo->node,
                                          pinfo->pspec);

      if (!res)
        {
          CLUTTER_NOTE (SCRIPT, "Property '%s' ignored", pinfo->name);
          unparsed = g_list_prepend (unparsed, pinfo);
          continue;
        }

      param.name = g_strdup (pinfo->name);

      g_array_append_val (*params, param);

      property_info_free (pinfo);
    }

  g_list_free (properties);

  return unparsed;
}

static GList *
clutter_script_construct_parameters (ClutterScript  *script,
                                     GType           gtype,
                                     const gchar    *name,
                                     GList          *properties,
                                     GArray        **construct_params)
{
  GObjectClass *klass;
  GList *l, *unparsed;

  klass = g_type_class_ref (gtype);
  g_assert (klass != NULL);

  *construct_params = g_array_new (FALSE, FALSE, sizeof (GParameter));

  unparsed = NULL;

  for (l = properties; l != NULL; l = l->next)
    {
      PropertyInfo *pinfo = l->data;
      GParameter param = { NULL };
      GParamSpec *pspec = NULL;

      /* we allow custom property names for classes, so if we
       * don't find a corresponding GObject property for this
       * class we just skip it and let the class itself deal
       * with it later on
       */
      pspec = g_object_class_find_property (klass, pinfo->name);
      if (pspec)
        pinfo->pspec = g_param_spec_ref (pspec);
      else
        {
          pinfo->pspec = NULL;
          unparsed = g_list_prepend (unparsed, pinfo);
          continue;
        }

      if (!(pspec->flags & G_PARAM_CONSTRUCT_ONLY))
        {
          unparsed = g_list_prepend (unparsed, pinfo);
          continue;
        }

      param.name = g_strdup (pinfo->name);

      if (!_clutter_script_parse_node (script, &param.value,
                                       pinfo->name,
                                       pinfo->node,
                                       pinfo->pspec))
        {
          unparsed = g_list_prepend (unparsed, pinfo);
          continue;
        }

      g_array_append_val (*construct_params, param);

      property_info_free (pinfo);
    }

  g_list_free (properties);

  g_type_class_unref (klass);

  return unparsed;
}

static void
apply_layout_properties (ClutterScript    *script,
                         ClutterContainer *container,
                         ClutterActor     *actor,
                         ObjectInfo       *oinfo)
{
  ClutterScriptable *scriptable = NULL;
  ClutterScriptableIface *iface = NULL;
  gboolean parse_custom_node = FALSE;
  GList *l, *unresolved, *properties;
  ClutterLayoutManager *manager;
  GType meta_type;

  manager = g_object_get_data (G_OBJECT (container), "clutter-layout-manager");
  if (manager == NULL)
    return;

  meta_type = _clutter_layout_manager_get_child_meta_type (manager);
  if (meta_type == G_TYPE_INVALID)
    return;

  CLUTTER_NOTE (SCRIPT, "Layout manager of type '%s' with meta type '%s'",
                G_OBJECT_TYPE_NAME (manager),
                g_type_name (meta_type));

  /* shortcut, to avoid typechecking every time */
  if (CLUTTER_IS_SCRIPTABLE (manager))
    {
      scriptable = CLUTTER_SCRIPTABLE (manager);
      iface = CLUTTER_SCRIPTABLE_GET_IFACE (scriptable);

      parse_custom_node = iface->parse_custom_node != NULL ? TRUE : FALSE;
    }

  properties = oinfo->properties;
  oinfo->properties = NULL;

  unresolved = NULL;
  for (l = properties; l != NULL; l = l->next)
    {
      PropertyInfo *pinfo = l->data;
      GValue value = G_VALUE_INIT;
      gboolean res = FALSE;
      const gchar *name;

      if (!pinfo->is_layout)
        {
          unresolved = g_list_prepend (unresolved, pinfo);
          continue;
        }

      name = pinfo->name + strlen ("layout::");

      pinfo->pspec =
        clutter_layout_manager_find_child_property (manager, name);

      if (pinfo->pspec != NULL)
        g_param_spec_ref (pinfo->pspec);

      CLUTTER_NOTE (SCRIPT, "Parsing %s layout property (id:%s)",
                    pinfo->pspec != NULL ? "regular" : "custom",
                    name);

      if (parse_custom_node)
        res = iface->parse_custom_node (scriptable, script, &value,
                                        name,
                                        pinfo->node);

      if (!res)
        res = _clutter_script_parse_node (script, &value,
                                          name,
                                          pinfo->node,
                                          pinfo->pspec);

      if (!res)
        {
          CLUTTER_NOTE (SCRIPT, "Layout property '%s' ignored", name);
          unresolved = g_list_prepend (unresolved, pinfo);
          continue;
        }

      CLUTTER_NOTE (SCRIPT,
                    "Setting %s layout property '%s' (type:%s) to "
                    "object '%s' (id:%s)",
                    iface->set_custom_property != NULL ? "custom" : "regular",
                    name,
                    g_type_name (G_VALUE_TYPE (&value)),
                    g_type_name (oinfo->gtype),
                    oinfo->id);

      clutter_layout_manager_child_set_property (manager, container, actor,
                                                 name,
                                                 &value);

      g_value_unset (&value);

      property_info_free (pinfo);
    }

  g_list_free (properties);

  oinfo->properties = unresolved;
}

static void
apply_child_properties (ClutterScript    *script,
                        ClutterContainer *container,
                        ClutterActor     *actor,
                        ObjectInfo       *oinfo)
{
  ClutterScriptable *scriptable = NULL;
  ClutterScriptableIface *iface = NULL;
  gboolean parse_custom_node = FALSE;
  GList *l, *unresolved, *properties;
  GObjectClass *klass;
  GType meta_type;

  meta_type = CLUTTER_CONTAINER_GET_IFACE (container)->child_meta_type;
  if (meta_type == G_TYPE_INVALID)
    return;

  klass = G_OBJECT_GET_CLASS (container);

  /* shortcut, to avoid typechecking every time */
  if (CLUTTER_IS_SCRIPTABLE (container))
    {
      scriptable = CLUTTER_SCRIPTABLE (container);
      iface = CLUTTER_SCRIPTABLE_GET_IFACE (scriptable);

      parse_custom_node = iface->parse_custom_node != NULL ? TRUE : FALSE;
    }

  properties = oinfo->properties;
  oinfo->properties = NULL;

  unresolved = NULL;
  for (l = properties; l != NULL; l = l->next)
    {
      PropertyInfo *pinfo = l->data;
      GValue value = G_VALUE_INIT;
      gboolean res = FALSE;
      const gchar *name;

      if (!pinfo->is_child)
        {
          unresolved = g_list_prepend (unresolved, pinfo);
          continue;
        }

      name = pinfo->name + strlen ("child::");

      pinfo->pspec =
        clutter_container_class_find_child_property (klass, name);

      if (pinfo->pspec != NULL)
        g_param_spec_ref (pinfo->pspec);

      CLUTTER_NOTE (SCRIPT, "Parsing %s child property (id:%s)",
                    pinfo->pspec != NULL ? "regular" : "custom",
                    name);

      if (parse_custom_node)
        res = iface->parse_custom_node (scriptable, script, &value,
                                        name,
                                        pinfo->node);

      if (!res)
        res = _clutter_script_parse_node (script, &value,
                                          name,
                                          pinfo->node,
                                          pinfo->pspec);

      if (!res)
        {
          CLUTTER_NOTE (SCRIPT, "Child property '%s' ignored", name);
          unresolved = g_list_prepend (unresolved, pinfo);
          continue;
        }


      CLUTTER_NOTE (SCRIPT,
                    "Setting %s child property '%s' (type:%s) to "
                    "object '%s' (id:%s)",
                    iface->set_custom_property != NULL ? "custom" : "regular",
                    name,
                    g_type_name (G_VALUE_TYPE (&value)),
                    g_type_name (oinfo->gtype),
                    oinfo->id);

      clutter_container_child_set_property (container, actor,
                                            name,
                                            &value);

      g_value_unset (&value);

      property_info_free (pinfo);
    }

  g_list_free (properties);

  oinfo->properties = unresolved;
}

static void
add_children (ClutterScript *script,
              ObjectInfo    *oinfo)
{
  ClutterContainer *container = CLUTTER_CONTAINER (oinfo->object);
  GList *l, *unresolved;

  unresolved = NULL;
  for (l = oinfo->children; l != NULL; l = l->next)
    {
      const gchar *name = l->data;
      GObject *object = NULL;
      ObjectInfo *child_info;

      child_info = _clutter_script_get_object_info (script, name);
      if (child_info != NULL)
        {
          _clutter_script_construct_object (script, child_info);
          object = child_info->object;
        }

      if (object == NULL)
        {
          unresolved = g_list_prepend (unresolved, g_strdup (name));
          continue;
        }

      if (!CLUTTER_IS_ACTOR (object))
        {
          g_warning ("The object definition '%s' (type: %s) is not "
                     "an actor, but it is referenced in the 'children' "
                     "member of the container '%s' (type: %s); skipping.",
                     child_info->id,
                     g_type_name (child_info->gtype),
                     oinfo->id,
                     g_type_name (oinfo->gtype));
          continue;
        }

      CLUTTER_NOTE (SCRIPT, "Adding children '%s' to actor of type '%s'",
                    name,
                    g_type_name (G_OBJECT_TYPE (container)));

      clutter_container_add_actor (container, CLUTTER_ACTOR (object));
    }

  g_list_foreach (oinfo->children, (GFunc) free, NULL);
  g_list_free (oinfo->children);

  oinfo->children = unresolved;
}

static inline void
_clutter_script_check_unresolved (ClutterScript *script,
                                  ObjectInfo    *oinfo)
{
  if (oinfo->children != NULL && CLUTTER_IS_CONTAINER (oinfo->object))
    add_children (script, oinfo);

  /* this is a bit *eugh*, but it allows us to effectively make sure
   * that child and layout properties are parsed and applied to the
   * right child
   */
  if (oinfo->properties != NULL && CLUTTER_IS_ACTOR (oinfo->object))
    {
      ClutterActor *parent;

      parent = clutter_actor_get_parent (CLUTTER_ACTOR (oinfo->object));
      if (parent != NULL)
        {
          ClutterContainer *container = CLUTTER_CONTAINER (parent);
          ClutterActor *child;

          for (child = clutter_actor_get_first_child (parent);
               child != NULL;
               child = clutter_actor_get_next_sibling (child))
            {
              ObjectInfo *child_info;
              const gchar *id_;

              id_ = clutter_get_script_id (G_OBJECT (child));
              if (id_ == NULL || *id_ == '\0')
                continue;

              child_info = _clutter_script_get_object_info (script, id_);
              if (child_info == NULL)
                continue;

              apply_child_properties (script, container,
                                      child,
                                      child_info);
              apply_layout_properties (script, container,
                                       child,
                                       child_info);
            }
        }
    }

  if (oinfo->properties || oinfo->children)
    oinfo->has_unresolved = TRUE;
  else
    oinfo->has_unresolved = FALSE;
}

void
_clutter_script_apply_properties (ClutterScript *script,
                                  ObjectInfo    *oinfo)
{
  ClutterScriptable *scriptable = NULL;
  ClutterScriptableIface *iface = NULL;
  gboolean set_custom_property = FALSE;
  GObject *object = oinfo->object;
  GList *properties;
  GArray *params;
  guint i;

  if (!oinfo->has_unresolved)
    return;

  /* shortcut, to avoid typechecking every time */
  if (CLUTTER_IS_SCRIPTABLE (object))
    {
      scriptable = CLUTTER_SCRIPTABLE (object);
      iface = CLUTTER_SCRIPTABLE_GET_IFACE (scriptable);

      if (iface->set_custom_property)
        set_custom_property = TRUE;
    }

  /* then we get the rest of the parameters, asking the object itself
   * to translate them for us, if we cannot do that
   */
  properties = oinfo->properties;
  oinfo->properties = clutter_script_translate_parameters (script,
                                                           object,
                                                           oinfo->id,
                                                           properties,
                                                           &params);

  /* consume all the properties we could translate in this pass */
  for (i = 0; i < params->len; i++)
    {
      GParameter *param = &g_array_index (params, GParameter, i);

      CLUTTER_NOTE (SCRIPT,
                    "Setting %s property '%s' (type:%s) to object '%s' (id:%s)",
                    set_custom_property ? "custom" : "regular",
                    param->name,
                    g_type_name (G_VALUE_TYPE (&param->value)),
                    g_type_name (oinfo->gtype),
                    oinfo->id);

      if (set_custom_property)
        iface->set_custom_property (scriptable, script,
                                    param->name,
                                    &param->value);
      else
        g_object_set_property (object, param->name, &param->value);

      free ((gchar *) param->name);
      g_value_unset (&param->value);
    }

  g_array_free (params, TRUE);

  _clutter_script_check_unresolved (script, oinfo);
}

void
_clutter_script_construct_object (ClutterScript *script,
                                  ObjectInfo    *oinfo)
{
  GArray *params = NULL;
  guint i;

  /* we have completely updated the object */
  if (oinfo->object != NULL)
    {
      if (oinfo->has_unresolved)
        _clutter_script_check_unresolved (script, oinfo);

      return;
    }

  if (oinfo->gtype == G_TYPE_INVALID)
    {
      if (G_UNLIKELY (oinfo->type_func))
        oinfo->gtype = _clutter_script_get_type_from_symbol (oinfo->type_func);
      else
        oinfo->gtype = clutter_script_get_type_from_name (script, oinfo->class_name);

      if (G_UNLIKELY (oinfo->gtype == G_TYPE_INVALID))
        return;
    }

  oinfo->is_actor = g_type_is_a (oinfo->gtype, CLUTTER_TYPE_ACTOR);
  if (oinfo->is_actor)
    oinfo->is_stage = g_type_is_a (oinfo->gtype, CLUTTER_TYPE_STAGE);

  if (oinfo->is_stage && oinfo->is_stage_default)
    {
      ClutterStageManager *manager = clutter_stage_manager_get_default ();
      GList *properties = oinfo->properties;
      ClutterStage *default_stage;

      /* the default stage is a complex beast: we cannot create it using
       * g_object_newv() but we need clutter_script_construct_parameters()
       * to add the GParamSpec to the PropertyInfo pspec member, so
       * that we don't have to implement every complex property (like
       * the "color" one) directly inside the ClutterStage class.
       */
      oinfo->properties =
        clutter_script_construct_parameters (script,
                                             oinfo->gtype,
                                             oinfo->id,
                                             properties,
                                             &params);

      default_stage = clutter_stage_manager_get_default_stage (manager);
      oinfo->object = G_OBJECT (default_stage);

      for (i = 0; i < params->len; i++)
        {
          GParameter *param = &g_array_index (params, GParameter, i);

          free ((gchar *) param->name);
          g_value_unset (&param->value);
        }

      g_array_free (params, TRUE);
    }
  else
    {
      GList *properties = oinfo->properties;
      GParameter *parameters;

      /* every other object: first, we get the construction parameters */
      oinfo->properties =
        clutter_script_construct_parameters (script,
                                             oinfo->gtype,
                                             oinfo->id,
                                             properties,
                                             &params);

      parameters = (GParameter *) (void *) params->data;
      oinfo->object = g_object_newv (oinfo->gtype,
                                     params->len,
                                     parameters);

      /* by sinking the floating reference, we make sure that the reference
       * count is correct whether the object is referenced from somewhere
       * else too or only by this ClutterScript object.
       */
      g_object_ref_sink (oinfo->object);

      for (i = 0; i < params->len; i++)
        {
          GParameter *param = &g_array_index (params, GParameter, i);

          free ((gchar *) param->name);
          g_value_unset (&param->value);
        }

      g_array_free (params, TRUE);
   }

  g_assert (oinfo->object != NULL);

  if (CLUTTER_IS_SCRIPTABLE (oinfo->object))
    clutter_scriptable_set_id (CLUTTER_SCRIPTABLE (oinfo->object), oinfo->id);
  else
    g_object_set_data_full (oinfo->object, "clutter-script-id",
                            g_strdup (oinfo->id),
                            free);

  _clutter_script_check_unresolved (script, oinfo);
}
