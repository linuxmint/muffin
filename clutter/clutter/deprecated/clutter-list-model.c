/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *             Neil Jagdish Patel <njp@o-hand.com>
 *             Emmanuele Bassi <ebassi@openedhand.com>
 *
 * Copyright (C) 2006 OpenedHand
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
 *
 */

/**
 * SECTION:clutter-list-model
 * @short_description: List model implementation
 *
 * #ClutterListModel is a #ClutterModel implementation provided by
 * Clutter. #ClutterListModel uses a #GSequence for storing the
 * values for each row, so it's optimized for insertion and look up
 * in sorted lists.
 *
 * #ClutterListModel is available since Clutter 0.6
 *
 * Deprecated: 1.24: Use a #GListStore instance containing a custom
 *   object type with properties for each column instead.
 */

#ifdef HAVE_CONFIG_H
#include "clutter-build-config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <glib-object.h>

#define CLUTTER_DISABLE_DEPRECATION_WARNINGS
#include "clutter-list-model.h"

#include "clutter-model.h"
#include "clutter-model-private.h"
#include "clutter-private.h"
#include "clutter-debug.h"

#define CLUTTER_TYPE_LIST_MODEL_ITER                 \
        (clutter_list_model_iter_get_type())
#define CLUTTER_LIST_MODEL_ITER(obj)                 \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),           \
         CLUTTER_TYPE_LIST_MODEL_ITER,               \
         ClutterListModelIter))
#define CLUTTER_IS_LIST_MODEL_ITER(obj)              \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),           \
         CLUTTER_TYPE_LIST_MODEL_ITER))
#define CLUTTER_LIST_MODEL_ITER_CLASS(klass)         \
        (G_TYPE_CHECK_CLASS_CAST ((klass),           \
         CLUTTER_TYPE_LIST_MODEL_ITER,               \
         ClutterListModelIterClass))
#define CLUTTER_IS_LIST_MODEL_ITER_CLASS(klass)      \
        (G_TYPE_CHECK_CLASS_TYPE ((klass),           \
         CLUTTER_TYPE_LIST_MODEL_ITER))
#define CLUTTER_LIST_MODEL_ITER_GET_CLASS(obj)       \
        (G_TYPE_INSTANCE_GET_CLASS ((obj),           \
         CLUTTER_TYPE_LIST_MODEL_ITER,               \
         ClutterListModelIterClass))

typedef struct _ClutterListModelIter    ClutterListModelIter;
typedef struct _ClutterModelIterClass   ClutterListModelIterClass;

struct _ClutterListModelPrivate
{
  GSequence *sequence;

  ClutterModelIter *temp_iter;
};

struct _ClutterListModelIter
{
  ClutterModelIter parent_instance;

  GSequenceIter *seq_iter;
};



GType clutter_list_model_iter_get_type (void);

/*
 * ClutterListModel
 */

G_DEFINE_TYPE (ClutterListModelIter,
               clutter_list_model_iter,
               CLUTTER_TYPE_MODEL_ITER)

static void
clutter_list_model_iter_get_value (ClutterModelIter *iter,
                                   guint             column,
                                   GValue           *value)
{
  ClutterListModelIter *iter_default;
  GValue *values;
  GValue *iter_value;
  GValue real_value = G_VALUE_INIT;
  gboolean converted = FALSE;

  iter_default = CLUTTER_LIST_MODEL_ITER (iter);
  g_assert (iter_default->seq_iter != NULL);

  values = g_sequence_get (iter_default->seq_iter);
  iter_value = &values[column];
  g_assert (iter_value != NULL);

  if (!g_type_is_a (G_VALUE_TYPE (value), G_VALUE_TYPE (iter_value)))
    {
      if (!g_value_type_compatible (G_VALUE_TYPE (value),
                                    G_VALUE_TYPE (iter_value)) &&
          !g_value_type_compatible (G_VALUE_TYPE (iter_value),
                                    G_VALUE_TYPE (value)))
        {
          g_warning ("%s: Unable to convert from %s to %s",
                     G_STRLOC,
                     g_type_name (G_VALUE_TYPE (value)),
                     g_type_name (G_VALUE_TYPE (iter_value)));
          return;
        }

      if (!g_value_transform (iter_value, &real_value))
        {
          g_warning ("%s: Unable to make conversion from %s to %s",
                     G_STRLOC,
                     g_type_name (G_VALUE_TYPE (value)),
                     g_type_name (G_VALUE_TYPE (iter_value)));
          g_value_unset (&real_value);
        }

      converted = TRUE;
    }

  if (converted)
    {
      g_value_copy (&real_value, value);
      g_value_unset (&real_value);
    }
  else
    g_value_copy (iter_value, value);
}

