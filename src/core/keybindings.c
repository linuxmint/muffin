/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Muffin Keybindings */
/* 
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2002 Red Hat Inc.
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
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

/**
 * SECTION:keybindings
 * @title: MetaKeybinding
 * @short_description: Key bindings
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define _SVID_SOURCE /* for putenv() */

#include <config.h>
#include "keybindings-private.h"
#include "workspace-private.h"
#include <meta/errors.h>
#include "edge-resistance.h"
#include "ui.h"
#include "frame.h"
#include "place.h"
#include <meta/prefs.h>
#include <meta/util.h>

#include <X11/keysym.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_XKB
#include <X11/XKBlib.h>
#endif

#define SCHEMA_MUFFIN_KEYBINDINGS "org.cinnamon.desktop.keybindings.wm"
#define SCHEMA_MUFFIN "org.cinnamon.muffin"

static gboolean all_bindings_disabled = FALSE;
static gboolean modifier_only_is_down = FALSE;

static gboolean add_builtin_keybinding (MetaDisplay          *display,
                                        const char           *name,
                                        const char           *schema,
                                        MetaKeyBindingFlags   flags,
                                        MetaKeyBindingAction  action,
                                        MetaKeyHandlerFunc    handler,
                                        int                   handler_arg);

static void invoke_handler_by_name (MetaDisplay    *display,
                                    MetaScreen     *screen,
                                    const char     *handler_name,
                                    MetaWindow     *window,
                                    XEvent         *event);

enum {
  META_MOVE_TO_XCHANGE_FLAG = 8, // 1000
  META_MOVE_TO_YCHANGE_FLAG = 4, // 0100
  META_MOVE_TO_RIGHT_FLAG   = 2, // 0010
  META_MOVE_TO_BOTTOM_FLAG  = 1  // 0001
};

// Using flags above
enum {
  META_MOVE_TO_SE = 15, // 1111
  META_MOVE_TO_NE = 14, // 1110
  META_MOVE_TO_SW = 13, // 1101
  META_MOVE_TO_NW = 12, // 1100
  META_MOVE_TO_E  = 10, // 1010
  META_MOVE_TO_W  = 8,  // 1000
  META_MOVE_TO_S  = 5,  // 0101
  META_MOVE_TO_N  = 4   // 0100
};

static void
meta_key_binding_free (MetaKeyBinding *binding)
{
  g_slice_free (MetaKeyBinding, binding);
}

static MetaKeyBinding *
meta_key_binding_copy (MetaKeyBinding *binding)
{
  return g_slice_dup (MetaKeyBinding, binding);
}

GType
meta_key_binding_get_type (void)
{
  static GType type_id = 0;

  if (G_UNLIKELY (type_id == 0))
    type_id = g_boxed_type_register_static (g_intern_static_string ("MetaKeyBinding"),
                                            (GBoxedCopyFunc)meta_key_binding_copy,
                                            (GBoxedFreeFunc)meta_key_binding_free);

  return type_id;
}

const char *
meta_key_binding_get_name (MetaKeyBinding *binding)
{
  return binding->name;
}

MetaVirtualModifier
meta_key_binding_get_modifiers (MetaKeyBinding *binding)
{
  return binding->modifiers;
}

guint
meta_key_binding_get_mask (MetaKeyBinding *binding)
{
  return binding->mask;
}

/* These can't be bound to anything, but they are used to handle
 * various other events.  TODO: Possibly we should include them as event
 * handler functions and have some kind of flag to say they're unbindable.
 */

static gboolean process_mouse_move_resize_grab (MetaDisplay *display,
                                                MetaScreen  *screen,
                                                MetaWindow  *window,
                                                XEvent      *event,
                                                KeySym       keysym);

static gboolean process_keyboard_move_grab (MetaDisplay *display,
                                            MetaScreen  *screen,
                                            MetaWindow  *window,
                                            XEvent      *event,
                                            KeySym       keysym);

static gboolean process_keyboard_resize_grab (MetaDisplay *display,
                                              MetaScreen  *screen,
                                              MetaWindow  *window,
                                              XEvent      *event,
                                              KeySym       keysym);

static void regrab_key_bindings         (MetaDisplay *display);


static GHashTable *key_handlers;

#define HANDLER(name) g_hash_table_lookup (key_handlers, (name))

static void
key_handler_free (MetaKeyHandler *handler)
{
  g_free (handler->name);
  if (handler->user_data_free_func && handler->user_data)
    handler->user_data_free_func (handler->user_data);
  g_free (handler);
}

static void
reload_keymap (MetaDisplay *display)
{
  if (display->keymap)
    meta_XFree (display->keymap);

  /* This is expensive to compute, so we'll lazily load if and when we first
   * need it */
  display->above_tab_keycode = 0;

  display->keymap = XGetKeyboardMapping (display->xdisplay,
                                         display->min_keycode,
                                         display->max_keycode -
                                         display->min_keycode + 1,
                                         &display->keysyms_per_keycode);  
}

static void
reload_modmap (MetaDisplay *display)
{
  XModifierKeymap *modmap;
  int map_size;
  int i;
  
  if (display->modmap)
    XFreeModifiermap (display->modmap);

  modmap = XGetModifierMapping (display->xdisplay);
  display->modmap = modmap;

  display->ignored_modifier_mask = 0;

  /* Multiple bits may get set in each of these */
  display->num_lock_mask = 0;
  display->scroll_lock_mask = 0;
  display->meta_mask = 0;
  display->hyper_mask = 0;
  display->super_mask = 0;
  
  /* there are 8 modifiers, and the first 3 are shift, shift lock,
   * and control
   */
  map_size = 8 * modmap->max_keypermod;
  i = 3 * modmap->max_keypermod;
  while (i < map_size)
    {
      /* get the key code at this point in the map,
       * see if its keysym is one we're interested in
       */
      int keycode = modmap->modifiermap[i];
      
      if (keycode >= display->min_keycode &&
          keycode <= display->max_keycode)
        {
          int j = 0;
          KeySym *syms = display->keymap +
            (keycode - display->min_keycode) * display->keysyms_per_keycode;

          while (j < display->keysyms_per_keycode)
            {
              if (syms[j] != 0)
                {
                  const char *str;
                  
                  str = XKeysymToString (syms[j]);
                  meta_topic (META_DEBUG_KEYBINDINGS,
                              "Keysym %s bound to modifier 0x%x\n",
                              str ? str : "none",
                              (1 << ( i / modmap->max_keypermod)));
                }
              
              if (syms[j] == XK_Num_Lock)
                {
                  /* Mod1Mask is 1 << 3 for example, i.e. the
                   * fourth modifier, i / keyspermod is the modifier
                   * index
                   */
                  
                  display->num_lock_mask |= (1 << ( i / modmap->max_keypermod));
                }
              else if (syms[j] == XK_Scroll_Lock)
                {
                  display->scroll_lock_mask |= (1 << ( i / modmap->max_keypermod));
                }
              else if (syms[j] == XK_Super_L ||
                       syms[j] == XK_Super_R)
                {
                  display->super_mask |= (1 << ( i / modmap->max_keypermod));
                }
              else if (syms[j] == XK_Hyper_L ||
                       syms[j] == XK_Hyper_R)
                {
                  display->hyper_mask |= (1 << ( i / modmap->max_keypermod));
                }              
              else if (syms[j] == XK_Meta_L ||
                       syms[j] == XK_Meta_R)
                {
                  display->meta_mask |= (1 << ( i / modmap->max_keypermod));
                }
              
              ++j;
            }
        }
      
      ++i;
    }

  display->ignored_modifier_mask = (display->num_lock_mask |
                                    display->scroll_lock_mask |
                                    LockMask);

  meta_topic (META_DEBUG_KEYBINDINGS,
              "Ignoring modmask 0x%x num lock 0x%x scroll lock 0x%x hyper 0x%x super 0x%x meta 0x%x\n",
              display->ignored_modifier_mask,
              display->num_lock_mask,
              display->scroll_lock_mask,
              display->hyper_mask,
              display->super_mask,
              display->meta_mask);
}

static guint
keysym_to_keycode (MetaDisplay *display,
                   guint        keysym)
{
  if (keysym == META_KEY_ABOVE_TAB)
    return meta_display_get_above_tab_keycode (display);
  else
    return XKeysymToKeycode (display->xdisplay, keysym);
}

static void
reload_keycodes (MetaDisplay *display)
{
  meta_topic (META_DEBUG_KEYBINDINGS,
              "Reloading keycodes for binding tables\n");

  if (display->key_bindings)
    {
      int i;
      
      i = 0;
      while (i < display->n_key_bindings)
        {
          if (display->key_bindings[i].keysym != 0)
            {
              display->key_bindings[i].keycode =
                keysym_to_keycode (display, display->key_bindings[i].keysym);
            }
          
          ++i;
        }
    }
}

static void
reload_modifiers (MetaDisplay *display)
{
  meta_topic (META_DEBUG_KEYBINDINGS,
              "Reloading keycodes for binding tables\n");
  
  if (display->key_bindings)
    {
      int i;
      
      i = 0;
      while (i < display->n_key_bindings)
        {
          meta_display_devirtualize_modifiers (display,
                                               display->key_bindings[i].modifiers,
                                               &display->key_bindings[i].mask);

          meta_topic (META_DEBUG_KEYBINDINGS,
                      " Devirtualized mods 0x%x -> 0x%x (%s)\n",
                      display->key_bindings[i].modifiers,
                      display->key_bindings[i].mask,
                      display->key_bindings[i].name);          
          
          ++i;
        }
    }
}


static int
count_bindings (GList *prefs)
{
  GList *p;
  int count;

  count = 0;
  p = prefs;
  while (p)
    {
      MetaKeyPref *pref = (MetaKeyPref*)p->data;
      GSList *tmp = pref->bindings;

      while (tmp)
        {
          MetaKeyCombo *combo = tmp->data;

          if (combo && (combo->keysym != None || combo->keycode != 0))
            {
              count += 1;

              if (pref->add_shift &&
                  (combo->modifiers & META_VIRTUAL_SHIFT_MASK) == 0)
                count += 1;
            }

          tmp = tmp->next;
        }

      p = p->next;
    }

  return count;
}

static void
rebuild_binding_table (MetaDisplay     *display,
                       MetaKeyBinding **bindings_p,
                       int             *n_bindings_p,
                       GList           *prefs)
{
  GList *p;
  int n_bindings;
  int i;
  
  n_bindings = count_bindings (prefs);
  g_free (*bindings_p);
  *bindings_p = g_new0 (MetaKeyBinding, n_bindings);

  i = 0;
  p = prefs;
  while (p)
    {
      MetaKeyPref *pref = (MetaKeyPref*)p->data;
      GSList *tmp = pref->bindings;

      while (tmp)
        {
          MetaKeyCombo *combo = tmp->data;

          if (combo && (combo->keysym != None || combo->keycode != 0))
            {
              MetaKeyHandler *handler = HANDLER (pref->name);

              (*bindings_p)[i].name = pref->name;
              (*bindings_p)[i].handler = handler;
              (*bindings_p)[i].keysym = combo->keysym;
              (*bindings_p)[i].keycode = combo->keycode;
              (*bindings_p)[i].modifiers = combo->modifiers;
              (*bindings_p)[i].mask = 0;
          
              ++i;

              if (pref->add_shift &&
                  (combo->modifiers & META_VIRTUAL_SHIFT_MASK) == 0)
                {
                  meta_topic (META_DEBUG_KEYBINDINGS,
                              "Binding %s also needs Shift grabbed\n",
                               pref->name);
              
                  (*bindings_p)[i].name = pref->name;
                  (*bindings_p)[i].handler = handler;
                  (*bindings_p)[i].keysym = combo->keysym;
                  (*bindings_p)[i].keycode = combo->keycode;
                  (*bindings_p)[i].modifiers = combo->modifiers |
                    META_VIRTUAL_SHIFT_MASK;
                  (*bindings_p)[i].mask = 0;
              
                  ++i;
                }
            }
            
          tmp = tmp->next;
        }
      
      p = p->next;
    }

  g_assert (i == n_bindings);
  
  *n_bindings_p = i;

  meta_topic (META_DEBUG_KEYBINDINGS,
              " %d bindings in table\n",
              *n_bindings_p);
}

