/*
 * Copyright (C) 2018 Red Hat
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

#include "config.h"

#include <gdk/gdkx.h>

#include "core/meta-selection-private.h"
#include "meta/meta-selection-source-memory.h"
#include "x11/meta-selection-source-x11-private.h"
#include "x11/meta-x11-selection-output-stream-private.h"
#include "x11/meta-x11-selection-private.h"

#define UTF8_STRING_MIMETYPE "text/plain;charset=utf-8"
#define STRING_MIMETYPE "text/plain"

static gboolean
atom_to_selection_type (Display           *xdisplay,
                        Atom               selection,
                        MetaSelectionType *selection_type)
{
  if (selection == XInternAtom (xdisplay, "PRIMARY", False))
    *selection_type = META_SELECTION_PRIMARY;
  else if (selection == XInternAtom (xdisplay, "CLIPBOARD", False))
    *selection_type = META_SELECTION_CLIPBOARD;
  else if (selection == XInternAtom (xdisplay, "XdndSelection", False))
    *selection_type = META_SELECTION_DND;
  else
    return FALSE;

  return TRUE;
}

static Atom
selection_to_atom (MetaSelectionType  type,
                   Display           *xdisplay)
{
  Atom atom;

  switch (type)
    {
    case META_SELECTION_PRIMARY:
      atom = XInternAtom (xdisplay, "PRIMARY", False);
      break;
    case META_SELECTION_CLIPBOARD:
      atom = XInternAtom (xdisplay, "CLIPBOARD", False);
      break;
    case META_SELECTION_DND:
      atom = XInternAtom (xdisplay, "XdndSelection", False);
      break;
    default:
      g_warn_if_reached ();
      atom = None;
      break;
    }

  return atom;
}

static GBytes *
mimetypes_to_bytes (GList   *mimetypes,
                    Display *xdisplay)
{
  GArray *atoms = g_array_new (FALSE, FALSE, sizeof (Atom));
  GList *l;
  char *mimetype;
  Atom atom;
  gboolean utf8_string_found = FALSE, utf8_string_mimetype_found = FALSE;
  gboolean string_found = FALSE, string_mimetype_found = FALSE;
  GBytes *bytes;

  for (l = mimetypes; l; l = l->next)
    {
      mimetype = l->data;
      atom = XInternAtom (xdisplay, mimetype, False);
      g_array_append_val (atoms, atom);
      utf8_string_mimetype_found |= strcmp (mimetype, UTF8_STRING_MIMETYPE) == 0;
      utf8_string_found |= strcmp (mimetype, "UTF8_STRING") == 0;
      string_mimetype_found |= strcmp (mimetype, STRING_MIMETYPE) == 0;
      string_found |= strcmp (mimetype, "STRING") == 0;
    }

  /* Some X11 clients can only handle STRING/UTF8_STRING but not the
   * corresponding mimetypes. */
  if (utf8_string_mimetype_found && !utf8_string_found)
    {
      atom = XInternAtom (xdisplay, "UTF8_STRING", False);
      g_array_append_val (atoms, atom);
    }

  if (string_mimetype_found && !string_found)
    {
      atom = XInternAtom (xdisplay, "STRING", False);
      g_array_append_val (atoms, atom);
    }

  atom = XInternAtom (xdisplay, "TARGETS", False);
  g_array_append_val (atoms, atom);

  atom = XInternAtom (xdisplay, "TIMESTAMP", False);
  g_array_append_val (atoms, atom);

  bytes = g_bytes_new_take (atoms->data, atoms->len * sizeof (Atom));
  g_array_free (atoms, FALSE);

  return bytes;
}

static void
send_selection_notify (XSelectionRequestEvent *request_event,
                       gboolean                accepted)
{
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  XSelectionEvent event;

  memset(&event, 0, sizeof (XSelectionEvent));
  event.type = SelectionNotify;
  event.time = request_event->time;
  event.requestor = request_event->requestor;
  event.selection = request_event->selection;
  event.target = request_event->target;
  event.property = accepted ? request_event->property : None;

  XSendEvent (xdisplay, request_event->requestor,
              False, NoEventMask, (XEvent *) &event);
}