static void
clutter_list_model_iter_set_value (ClutterModelIter *iter,
                                   guint             column,
                                   const GValue     *value)
{
  ClutterListModelIter *iter_default;
  GValue *values;
  GValue *iter_value;
  GValue real_value = G_VALUE_INIT;
  gboolean converted = FALSE;

  iter_default = CLUTTER_LIST_MODEL_ITER (iter);
  g_assert (iter_default->seq_iter != NULL);

  values = g_sequence_get (iter_default->seq_iter);
  iter_value = &values[column];
  g_assert (iter_value != NULL);

  if (!g_type_is_a (G_VALUE_TYPE (value), G_VALUE_TYPE (iter_value)))
    {
      if (!g_value_type_compatible (G_VALUE_TYPE (value),
                                    G_VALUE_TYPE (iter_value)) &&
          !g_value_type_compatible (G_VALUE_TYPE (iter_value),
                                    G_VALUE_TYPE (value)))
        {
          g_warning ("%s: Unable to convert from %s to %s\n",
                     G_STRLOC,
                     g_type_name (G_VALUE_TYPE (value)),
                     g_type_name (G_VALUE_TYPE (iter_value)));
          return;
        }

      if (!g_value_transform (value, &real_value))
        {
          g_warning ("%s: Unable to make conversion from %s to %s\n",
                     G_STRLOC,
                     g_type_name (G_VALUE_TYPE (value)),
                     g_type_name (G_VALUE_TYPE (iter_value)));
          g_value_unset (&real_value);
        }

      converted = TRUE;
    }

  if (converted)
    {
      g_value_copy (&real_value, iter_value);
      g_value_unset (&real_value);
    }
  else
    g_value_copy (value, iter_value);
}

static gboolean
clutter_list_model_iter_is_first (ClutterModelIter *iter)
{
  ClutterListModelIter *iter_default;
  ClutterModel *model;
  ClutterModelIter *temp_iter;
  GSequence *sequence;
  GSequenceIter *begin, *end;

  iter_default = CLUTTER_LIST_MODEL_ITER (iter);
  g_assert (iter_default->seq_iter != NULL);

  model = clutter_model_iter_get_model (iter);

  sequence = CLUTTER_LIST_MODEL (model)->priv->sequence;

  begin = g_sequence_get_begin_iter (sequence);
  end   = iter_default->seq_iter;

  temp_iter = CLUTTER_LIST_MODEL (model)->priv->temp_iter;

  while (!g_sequence_iter_is_begin (begin))
    {
      CLUTTER_LIST_MODEL_ITER (temp_iter)->seq_iter = begin;

      if (clutter_model_filter_iter (model, temp_iter))
        {
          end = begin;
          break;
        }

      begin = g_sequence_iter_next (begin);
    }

  /* This is because the 'begin_iter' is always *before* the last valid
   * iter, otherwise we'd have endless loops
   */
  end = g_sequence_iter_prev (end);

  return iter_default->seq_iter == end;
}