static void
rebuild_key_binding_table (MetaDisplay *display)
{
  GList *prefs;

  meta_topic (META_DEBUG_KEYBINDINGS,
              "Rebuilding key binding table from preferences\n");

  prefs = meta_prefs_get_keybindings ();
  rebuild_binding_table (display,
                         &display->key_bindings,
                         &display->n_key_bindings,
                         prefs);
  g_list_free (prefs);
}

static void
regrab_key_bindings (MetaDisplay *display)
{
  GSList *tmp;
  GSList *windows;

  meta_error_trap_push (display); /* for efficiency push outer trap */
  
  tmp = display->screens;
  while (tmp != NULL)
    {
      MetaScreen *screen = tmp->data;

      meta_screen_ungrab_keys (screen);
      meta_screen_grab_keys (screen);

      tmp = tmp->next;
    }

  windows = meta_display_list_windows (display, META_LIST_DEFAULT);
  tmp = windows;
  while (tmp != NULL)
    {
      MetaWindow *w = tmp->data;
      
      meta_window_ungrab_keys (w);
      meta_window_grab_keys (w);
      
      tmp = tmp->next;
    }
  meta_error_trap_pop (display);

  g_slist_free (windows);
}

static MetaKeyBinding *
display_get_keybinding (MetaDisplay  *display,
                        unsigned int  keysym,
                        unsigned int  keycode,
                        unsigned long mask)
{
  int i;

  i = display->n_key_bindings - 1;
  while (i >= 0)
    {
      if (display->key_bindings[i].keysym == keysym &&
          display->key_bindings[i].keycode == keycode &&
          display->key_bindings[i].mask == mask)
        {
          return &display->key_bindings[i];
        }
      
      --i;
    }

  return NULL;
}

static gboolean
add_keybinding_internal (MetaDisplay          *display,
                         const char           *name,
                         const char           *schema,
                         MetaKeyBindingFlags   flags,
                         MetaKeyBindingAction  action,
                         MetaKeyHandlerFunc    func,
                         int                   data,
                         gpointer              user_data,
                         GDestroyNotify        free_data)
{
  MetaKeyHandler *handler;

  if (!meta_prefs_add_keybinding (name, schema, action, flags))
    return FALSE;

  handler = g_new0 (MetaKeyHandler, 1);
  handler->name = g_strdup (name);
  handler->func = func;
  handler->default_func = func;
  handler->data = data;
  handler->flags = flags;
  handler->action = action;
  handler->user_data = user_data;
  handler->user_data_free_func = free_data;

  g_hash_table_insert (key_handlers, g_strdup (name), handler);

  return TRUE;
}

static gboolean
add_builtin_keybinding (MetaDisplay          *display,
                        const char           *name,
                        const char           *schema,
                        MetaKeyBindingFlags   flags,
                        MetaKeyBindingAction  action,
                        MetaKeyHandlerFunc    handler,
                        int                   handler_arg)
{
  return add_keybinding_internal (display, name, schema,
                                  flags | META_KEY_BINDING_BUILTIN,
                                  action, handler, handler_arg, NULL, NULL);
}

/**
 * meta_display_add_keybinding:
 * @display: a #MetaDisplay
 * @name: the binding's name
 * @schema: the #GSettings schema where @name is stored
 * @flags: flags to specify binding details
 * @handler: function to run when the keybinding is invoked
 * @user_data: the data to pass to @handler
 * @free_data: function to free @user_data
 *
 * Add a keybinding at runtime. The key @name in @schema needs to be of type
 * %G_VARIANT_TYPE_STRING_ARRAY, with each string describing a keybinding in
 * the form of "&lt;Control&gt;a" or "&lt;Shift&gt;&lt;Alt&gt;F1". The parser
 * is fairly liberal and allows lower or upper case, and also abbreviations
 * such as "&lt;Ctl&gt;" and "&lt;Ctrl&gt;". If the key is set to the empty
 * list or a list with a single element of either "" or "disabled", the
 * keybinding is disabled.  If %META_KEY_BINDING_REVERSES is specified in
 * @flags, the binding may be reversed by holding down the "shift" key;
 * therefore, "&lt;Shift&gt;"
 * cannot be one of the keys used. @handler is expected to check for the
 * "shift" modifier in this case and reverse its action.
 *
 * Use meta_display_remove_keybinding() to remove the binding.
 *
 * Returns: %TRUE if the keybinding was added successfully,
 *          otherwise %FALSE
 */
gboolean
meta_display_add_keybinding (MetaDisplay         *display,
                             const char          *name,
                             const char          *schema,
                             MetaKeyBindingFlags  flags,
                             MetaKeyHandlerFunc   handler,
                             gpointer             user_data,
                             GDestroyNotify       free_data)
{
  return add_keybinding_internal (display, name, schema, flags,
                                  META_KEYBINDING_ACTION_NONE,
                                  handler, 0, user_data, free_data);
}

/**
 * meta_display_remove_keybinding:
 * @display: the #MetaDisplay
 * @name: name of the keybinding to remove
 *
 * Remove keybinding @name; the function will fail if @name is not a known
 * keybinding or has not been added with meta_display_add_keybinding().
 *
 * Returns: %TRUE if the binding has been removed sucessfully,
 *          otherwise %FALSE
 */
gboolean
meta_display_remove_keybinding (MetaDisplay *display,
                                const char  *name)
{
  if (!meta_prefs_remove_keybinding (name))
    return FALSE;

  g_hash_table_remove (key_handlers, name);

  return TRUE;
}

static gboolean
add_custom_keybinding_internal (MetaDisplay          *display,
                              const char           *name,
                              const char          **bindings,
                              MetaKeyBindingFlags   flags,
                              MetaKeyBindingAction  action,
                              MetaKeyHandlerFunc    func,
                              int                   data,
                              gpointer              user_data,
                              GDestroyNotify        free_data)
{
  MetaKeyHandler *handler;

  if (!meta_prefs_add_custom_keybinding (name, bindings, action, flags))
    return FALSE;

  handler = g_new0 (MetaKeyHandler, 1);
  handler->name = g_strdup (name);
  handler->func = func;
  handler->default_func = func;
  handler->data = data;
  handler->flags = flags;
  handler->action = action;
  handler->user_data = user_data;
  handler->user_data_free_func = free_data;

  g_hash_table_insert (key_handlers, g_strdup (name), handler);

  return TRUE;
}


/**
 * meta_display_add_custom_keybinding:
 * @display: a #MetaDisplay
 * @name: the binding's unique name
 * @bindings: (allow-none) (array zero-terminated=1): array of parseable keystrokes
 * @callback: function to run when the keybinding is invoked
 * @user_data: the data to pass to @handler
 * @free_data: function to free @user_data
 *
 *
 * Use meta_display_remove_custom_keybinding() to remove the binding.
 *
 * Returns: %TRUE if the keybinding was added successfully,
 *          otherwise %FALSE
 */
gboolean
meta_display_add_custom_keybinding (MetaDisplay       *display,
                                  const char          *name,
                                  const char         **bindings,
                                  MetaKeyHandlerFunc   callback,
                                  gpointer             user_data,
                                  GDestroyNotify       free_data)

{
  return add_custom_keybinding_internal (display, name, bindings,
                                       META_KEY_BINDING_PER_WINDOW,
                                       META_KEYBINDING_ACTION_CUSTOM,
                                       (MetaKeyHandlerFunc)callback, 0, user_data, free_data);
}

/**
 * meta_display_remove_custom_keybinding:
 * @display: the #MetaDisplay
 * @name: name of the keybinding to remove
 *
 * Remove keybinding @name; the function will fail if @name is not a known
 * keybinding or has not been added with meta_display_add_custom_keybinding().
 *
 * Returns: %TRUE if the binding has been removed sucessfully,
 *          otherwise %FALSE
 */
gboolean
meta_display_remove_custom_keybinding (MetaDisplay *display,
                                     const char  *name)
{
  if (!meta_prefs_remove_custom_keybinding (name))
    return FALSE;

  g_hash_table_remove (key_handlers, name);

  return TRUE;
}

/**
 * meta_display_get_keybinding_action:
 * @display: A #MetaDisplay
 * @keycode: Raw keycode
 * @mask: Event mask
 *
 * Get the #MetaKeyBindingAction bound to %keycode. Only builtin
 * keybindings have an associated #MetaKeyBindingAction, for
 * bindings added dynamically with meta_display_add_keybinding()
 * the function will always return %META_KEYBINDING_ACTION_NONE.
 *
 * Returns: The action that should be taken for the given key, or
 * %META_KEYBINDING_ACTION_NONE.
 */
MetaKeyBindingAction
meta_display_get_keybinding_action (MetaDisplay  *display,
                                    unsigned int  keycode,
                                    unsigned long mask)
{
  MetaKeyBinding *binding;
  KeySym keysym;

  keysym = XkbKeycodeToKeysym (display->xdisplay, keycode, 0, 0);
  mask = mask & 0xff & ~display->ignored_modifier_mask;
  binding = display_get_keybinding (display, keysym, keycode, mask);

  if (!binding && keycode == meta_display_get_above_tab_keycode (display))
    binding = display_get_keybinding (display, META_KEY_ABOVE_TAB, keycode, mask);

  if (binding)
    return meta_prefs_get_keybinding_action (binding->name);
  else
    return META_KEYBINDING_ACTION_NONE;
}

void
meta_display_keybinding_action_invoke_by_code (MetaDisplay  *display,
                                               unsigned int  keycode,
                                               unsigned long mask)
{
  MetaKeyBinding *binding;
  KeySym keysym;

  keysym = XkbKeycodeToKeysym (display->xdisplay, keycode, 0, 0);
  mask = mask & 0xff & ~display->ignored_modifier_mask;
  binding = display_get_keybinding (display, keysym, keycode, mask);

  if (!binding && keycode == meta_display_get_above_tab_keycode (display))
    binding = display_get_keybinding (display, META_KEY_ABOVE_TAB, keycode, mask);

  if (binding)
    invoke_handler_by_name (display, NULL, binding->name, NULL, NULL);
}

LOCAL_SYMBOL void
meta_display_process_mapping_event (MetaDisplay *display,
                                    XEvent      *event)
{ 
  gboolean keymap_changed = FALSE;
  gboolean modmap_changed = FALSE;

#ifdef HAVE_XKB
  if (event->type == display->xkb_base_event_type)
    {
      meta_topic (META_DEBUG_KEYBINDINGS,
                  "XKB mapping changed, will redo keybindings\n");

      keymap_changed = TRUE;
      modmap_changed = TRUE;
    }
  else
#endif
  if (event->xmapping.request == MappingModifier)
    {
      meta_topic (META_DEBUG_KEYBINDINGS,
                  "Received MappingModifier event, will reload modmap and redo keybindings\n");

      modmap_changed = TRUE;
    }
  else if (event->xmapping.request == MappingKeyboard)
    {
      meta_topic (META_DEBUG_KEYBINDINGS,
                  "Received MappingKeyboard event, will reload keycodes and redo keybindings\n");

      keymap_changed = TRUE;
    }

  /* Now to do the work itself */

  if (keymap_changed || modmap_changed)
    {
      if (keymap_changed)
        reload_keymap (display);

      /* Deciphering the modmap depends on the loaded keysyms to find out
       * what modifiers is Super and so forth, so we need to reload it
       * even when only the keymap changes */
      reload_modmap (display);

      if (keymap_changed)
        reload_keycodes (display);

      reload_modifiers (display);

      regrab_key_bindings (display);
    }
}

static void
bindings_changed_callback (MetaPreference pref,
                           void          *data)
{
  MetaDisplay *display;

  display = data;
  
  switch (pref)
    {
    case META_PREF_KEYBINDINGS:
      rebuild_key_binding_table (display);
      reload_keycodes (display);
      reload_modifiers (display);
      regrab_key_bindings (display);
      break;
    default:
      break;
    }
}

