/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2010 Intel Corporation.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 *
 * Authors:
 *   Damien Lespiau <damien.lespiau@intel.com>
 *   Robert Bragg <robert@linux.intel.com>
 */

/* For an overview of the functionality implemented here, please see
 * cogl-buffer.h, which contains the gtk-doc section overview for the
 * Pixel Buffers API.
 */

#ifdef HAVE_CONFIG_H
#include "cogl-config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <glib.h>

#include "cogl-util.h"
#include "cogl-context-private.h"
#include "cogl-object-private.h"
#include "cogl-pixel-buffer-private.h"

/* XXX:
 * The CoglObject macros don't support any form of inheritance, so for
 * now we implement the CoglObject support for the CoglBuffer
 * abstract class manually.
 */

static GSList *_cogl_buffer_types;

void
_cogl_buffer_register_buffer_type (const CoglObjectClass *klass)
{
  _cogl_buffer_types = g_slist_prepend (_cogl_buffer_types, (void *) klass);
}

CoglBool
cogl_is_buffer (void *object)
{
  const CoglObject *obj = object;
  GSList *l;

  if (object == NULL)
    return FALSE;

  for (l = _cogl_buffer_types; l; l = l->next)
    if (l->data == obj->klass)
      return TRUE;

  return FALSE;
}

/*
 * Fallback path, buffer->data points to a malloc'ed buffer.
 */

static void *
malloc_map_range (CoglBuffer *buffer,
                  size_t offset,
                  size_t size,
                  CoglBufferAccess access,
                  CoglBufferMapHint hints,
                  CoglError **error)
{
  buffer->flags |= COGL_BUFFER_FLAG_MAPPED;
  return buffer->data + offset;
}

static void
malloc_unmap (CoglBuffer *buffer)
{
  buffer->flags &= ~COGL_BUFFER_FLAG_MAPPED;
}

static CoglBool
malloc_set_data (CoglBuffer *buffer,
                 unsigned int offset,
                 const void *data,
                 unsigned int size,
                 CoglError **error)
{
  memcpy (buffer->data + offset, data, size);
  return TRUE;
}

void
_cogl_buffer_initialize (CoglBuffer *buffer,
                         CoglContext *ctx,
                         size_t size,
                         CoglBufferBindTarget default_target,
                         CoglBufferUsageHint usage_hint,
                         CoglBufferUpdateHint update_hint)
{
  CoglBool use_malloc = FALSE;

  buffer->context = ctx;
  buffer->flags = COGL_BUFFER_FLAG_NONE;
  buffer->store_created = FALSE;
  buffer->size = size;
  buffer->last_target = default_target;
  buffer->usage_hint = usage_hint;
  buffer->update_hint = update_hint;
  buffer->data = NULL;
  buffer->immutable_ref = 0;

  if (default_target == COGL_BUFFER_BIND_TARGET_PIXEL_PACK ||
      default_target == COGL_BUFFER_BIND_TARGET_PIXEL_UNPACK)
    {
      if (!_cogl_has_private_feature (ctx, COGL_PRIVATE_FEATURE_PBOS))
        use_malloc = TRUE;
    }
  else if (default_target == COGL_BUFFER_BIND_TARGET_ATTRIBUTE_BUFFER ||
           default_target == COGL_BUFFER_BIND_TARGET_INDEX_BUFFER)
    {
      if (!_cogl_has_private_feature (ctx, COGL_PRIVATE_FEATURE_VBOS))
        use_malloc = TRUE;
    }

  if (use_malloc)
    {
      buffer->vtable.map_range = malloc_map_range;
      buffer->vtable.unmap = malloc_unmap;
      buffer->vtable.set_data = malloc_set_data;

      buffer->data = malloc (size);
    }
  else
    {
      buffer->vtable.map_range = ctx->driver_vtable->buffer_map_range;
      buffer->vtable.unmap = ctx->driver_vtable->buffer_unmap;
      buffer->vtable.set_data = ctx->driver_vtable->buffer_set_data;

      ctx->driver_vtable->buffer_create (buffer);

      buffer->flags |= COGL_BUFFER_FLAG_BUFFER_OBJECT;
    }
}

