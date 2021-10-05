/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Benjamin Otte <otte@gnome.org>
 *         Christian Kellner <gicmo@gnome.org>
 */

#include "config.h"

#include "meta-x11-selection-output-stream-private.h"

#include "meta/meta-x11-errors.h"
#include "x11/meta-x11-display-private.h"

typedef struct _MetaX11SelectionOutputStreamPrivate MetaX11SelectionOutputStreamPrivate;

struct _MetaX11SelectionOutputStream
{
  GOutputStream parent_instance;
};

struct _MetaX11SelectionOutputStreamPrivate
{
  MetaX11Display *x11_display;
  Window xwindow;
  Atom xselection;
  Atom xtarget;
  Atom xproperty;
  Atom xtype;
  int format;
  gulong timestamp;

  GMutex mutex;
  GCond cond;
  GByteArray *data;
  guint flush_requested : 1;

  GTask *pending_task;

  guint incr : 1;
  guint delete_pending : 1;
  guint pipe_error : 1;
};

G_DEFINE_TYPE_WITH_PRIVATE (MetaX11SelectionOutputStream,
                            meta_x11_selection_output_stream,
                            G_TYPE_OUTPUT_STREAM);

static size_t get_element_size (int format);

static void
meta_x11_selection_output_stream_notify_selection (MetaX11SelectionOutputStream *stream)
{
  MetaX11SelectionOutputStreamPrivate *priv =
    meta_x11_selection_output_stream_get_instance_private (stream);
  XSelectionEvent event;
  Display *xdisplay;

  event = (XSelectionEvent) {
    .type = SelectionNotify,
    .time = priv->timestamp,
    .requestor = priv->xwindow,
    .selection = priv->xselection,
    .target = priv->xtarget,
    .property = priv->xproperty,
  };

  meta_x11_error_trap_push (priv->x11_display);

  xdisplay = priv->x11_display->xdisplay;

  XSendEvent (xdisplay,
              priv->xwindow, False, NoEventMask,
              (XEvent *) &event);
  XSync (xdisplay, False);

  meta_x11_error_trap_pop (priv->x11_display);
}

static gboolean
meta_x11_selection_output_stream_can_flush (MetaX11SelectionOutputStream *stream)
{
  MetaX11SelectionOutputStreamPrivate *priv =
    meta_x11_selection_output_stream_get_instance_private (stream);

  if (priv->delete_pending)
    return FALSE;
  if (!g_output_stream_is_closing (G_OUTPUT_STREAM (stream)) &&
      priv->data->len < get_element_size (priv->format))
    return FALSE;

  return TRUE;
}

static size_t
get_max_request_size (MetaX11Display *display)
{
  size_t size;

  size = XExtendedMaxRequestSize (display->xdisplay);
  if (size <= 0)
    size = XMaxRequestSize (display->xdisplay);

  return (size - 100) * 4;
}

static gboolean
meta_x11_selection_output_stream_needs_flush_unlocked (MetaX11SelectionOutputStream *stream)
{
  MetaX11SelectionOutputStreamPrivate *priv =
    meta_x11_selection_output_stream_get_instance_private (stream);

  if (priv->data->len == 0)
    {
      if (priv->incr)
        return g_output_stream_is_closing (G_OUTPUT_STREAM (stream));
      else
        return FALSE;
    }

  if (g_output_stream_is_closing (G_OUTPUT_STREAM (stream)))
    return TRUE;

  if (priv->flush_requested)
    return TRUE;

  return priv->data->len >= get_max_request_size (priv->x11_display);
}

static gboolean
meta_x11_selection_output_stream_needs_flush (MetaX11SelectionOutputStream *stream)
{
  MetaX11SelectionOutputStreamPrivate *priv =
    meta_x11_selection_output_stream_get_instance_private (stream);

  gboolean result;

  g_mutex_lock (&priv->mutex);

  result = meta_x11_selection_output_stream_needs_flush_unlocked (stream);

  g_mutex_unlock (&priv->mutex);

  return result;
}

static size_t
get_element_size (int format)
{
  switch (format)
    {
    case 8:
      return 1;

    case 16:
      return sizeof (short);

    case 32:
      return sizeof (long);

    default:
      g_warning ("Unknown format %u", format);
      return 1;
    }
}