void
meta_display_rebuild_keybindings (MetaDisplay *display)
{
    rebuild_key_binding_table (display);
    reload_keycodes (display);
    reload_modifiers (display);
    regrab_key_bindings (display);
}

LOCAL_SYMBOL void
meta_display_shutdown_keys (MetaDisplay *display)
{
  /* Note that display->xdisplay is invalid in this function */
  
  meta_prefs_remove_listener (bindings_changed_callback, display);

  if (display->keymap)
    meta_XFree (display->keymap);
  
  if (display->modmap)
    XFreeModifiermap (display->modmap);
  g_free (display->key_bindings);
}

static const char*
keysym_name (int keysym)
{
  const char *name;
  
  name = XKeysymToString (keysym);
  if (name == NULL)
    name = "(unknown)";

  return name;
}

/* Grab/ungrab, ignoring all annoying modifiers like NumLock etc. */
static void
meta_change_keygrab (MetaDisplay *display,
                     Window       xwindow,
                     gboolean     grab,
                     int          keysym,
                     unsigned int keycode,
                     int          modmask)
{
  unsigned int ignored_mask;

  /* Grab keycode/modmask, together with
   * all combinations of ignored modifiers.
   * X provides no better way to do this.
   */

  meta_topic (META_DEBUG_KEYBINDINGS,
              "%s keybinding %s keycode %d mask 0x%x on 0x%lx\n",
              grab ? "Grabbing" : "Ungrabbing",
              keysym_name (keysym), keycode,
              modmask, xwindow);

  /* efficiency, avoid so many XSync() */
  meta_error_trap_push (display);
  
  ignored_mask = 0;
  while (ignored_mask <= display->ignored_modifier_mask)
    {
      if (ignored_mask & ~(display->ignored_modifier_mask))
        {
          /* Not a combination of ignored modifiers
           * (it contains some non-ignored modifiers)
           */
          ++ignored_mask;
          continue;
        }

      if (meta_is_debugging ())
        meta_error_trap_push_with_return (display);
      if (grab)
        XGrabKey (display->xdisplay, keycode,
                  modmask | ignored_mask,
                  xwindow,
                  True,
                  GrabModeAsync, GrabModeSync);
      else
        XUngrabKey (display->xdisplay, keycode,
                    modmask | ignored_mask,
                    xwindow);

      if (meta_is_debugging ())
        {
          int result;
          
          result = meta_error_trap_pop_with_return (display);
          
          if (grab && result != Success)
            {      
              if (result == BadAccess)
                meta_warning (_("Some other program is already using the key %s with modifiers %x as a binding\n"), keysym_name (keysym), modmask | ignored_mask);
              else
                meta_topic (META_DEBUG_KEYBINDINGS,
                            "Failed to grab key %s with modifiers %x\n",
                            keysym_name (keysym), modmask | ignored_mask);
            }
        }

      ++ignored_mask;
    }

  meta_error_trap_pop (display);
}

static void
meta_grab_key (MetaDisplay *display,
               Window       xwindow,
               int          keysym,
               unsigned int keycode,
               int          modmask)
{
  meta_change_keygrab (display, xwindow, TRUE, keysym, keycode, modmask);
}

static void
grab_keys (MetaKeyBinding *bindings,
           int             n_bindings,
           MetaDisplay    *display,
           Window          xwindow,
           gboolean        binding_per_window)
{
  int i;

  g_assert (n_bindings == 0 || bindings != NULL);

  meta_error_trap_push (display);
  
  i = 0;
  while (i < n_bindings)
    {
      if (!!binding_per_window ==
          !!(bindings[i].handler->flags & META_KEY_BINDING_PER_WINDOW) &&
          bindings[i].keycode != 0)
        {
          meta_grab_key (display, xwindow,
                         bindings[i].keysym,
                         bindings[i].keycode,
                         bindings[i].mask);
        }
      
      ++i;
    }

  meta_error_trap_pop (display);
}

static void
ungrab_all_keys (MetaDisplay *display,
                 Window       xwindow)
{
  if (meta_is_debugging ())
    meta_error_trap_push_with_return (display);
  else
    meta_error_trap_push (display);

  XUngrabKey (display->xdisplay, AnyKey, AnyModifier,
              xwindow);

  if (meta_is_debugging ())
    {
      int result;
      
      result = meta_error_trap_pop_with_return (display);
      
      if (result != Success)    
        meta_topic (META_DEBUG_KEYBINDINGS,
                    "Ungrabbing all keys on 0x%lx failed\n", xwindow);
    }
  else
    meta_error_trap_pop (display);
}

LOCAL_SYMBOL void
meta_screen_grab_keys (MetaScreen *screen)
{
  if (screen->all_keys_grabbed)
    return;

  if (screen->keys_grabbed)
    return;

  grab_keys (screen->display->key_bindings,
             screen->display->n_key_bindings,
             screen->display, screen->xroot,
             FALSE);

  screen->keys_grabbed = TRUE;
}

LOCAL_SYMBOL void
meta_screen_ungrab_keys (MetaScreen  *screen)
{
  if (screen->keys_grabbed)
    {
      ungrab_all_keys (screen->display, screen->xroot);
      screen->keys_grabbed = FALSE;
    }
}

LOCAL_SYMBOL void
meta_window_grab_keys (MetaWindow  *window)
{
  if (window->all_keys_grabbed)
    return;

  if (window->type == META_WINDOW_DOCK
      || window->override_redirect)
    {
      if (window->keys_grabbed)
        ungrab_all_keys (window->display, window->xwindow);
      window->keys_grabbed = FALSE;
      return;
    }
  
  if (window->keys_grabbed)
    {
      if (window->frame && !window->grab_on_frame)
        ungrab_all_keys (window->display, window->xwindow);
      else if (window->frame == NULL &&
               window->grab_on_frame)
        ; /* continue to regrab on client window */
      else
        return; /* already all good */
    }
  
  grab_keys (window->display->key_bindings,
             window->display->n_key_bindings,
             window->display,
             window->frame ? window->frame->xwindow : window->xwindow,
             TRUE);

  window->keys_grabbed = TRUE;
  window->grab_on_frame = window->frame != NULL;
}

LOCAL_SYMBOL void
meta_window_ungrab_keys (MetaWindow  *window)
{
  if (window->keys_grabbed)
    {
      if (window->grab_on_frame &&
          window->frame != NULL)
        ungrab_all_keys (window->display,
                         window->frame->xwindow);
      else if (!window->grab_on_frame)
        ungrab_all_keys (window->display,
                         window->xwindow);

      window->keys_grabbed = FALSE;
    }
}

#ifdef WITH_VERBOSE_MODE
static const char*
grab_status_to_string (int status)
{
  switch (status)
    {
    case AlreadyGrabbed:
      return "AlreadyGrabbed";
    case GrabSuccess:
      return "GrabSuccess";
    case GrabNotViewable:
      return "GrabNotViewable";
    case GrabFrozen:
      return "GrabFrozen";
    case GrabInvalidTime:
      return "GrabInvalidTime";
    default:
      return "(unknown)";
    }
}
#endif /* WITH_VERBOSE_MODE */

static gboolean
grab_keyboard (MetaDisplay *display,
               Window       xwindow,
               guint32      timestamp)
{
  int result;
  int grab_status;
  
  /* Grab the keyboard, so we get key releases and all key
   * presses
   */
  meta_error_trap_push_with_return (display);

  grab_status = XGrabKeyboard (display->xdisplay,
                               xwindow, True,
                               GrabModeAsync, GrabModeAsync,
                               timestamp);
  
  if (grab_status != GrabSuccess)
    {
      meta_error_trap_pop_with_return (display);
      meta_topic (META_DEBUG_KEYBINDINGS,
                  "XGrabKeyboard() returned failure status %s time %u\n",
                  grab_status_to_string (grab_status),
                  timestamp);
      return FALSE;
    }
  else
    {
      result = meta_error_trap_pop_with_return (display);
      if (result != Success)
        {
          meta_topic (META_DEBUG_KEYBINDINGS,
                      "XGrabKeyboard() resulted in an error\n");
          return FALSE;
        }
    }
       
  meta_topic (META_DEBUG_KEYBINDINGS, "Grabbed all keys\n");
       
  return TRUE;
}

static void
ungrab_keyboard (MetaDisplay *display, guint32 timestamp)
{
  meta_error_trap_push (display);

  meta_topic (META_DEBUG_KEYBINDINGS,
              "Ungrabbing keyboard with timestamp %u\n",
              timestamp);
  XUngrabKeyboard (display->xdisplay, timestamp);
  meta_error_trap_pop (display);
}

gboolean
meta_screen_grab_all_keys (MetaScreen *screen, guint32 timestamp)
{
  gboolean retval;

  if (screen->all_keys_grabbed)
    return FALSE;
  
  if (screen->keys_grabbed)
    meta_screen_ungrab_keys (screen);

  meta_topic (META_DEBUG_KEYBINDINGS,
              "Grabbing all keys on RootWindow\n");
  retval = grab_keyboard (screen->display, screen->xroot, timestamp);
  if (retval)
    {
      screen->all_keys_grabbed = TRUE;
      g_object_notify (G_OBJECT (screen), "keyboard-grabbed");
    }
  else
    meta_screen_grab_keys (screen);

  return retval;
}

void
meta_screen_ungrab_all_keys (MetaScreen *screen, guint32 timestamp)
{
  if (screen->all_keys_grabbed)
    {
      ungrab_keyboard (screen->display, timestamp);

      screen->all_keys_grabbed = FALSE;
      screen->keys_grabbed = FALSE;

      /* Re-establish our standard bindings */
      meta_screen_grab_keys (screen);
      g_object_notify (G_OBJECT (screen), "keyboard-grabbed");
    }
}

LOCAL_SYMBOL gboolean
meta_window_grab_all_keys (MetaWindow  *window,
                           guint32      timestamp)
{
  Window grabwindow;
  gboolean retval;
  
  if (window->all_keys_grabbed)
    return FALSE;
  
  if (window->keys_grabbed)
    meta_window_ungrab_keys (window);

  /* Make sure the window is focused, otherwise the grab
   * won't do a lot of good.
   */
  meta_topic (META_DEBUG_FOCUS,
              "Focusing %s because we're grabbing all its keys\n",
              window->desc);
  meta_window_focus (window, timestamp);
  
  grabwindow = window->frame ? window->frame->xwindow : window->xwindow;

  meta_topic (META_DEBUG_KEYBINDINGS,
              "Grabbing all keys on window %s\n", window->desc);
  retval = grab_keyboard (window->display, grabwindow, timestamp);
  if (retval)
    {
      window->keys_grabbed = FALSE;
      window->all_keys_grabbed = TRUE;
      window->grab_on_frame = window->frame != NULL;
    }

  return retval;
}

LOCAL_SYMBOL void
meta_window_ungrab_all_keys (MetaWindow *window, guint32 timestamp)
{
  if (window->all_keys_grabbed)
    {
      ungrab_keyboard (window->display, timestamp);

      window->grab_on_frame = FALSE;
      window->all_keys_grabbed = FALSE;
      window->keys_grabbed = FALSE;

      /* Re-establish our standard bindings */
      meta_window_grab_keys (window);
    }
}

static gboolean 
is_modifier (MetaDisplay *display,
             unsigned int keycode)
{
  int i;
  int map_size;
  gboolean retval = FALSE;  

  g_assert (display->modmap);
  
  map_size = 8 * display->modmap->max_keypermod;
  i = 0;
  while (i < map_size)
    {
      if (keycode == display->modmap->modifiermap[i])
        {
          retval = TRUE;
          break;
        }
      ++i;
    }
  
  return retval;
}

/* Indexes:
 * shift = 0
 * lock = 1
 * control = 2
 * mod1 = 3
 * mod2 = 4
 * mod3 = 5
 * mod4 = 6
 * mod5 = 7
 */