void
_cogl_buffer_fini (CoglBuffer *buffer)
{
  _COGL_RETURN_IF_FAIL (!(buffer->flags & COGL_BUFFER_FLAG_MAPPED));
  _COGL_RETURN_IF_FAIL (buffer->immutable_ref == 0);

  if (buffer->flags & COGL_BUFFER_FLAG_BUFFER_OBJECT)
    buffer->context->driver_vtable->buffer_destroy (buffer);
  else
    free (buffer->data);
}

unsigned int
cogl_buffer_get_size (CoglBuffer *buffer)
{
  if (!cogl_is_buffer (buffer))
    return 0;

  return COGL_BUFFER (buffer)->size;
}

void
cogl_buffer_set_update_hint (CoglBuffer *buffer,
                             CoglBufferUpdateHint hint)
{
  if (!cogl_is_buffer (buffer))
    return;

  if (G_UNLIKELY (hint > COGL_BUFFER_UPDATE_HINT_STREAM))
    hint = COGL_BUFFER_UPDATE_HINT_STATIC;

  buffer->update_hint = hint;
}

CoglBufferUpdateHint
cogl_buffer_get_update_hint (CoglBuffer *buffer)
{
  if (!cogl_is_buffer (buffer))
    return FALSE;

  return buffer->update_hint;
}

static void
warn_about_midscene_changes (void)
{
  static CoglBool seen = FALSE;
  if (!seen)
    {
      g_warning ("Mid-scene modification of buffers has "
                 "undefined results\n");
      seen = TRUE;
    }
}

void *
_cogl_buffer_map (CoglBuffer *buffer,
                  CoglBufferAccess access,
                  CoglBufferMapHint hints,
                  CoglError **error)
{
  _COGL_RETURN_VAL_IF_FAIL (cogl_is_buffer (buffer), NULL);

  return cogl_buffer_map_range (buffer, 0, buffer->size, access, hints, error);
}

void *
cogl_buffer_map (CoglBuffer *buffer,
                 CoglBufferAccess access,
                 CoglBufferMapHint hints)
{
  CoglError *ignore_error = NULL;
  void *ptr =
    cogl_buffer_map_range (buffer, 0, buffer->size, access, hints,
                           &ignore_error);
  if (!ptr)
    cogl_error_free (ignore_error);
  return ptr;
}

void *
cogl_buffer_map_range (CoglBuffer *buffer,
                       size_t offset,
                       size_t size,
                       CoglBufferAccess access,
                       CoglBufferMapHint hints,
                       CoglError **error)
{
  _COGL_RETURN_VAL_IF_FAIL (cogl_is_buffer (buffer), NULL);
  _COGL_RETURN_VAL_IF_FAIL (!(buffer->flags & COGL_BUFFER_FLAG_MAPPED), NULL);

  if (G_UNLIKELY (buffer->immutable_ref))
    warn_about_midscene_changes ();

  buffer->data = buffer->vtable.map_range (buffer,
                                           offset,
                                           size,
                                           access,
                                           hints,
                                           error);

  return buffer->data;
}

void
cogl_buffer_unmap (CoglBuffer *buffer)
{
  if (!cogl_is_buffer (buffer))
    return;

  if (!(buffer->flags & COGL_BUFFER_FLAG_MAPPED))
    return;

  buffer->vtable.unmap (buffer);
}

void *
_cogl_buffer_map_for_fill_or_fallback (CoglBuffer *buffer)
{
  return _cogl_buffer_map_range_for_fill_or_fallback (buffer, 0, buffer->size);
}