static gboolean
meta_x11_selection_output_stream_check_pipe (MetaX11SelectionOutputStream  *stream,
                                             GError                       **error)
{
  MetaX11SelectionOutputStreamPrivate *priv =
    meta_x11_selection_output_stream_get_instance_private (stream);

  if (priv->pipe_error)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_BROKEN_PIPE,
                   "Connection with client was broken");
      return FALSE;
    }

  return TRUE;
}

static void
meta_x11_selection_output_stream_perform_flush (MetaX11SelectionOutputStream *stream)
{
  MetaX11SelectionOutputStreamPrivate *priv =
    meta_x11_selection_output_stream_get_instance_private (stream);
  Display *xdisplay;
  size_t element_size, n_elements, max_size;
  gboolean first_chunk = FALSE;
  int error_code;

  g_assert (!priv->delete_pending);

  xdisplay = priv->x11_display->xdisplay;

  /* We operate on a foreign window, better guard against catastrophe */
  meta_x11_error_trap_push (priv->x11_display);

  g_mutex_lock (&priv->mutex);

  element_size = get_element_size (priv->format);
  n_elements = priv->data->len / element_size;
  max_size = get_max_request_size (priv->x11_display);

  if (!priv->incr)
    first_chunk = TRUE;

  if (!priv->incr && priv->data->len > max_size)
    {
      XWindowAttributes attrs;

      priv->incr = TRUE;
      XGetWindowAttributes (xdisplay,
			    priv->xwindow,
			    &attrs);
      if (!(attrs.your_event_mask & PropertyChangeMask))
        {
          XSelectInput (xdisplay, priv->xwindow, attrs.your_event_mask | PropertyChangeMask);
        }

      XChangeProperty (xdisplay,
                       priv->xwindow,
                       priv->xproperty,
                       XInternAtom (priv->x11_display->xdisplay, "INCR", False),
                       32,
                       PropModeReplace,
                       (guchar *) &(long) { n_elements },
                       1);
      priv->delete_pending = TRUE;
    }
  else
    {
      size_t copy_n_elements;

      if (priv->incr && priv->data->len > 0)
        priv->delete_pending = TRUE;

      copy_n_elements = MIN (n_elements, max_size / element_size);

      XChangeProperty (xdisplay,
                       priv->xwindow,
                       priv->xproperty,
                       priv->xtype,
                       priv->format,
                       PropModeReplace,
                       priv->data->data,
                       copy_n_elements);
      g_byte_array_remove_range (priv->data, 0, copy_n_elements * element_size);
    }

  if (first_chunk)
    meta_x11_selection_output_stream_notify_selection (stream);

  g_cond_broadcast (&priv->cond);
  g_mutex_unlock (&priv->mutex);

  error_code = meta_x11_error_trap_pop_with_return (priv->x11_display);

  if (error_code != Success)
    {
      char error_str[100];

      priv->flush_requested = FALSE;
      priv->delete_pending = FALSE;
      priv->pipe_error = TRUE;

      if (priv->pending_task)
        {
          XGetErrorText (xdisplay, error_code, error_str, sizeof (error_str));
          g_task_return_new_error (priv->pending_task,
                                   G_IO_ERROR,
                                   G_IO_ERROR_BROKEN_PIPE,
                                   "Failed to flush selection output stream: %s",
                                   error_str);
          g_clear_object (&priv->pending_task);
        }
    }
  else if (priv->pending_task && priv->data->len == 0 && !priv->delete_pending)
    {
      size_t result;

      priv->flush_requested = FALSE;
      result = GPOINTER_TO_SIZE (g_task_get_task_data (priv->pending_task));
      g_task_return_int (priv->pending_task, result);
      g_clear_object (&priv->pending_task);
    }
}

static gboolean
meta_x11_selection_output_stream_invoke_flush (gpointer data)
{
  MetaX11SelectionOutputStream *stream =
    META_X11_SELECTION_OUTPUT_STREAM (data);
  MetaX11SelectionOutputStreamPrivate *priv =
    meta_x11_selection_output_stream_get_instance_private (stream);

  if (meta_x11_selection_output_stream_needs_flush (stream) &&
      meta_x11_selection_output_stream_can_flush (stream))
    meta_x11_selection_output_stream_perform_flush (stream);

  if (priv->delete_pending || priv->data->len > 0)
    return G_SOURCE_CONTINUE;
  else
    return G_SOURCE_REMOVE;
}