static void
invoke_handler (MetaDisplay    *display,
                MetaScreen     *screen,
                MetaKeyHandler *handler,
                MetaWindow     *window,
                XEvent         *event,
                MetaKeyBinding *binding)

{
  if (handler->func)
    (* handler->func) (display, screen,
                       handler->flags & META_KEY_BINDING_PER_WINDOW ?
                           window : NULL,
                       event,
                       binding,
                       handler->user_data);
  else
    (* handler->default_func) (display, screen,
                               handler->flags & META_KEY_BINDING_PER_WINDOW ?
                                   window: NULL,
                               event,
                               binding,
                               NULL);
}

static void
invoke_handler_by_name (MetaDisplay    *display,
                        MetaScreen     *screen,
                        const char     *handler_name,
                        MetaWindow     *window,
                        XEvent         *event)
{
  MetaKeyHandler *handler;

  handler = HANDLER (handler_name);
  if (handler)
    invoke_handler (display, screen, handler, window, event, NULL);
}


static gboolean
modifier_only_keysym (KeySym keysym)
{
  return keysym == XK_Super_L ||
         keysym == XK_Super_R ||
         keysym == XK_Control_L ||
         keysym == XK_Control_R ||
         keysym == XK_Alt_L ||
         keysym == XK_Alt_R ||
         keysym == XK_Shift_L ||
         keysym == XK_Shift_R;
}

static void
strip_self_mod (KeySym keysym, unsigned long *mask)
{
  unsigned long mod = 0;

  switch (keysym)
    {
      case XK_Super_L:
      case XK_Super_R:
        mod = GDK_MOD4_MASK;
        break;
      case XK_Control_L:
      case XK_Control_R:
        mod = GDK_CONTROL_MASK;
        break;
      case XK_Alt_L:
      case XK_Alt_R:
        mod = GDK_MOD1_MASK;
        break;
      case XK_Shift_L:
      case XK_Shift_R:
        mod = GDK_SHIFT_MASK;
        break;
      default:
        mod = 0;
        break;
    }
  
  *mask = *mask & ~mod;
}

static gboolean
is_modifier_only_kb (MetaDisplay *display, XEvent *event, KeySym keysym)
{
    unsigned long mask;

    mask = (event->xkey.state & 0xff & ~(display->ignored_modifier_mask));
    strip_self_mod (keysym, &mask);

    return mask == 0 && modifier_only_keysym (keysym);
}

/* now called from only one place, may be worth merging */
static gboolean
process_event (MetaKeyBinding       *bindings,
               int                   n_bindings,
               MetaDisplay          *display,
               MetaScreen           *screen,
               MetaWindow           *window,
               XEvent               *event,
               KeySym                keysym,
               gboolean              on_window,
               gboolean              allow_release)
{
  int i;
  unsigned long mask;

  /* we used to have release-based bindings but no longer. */
  if (event->type == KeyRelease && !allow_release)
    return FALSE;

  mask = event->xkey.state & 0xff & ~(display->ignored_modifier_mask);

  if (allow_release)
    {
      strip_self_mod (keysym, &mask);
    }

  /*
   * TODO: This would be better done with a hash table;
   * it doesn't suit to use O(n) for such a common operation.
   */
  for (i=0; i<n_bindings; i++)
    {
      MetaKeyHandler *handler = bindings[i].handler;

      /* Custom keybindings are from Cinnamon, and never need a window */
      if (!on_window && handler->flags & META_KEY_BINDING_PER_WINDOW && handler->action < META_KEYBINDING_ACTION_CUSTOM)
        continue;

      if ((event->type != KeyPress && !allow_release) ||
          bindings[i].keycode != event->xkey.keycode ||
          mask != bindings[i].mask)
        continue;

      /*
       * window must be non-NULL for on_window to be true,
       * and so also window must be non-NULL if we get here and
       * this is a META_KEY_BINDING_PER_WINDOW binding.
       */

      meta_topic (META_DEBUG_KEYBINDINGS,
                  "Binding keycode 0x%x mask 0x%x matches event 0x%x state 0x%x\n",
                  bindings[i].keycode, bindings[i].mask,
                  event->xkey.keycode, event->xkey.state);

      if (handler == NULL)
        meta_bug ("Binding %s has no handler\n", bindings[i].name);
      else
        meta_topic (META_DEBUG_KEYBINDINGS,
                    "Running handler for %s\n",
                    bindings[i].name);

      /* Global keybindings count as a let-the-terminal-lose-focus
       * due to new window mapping until the user starts
       * interacting with the terminal again.
       */
      display->allow_terminal_deactivation = TRUE;

      invoke_handler (display, screen, handler, window, event, &bindings[i]);

      return TRUE;
    }

  meta_topic (META_DEBUG_KEYBINDINGS,
              "No handler found for this event in this binding table\n");
  return FALSE;
}

static gboolean
process_modifier_key (MetaDisplay *display,
                     MetaScreen *screen,
                     MetaWindow *window,
                     XEvent *event,
                     KeySym keysym,
                     gboolean have_window,
                     gboolean *allow_key_up)
{

  if (event->xkey.type == KeyPress)
    {
        /* check if we're just Pressing a captured modifier at this time */
      if (is_modifier_only_kb (display, event, keysym))
        {
          /* remember this state, because we'll need to know it for subsequent events */
          modifier_only_is_down = TRUE;
          /* We keep the keyboard frozen - this allows us to use ReplayKeyboard
           * on the next event if it's not the release of this modifier key */
          XAllowEvents (display->xdisplay, SyncKeyboard, event->xkey.time);
          return TRUE;
        }
      else
        {
          /* If it's a press, with a mod+key combination, we try our own keybindings
           * first, then pass it to the current window if we don't have anything for it.
           * Also, negate modifier_only_is_down.  At this point we won't want a modifier
           * release to trigger a binding, since when it happens now, it will only be the
           * user releasing the keys after a combination binding has been used
           */
          modifier_only_is_down = FALSE;
            /* Try our keybindings */
          if (process_event (display->key_bindings,
                             display->n_key_bindings,
                             display, screen, window, event, keysym,
                             have_window, FALSE))
            {
              /* we had a binding, we're done */
              XAllowEvents (display->xdisplay, AsyncKeyboard, event->xkey.time);
              return TRUE;
            }
          else
            {
              /* if we have nothing, let focused window handle it */
              XAllowEvents (display->xdisplay, ReplayKeyboard, event->xkey.time);
              return FALSE;
            }
        }
    }
  else
    {
      /* We only care about key releases for modifier-only bindings -
       * and only when that release directly follows a press.  When this happens,
       * negate modifier_only_is_down, and allow the binding handler to accept
       * a key release.
       */
      if (is_modifier_only_kb (display, event, keysym))
        {
          if (modifier_only_is_down)
            *allow_key_up = TRUE;
          modifier_only_is_down = FALSE;
        }
    }

  return FALSE;
}

/* Handle a key event. May be called recursively: some key events cause
 * grabs to be ended and then need to be processed again in their own
 * right. This cannot cause infinite recursion because we never call
 * ourselves when there wasn't a grab, and we always clear the grab
 * first; the invariant is enforced using an assertion. See #112560.
 *
 * The return value is whether we handled the key event.
 *
 * FIXME: We need to prove there are no race conditions here.
 * FIXME: Does it correctly handle alt-Tab being followed by another
 * grabbing keypress without letting go of alt?
 * FIXME: An iterative solution would probably be simpler to understand
 * (and help us solve the other fixmes).
 */
LOCAL_SYMBOL gboolean
meta_display_process_key_event (MetaDisplay *display,
                                MetaWindow  *window,
                                XEvent      *event)
{
  KeySym keysym;
  gboolean keep_grab;
  gboolean all_keys_grabbed;
  gboolean handled;
  gboolean allow_key_up = FALSE;
  const char *str;
  MetaScreen *screen;

  if (all_bindings_disabled)
    {
      /* In this mode, we try to pretend we don't have grabs, so we
       * immediately replay events and drop the grab. (This still
       * messes up global passive grabs from other clients.) The
       * FALSE return here is a little suspect, but we don't really
       * know if we'll see the event again or not, and it's pretty
       * poorly defined how this mode is supposed to interact with
       * plugins.
       */
      XAllowEvents (display->xdisplay, ReplayKeyboard, event->xkey.time);
      return FALSE;
    }

  /* if key event was on root window, we have a shortcut */
  screen = meta_display_screen_for_root (display, event->xkey.window);
  
  /* else round-trip to server */
  if (screen == NULL)
    screen = meta_display_screen_for_xwindow (display,
                                              event->xany.window);

  if (screen == NULL)
    return FALSE; /* event window is destroyed */
  
  /* ignore key events on popup menus and such. */
  if (meta_ui_window_is_widget (screen->ui, event->xany.window))
    return FALSE;
  
  /* window may be NULL */
  
  keysym = XkbKeycodeToKeysym (display->xdisplay, event->xkey.keycode, 0, 0);

  str = XKeysymToString (keysym);
  
  /* was topic */
  meta_topic (META_DEBUG_KEYBINDINGS,
              "Processing key %s event, keysym: %s state: 0x%x window: %s\n",
              event->type == KeyPress ? "press" : "release",
              str ? str : "none", event->xkey.state,
              window ? window->desc : "(no window)");

  all_keys_grabbed = window ? window->all_keys_grabbed : screen->all_keys_grabbed;

  if (!all_keys_grabbed)
    {
      handled = process_modifier_key (display, screen, window, event, keysym, (!all_keys_grabbed && window), &allow_key_up);
      if (handled)
        return TRUE;
    }

  XAllowEvents (display->xdisplay, AsyncKeyboard, event->xkey.time);

  keep_grab = TRUE;
  if (all_keys_grabbed)
    {
      if (display->grab_op == META_GRAB_OP_NONE)
        return TRUE;
      /* If we get here we have a global grab, because
        * we're in some special keyboard mode such as window move
        * mode.
        */
      if (window ? (window == display->grab_window) :
          (screen == display->grab_screen))
        {
          switch (display->grab_op)
            {
            case META_GRAB_OP_MOVING:
            case META_GRAB_OP_RESIZING_SE:
            case META_GRAB_OP_RESIZING_S:      
            case META_GRAB_OP_RESIZING_SW:      
            case META_GRAB_OP_RESIZING_N:
            case META_GRAB_OP_RESIZING_NE:
            case META_GRAB_OP_RESIZING_NW:
            case META_GRAB_OP_RESIZING_W:
            case META_GRAB_OP_RESIZING_E:
              meta_topic (META_DEBUG_KEYBINDINGS,
                          "Processing event for mouse-only move/resize\n");
              g_assert (window != NULL);
              keep_grab = process_mouse_move_resize_grab (display, screen,
                                                          window, event, keysym);
              break;
 
            case META_GRAB_OP_KEYBOARD_MOVING:
              meta_topic (META_DEBUG_KEYBINDINGS,
                          "Processing event for keyboard move\n");
              g_assert (window != NULL);
              keep_grab = process_keyboard_move_grab (display, screen,
                                                      window, event, keysym);
              break;
              
            case META_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN:
            case META_GRAB_OP_KEYBOARD_RESIZING_S:
            case META_GRAB_OP_KEYBOARD_RESIZING_N:
            case META_GRAB_OP_KEYBOARD_RESIZING_W:
            case META_GRAB_OP_KEYBOARD_RESIZING_E:
            case META_GRAB_OP_KEYBOARD_RESIZING_SE:
            case META_GRAB_OP_KEYBOARD_RESIZING_NE:
            case META_GRAB_OP_KEYBOARD_RESIZING_SW:
            case META_GRAB_OP_KEYBOARD_RESIZING_NW:          
              meta_topic (META_DEBUG_KEYBINDINGS,
                          "Processing event for keyboard resize\n");
              g_assert (window != NULL);
              keep_grab = process_keyboard_resize_grab (display, screen,
                                                        window, event, keysym);
              break;
 
            default:
              break;
            }
        }
      if (!keep_grab)
        {
          meta_topic (META_DEBUG_KEYBINDINGS,
                      "Ending grab op %u on key event sym %s\n",
                      display->grab_op, XKeysymToString (keysym));
          meta_display_end_grab_op (display, event->xkey.time);
        }

      return TRUE;
    }
  
  /* Do the normal keybindings */
  return process_event (display->key_bindings,
                        display->n_key_bindings,
                        display, screen, window, event, keysym,
                        !all_keys_grabbed && window, allow_key_up);
}