static void
write_mimetypes_cb (GOutputStream *stream,
                    GAsyncResult  *res,
                    gpointer       user_data)
{
  GError *error = NULL;

  g_output_stream_write_bytes_finish (stream, res, &error);
  g_output_stream_close (stream, NULL, NULL);
  g_object_unref (stream);

  if (error)
    {
      g_warning ("Could not fetch selection mimetypes: %s\n", error->message);
      g_error_free (error);
    }
}

static void
transfer_cb (MetaSelection *selection,
             GAsyncResult  *res,
             GOutputStream *output)
{
  GError *error = NULL;

  if (!meta_selection_transfer_finish (selection, res, &error))
    {
      g_warning ("Error writing data to X11 selection: %s", error->message);
      g_error_free (error);
    }

  g_output_stream_close (output, NULL, NULL);
  g_object_unref (output);
}

static char *
meta_x11_selection_find_target (MetaX11Display     *x11_display,
                                MetaSelection      *selection,
                                MetaSelectionType   selection_type,
                                Atom                selection_atom)
{
  GList* mimetypes = NULL;
  const gchar *atom_name;
  char *retval;

  mimetypes = meta_selection_get_mimetypes (selection, selection_type);
  atom_name = gdk_x11_get_xatom_name (selection_atom);

  if (g_list_find_custom (mimetypes, atom_name, (GCompareFunc) g_strcmp0))
    {
      retval = g_strdup (atom_name);
    }
  else if (strcmp (atom_name, "UTF8_STRING") == 0 &&
           g_list_find_custom (mimetypes, UTF8_STRING_MIMETYPE,
                               (GCompareFunc) g_strcmp0))
    {
      retval = g_strdup (UTF8_STRING_MIMETYPE);
    }
  else if (strcmp (atom_name, "STRING") == 0 &&
           g_list_find_custom (mimetypes, STRING_MIMETYPE,
                               (GCompareFunc) g_strcmp0))
    {
      retval = g_strdup (STRING_MIMETYPE);
    }
  else
    {
      retval = NULL;
    }

  g_list_free_full (mimetypes, g_free);

  return retval;
}

static gboolean
meta_x11_selection_handle_selection_request (MetaX11Display *x11_display,
                                             XEvent         *xevent)
{
  XSelectionRequestEvent *event = (XSelectionRequestEvent *) xevent;
  MetaSelectionType selection_type;
  MetaSelection *selection;
  GOutputStream *output;
  GList *mimetypes;

  if (!atom_to_selection_type (x11_display->xdisplay, event->selection, &selection_type))
    return FALSE;
  if (x11_display->selection.xwindow != event->owner)
    return FALSE;

  selection = meta_display_get_selection (meta_get_display ());

  if (event->target == gdk_x11_get_xatom_by_name ("TARGETS"))
    {
      GBytes *bytes;

      mimetypes = meta_selection_get_mimetypes (selection, selection_type);

      if (!mimetypes)
        {
          send_selection_notify (event, FALSE);
          return FALSE;
        }

      output = meta_x11_selection_output_stream_new (x11_display, event->requestor,
                                                     gdk_x11_get_xatom_name (event->selection),
                                                     gdk_x11_get_xatom_name (event->target),
                                                     gdk_x11_get_xatom_name (event->property),
                                                     "ATOM", 32, event->time);

      bytes = mimetypes_to_bytes (mimetypes, x11_display->xdisplay);
      g_list_free_full (mimetypes, g_free);

      g_output_stream_write_bytes_async (output,
                                         bytes,
                                         G_PRIORITY_DEFAULT,
                                         NULL,
                                         (GAsyncReadyCallback) write_mimetypes_cb,
                                         output);
      g_bytes_unref (bytes);
      return TRUE;
    }
  else if (event->target == gdk_x11_get_xatom_by_name ("DELETE"))
    {
      /* DnD only, this is just handled through other means on our non-x11
       * sources, so just go with it.
       */
      send_selection_notify (event, TRUE);
    }
  else
    {
      g_autofree char *target = NULL;

      target = meta_x11_selection_find_target (x11_display, selection,
                                               selection_type, event->target);

      if (target != NULL)
        {
          output = meta_x11_selection_output_stream_new (x11_display,
                                                         event->requestor,
                                                         gdk_x11_get_xatom_name (event->selection),
                                                         gdk_x11_get_xatom_name (event->target),
                                                         gdk_x11_get_xatom_name (event->property),
                                                         gdk_x11_get_xatom_name (event->target),
                                                         8, event->time);

          meta_selection_transfer_async (selection,
                                         selection_type,
                                         target,
                                         -1,
                                         output,
                                         NULL,
                                         (GAsyncReadyCallback) transfer_cb,
                                         output);
          return TRUE;
        }
      else
        {
          send_selection_notify (event, FALSE);
        }
    }

  return FALSE;
}