static gboolean
clutter_list_model_iter_is_last (ClutterModelIter *iter)
{
  ClutterListModelIter *iter_default;
  ClutterModelIter *temp_iter;
  ClutterModel *model;
  GSequence *sequence;
  GSequenceIter *begin, *end;

  iter_default = CLUTTER_LIST_MODEL_ITER (iter);
  g_assert (iter_default->seq_iter != NULL);

  if (g_sequence_iter_is_end (iter_default->seq_iter))
    return TRUE;

  model = clutter_model_iter_get_model (iter);

  sequence = CLUTTER_LIST_MODEL (model)->priv->sequence;

  begin = g_sequence_get_end_iter (sequence);
  begin = g_sequence_iter_prev (begin);
  end   = iter_default->seq_iter;

  temp_iter = CLUTTER_LIST_MODEL (model)->priv->temp_iter;

  while (!g_sequence_iter_is_begin (begin))
    {
      CLUTTER_LIST_MODEL_ITER (temp_iter)->seq_iter = begin;

      if (clutter_model_filter_iter (model, temp_iter))
        {
          end = begin;
          break;
        }

      begin = g_sequence_iter_prev (begin);
    }

  /* This is because the 'end_iter' is always *after* the last valid iter.
   * Otherwise we'd have endless loops
   */
  end = g_sequence_iter_next (end);

  return iter_default->seq_iter == end;
}

static ClutterModelIter *
clutter_list_model_iter_next (ClutterModelIter *iter)
{
  ClutterListModelIter *iter_default;
  ClutterModelIter *temp_iter;
  ClutterModel *model = NULL;
  GSequenceIter *filter_next;
  guint row;

  iter_default = CLUTTER_LIST_MODEL_ITER (iter);
  g_assert (iter_default->seq_iter != NULL);

  model = clutter_model_iter_get_model (iter);
  row   = clutter_model_iter_get_row (iter);

  filter_next = g_sequence_iter_next (iter_default->seq_iter);
  g_assert (filter_next != NULL);

  temp_iter = CLUTTER_LIST_MODEL (model)->priv->temp_iter;

  while (!g_sequence_iter_is_end (filter_next))
    {
      CLUTTER_LIST_MODEL_ITER (temp_iter)->seq_iter = filter_next;

      if (clutter_model_filter_iter (model, temp_iter))
        {
          row += 1;
          break;
        }

      filter_next = g_sequence_iter_next (filter_next);
    }

  if (g_sequence_iter_is_end (filter_next))
    row += 1;

  /* update the iterator and return it */
  _clutter_model_iter_set_row (CLUTTER_MODEL_ITER (iter_default), row);
  iter_default->seq_iter = filter_next;

  return CLUTTER_MODEL_ITER (iter_default);
}

static ClutterModelIter *
clutter_list_model_iter_prev (ClutterModelIter *iter)
{
  ClutterListModelIter *iter_default;
  ClutterModelIter *temp_iter;
  ClutterModel *model;
  GSequenceIter *filter_prev;
  guint row;

  iter_default = CLUTTER_LIST_MODEL_ITER (iter);
  g_assert (iter_default->seq_iter != NULL);

  model = clutter_model_iter_get_model (iter);
  row   = clutter_model_iter_get_row (iter);

  filter_prev = g_sequence_iter_prev (iter_default->seq_iter);
  g_assert (filter_prev != NULL);

  temp_iter = CLUTTER_LIST_MODEL (model)->priv->temp_iter;

  while (!g_sequence_iter_is_begin (filter_prev))
    {
      CLUTTER_LIST_MODEL_ITER (temp_iter)->seq_iter = filter_prev;

      if (clutter_model_filter_iter (model, temp_iter))
        {
          row -= 1;
          break;
        }

      filter_prev = g_sequence_iter_prev (filter_prev);
    }

  if (g_sequence_iter_is_begin (filter_prev))
    row -= 1;

  /* update the iterator and return it */
  _clutter_model_iter_set_row (CLUTTER_MODEL_ITER (iter_default), row);
  iter_default->seq_iter = filter_prev;

  return CLUTTER_MODEL_ITER (iter_default);
}

static ClutterModelIter *
clutter_list_model_iter_copy (ClutterModelIter *iter)
{
  ClutterListModelIter *iter_default;
  ClutterListModelIter *iter_copy;
  ClutterModel *model;
  guint row;

  iter_default = CLUTTER_LIST_MODEL_ITER (iter);

  model = clutter_model_iter_get_model (iter);
  row   = clutter_model_iter_get_row (iter) - 1;

  iter_copy = g_object_new (CLUTTER_TYPE_LIST_MODEL_ITER,
                            "model", model,
                            "row", row,
                            NULL);

  /* this is safe, because the seq_iter pointer on the passed
   * iterator will be always be overwritten in ::next or ::prev
   */
  iter_copy->seq_iter = iter_default->seq_iter;

  return CLUTTER_MODEL_ITER (iter_copy);
}