static gboolean
process_mouse_move_resize_grab (MetaDisplay *display,
                                MetaScreen  *screen,
                                MetaWindow  *window,
                                XEvent      *event,
                                KeySym       keysym)
{
  /* don't care about releases, but eat them, don't end grab */
  if (event->type == KeyRelease)
    return TRUE;

  if (keysym == XK_Escape)
    {
      /* Hide the tiling preview if necessary */
      meta_screen_tile_preview_hide (screen);
      meta_screen_tile_hud_hide (screen);

      /* Restore the original tile mode */
      window->tile_mode = display->grab_tile_mode;
      window->tile_monitor_number = display->grab_tile_monitor_number;

      /* End move or resize and restore to original state.  If the
       * window was a maximized window that had been "shaken loose" we
       * need to remaximize it.  In normal cases, we need to do a
       * moveresize now to get the position back to the original.
       */
      if (window->shaken_loose)
        meta_window_maximize (window,
                              META_MAXIMIZE_HORIZONTAL |
                              META_MAXIMIZE_VERTICAL);
      else if (window->tile_mode != META_TILE_NONE) {
        window->custom_snap_size = FALSE;
        meta_window_real_tile (window, FALSE);
      } else
        meta_window_move_resize (display->grab_window,
                                 TRUE,
                                 display->grab_initial_window_pos.x,
                                 display->grab_initial_window_pos.y,
                                 display->grab_initial_window_pos.width,
                                 display->grab_initial_window_pos.height);

      /* End grab */
      return FALSE;
    }

  return TRUE;
}

static gboolean
process_keyboard_move_grab (MetaDisplay *display,
                            MetaScreen  *screen,
                            MetaWindow  *window,
                            XEvent      *event,
                            KeySym       keysym)
{
  gboolean handled;
  int x, y;
  int incr;
  gboolean smart_snap;
  
  handled = FALSE;

  /* don't care about releases, but eat them, don't end grab */
  if (event->type == KeyRelease)
    return TRUE;

  /* don't end grab on modifier key presses */
  if (is_modifier (display, event->xkey.keycode))
    return TRUE;

  meta_window_get_position (window, &x, &y);

  smart_snap = (event->xkey.state & ShiftMask) != 0;
  
#define SMALL_INCREMENT 1
#define NORMAL_INCREMENT 10

  if (smart_snap)
    incr = 1;
  else if (event->xkey.state & ControlMask)
    incr = SMALL_INCREMENT;
  else
    incr = NORMAL_INCREMENT;

  if (keysym == XK_Escape)
    {
      /* End move and restore to original state.  If the window was a
       * maximized window that had been "shaken loose" we need to
       * remaximize it.  In normal cases, we need to do a moveresize
       * now to get the position back to the original.
       */
      if (window->shaken_loose)
        meta_window_maximize (window,
                              META_MAXIMIZE_HORIZONTAL |
                              META_MAXIMIZE_VERTICAL);
      else
        meta_window_move_resize (display->grab_window,
                                 TRUE,
                                 display->grab_initial_window_pos.x,
                                 display->grab_initial_window_pos.y,
                                 display->grab_initial_window_pos.width,
                                 display->grab_initial_window_pos.height);
    }
  
  /* When moving by increments, we still snap to edges if the move
   * to the edge is smaller than the increment. This is because
   * Shift + arrow to snap is sort of a hidden feature. This way
   * people using just arrows shouldn't get too frustrated.
   */
  switch (keysym)
    {
    case XK_KP_Home:
    case XK_KP_Prior:
    case XK_Up:
    case XK_KP_Up:
      y -= incr;
      handled = TRUE;
      break;
    case XK_KP_End:
    case XK_KP_Next:
    case XK_Down:
    case XK_KP_Down:
      y += incr;
      handled = TRUE;
      break;
    }
  
  switch (keysym)
    {
    case XK_KP_Home:
    case XK_KP_End:
    case XK_Left:
    case XK_KP_Left:
      x -= incr;
      handled = TRUE;
      break;
    case XK_KP_Prior:
    case XK_KP_Next:
    case XK_Right:
    case XK_KP_Right:
      x += incr;
      handled = TRUE;
      break;
    }

  if (handled)
    {
      MetaRectangle old_rect;
      meta_topic (META_DEBUG_KEYBINDINGS,
                  "Computed new window location %d,%d due to keypress\n",
                  x, y);

      meta_window_get_client_root_coords (window, &old_rect);

      meta_window_edge_resistance_for_move (window, 
                                            old_rect.x,
                                            old_rect.y,
                                            &x,
                                            &y,
                                            NULL,
                                            smart_snap,
                                            TRUE);

      meta_window_move (window, TRUE, x, y);
      meta_window_update_keyboard_move (window);
    }

  return handled;
}

static gboolean
process_keyboard_resize_grab_op_change (MetaDisplay *display,
                                        MetaScreen  *screen,
                                        MetaWindow  *window,
                                        XEvent      *event,
                                        KeySym       keysym)
{
  gboolean handled;

  handled = FALSE;
  switch (display->grab_op)
    {
    case META_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN:
      switch (keysym)
        {
        case XK_Up:
        case XK_KP_Up:
          display->grab_op = META_GRAB_OP_KEYBOARD_RESIZING_N;          
          handled = TRUE;
          break;
        case XK_Down:
        case XK_KP_Down:
          display->grab_op = META_GRAB_OP_KEYBOARD_RESIZING_S;
          handled = TRUE;
          break;
        case XK_Left:
        case XK_KP_Left:
          display->grab_op = META_GRAB_OP_KEYBOARD_RESIZING_W;
          handled = TRUE;
          break;
        case XK_Right:
        case XK_KP_Right:
          display->grab_op = META_GRAB_OP_KEYBOARD_RESIZING_E;
          handled = TRUE;
          break;
        }
      break;
      
    case META_GRAB_OP_KEYBOARD_RESIZING_S:
      switch (keysym)
        {
        case XK_Left:
        case XK_KP_Left:
          display->grab_op = META_GRAB_OP_KEYBOARD_RESIZING_W;
          handled = TRUE;
          break;
        case XK_Right:
        case XK_KP_Right:
          display->grab_op = META_GRAB_OP_KEYBOARD_RESIZING_E;
          handled = TRUE;
          break;
        }
      break;

    case META_GRAB_OP_KEYBOARD_RESIZING_N:
      switch (keysym)
        {
        case XK_Left:
        case XK_KP_Left:
          display->grab_op = META_GRAB_OP_KEYBOARD_RESIZING_W;
          handled = TRUE;
          break;
        case XK_Right:
        case XK_KP_Right:
          display->grab_op = META_GRAB_OP_KEYBOARD_RESIZING_E;
          handled = TRUE;
          break;
        }
      break;
      
    case META_GRAB_OP_KEYBOARD_RESIZING_W:
      switch (keysym)
        {
        case XK_Up:
        case XK_KP_Up:
          display->grab_op = META_GRAB_OP_KEYBOARD_RESIZING_N;          
          handled = TRUE;
          break;
        case XK_Down:
        case XK_KP_Down:
          display->grab_op = META_GRAB_OP_KEYBOARD_RESIZING_S;
          handled = TRUE;
          break;
        }
      break;

    case META_GRAB_OP_KEYBOARD_RESIZING_E:
      switch (keysym)
        {
        case XK_Up:
        case XK_KP_Up:
          display->grab_op = META_GRAB_OP_KEYBOARD_RESIZING_N; 
          handled = TRUE;
          break;
        case XK_Down:
        case XK_KP_Down:
          display->grab_op = META_GRAB_OP_KEYBOARD_RESIZING_S;
          handled = TRUE;
          break;
        }
      break;
      
    case META_GRAB_OP_KEYBOARD_RESIZING_SE:
    case META_GRAB_OP_KEYBOARD_RESIZING_NE:
    case META_GRAB_OP_KEYBOARD_RESIZING_SW:
    case META_GRAB_OP_KEYBOARD_RESIZING_NW:
      break;

    default:
      g_assert_not_reached ();
      break;
    }

  if (handled)
    {
      meta_window_update_keyboard_resize (window, TRUE);
      return TRUE; 
    }

  return FALSE;
}

static gboolean
process_keyboard_resize_grab (MetaDisplay *display,
                              MetaScreen  *screen,
                              MetaWindow  *window,
                              XEvent      *event,
                              KeySym       keysym)
{
  gboolean handled;
  int height_inc;
  int width_inc;
  int width, height;
  gboolean smart_snap;
  int gravity;
  
  handled = FALSE;

  /* don't care about releases, but eat them, don't end grab */
  if (event->type == KeyRelease)
    return TRUE;

  /* don't end grab on modifier key presses */
  if (is_modifier (display, event->xkey.keycode))
    return TRUE;

  if (keysym == XK_Escape)
    {
      /* End resize and restore to original state. */
      meta_window_move_resize (display->grab_window,
                               TRUE,
                               display->grab_initial_window_pos.x,
                               display->grab_initial_window_pos.y,
                               display->grab_initial_window_pos.width,
                               display->grab_initial_window_pos.height);

      return FALSE;
    }

  if (process_keyboard_resize_grab_op_change (display, screen, window, 
                                              event, keysym))
    return TRUE;

  width = window->rect.width;
  height = window->rect.height;

  gravity = meta_resize_gravity_from_grab_op (display->grab_op);

  smart_snap = (event->xkey.state & ShiftMask) != 0;
  
#define SMALL_INCREMENT 1
#define NORMAL_INCREMENT 10

  if (smart_snap)
    {
      height_inc = 1;
      width_inc = 1;
    }
  else if (event->xkey.state & ControlMask)
    {
      width_inc = SMALL_INCREMENT;
      height_inc = SMALL_INCREMENT;
    }  
  else
    {
      width_inc = NORMAL_INCREMENT;
      height_inc = NORMAL_INCREMENT;
    }

  /* If this is a resize increment window, make the amount we resize
   * the window by match that amount (well, unless snap resizing...)
   */
  if (window->size_hints.width_inc > 1)
    width_inc = window->size_hints.width_inc;
  if (window->size_hints.height_inc > 1)
    height_inc = window->size_hints.height_inc;
  
  switch (keysym)
    {
    case XK_Up:
    case XK_KP_Up:
      switch (gravity)
        {
        case NorthGravity:
        case NorthWestGravity:
        case NorthEastGravity:
          /* Move bottom edge up */
          height -= height_inc;
          break;

        case SouthGravity:
        case SouthWestGravity:
        case SouthEastGravity:
          /* Move top edge up */
          height += height_inc;
          break;

        case EastGravity:
        case WestGravity:
        case CenterGravity:
          g_assert_not_reached ();
          break;
        }
      
      handled = TRUE;
      break;

    case XK_Down:
    case XK_KP_Down:
      switch (gravity)
        {
        case NorthGravity:
        case NorthWestGravity:
        case NorthEastGravity:
          /* Move bottom edge down */
          height += height_inc;
          break;

        case SouthGravity:
        case SouthWestGravity:
        case SouthEastGravity:
          /* Move top edge down */
          height -= height_inc;
          break;

        case EastGravity:
        case WestGravity:
        case CenterGravity:
          g_assert_not_reached ();
          break;
        }
      
      handled = TRUE;
      break;
      
    case XK_Left:
    case XK_KP_Left:
      switch (gravity)
        {
        case EastGravity:
        case SouthEastGravity:
        case NorthEastGravity:
          /* Move left edge left */
          width += width_inc;
          break;

        case WestGravity:
        case SouthWestGravity:
        case NorthWestGravity:
          /* Move right edge left */
          width -= width_inc;
          break;

        case NorthGravity:
        case SouthGravity:
        case CenterGravity:
          g_assert_not_reached ();
          break;
        }
      
      handled = TRUE;
      break;
      
    case XK_Right:
    case XK_KP_Right:
      switch (gravity)
        {
        case EastGravity:
        case SouthEastGravity:
        case NorthEastGravity:
          /* Move left edge right */
          width -= width_inc;
          break;

        case WestGravity:
        case SouthWestGravity:
        case NorthWestGravity:
          /* Move right edge right */
          width += width_inc;
          break;

        case NorthGravity:
        case SouthGravity:
        case CenterGravity:
          g_assert_not_reached ();
          break;
        }
      
      handled = TRUE;
      break;
          
    default:
      break;
    }

  /* fixup hack (just paranoia, not sure it's required) */
  if (height < 1)
    height = 1;
  if (width < 1)
    width = 1;
  
  if (handled)
    {
      MetaRectangle old_rect;
      meta_topic (META_DEBUG_KEYBINDINGS,
                  "Computed new window size due to keypress: "
                  "%dx%d, gravity %s\n",
                  width, height, meta_gravity_to_string (gravity));
      
      old_rect = window->rect;  /* Don't actually care about x,y */

      /* Do any edge resistance/snapping */
      meta_window_edge_resistance_for_resize (window,
                                              old_rect.width,
                                              old_rect.height,
                                              &width,
                                              &height,
                                              gravity,
                                              NULL,
                                              smart_snap,
                                              TRUE);

      /* We don't need to update unless the specified width and height
       * are actually different from what we had before.
       */
      if (window->rect.width != width || window->rect.height != height)
        meta_window_resize_with_gravity (window, 
                                         TRUE,
                                         width,
                                         height,
                                         gravity);

      meta_window_update_keyboard_resize (window, FALSE);
    }

  return handled;
}