static gssize
meta_x11_selection_output_stream_write (GOutputStream  *output_stream,
                                        const void     *buffer,
                                        size_t          count,
                                        GCancellable   *cancellable,
                                        GError        **error)
{
  MetaX11SelectionOutputStream *stream =
    META_X11_SELECTION_OUTPUT_STREAM (output_stream);
  MetaX11SelectionOutputStreamPrivate *priv =
    meta_x11_selection_output_stream_get_instance_private (stream);

  if (!meta_x11_selection_output_stream_check_pipe (stream, error))
    return -1;

  g_mutex_lock (&priv->mutex);
  g_byte_array_append (priv->data, buffer, count);
  g_mutex_unlock (&priv->mutex);

  g_main_context_invoke (NULL, meta_x11_selection_output_stream_invoke_flush, stream);

  g_mutex_lock (&priv->mutex);
  if (meta_x11_selection_output_stream_needs_flush_unlocked (stream))
    g_cond_wait (&priv->cond, &priv->mutex);
  g_mutex_unlock (&priv->mutex);

  return count;
}

static void
meta_x11_selection_output_stream_write_async (GOutputStream       *output_stream,
                                              const void          *buffer,
                                              size_t               count,
                                              int                  io_priority,
                                              GCancellable        *cancellable,
                                              GAsyncReadyCallback  callback,
                                              gpointer             user_data)
{
  MetaX11SelectionOutputStream *stream =
    META_X11_SELECTION_OUTPUT_STREAM (output_stream);
  MetaX11SelectionOutputStreamPrivate *priv =
    meta_x11_selection_output_stream_get_instance_private (stream);
  GError *error = NULL;
  GTask *task;

  task = g_task_new (stream, cancellable, callback, user_data);
  g_task_set_source_tag (task, meta_x11_selection_output_stream_write_async);
  g_task_set_priority (task, io_priority);

  if (!meta_x11_selection_output_stream_check_pipe (stream, &error))
    {
      g_task_return_error (task, error);
      return;
    }

  g_mutex_lock (&priv->mutex);
  g_byte_array_append (priv->data, buffer, count);
  g_mutex_unlock (&priv->mutex);

  if (!meta_x11_selection_output_stream_needs_flush (stream))
    {
      g_task_return_int (task, count);
      g_object_unref (task);
      return;
    }
  else
    {
      if (meta_x11_selection_output_stream_can_flush (stream))
        meta_x11_selection_output_stream_perform_flush (stream);

      g_task_return_int (task, count);
      g_object_unref (task);
      return;
    }
}

static gssize
meta_x11_selection_output_stream_write_finish (GOutputStream  *stream,
                                               GAsyncResult   *result,
                                               GError        **error)
{
  g_return_val_if_fail (g_task_is_valid (result, stream), -1);
  g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) ==
                        meta_x11_selection_output_stream_write_async, -1);

  return g_task_propagate_int (G_TASK (result), error);
}

static gboolean
meta_x11_selection_output_request_flush (MetaX11SelectionOutputStream *stream)
{
  MetaX11SelectionOutputStreamPrivate *priv =
    meta_x11_selection_output_stream_get_instance_private (stream);
  gboolean needs_flush;

  g_mutex_lock (&priv->mutex);

  if (priv->data->len > 0)
    priv->flush_requested = TRUE;

  needs_flush = meta_x11_selection_output_stream_needs_flush_unlocked (stream);
  g_mutex_unlock (&priv->mutex);

  return needs_flush;
}

static gboolean
meta_x11_selection_output_stream_flush (GOutputStream  *output_stream,
                                        GCancellable   *cancellable,
                                        GError        **error)
{
  MetaX11SelectionOutputStream *stream =
    META_X11_SELECTION_OUTPUT_STREAM (output_stream);
  MetaX11SelectionOutputStreamPrivate *priv =
    meta_x11_selection_output_stream_get_instance_private (stream);

  if (!meta_x11_selection_output_stream_check_pipe (stream, error))
    return FALSE;
  if (!meta_x11_selection_output_request_flush (stream))
    return TRUE;

  g_main_context_invoke (NULL, meta_x11_selection_output_stream_invoke_flush,
                         stream);

  g_mutex_lock (&priv->mutex);
  if (meta_x11_selection_output_stream_needs_flush_unlocked (stream))
    g_cond_wait (&priv->cond, &priv->mutex);
  g_mutex_unlock (&priv->mutex);

  return TRUE;
}