static void
clutter_list_model_iter_class_init (ClutterListModelIterClass *klass)
{
  ClutterModelIterClass *iter_class = CLUTTER_MODEL_ITER_CLASS (klass);

  iter_class->get_value = clutter_list_model_iter_get_value;
  iter_class->set_value = clutter_list_model_iter_set_value;
  iter_class->is_first  = clutter_list_model_iter_is_first;
  iter_class->is_last   = clutter_list_model_iter_is_last;
  iter_class->next      = clutter_list_model_iter_next;
  iter_class->prev      = clutter_list_model_iter_prev;
  iter_class->copy      = clutter_list_model_iter_copy;
}

static void
clutter_list_model_iter_init (ClutterListModelIter *iter)
{
  iter->seq_iter = NULL;
}

/*
 * ClutterListModel
 */

G_DEFINE_TYPE_WITH_PRIVATE (ClutterListModel, clutter_list_model, CLUTTER_TYPE_MODEL)

static ClutterModelIter *
clutter_list_model_get_iter_at_row (ClutterModel *model,
                                    guint         row)
{
  ClutterListModel *model_default = CLUTTER_LIST_MODEL (model);
  GSequence *sequence = model_default->priv->sequence;
  GSequenceIter *filter_next;
  gint seq_length = g_sequence_get_length (sequence);
  ClutterListModelIter *retval;
  gint count = -1;

  if (row >= seq_length)
    return NULL;

  retval = g_object_new (CLUTTER_TYPE_LIST_MODEL_ITER,
                         "model", model,
                         "row", row,
                         NULL);

  /* short-circuit in case we don't have a filter in place */
  if (!clutter_model_get_filter_set (model))
    {
      retval->seq_iter = g_sequence_get_iter_at_pos (sequence, row);

      return CLUTTER_MODEL_ITER (retval);
    }

  filter_next = g_sequence_get_begin_iter (sequence);
  g_assert (filter_next != NULL);

  while (!g_sequence_iter_is_end (filter_next))
    {
      retval->seq_iter = filter_next;

      if (clutter_model_filter_iter (model, CLUTTER_MODEL_ITER (retval)))
        {
          /* We've found a row that is valid under the filter */
          count++;
          if (count == row)
            break;
        }

      filter_next = g_sequence_iter_next (filter_next);
    }

  if (count != row)
    {
      g_object_unref (retval);
      return NULL;
    }
  return CLUTTER_MODEL_ITER (retval);
}

static ClutterModelIter *
clutter_list_model_insert_row (ClutterModel *model,
                               gint          index_)
{
  ClutterListModel *model_default = CLUTTER_LIST_MODEL (model);
  GSequence *sequence = model_default->priv->sequence;
  ClutterListModelIter *retval;
  guint n_columns, i, pos;
  GValue *values;
  GSequenceIter *seq_iter;

  n_columns = clutter_model_get_n_columns (model);
  values = g_new0 (GValue, n_columns);

  for (i = 0; i < n_columns; i++)
    g_value_init (&values[i], clutter_model_get_column_type (model, i));

  if (index_ < 0)
    {
      seq_iter = g_sequence_append (sequence, values);
      pos = g_sequence_get_length (sequence) - 1;
    }
  else if (index_ == 0)
    {
      seq_iter = g_sequence_prepend (sequence, values);
      pos = 0;
    }
  else
    {
      seq_iter = g_sequence_get_iter_at_pos (sequence, index_);
      seq_iter = g_sequence_insert_before (seq_iter, values);
      pos = index_;
    }

  retval = g_object_new (CLUTTER_TYPE_LIST_MODEL_ITER,
                         "model", model,
                         "row", pos,
                         NULL);
  retval->seq_iter = seq_iter;

  return CLUTTER_MODEL_ITER (retval);
}