static void
handle_switch_to_workspace (MetaDisplay    *display,
                           MetaScreen     *screen,
                           MetaWindow     *event_window,
                           XEvent         *event,
                           MetaKeyBinding *binding,
                           gpointer        dummy)
{
  gint which = binding->handler->data;
  MetaWorkspace *workspace;
  
  workspace = meta_screen_get_workspace_by_index (screen, which);
  
  if (workspace)
      meta_workspace_activate (workspace, event->xkey.time);
}

static void
handle_maximize_vertically (MetaDisplay    *display,
                      MetaScreen     *screen,
                      MetaWindow     *window,
                      XEvent         *event,
                      MetaKeyBinding *binding,
                      gpointer        dummy)
{
  if (window->has_resize_func)
    {
      if (window->maximized_vertically)
        meta_window_unmaximize (window, META_MAXIMIZE_VERTICAL);
      else
        meta_window_maximize (window, META_MAXIMIZE_VERTICAL);
    }
}

static void
handle_maximize_horizontally (MetaDisplay    *display,
                       MetaScreen     *screen,
                       MetaWindow     *window,
                       XEvent         *event,
                       MetaKeyBinding *binding,
                       gpointer        dummy)
{
  if (window->has_resize_func)
    {
      if (window->maximized_horizontally)
        meta_window_unmaximize (window, META_MAXIMIZE_HORIZONTAL);
      else
        meta_window_maximize (window, META_MAXIMIZE_HORIZONTAL);
    }
}

static void
handle_opacity (MetaDisplay    *display,
                MetaScreen     *screen,
                MetaWindow     *window,
                XEvent         *event,
                MetaKeyBinding *binding,
                gpointer        dummy)
{
    MetaKeyBindingAction action = meta_prefs_get_keybinding_action (binding->name);

    meta_window_adjust_opacity (window, action == META_KEYBINDING_ACTION_INCREASE_OPACITY);
}

static void
handle_move_to (MetaDisplay    *display,
                MetaScreen     *screen,
                MetaWindow     *window,
                XEvent         *event,
                MetaKeyBinding *binding,
                gpointer        dummy)
{

  guint direction = binding->handler->data;
  MetaRectangle work_area;
  MetaRectangle outer;
  int new_x, new_y;

  meta_window_get_work_area_all_monitors (window, &work_area);
  meta_window_get_outer_rect (window, &outer);

  if (direction & META_MOVE_TO_XCHANGE_FLAG) {
    new_x = work_area.x + (direction & META_MOVE_TO_RIGHT_FLAG ?
            work_area.width - outer.width :
            0);
  } else {
    new_x = outer.x;
  }

  if (direction & META_MOVE_TO_YCHANGE_FLAG) {
    new_y = work_area.y + (direction & META_MOVE_TO_BOTTOM_FLAG ?
            work_area.height - outer.height :
            0);
  } else {
    new_y = outer.y;
  }

  meta_window_move_frame (window,
                          TRUE,
                          new_x,
                          new_y);
}

static void
handle_move_to_center  (MetaDisplay    *display,
                        MetaScreen     *screen,
                        MetaWindow     *window,
                        XEvent         *event,
                        MetaKeyBinding *binding,
                        gpointer        dummy)
{
  MetaRectangle work_area;
  MetaRectangle outer;
  int orig_x, orig_y;
  int frame_width, frame_height;

  meta_window_get_work_area_all_monitors (window, &work_area);
  meta_window_get_outer_rect (window, &outer);
  meta_window_get_position (window, &orig_x, &orig_y);

  frame_width = (window->frame ? window->frame->child_x : 0);
  frame_height = (window->frame ? window->frame->child_y : 0);

  meta_window_move_resize (window,
          TRUE,
          work_area.x + (work_area.width +frame_width -outer.width )/2,
          work_area.y + (work_area.height+frame_height-outer.height)/2,
          window->rect.width,
          window->rect.height);
}

static void
handle_show_desktop (MetaDisplay    *display,
                       MetaScreen     *screen,
                       MetaWindow     *window,
                       XEvent         *event,
                       MetaKeyBinding *binding,
                       gpointer        dummy)
{
  meta_screen_toggle_desktop (screen, event->xkey.time);
}

static void
handle_toggle_recording (MetaDisplay    *display,
                         MetaScreen     *screen,
                         MetaWindow     *window,
                         XEvent         *event,
                         MetaKeyBinding *binding,
                         gpointer        dummy)
{
  g_signal_emit_by_name (screen, "toggle-recording");
}

static void
handle_activate_window_menu (MetaDisplay    *display,
                      MetaScreen     *screen,
                      MetaWindow     *event_window,
                      XEvent         *event,
                      MetaKeyBinding *binding,
                      gpointer        dummy)
{
  if (display->focus_window)
    {
      int x, y;

      meta_window_get_position (display->focus_window,
                                &x, &y);
      
      if (meta_ui_get_direction() == META_UI_DIRECTION_RTL)
	  x += display->focus_window->rect.width;

      meta_window_show_menu (display->focus_window,
                             x, y,
                             0,
                             event->xkey.time);
    }
}

static void
handle_toggle_fullscreen  (MetaDisplay    *display,
                           MetaScreen     *screen,
                           MetaWindow     *window,
                           XEvent         *event,
                           MetaKeyBinding *binding,
                           gpointer        dummy)
{
  if (window->fullscreen)
    meta_window_unmake_fullscreen (window);
  else if (window->has_fullscreen_func)
    meta_window_make_fullscreen (window);
}

static void
handle_toggle_above       (MetaDisplay    *display,
                           MetaScreen     *screen,
                           MetaWindow     *window,
                           XEvent         *event,
                           MetaKeyBinding *binding,
                           gpointer        dummy)
{
  if (window->wm_state_above)
    meta_window_unmake_above (window);
  else
    meta_window_make_above (window);
}

static MetaTileMode
get_new_tile_mode (MetaTileMode direction,
                   MetaTileMode current)
{
    MetaTileMode ret = META_TILE_NONE;
    switch (current) {
        case META_TILE_NONE:
            ret = direction;
            break;
        case META_TILE_MAXIMIZE:
            if (direction == META_TILE_LEFT)
                ret = META_TILE_LEFT;
            else if (direction == META_TILE_RIGHT)
                ret = META_TILE_RIGHT;
            else if (direction == META_TILE_TOP)
                ret = direction;
            else if (direction == META_TILE_BOTTOM)
                ret = META_TILE_TOP;
            else
                ret = META_TILE_NONE;
            break;
        case META_TILE_LEFT:
            if (direction == META_TILE_LEFT)
                ret = META_TILE_LEFT;
            else if (direction == META_TILE_RIGHT)
                ret = META_TILE_NONE;
            else if (direction == META_TILE_TOP)
                ret = META_TILE_ULC;
            else
                ret = META_TILE_LLC;
            break;
        case META_TILE_RIGHT:
            if (direction == META_TILE_LEFT)
                ret = META_TILE_NONE;
            else if (direction == META_TILE_RIGHT)
                ret = META_TILE_RIGHT;
            else if (direction == META_TILE_TOP)
                ret = META_TILE_URC;
            else
                ret = META_TILE_LRC;
            break;
        case META_TILE_TOP:
            if (direction == META_TILE_LEFT)
                ret = META_TILE_ULC;
            else if (direction == META_TILE_RIGHT)
                ret = META_TILE_URC;
            else if (direction == META_TILE_TOP)
                ret = META_TILE_MAXIMIZE;
            else
                ret = META_TILE_NONE;
            break;
        case META_TILE_BOTTOM:
            if (direction == META_TILE_LEFT)
                ret = META_TILE_LLC;
            else if (direction == META_TILE_RIGHT)
                ret = META_TILE_LRC;
            else if (direction == META_TILE_TOP)
                ret = META_TILE_NONE;
            else
                ret = META_TILE_BOTTOM;
            break;
        case META_TILE_ULC:
            if (direction == META_TILE_LEFT)
                ret = META_TILE_ULC;
            else if (direction == META_TILE_RIGHT)
                ret = META_TILE_TOP;
            else if (direction == META_TILE_TOP)
                ret = META_TILE_ULC;
            else
                ret = META_TILE_LEFT;
            break;
        case META_TILE_LLC:
            if (direction == META_TILE_LEFT)
                ret = META_TILE_LLC;
            else if (direction == META_TILE_RIGHT)
                ret = META_TILE_BOTTOM;
            else if (direction == META_TILE_TOP)
                ret = META_TILE_LEFT;
            else
                ret = META_TILE_LLC;
            break;
        case META_TILE_URC:
            if (direction == META_TILE_LEFT)
                ret = META_TILE_TOP;
            else if (direction == META_TILE_RIGHT)
                ret = META_TILE_URC;
            else if (direction == META_TILE_TOP)
                ret = META_TILE_URC;
            else
                ret = META_TILE_RIGHT;
            break;
        case META_TILE_LRC:
            if (direction == META_TILE_LEFT)
                ret = META_TILE_BOTTOM;
            else if (direction == META_TILE_RIGHT)
                ret = META_TILE_LRC;
            else if (direction == META_TILE_TOP)
                ret = META_TILE_RIGHT;
            else
                ret = META_TILE_LRC;
            break;
        default:
            ret = current;
            break;
    }
    return ret;
}

static void
handle_tile_action (MetaDisplay    *display,
                     MetaScreen     *screen,
                     MetaWindow     *window,
                     XEvent         *event,
                     MetaKeyBinding *binding,
                     gpointer        dummy)
{
  MetaTileMode mode = binding->handler->data;
  MetaKeyBindingAction action = meta_prefs_get_keybinding_action (binding->name);
  gboolean snap = action == META_KEYBINDING_ACTION_PUSH_SNAP_LEFT ||
                  action == META_KEYBINDING_ACTION_PUSH_SNAP_RIGHT ||
                  action == META_KEYBINDING_ACTION_PUSH_SNAP_UP ||
                  action == META_KEYBINDING_ACTION_PUSH_SNAP_DOWN;

  MetaTileMode new_mode = get_new_tile_mode (mode, window->tile_mode);
  if (new_mode == window->tile_mode)
    return;

  if (!meta_window_can_tile (window, new_mode))
    return;

  meta_window_tile (window, new_mode, snap);
}