static void
meta_x11_selection_output_stream_flush_async (GOutputStream       *output_stream,
                                              int                  io_priority,
                                              GCancellable        *cancellable,
                                              GAsyncReadyCallback  callback,
                                              gpointer             user_data)
{
  MetaX11SelectionOutputStream *stream =
    META_X11_SELECTION_OUTPUT_STREAM (output_stream);
  MetaX11SelectionOutputStreamPrivate *priv =
    meta_x11_selection_output_stream_get_instance_private (stream);
  GError *error = NULL;
  GTask *task;

  task = g_task_new (stream, cancellable, callback, user_data);
  g_task_set_source_tag (task, meta_x11_selection_output_stream_flush_async);
  g_task_set_priority (task, io_priority);

  if (!meta_x11_selection_output_stream_check_pipe (stream, &error))
    {
      g_task_return_error (task, error);
      return;
    }

  if (!meta_x11_selection_output_stream_can_flush (stream))
    {
      if (meta_x11_selection_output_request_flush (stream))
        {
          g_assert (priv->pending_task == NULL);
          priv->pending_task = task;
          return;
        }
      else
        {
          g_task_return_boolean (task, TRUE);
          g_object_unref (task);
          return;
        }
    }

  g_assert (priv->pending_task == NULL);
  priv->pending_task = task;
  meta_x11_selection_output_stream_perform_flush (stream);
}