static void
clutter_list_model_remove_row (ClutterModel *model,
                               guint         row)
{
  ClutterListModel *model_default = CLUTTER_LIST_MODEL (model);
  GSequence *sequence = model_default->priv->sequence;
  GSequenceIter *seq_iter;
  guint pos = 0;

  seq_iter = g_sequence_get_begin_iter (sequence);
  while (!g_sequence_iter_is_end (seq_iter))
    {
      if (clutter_model_filter_row (model, pos))
        {
          if (pos == row)
            {
              ClutterModelIter *iter;

              iter = g_object_new (CLUTTER_TYPE_LIST_MODEL_ITER,
                                   "model", model,
                                   "row", pos,
                                   NULL);
              CLUTTER_LIST_MODEL_ITER (iter)->seq_iter = seq_iter;

              /* the actual row is removed from the sequence inside
               * the ::row-removed signal class handler, so that every
               * handler connected to ::row-removed will still get
               * a valid iterator, and every signal connected to
               * ::row-removed with the AFTER flag will get an updated
               * model
               */
              g_signal_emit_by_name (model, "row-removed", iter);

              g_object_unref (iter);

              break;
            }
        }

      pos += 1;
      seq_iter = g_sequence_iter_next (seq_iter);
    }
}

typedef struct
{
  ClutterModel *model;
  guint column;
  ClutterModelSortFunc func;
  gpointer data;
} SortClosure;

static gint
sort_model_default (gconstpointer a,
                    gconstpointer b,
                    gpointer      data)
{
  const GValue *row_a = a;
  const GValue *row_b = b;
  SortClosure *clos = data;

  return clos->func (clos->model,
                     &row_a[clos->column],
                     &row_b[clos->column],
                     clos->data);
}

static void
clutter_list_model_resort (ClutterModel         *model,
                           ClutterModelSortFunc  func,
                           gpointer              data)
{
  SortClosure sort_closure = { NULL, 0, NULL, NULL };

  sort_closure.model  = model;
  sort_closure.column = clutter_model_get_sorting_column (model);
  sort_closure.func   = func;
  sort_closure.data   = data;

  g_sequence_sort (CLUTTER_LIST_MODEL (model)->priv->sequence,
                   sort_model_default,
                   &sort_closure);
}

static guint
clutter_list_model_get_n_rows (ClutterModel *model)
{
  ClutterListModel *list_model = CLUTTER_LIST_MODEL (model);

  /* short-circuit in case we don't have a filter in place */
  if (!clutter_model_get_filter_set (model))
    return g_sequence_get_length (list_model->priv->sequence);

  return CLUTTER_MODEL_CLASS (clutter_list_model_parent_class)->get_n_rows (model);
}

static void
clutter_list_model_row_removed (ClutterModel     *model,
                                ClutterModelIter *iter)
{
  ClutterListModelIter *iter_default;
  guint i, n_columns;
  GValue *values;

  n_columns = clutter_model_get_n_columns (model);

  iter_default = CLUTTER_LIST_MODEL_ITER (iter);

  values = g_sequence_get (iter_default->seq_iter);

  for (i = 0; i < n_columns; i++)
    g_value_unset (&values[i]);

  free (values);

  g_sequence_remove (iter_default->seq_iter);
  iter_default->seq_iter = NULL;
}

static void
clutter_list_model_finalize (GObject *gobject)
{
  ClutterListModel *model = CLUTTER_LIST_MODEL (gobject);
  GSequence *sequence = model->priv->sequence;
  GSequenceIter *iter;
  guint n_columns, i;

  n_columns = clutter_model_get_n_columns (CLUTTER_MODEL (gobject));

  iter = g_sequence_get_begin_iter (sequence);
  while (!g_sequence_iter_is_end (iter))
    {
      GValue *values = g_sequence_get (iter);

      for (i = 0; i < n_columns; i++)
        g_value_unset (&values[i]);

      free (values);

      iter = g_sequence_iter_next (iter);
    }
  g_sequence_free (sequence);

  G_OBJECT_CLASS (clutter_list_model_parent_class)->finalize (gobject);
}

static void
clutter_list_model_dispose (GObject *gobject)
{
  ClutterListModel *model = CLUTTER_LIST_MODEL (gobject);

  if (model->priv->temp_iter)
    {
      g_object_unref (model->priv->temp_iter);
      model->priv->temp_iter = NULL;
    }

  G_OBJECT_CLASS (clutter_list_model_parent_class)->dispose (gobject);
}