static void
handle_toggle_maximized    (MetaDisplay    *display,
                           MetaScreen     *screen,
                           MetaWindow     *window,
                           XEvent         *event,
                           MetaKeyBinding *binding,
                           gpointer        dummy)
{
  if (META_WINDOW_MAXIMIZED (window))
    meta_window_unmaximize (window,
                            META_MAXIMIZE_HORIZONTAL |
                            META_MAXIMIZE_VERTICAL);
  else if (window->has_maximize_func)
    meta_window_maximize (window,
                          META_MAXIMIZE_HORIZONTAL |
                          META_MAXIMIZE_VERTICAL);
}

static void
handle_maximize           (MetaDisplay    *display,
                           MetaScreen     *screen,
                           MetaWindow     *window,
                           XEvent         *event,
                           MetaKeyBinding *binding,
                           gpointer        dummy)
{
  if (window->has_maximize_func)
    meta_window_maximize (window,
                          META_MAXIMIZE_HORIZONTAL |
                          META_MAXIMIZE_VERTICAL);
}

static void
handle_unmaximize         (MetaDisplay    *display,
                           MetaScreen     *screen,
                           MetaWindow     *window,
                           XEvent         *event,
                           MetaKeyBinding *binding,
                           gpointer        dummy)
{
  if (window->maximized_vertically || window->maximized_horizontally)
    meta_window_unmaximize (window,
                            META_MAXIMIZE_HORIZONTAL |
                            META_MAXIMIZE_VERTICAL);
}

static void
handle_toggle_shaded      (MetaDisplay    *display,
                           MetaScreen     *screen,
                           MetaWindow     *window,
                           XEvent         *event,
                           MetaKeyBinding *binding,
                           gpointer        dummy)
{
  if (window->shaded)
    meta_window_unshade (window, event->xkey.time);
  else if (window->has_shade_func)
    meta_window_shade (window, event->xkey.time);
}

static void
handle_close              (MetaDisplay    *display,
                           MetaScreen     *screen,
                           MetaWindow     *window,
                           XEvent         *event,
                           MetaKeyBinding *binding,
                           gpointer        dummy)
{
  if (window->has_close_func)
    meta_window_delete (window, event->xkey.time);
}

static void
handle_minimize        (MetaDisplay    *display,
                        MetaScreen     *screen,
                        MetaWindow     *window,
                        XEvent         *event,
                        MetaKeyBinding *binding,
                        gpointer        dummy)
{
  if (window->has_minimize_func)
    meta_window_minimize (window);
}

static void
handle_begin_move         (MetaDisplay    *display,
                           MetaScreen     *screen,
                           MetaWindow     *window,
                           XEvent         *event,
                           MetaKeyBinding *binding,
                           gpointer        dummy)
{
  if (window->has_move_func)
    {
      meta_window_begin_grab_op (window,
                                 META_GRAB_OP_KEYBOARD_MOVING,
                                 FALSE,
                                 event->xkey.time);
    }
}

static void
handle_begin_resize       (MetaDisplay    *display,
                           MetaScreen     *screen,
                           MetaWindow     *window,
                           XEvent         *event,
                           MetaKeyBinding *binding,
                           gpointer        dummy)
{
  if (window->has_resize_func)
    {
      meta_window_begin_grab_op (window,
                                 META_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN,
                                 FALSE,
                                 event->xkey.time);
    }
}

static void
handle_toggle_on_all_workspaces (MetaDisplay    *display,
                           MetaScreen     *screen,
                           MetaWindow     *window,
                           XEvent         *event,
                           MetaKeyBinding *binding,
                           gpointer        dummy)
{
  if (window->on_all_workspaces_requested)
    meta_window_unstick (window);
  else
    meta_window_stick (window);
}

static void
handle_move_to_workspace  (MetaDisplay    *display,
                              MetaScreen     *screen,
                              MetaWindow     *window,
                              XEvent         *event,
                           MetaKeyBinding *binding,
                           gpointer        dummy)
{
  gint which = binding->handler->data;
  gboolean flip = (which < 0);
  gboolean new = (which == META_MOTION_NOT_EXIST_YET);
  MetaWorkspace *workspace;
  
  /* If which is zero or positive, it's a workspace number, and the window
   * should move to the workspace with that number.
   *
   * However, if it's negative, it's a direction with respect to the current
   * position; it's expressed as a member of the MetaMotionDirection enum,
   * all of whose members are negative.  Such a change is called a flip.
   */

  if (window->always_sticky)
    return;
  
  workspace = NULL;
  if (!new) {
    if (flip)
      {      
        workspace = meta_workspace_get_neighbor (screen->active_workspace,
                                                 which);
      }
    else
      {
        workspace = meta_screen_get_workspace_by_index (screen, which);
      }
  }
  
  if (workspace)
    {
      /* Activate second, so the window is never unmapped */
      meta_window_change_workspace (window, workspace);
      if (flip)
        {
          meta_topic (META_DEBUG_FOCUS,
                      "Resetting mouse_mode to FALSE due to "
                      "handle_move_to_workspace() call with flip set.\n");
          workspace->screen->display->mouse_mode = FALSE;
          meta_workspace_activate_with_focus (workspace,
                                              window,
                                              event->xkey.time);
        }
    }
  else if (new)
    {
      workspace = meta_screen_append_new_workspace (window->screen, FALSE, event->xkey.time);
      GSettings *cinnamon = g_settings_new ("org.cinnamon");
      g_settings_set_int (cinnamon, "number-workspaces", g_list_length (screen->workspaces));
      g_object_unref (cinnamon);

      meta_window_change_workspace (window, workspace);
    }
}

static void
handle_move_to_monitor (MetaDisplay    *display,
                        MetaScreen     *screen,
                        MetaWindow     *window,
                        XEvent         *event,
                        MetaKeyBinding *binding,
                        gpointer        dummy)
{
  gint which = binding->handler->data;
  const MetaMonitorInfo *current, *new;

  current = meta_screen_get_monitor_for_window (screen, window);
  new = meta_screen_get_monitor_neighbor (screen, current->number, which);

  if (new == NULL)
    return;

  meta_window_move_to_monitor (window, new->number);
}

static void 
handle_raise_or_lower (MetaDisplay    *display,
                       MetaScreen     *screen,
		       MetaWindow     *window,
		       XEvent         *event,
		       MetaKeyBinding *binding,
                       gpointer        dummy)
{
  /* Get window at pointer */
  
  MetaWindow *above = NULL;
      
  /* Check if top */
  if (meta_stack_get_top (window->screen->stack) == window)
    {
      meta_window_lower (window);
      return;
    }
      
  /* else check if windows in same layer are intersecting it */
  
  above = meta_stack_get_above (window->screen->stack, window, TRUE); 

  while (above)
    {
      MetaRectangle tmp, win_rect, above_rect;
      
      if (above->mapped)
        {
          meta_window_get_outer_rect (window, &win_rect);
          meta_window_get_outer_rect (above, &above_rect);
          
          /* Check if obscured */
          if (meta_rectangle_intersect (&win_rect, &above_rect, &tmp))
            {
              meta_window_raise (window);
              return;
            }
        }
	  
      above = meta_stack_get_above (window->screen->stack, above, TRUE); 
    }

  /* window is not obscured */
  meta_window_lower (window);
}

static void
handle_raise (MetaDisplay    *display,
              MetaScreen     *screen,
              MetaWindow     *window,
              XEvent         *event,
              MetaKeyBinding *binding,
              gpointer        dummy)
{
  meta_window_raise (window);
}

static void
handle_lower (MetaDisplay    *display,
              MetaScreen     *screen,
              MetaWindow     *window,
              XEvent         *event,
              MetaKeyBinding *binding,
              gpointer        dummy)
{
  meta_window_lower (window);
}

static void
handle_set_spew_mark (MetaDisplay    *display,
                  MetaScreen     *screen,
                  MetaWindow     *window,
                  XEvent         *event,
                      MetaKeyBinding *binding,
                      gpointer        dummy)
{
  meta_verbose ("-- MARK MARK MARK MARK --\n");
}

LOCAL_SYMBOL void
meta_set_keybindings_disabled (gboolean setting)
{
  all_bindings_disabled = setting;
  meta_topic (META_DEBUG_KEYBINDINGS,
              "Keybindings %s\n", all_bindings_disabled ? "disabled" : "enabled");
}

gboolean
meta_keybindings_set_custom_handler (const gchar        *name,
                                     MetaKeyHandlerFunc  handler,
                                     gpointer            user_data,
                                     GDestroyNotify      free_data)
{
  MetaKeyHandler *key_handler = HANDLER (name);

  if (!key_handler)
    return FALSE;

  if (key_handler->user_data_free_func && key_handler->user_data)
    key_handler->user_data_free_func (key_handler->user_data);

  key_handler->func = handler;
  key_handler->user_data = user_data;
  key_handler->user_data_free_func = free_data;

  return TRUE;
}

static void
init_builtin_key_bindings (MetaDisplay *display)
{
#define REVERSES_AND_REVERSED (META_KEY_BINDING_REVERSES | \
                               META_KEY_BINDING_IS_REVERSED)

  add_builtin_keybinding (display,
                          "switch-to-workspace-1",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_WORKSPACE_1,
                          handle_switch_to_workspace, 0);
  add_builtin_keybinding (display,
                          "switch-to-workspace-2",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_WORKSPACE_2,
                          handle_switch_to_workspace, 1);
  add_builtin_keybinding (display,
                          "switch-to-workspace-3",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_WORKSPACE_3,
                          handle_switch_to_workspace, 2);
  add_builtin_keybinding (display,
                          "switch-to-workspace-4",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_WORKSPACE_4,
                          handle_switch_to_workspace, 3);
  add_builtin_keybinding (display,
                          "switch-to-workspace-5",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_WORKSPACE_5,
                          handle_switch_to_workspace, 4);
  add_builtin_keybinding (display,
                          "switch-to-workspace-6",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_WORKSPACE_6,
                          handle_switch_to_workspace, 5);
  add_builtin_keybinding (display,
                          "switch-to-workspace-7",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_WORKSPACE_7,
                          handle_switch_to_workspace, 6);
  add_builtin_keybinding (display,
                          "switch-to-workspace-8",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_WORKSPACE_8,
                          handle_switch_to_workspace, 7);
  add_builtin_keybinding (display,
                          "switch-to-workspace-9",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_WORKSPACE_9,
                          handle_switch_to_workspace, 8);
  add_builtin_keybinding (display,
                          "switch-to-workspace-10",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_WORKSPACE_10,
                          handle_switch_to_workspace, 9);
  add_builtin_keybinding (display,
                          "switch-to-workspace-11",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_WORKSPACE_11,
                          handle_switch_to_workspace, 10);
  add_builtin_keybinding (display,
                          "switch-to-workspace-12",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_WORKSPACE_12,
                          handle_switch_to_workspace, 11);

  // Handled by Cinnamon, hence handler is NULL
  add_builtin_keybinding (display,
                          "switch-to-workspace-left",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_WORKSPACE_LEFT,
                          NULL, META_MOTION_LEFT);

  add_builtin_keybinding (display,
                          "switch-to-workspace-right",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_WORKSPACE_RIGHT,
                          NULL, META_MOTION_RIGHT);

  add_builtin_keybinding (display,
                          "switch-to-workspace-up",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_WORKSPACE_UP,
                          NULL, META_MOTION_UP);

  add_builtin_keybinding (display,
                          "switch-to-workspace-down",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_WORKSPACE_DOWN,
                          NULL, META_MOTION_DOWN);

  /* The ones which have inverses.  These can't be bound to any keystroke
   * containing Shift because Shift will invert their "backward" state.
   *
   * TODO: "NORMAL" and "DOCKS" should be renamed to the same name as their
   * action, for obviousness.
   *
   * TODO: handle_switch and handle_cycle should probably really be the
   * same function checking a bit in the parameter for difference.
   */

  add_builtin_keybinding (display,
                          "switch-group",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_REVERSES,
                          META_KEYBINDING_ACTION_SWITCH_GROUP,
                          NULL, META_TAB_LIST_GROUP);

  add_builtin_keybinding (display,
                          "switch-group-backward",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          REVERSES_AND_REVERSED,
                          META_KEYBINDING_ACTION_SWITCH_GROUP_BACKWARD,
                          NULL, META_TAB_LIST_GROUP);

  add_builtin_keybinding (display,
                          "switch-windows",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_REVERSES,
                          META_KEYBINDING_ACTION_SWITCH_WINDOWS,
                          NULL, META_TAB_LIST_NORMAL);

  add_builtin_keybinding (display,
                          "switch-windows-backward",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          REVERSES_AND_REVERSED,
                          META_KEYBINDING_ACTION_SWITCH_WINDOWS_BACKWARD,
                          NULL, META_TAB_LIST_NORMAL);

  add_builtin_keybinding (display,
                          "switch-panels",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_REVERSES,
                          META_KEYBINDING_ACTION_SWITCH_PANELS,
                          NULL, META_TAB_LIST_DOCKS);

  add_builtin_keybinding (display,
                          "switch-panels-backward",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          REVERSES_AND_REVERSED,
                          META_KEYBINDING_ACTION_SWITCH_PANELS_BACKWARD,
                          NULL, META_TAB_LIST_DOCKS);

  add_builtin_keybinding (display,
                          "show-desktop",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_SHOW_DESKTOP,
                          handle_show_desktop, 0);

  add_builtin_keybinding (display,
                          "panel-run-dialog",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_PANEL_RUN_DIALOG,
                          NULL, META_KEYBINDING_ACTION_PANEL_RUN_DIALOG);

  add_builtin_keybinding (display,
                          "toggle-recording",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_TOGGLE_RECORDING,
                          handle_toggle_recording, 0);

  add_builtin_keybinding (display,
                          "set-spew-mark",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_SET_SPEW_MARK,
                          handle_set_spew_mark, 0);