void *
_cogl_buffer_map_range_for_fill_or_fallback (CoglBuffer *buffer,
                                             size_t offset,
                                             size_t size)
{
  CoglContext *ctx = buffer->context;
  void *ret;
  CoglError *ignore_error = NULL;

  _COGL_RETURN_VAL_IF_FAIL (!ctx->buffer_map_fallback_in_use, NULL);

  ctx->buffer_map_fallback_in_use = TRUE;

  ret = cogl_buffer_map_range (buffer,
                               offset,
                               size,
                               COGL_BUFFER_ACCESS_WRITE,
                               COGL_BUFFER_MAP_HINT_DISCARD,
                               &ignore_error);

  if (ret)
    return ret;

  cogl_error_free (ignore_error);

  /* If the map fails then we'll use a temporary buffer to fill
     the data and then upload it using cogl_buffer_set_data when
     the buffer is unmapped. The temporary buffer is shared to
     avoid reallocating it every time */
  g_byte_array_set_size (ctx->buffer_map_fallback_array, size);
  ctx->buffer_map_fallback_offset = offset;

  buffer->flags |= COGL_BUFFER_FLAG_MAPPED_FALLBACK;

  return ctx->buffer_map_fallback_array->data;
}

void
_cogl_buffer_unmap_for_fill_or_fallback (CoglBuffer *buffer)
{
  CoglContext *ctx = buffer->context;

  _COGL_RETURN_IF_FAIL (ctx->buffer_map_fallback_in_use);

  ctx->buffer_map_fallback_in_use = FALSE;

  if ((buffer->flags & COGL_BUFFER_FLAG_MAPPED_FALLBACK))
    {
      /* Note: don't try to catch OOM errors here since the use cases
       * we currently have for this api (the journal and path stroke
       * tesselator) don't have anything particularly sensible they
       * can do in response to a failure anyway so it seems better to
       * simply abort instead.
       *
       * If we find this is a problem for real world applications
       * then in the path tesselation case we could potentially add an
       * explicit cogl_path_tesselate_stroke() api that can throw an
       * error for the app to cache. For the journal we could
       * potentially flush the journal in smaller batches so we use
       * smaller buffers, though that would probably not help for
       * deferred renderers.
       */
      _cogl_buffer_set_data (buffer,
                             ctx->buffer_map_fallback_offset,
                             ctx->buffer_map_fallback_array->data,
                             ctx->buffer_map_fallback_array->len,
                             NULL);
      buffer->flags &= ~COGL_BUFFER_FLAG_MAPPED_FALLBACK;
    }
  else
    cogl_buffer_unmap (buffer);
}

CoglBool
_cogl_buffer_set_data (CoglBuffer *buffer,
                       size_t offset,
                       const void *data,
                       size_t size,
                       CoglError **error)
{
  _COGL_RETURN_VAL_IF_FAIL (cogl_is_buffer (buffer), FALSE);
  _COGL_RETURN_VAL_IF_FAIL ((offset + size) <= buffer->size, FALSE);

  if (G_UNLIKELY (buffer->immutable_ref))
    warn_about_midscene_changes ();

  return buffer->vtable.set_data (buffer, offset, data, size, error);
}

CoglBool
cogl_buffer_set_data (CoglBuffer *buffer,
                      size_t offset,
                      const void *data,
                      size_t size)
{
  CoglError *ignore_error = NULL;
  CoglBool status =
    _cogl_buffer_set_data (buffer, offset, data, size, &ignore_error);
  if (!status)
    cogl_error_free (ignore_error);
  return status;
}

CoglBuffer *
_cogl_buffer_immutable_ref (CoglBuffer *buffer)
{
  _COGL_RETURN_VAL_IF_FAIL (cogl_is_buffer (buffer), NULL);

  buffer->immutable_ref++;
  return buffer;
}

void
_cogl_buffer_immutable_unref (CoglBuffer *buffer)
{
  _COGL_RETURN_IF_FAIL (cogl_is_buffer (buffer));
  _COGL_RETURN_IF_FAIL (buffer->immutable_ref > 0);

  buffer->immutable_ref--;
}