static void
clutter_list_model_class_init (ClutterListModelClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterModelClass *model_class = CLUTTER_MODEL_CLASS (klass);

  gobject_class->finalize = clutter_list_model_finalize;
  gobject_class->dispose = clutter_list_model_dispose;

  model_class->get_iter_at_row = clutter_list_model_get_iter_at_row;
  model_class->insert_row = clutter_list_model_insert_row;
  model_class->remove_row = clutter_list_model_remove_row;
  model_class->resort = clutter_list_model_resort;
  model_class->get_n_rows = clutter_list_model_get_n_rows;
  model_class->row_removed = clutter_list_model_row_removed;
}

static void
clutter_list_model_init (ClutterListModel *model)
{
  model->priv = clutter_list_model_get_instance_private (model);

  model->priv->sequence = g_sequence_new (NULL);
  model->priv->temp_iter = g_object_new (CLUTTER_TYPE_LIST_MODEL_ITER,
                                         "model",
                                         model,
                                         NULL);
}

/**
 * clutter_list_model_new:
 * @n_columns: number of columns in the model
 * @...: @n_columns number of #GType and string pairs
 *
 * Creates a new default model with @n_columns columns with the types
 * and names passed in.
 *
 * For example:
 *
 * <informalexample><programlisting>
 * model = clutter_list_model_new (3,
 *                                 G_TYPE_INT,      "Score",
 *                                 G_TYPE_STRING,   "Team",
 *                                 GDK_TYPE_PIXBUF, "Logo");
 * </programlisting></informalexample>
 *
 * will create a new #ClutterModel with three columns of type int,
 * string and #GdkPixbuf respectively.
 *
 * Note that the name of the column can be set to %NULL, in which case
 * the canonical name of the type held by the column will be used as
 * the title.
 *
 * Return value: a new #ClutterListModel
 *
 * Since: 0.6
 *
 * Deprecated: 1.24: Use #GListStore instead
 */
ClutterModel *
clutter_list_model_new (guint n_columns,
                        ...)
{
  ClutterModel *model;
  va_list args;
  gint i;

  g_return_val_if_fail (n_columns > 0, NULL);

  model = g_object_new (CLUTTER_TYPE_LIST_MODEL, NULL);
  _clutter_model_set_n_columns (model, n_columns, TRUE, TRUE);

  va_start (args, n_columns);

  for (i = 0; i < n_columns; i++)
    {
      GType type = va_arg (args, GType);
      const gchar *name = va_arg (args, gchar*);

      if (!_clutter_model_check_type (type))
        {
          g_warning ("%s: Invalid type %s\n", G_STRLOC, g_type_name (type));
          g_object_unref (model);
          model = NULL;
          goto out;
        }

      _clutter_model_set_column_type (model, i, type);
      _clutter_model_set_column_name (model, i, name);
    }

 out:
  va_end (args);
  return model;
}

/**
 * clutter_list_model_newv:
 * @n_columns: number of columns in the model
 * @types: (array length=n_columns): an array of #GType types for the columns, from first to last
 * @names: (array length=n_columns): an array of names for the columns, from first to last
 *
 * Non-vararg version of clutter_list_model_new(). This function is
 * useful for language bindings.
 *
 * Return value: (transfer full): a new default #ClutterModel
 *
 * Since: 0.6
 *
 * Deprecated: 1.24: Use #GListStore instead
 */
ClutterModel *
clutter_list_model_newv (guint                n_columns,
                         GType               *types,
                         const gchar * const  names[])
{
  ClutterModel *model;
  gint i;

  g_return_val_if_fail (n_columns > 0, NULL);

  model = g_object_new (CLUTTER_TYPE_LIST_MODEL, NULL);
  _clutter_model_set_n_columns (model, n_columns, TRUE, TRUE);

  for (i = 0; i < n_columns; i++)
    {
      if (!_clutter_model_check_type (types[i]))
        {
          g_warning ("%s: Invalid type %s\n", G_STRLOC, g_type_name (types[i]));
          g_object_unref (model);
          return NULL;
        }

      _clutter_model_set_column_type (model, i, types[i]);
      _clutter_model_set_column_name (model, i, names[i]);
    }

  return model;
}