#undef REVERSES_AND_REVERSED

/************************ PER WINDOW BINDINGS ************************/

/* These take a window as an extra parameter; they have no effect
 * if no window is active.
 */

  add_builtin_keybinding (display,
                          "activate-window-menu",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_ACTIVATE_WINDOW_MENU,
                          handle_activate_window_menu, 0);

  add_builtin_keybinding (display,
                          "toggle-fullscreen",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_TOGGLE_FULLSCREEN,
                          handle_toggle_fullscreen, 0);

  add_builtin_keybinding (display,
                          "toggle-maximized",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_TOGGLE_MAXIMIZED,
                          handle_toggle_maximized, 0);

  add_builtin_keybinding (display,
                          "push-tile-left",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_PUSH_TILE_LEFT,
                          handle_tile_action, META_TILE_LEFT);

  add_builtin_keybinding (display,
                          "push-tile-right",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_PUSH_TILE_RIGHT,
                          handle_tile_action, META_TILE_RIGHT);

  add_builtin_keybinding (display,
                          "push-tile-up",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_PUSH_TILE_UP,
                          handle_tile_action, META_TILE_TOP);

  add_builtin_keybinding (display,
                          "push-tile-down",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_PUSH_TILE_DOWN,
                          handle_tile_action, META_TILE_BOTTOM);

  add_builtin_keybinding (display,
                          "push-snap-left",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_PUSH_SNAP_LEFT,
                          handle_tile_action, META_TILE_LEFT);

  add_builtin_keybinding (display,
                          "push-snap-right",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_PUSH_SNAP_RIGHT,
                          handle_tile_action, META_TILE_RIGHT);

  add_builtin_keybinding (display,
                          "push-snap-up",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_PUSH_SNAP_UP,
                          handle_tile_action, META_TILE_TOP);

  add_builtin_keybinding (display,
                          "push-snap-down",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_PUSH_SNAP_DOWN,
                          handle_tile_action, META_TILE_BOTTOM);



  add_builtin_keybinding (display,
                          "toggle-above",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_TOGGLE_ABOVE,
                          handle_toggle_above, 0);

  add_builtin_keybinding (display,
                          "maximize",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MAXIMIZE,
                          handle_maximize, 0);

  add_builtin_keybinding (display,
                          "unmaximize",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_UNMAXIMIZE,
                          handle_unmaximize, 0);

  add_builtin_keybinding (display,
                          "toggle-shaded",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_TOGGLE_SHADED,
                          handle_toggle_shaded, 0);

  add_builtin_keybinding (display,
                          "minimize",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MINIMIZE,
                          handle_minimize, 0);

  add_builtin_keybinding (display,
                          "close",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_CLOSE,
                          handle_close, 0);

  add_builtin_keybinding (display,
                          "begin-move",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_BEGIN_MOVE,
                          handle_begin_move, 0);

  add_builtin_keybinding (display,
                          "begin-resize",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_BEGIN_RESIZE,
                          handle_begin_resize, 0);

  add_builtin_keybinding (display,
                          "toggle-on-all-workspaces",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_TOGGLE_ON_ALL_WORKSPACES,
                          handle_toggle_on_all_workspaces, 0);

  add_builtin_keybinding (display,
                          "move-to-workspace-1",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_1,
                          handle_move_to_workspace, 0);

  add_builtin_keybinding (display,
                          "move-to-workspace-2",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_2,
                          handle_move_to_workspace, 1);

  add_builtin_keybinding (display,
                          "move-to-workspace-3",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_3,
                          handle_move_to_workspace, 2);

  add_builtin_keybinding (display,
                          "move-to-workspace-4",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_4,
                          handle_move_to_workspace, 3);

  add_builtin_keybinding (display,
                          "move-to-workspace-5",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_5,
                          handle_move_to_workspace, 4);

  add_builtin_keybinding (display,
                          "move-to-workspace-6",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_6,
                          handle_move_to_workspace, 5);

  add_builtin_keybinding (display,
                          "move-to-workspace-7",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_7,
                          handle_move_to_workspace, 6);

  add_builtin_keybinding (display,
                          "move-to-workspace-8",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_8,
                          handle_move_to_workspace, 7);

  add_builtin_keybinding (display,
                          "move-to-workspace-9",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_9,
                          handle_move_to_workspace, 8);

  add_builtin_keybinding (display,
                          "move-to-workspace-10",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_10,
                          handle_move_to_workspace, 9);

  add_builtin_keybinding (display,
                          "move-to-workspace-11",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_11,
                          handle_move_to_workspace, 10);

  add_builtin_keybinding (display,
                          "move-to-workspace-12",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_12,
                          handle_move_to_workspace, 11);

  add_builtin_keybinding (display,
                          "move-to-workspace-left",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_LEFT,
                          NULL, META_MOTION_LEFT);

  add_builtin_keybinding (display,
                          "move-to-workspace-right",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_RIGHT,
                          NULL, META_MOTION_RIGHT);

  add_builtin_keybinding (display,
                          "move-to-workspace-up",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_UP,
                          handle_move_to_workspace, META_MOTION_UP);

  add_builtin_keybinding (display,
                          "move-to-workspace-down",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_DOWN,
                          handle_move_to_workspace, META_MOTION_DOWN);

  add_builtin_keybinding (display,
                          "move-to-workspace-new",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_NEW,
                          handle_move_to_workspace, META_MOTION_NOT_EXIST_YET);

  add_builtin_keybinding (display,
                          "move-to-monitor-left",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_MONITOR_LEFT,
                          handle_move_to_monitor, META_SCREEN_LEFT);

  add_builtin_keybinding (display,
                          "move-to-monitor-right",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_MONITOR_RIGHT,
                          handle_move_to_monitor, META_SCREEN_RIGHT);

  add_builtin_keybinding (display,
                          "move-to-monitor-down",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_MONITOR_DOWN,
                          handle_move_to_monitor, META_SCREEN_DOWN);

  add_builtin_keybinding (display,
                          "move-to-monitor-up",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_MONITOR_UP,
                          handle_move_to_monitor, META_SCREEN_UP);

  add_builtin_keybinding (display,
                          "raise-or-lower",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_RAISE_OR_LOWER,
                          handle_raise_or_lower, 0);

  add_builtin_keybinding (display,
                          "raise",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_RAISE,
                          handle_raise, 0);

  add_builtin_keybinding (display,
                          "lower",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_LOWER,
                          handle_lower, 0);

  add_builtin_keybinding (display,
                          "maximize-vertically",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MAXIMIZE_VERTICALLY,
                          handle_maximize_vertically, 0);

  add_builtin_keybinding (display,
                          "maximize-horizontally",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MAXIMIZE_HORIZONTALLY,
                          handle_maximize_horizontally, 0);

  add_builtin_keybinding (display,
                          "move-to-corner-nw",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_CORNER_NW,
                          handle_move_to, META_MOVE_TO_NW);

  add_builtin_keybinding (display,
                          "move-to-corner-ne",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_CORNER_NE,
                          handle_move_to, META_MOVE_TO_NE);

  add_builtin_keybinding (display,
                          "move-to-corner-sw",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_CORNER_SW,
                          handle_move_to, META_MOVE_TO_SW);

  add_builtin_keybinding (display,
                          "move-to-corner-se",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_CORNER_SE,
                          handle_move_to, META_MOVE_TO_SE);

  add_builtin_keybinding (display,
                          "move-to-side-n",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_SIDE_N,
                          handle_move_to, META_MOVE_TO_N);

  add_builtin_keybinding (display,
                          "move-to-side-s",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_SIDE_S,
                          handle_move_to, META_MOVE_TO_S);

  add_builtin_keybinding (display,
                          "move-to-side-e",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_SIDE_E,
                          handle_move_to, META_MOVE_TO_E);

  add_builtin_keybinding (display,
                          "move-to-side-w",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_SIDE_W,
                          handle_move_to, META_MOVE_TO_W);

  add_builtin_keybinding (display,
                          "move-to-center",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_CENTER,
                          handle_move_to_center, 0);

  add_builtin_keybinding (display,
                          "increase-opacity",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_INCREASE_OPACITY,
                          handle_opacity, 0);

  add_builtin_keybinding (display,
                          "decrease-opacity",
                          SCHEMA_MUFFIN_KEYBINDINGS,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_DECREASE_OPACITY,
                          handle_opacity, 0);
}

LOCAL_SYMBOL void
meta_display_init_keys (MetaDisplay *display)
{
  /* Keybindings */
  display->keymap = NULL;
  display->keysyms_per_keycode = 0;
  display->modmap = NULL;
  display->min_keycode = 0;
  display->max_keycode = 0;
  display->ignored_modifier_mask = 0;
  display->num_lock_mask = 0;
  display->scroll_lock_mask = 0;
  display->hyper_mask = 0;
  display->super_mask = 0;
  display->meta_mask = 0;
  display->key_bindings = NULL;
  display->n_key_bindings = 0;

  XDisplayKeycodes (display->xdisplay,
                    &display->min_keycode,
                    &display->max_keycode);

  meta_topic (META_DEBUG_KEYBINDINGS,
              "Display has keycode range %d to %d\n",
              display->min_keycode,
              display->max_keycode);

  reload_keymap (display);
  reload_modmap (display);

  key_handlers = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                        (GDestroyNotify) key_handler_free);
  init_builtin_key_bindings (display);

  rebuild_key_binding_table (display);

  reload_keycodes (display);
  reload_modifiers (display);

  /* Keys are actually grabbed in meta_screen_grab_keys() */

  meta_prefs_add_listener (bindings_changed_callback, display);

#ifdef HAVE_XKB
  /* meta_display_init_keys() should have already called XkbQueryExtension() */
  if (display->xkb_base_event_type != -1)
    XkbSelectEvents (display->xdisplay, XkbUseCoreKbd,
                     XkbNewKeyboardNotifyMask | XkbMapNotifyMask,
                     XkbNewKeyboardNotifyMask | XkbMapNotifyMask);
#endif
}