typedef struct
{
  MetaX11Display *x11_display;
  MetaSelection *selection;
  MetaSelectionType selection_type;
} SourceNewData;

static void
source_new_cb (GObject      *object,
               GAsyncResult *res,
               gpointer      user_data)
{
  MetaSelectionSource *source;
  SourceNewData *data = user_data;
  MetaSelection *selection = data->selection;
  MetaSelectionType selection_type = data->selection_type;
  MetaX11Display *x11_display = data->x11_display;
  g_autoptr (GError) error = NULL;

  source = meta_selection_source_x11_new_finish (res, &error);
  if (source)
    {
      g_set_object (&x11_display->selection.owners[selection_type], source);
      meta_selection_set_owner (selection, selection_type, source);
      g_object_unref (source);
    }
  else if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      g_warning ("Could not create selection source for X11: %s",
                 error->message);
    }

  g_free (data);
}

static gboolean
unset_clipboard_owner (gpointer data)
{
  MetaDisplay *display = meta_get_display ();
  MetaSelection *selection = meta_display_get_selection (display);
  MetaX11Display *x11_display = meta_display_get_x11_display (display);

  meta_selection_unset_owner (selection, META_SELECTION_CLIPBOARD,
                              x11_display->selection.owners[META_SELECTION_CLIPBOARD]);
  g_clear_object (&x11_display->selection.owners[META_SELECTION_CLIPBOARD]);

  x11_display->selection.timeout_id = 0;

  return G_SOURCE_REMOVE;
}

static gboolean
meta_x11_selection_handle_xfixes_selection_notify (MetaX11Display *x11_display,
                                                   XEvent         *xevent)
{
  XFixesSelectionNotifyEvent *event = (XFixesSelectionNotifyEvent *) xevent;
  Display *xdisplay = x11_display->xdisplay;
  MetaSelectionType selection_type;
  MetaSelection *selection;

  if (!atom_to_selection_type (xdisplay, event->selection, &selection_type))
    return FALSE;

  selection = meta_display_get_selection (meta_get_display ());

  if (selection_type == META_SELECTION_CLIPBOARD)
    g_clear_handle_id (&x11_display->selection.timeout_id, g_source_remove);

  if (x11_display->selection.cancellables[selection_type])
    {
      g_cancellable_cancel (x11_display->selection.cancellables[selection_type]);
      g_clear_object (&x11_display->selection.cancellables[selection_type]);
    }

  x11_display->selection.cancellables[selection_type] = g_cancellable_new ();

  if (event->owner == None && x11_display->selection.owners[selection_type])
    {
      if (event->subtype == XFixesSetSelectionOwnerNotify)
        {
          MetaSelectionSource *source;

          /* Replace with an empty owner */
          source = g_object_new (META_TYPE_SELECTION_SOURCE_MEMORY, NULL);
          g_set_object (&x11_display->selection.owners[selection_type], source);
          meta_selection_set_owner (selection, selection_type, source);
          g_object_unref (source);
        }
      else if (event->subtype == XFixesSelectionWindowDestroyNotify &&
               selection_type == META_SELECTION_CLIPBOARD)
        {
          /* Selection window might have gotten destroyed as part of application
           * shutdown. Trigger restoring clipboard, but wait a bit, because some
           * clients, like wine, destroy the old window immediately before a new
           * selection. Restoring the clipboard in this case would overwrite the
           * new selection, so this will be cancelled when a new selection
           * arrives. */
          x11_display->selection.timeout_id = g_timeout_add (10,
                                                             unset_clipboard_owner,
                                                             NULL);
        }
      else
        {
          /* An X client went away, clear the selection */
          meta_selection_unset_owner (selection, selection_type,
                                      x11_display->selection.owners[selection_type]);
          g_clear_object (&x11_display->selection.owners[selection_type]);
        }
    }
  else if (event->owner != None && event->owner != x11_display->selection.xwindow)
    {
      SourceNewData *data;

      data = g_new (SourceNewData, 1);
      data->x11_display = x11_display;
      data->selection = selection;
      data->selection_type = selection_type;

      meta_selection_source_x11_new_async (x11_display,
                                           event->owner,
                                           event->timestamp,
                                           event->selection,
                                           x11_display->selection.cancellables[selection_type],
                                           source_new_cb,
                                           data);
    }

  return TRUE;
}