static gboolean
meta_x11_selection_output_stream_flush_finish (GOutputStream  *stream,
                                               GAsyncResult   *result,
                                               GError        **error)
{
  g_return_val_if_fail (g_task_is_valid (result, stream), FALSE);
  g_return_val_if_fail (g_async_result_is_tagged (result, meta_x11_selection_output_stream_flush_async), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static gboolean
meta_x11_selection_output_stream_invoke_close (gpointer stream)
{
  MetaX11SelectionOutputStreamPrivate *priv =
    meta_x11_selection_output_stream_get_instance_private (stream);

  priv->x11_display->selection.output_streams =
    g_list_remove (priv->x11_display->selection.output_streams, stream);

  return G_SOURCE_REMOVE;
}

static gboolean
meta_x11_selection_output_stream_close (GOutputStream  *stream,
                                        GCancellable   *cancellable,
                                        GError        **error)
{
  g_main_context_invoke (NULL, meta_x11_selection_output_stream_invoke_close, stream);

  return TRUE;
}

static void
meta_x11_selection_output_stream_close_async (GOutputStream       *stream,
                                              int                  io_priority,
                                              GCancellable        *cancellable,
                                              GAsyncReadyCallback  callback,
                                              gpointer             user_data)
{
  GTask *task;

  task = g_task_new (stream, cancellable, callback, user_data);
  g_task_set_source_tag (task, meta_x11_selection_output_stream_close_async);
  g_task_set_priority (task, io_priority);

  meta_x11_selection_output_stream_invoke_close (stream);
  g_task_return_boolean (task, TRUE);

  g_object_unref (task);
}

static gboolean
meta_x11_selection_output_stream_close_finish (GOutputStream  *stream,
                                               GAsyncResult   *result,
                                               GError        **error)
{
  g_return_val_if_fail (g_task_is_valid (result, stream), FALSE);
  g_return_val_if_fail (g_async_result_is_tagged (result, meta_x11_selection_output_stream_close_async), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
meta_x11_selection_output_stream_dispose (GObject *object)
{
  MetaX11SelectionOutputStream *stream =
    META_X11_SELECTION_OUTPUT_STREAM (object);
  MetaX11SelectionOutputStreamPrivate *priv =
    meta_x11_selection_output_stream_get_instance_private (stream);

  priv->x11_display->selection.output_streams =
    g_list_remove (priv->x11_display->selection.output_streams, stream);

  G_OBJECT_CLASS (meta_x11_selection_output_stream_parent_class)->dispose (object);
}

static void
meta_x11_selection_output_stream_finalize (GObject *object)
{
  MetaX11SelectionOutputStream *stream =
    META_X11_SELECTION_OUTPUT_STREAM (object);
  MetaX11SelectionOutputStreamPrivate *priv =
    meta_x11_selection_output_stream_get_instance_private (stream);

  g_byte_array_unref (priv->data);
  g_cond_clear (&priv->cond);
  g_mutex_clear (&priv->mutex);

  G_OBJECT_CLASS (meta_x11_selection_output_stream_parent_class)->finalize (object);
}

static void
meta_x11_selection_output_stream_class_init (MetaX11SelectionOutputStreamClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GOutputStreamClass *output_stream_class = G_OUTPUT_STREAM_CLASS (klass);

  object_class->dispose = meta_x11_selection_output_stream_dispose;
  object_class->finalize = meta_x11_selection_output_stream_finalize;

  output_stream_class->write_fn = meta_x11_selection_output_stream_write;
  output_stream_class->flush = meta_x11_selection_output_stream_flush;
  output_stream_class->close_fn = meta_x11_selection_output_stream_close;

  output_stream_class->write_async = meta_x11_selection_output_stream_write_async;
  output_stream_class->write_finish = meta_x11_selection_output_stream_write_finish;
  output_stream_class->flush_async = meta_x11_selection_output_stream_flush_async;
  output_stream_class->flush_finish = meta_x11_selection_output_stream_flush_finish;
  output_stream_class->close_async = meta_x11_selection_output_stream_close_async;
  output_stream_class->close_finish = meta_x11_selection_output_stream_close_finish;
}

static void
meta_x11_selection_output_stream_init (MetaX11SelectionOutputStream *stream)
{
  MetaX11SelectionOutputStreamPrivate *priv =
    meta_x11_selection_output_stream_get_instance_private (stream);

  g_mutex_init (&priv->mutex);
  g_cond_init (&priv->cond);
  priv->data = g_byte_array_new ();
}

gboolean
meta_x11_selection_output_stream_xevent (MetaX11SelectionOutputStream *stream,
                                         const XEvent                 *xevent)
{
  MetaX11SelectionOutputStreamPrivate *priv =
    meta_x11_selection_output_stream_get_instance_private (stream);
  Display *xdisplay = priv->x11_display->xdisplay;

  if (xevent->xany.display != xdisplay ||
      xevent->xany.window != priv->xwindow)
    return FALSE;

  switch (xevent->type)
    {
    case PropertyNotify:
      if (!priv->incr ||
          xevent->xproperty.atom != priv->xproperty ||
          xevent->xproperty.state != PropertyDelete)
        return FALSE;

      priv->delete_pending = FALSE;
      if (meta_x11_selection_output_stream_needs_flush (stream) &&
          meta_x11_selection_output_stream_can_flush (stream))
        meta_x11_selection_output_stream_perform_flush (stream);
      return FALSE;

    default:
      return FALSE;
    }
}

GOutputStream *
meta_x11_selection_output_stream_new (MetaX11Display *x11_display,
                                      Window          requestor,
                                      const char     *selection,
                                      const char     *target,
                                      const char     *property,
                                      const char     *type,
                                      int             format,
                                      gulong          timestamp)
{
  MetaX11SelectionOutputStream *stream;
  MetaX11SelectionOutputStreamPrivate *priv;

  stream = g_object_new (META_TYPE_X11_SELECTION_OUTPUT_STREAM, NULL);
  priv = meta_x11_selection_output_stream_get_instance_private (stream);

  x11_display->selection.output_streams =
    g_list_prepend (x11_display->selection.output_streams, stream);

  priv->x11_display = x11_display;
  priv->xwindow = requestor;
  priv->xselection = XInternAtom (x11_display->xdisplay, selection, False);
  priv->xtarget = XInternAtom (x11_display->xdisplay, target, False);
  priv->xproperty = XInternAtom (x11_display->xdisplay, property, False);
  priv->xtype = XInternAtom (x11_display->xdisplay, type, False);
  priv->format = format;
  priv->timestamp = timestamp;

  return G_OUTPUT_STREAM (stream);
}