gboolean
meta_x11_selection_handle_event (MetaX11Display *x11_display,
                                 XEvent         *xevent)
{
  if (xevent->type == SelectionRequest)
    return meta_x11_selection_handle_selection_request (x11_display, xevent);
  else if (xevent->type - x11_display->xfixes_event_base == XFixesSelectionNotify)
    return meta_x11_selection_handle_xfixes_selection_notify (x11_display, xevent);

  return FALSE;
}

static void
notify_selection_owner (MetaX11Display      *x11_display,
                        MetaSelectionType    selection_type,
                        MetaSelectionSource *new_owner)
{
  Display *xdisplay = x11_display->xdisplay;

  if (new_owner && new_owner != x11_display->selection.owners[selection_type])
    {
      if (x11_display->selection.cancellables[selection_type])
        {
          g_cancellable_cancel (x11_display->selection.cancellables[selection_type]);
          g_clear_object (&x11_display->selection.cancellables[selection_type]);
        }

      /* If the owner is non-X11, claim the selection on our selection
       * window, so X11 apps can interface with it.
       */
      XSetSelectionOwner (xdisplay,
                          selection_to_atom (selection_type, xdisplay),
                          x11_display->selection.xwindow,
                          META_CURRENT_TIME);
    }
}

void
meta_x11_selection_init (MetaX11Display *x11_display)
{
  XSetWindowAttributes attributes = { 0 };
  MetaDisplay *display = meta_get_display ();
  MetaSelection *selection;
  guint mask, i;

  attributes.event_mask = PropertyChangeMask | SubstructureNotifyMask;
  attributes.override_redirect = True;

  x11_display->selection.timeout_id = 0;
  x11_display->selection.xwindow =
    XCreateWindow (x11_display->xdisplay,
                   x11_display->xroot,
                   -1, -1, 1, 1,
                   0, /* border width */
                   0, /* depth */
                   InputOnly, /* class */
                   CopyFromParent, /* visual */
                   CWEventMask | CWOverrideRedirect,
                   &attributes);

  mask = XFixesSetSelectionOwnerNotifyMask |
    XFixesSelectionWindowDestroyNotifyMask |
    XFixesSelectionClientCloseNotifyMask;

  selection = meta_display_get_selection (display);

  for (i = 0; i < META_N_SELECTION_TYPES; i++)
    {
      MetaSelectionSource *owner;

      XFixesSelectSelectionInput (x11_display->xdisplay,
                                  x11_display->selection.xwindow,
                                  selection_to_atom (i, x11_display->xdisplay),
                                  mask);
      owner = meta_selection_get_current_owner (selection, i);
      notify_selection_owner (x11_display, i, owner);
    }

  g_signal_connect_swapped (selection,
                            "owner-changed",
                            G_CALLBACK (notify_selection_owner),
                            x11_display);
}

void
meta_x11_selection_shutdown (MetaX11Display *x11_display)
{
  MetaDisplay *display = meta_get_display ();
  guint i;

  g_signal_handlers_disconnect_by_func (meta_display_get_selection (display),
                                        notify_selection_owner,
                                        x11_display);

  for (i = 0; i < META_N_SELECTION_TYPES; i++)
    {
      g_clear_object (&x11_display->selection.owners[i]);
      if (x11_display->selection.cancellables[i])
        {
          g_cancellable_cancel (x11_display->selection.cancellables[i]);
          g_clear_object (&x11_display->selection.cancellables[i]);
        }
    }

  if (x11_display->selection.xwindow != None)
    {
      XDestroyWindow (x11_display->xdisplay, x11_display->selection.xwindow);
      x11_display->selection.xwindow = None;
    }

  g_clear_handle_id (&x11_display->selection.timeout_id, g_source_remove);
}
