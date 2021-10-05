/*
 * Copyright (C) 2018 Red Hat
 * Copyright 2020 DisplayLink (UK) Ltd.
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
 */

#include "config.h"

#include "backends/native/meta-kms-private.h"

#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-impl.h"
#include "backends/native/meta-kms-impl-simple.h"
#include "backends/native/meta-kms-update-private.h"
#include "backends/native/meta-udev.h"
#include "cogl/cogl.h"

/**
 * SECTION:kms
 * @short description: KMS abstraction
 * @title: KMS abstraction
 *
 * The KMS abstraction consists of various building blocks for helping out with
 * interacting with the various drm API's, enabling users to use a
 * transactional API, aiming to hide all interaction with the underlying APIs.
 *
 * The subsystem defines two separate contexts, the "main" context, and the
 * "impl" context. The main context is the context of which mutter as a whole
 * runs in. It uses the main GLib main loop and main context and always runs in
 * the main thread.
 *
 * The impl context is where all underlying API is being executed. While in the
 * current state, it always runs in the main thread, the aim is to be able to
 * execute the impl context in a dedicated thread.
 *
 * The public facing MetaKms API is always assumed to be executed from the main
 * context.
 *
 * The KMS abstraction consists of the following public components:
 *
 * #MetaKms:
 *
 * Main entry point; used by the native backend to create devices, post updates
 * etc.
 *
 * #MetaKmsDevice:
 *
 * A device (usually /dev/dri/cardN, where N being a number). Used to get KMS
 * objects, such as connectors, CRTCs, planes, as well as basic meta data such
 * as device path etc.
 *
 * #MetaKmsCrtc:
 *
 * Represents a CRTC. It manages a representation of the current CRTC state,
 * including current mode, coordinates, possible clones.
 *
 * #MetaKmsConnector:
 *
 * Represents a connector, e.g. a display port connection. It also manages a
 * representation of the current state, including meta data such as physical
 * dimension of the connected, available modes, EDID, tile info etc. It also
 * contains helper functions for configuration, as well as methods for adding
 * configuration to a transaction (See #MetaKmsUpdate).
 *
 * #MetaKmsPlane:
 *
 * Represents a hardware plane. A plane is used to define the content of what
 * should be presented on a CRTC. Planes can either be primary planes, used as
 * a backdrop for CRTCs, overlay planes, and cursor planes.
 *
 * #MetaKmsUpdate:
 *
 * A KMS transaction object, meant to be processed potentially atomically when
 * posted. An update consists of plane assignments, mode sets and KMS object
 * property entries. The user adds updates to the object, and then posts it via
 * MetaKms. It will then be processed by the MetaKms backend (See
 * #MetaKmsImpl), potentially atomically.
 *
 *
 * There are also these private objects, without public facing API:
 *
 * #MetaKmsImpl:
 *
 * The KMS backend implementation, running in the impl context. #MetaKmsImpl
 * itself is an abstract object, with potentially multiple implementations.
 * Currently only #MetaKmsImplSimple exists.
 *
 * #MetaKmsImplSimple:
 *
 * A KMS backend implementation using the non-atomic drmMode* API. While it's
 * interacted with using the transactional API, the #MetaKmsUpdate is processed
 * non-atomically.
 *
 * #MetaKmsImplDevice:
 *
 * An object linked to a #MetaKmsDevice, but where it is executed in the impl
 * context. It takes care of the updating of the various KMS object (CRTC,
 * connector, ..) states.
 *
 * #MetaKmsPageFlip:
 *
 * A object representing a page flip. It's created when a page flip is queued,
 * and contains information necessary to provide feedback to the one requesting
 * the page flip.
 *
 */

enum
{
  RESOURCES_CHANGED,

  N_SIGNALS
};

static int signals[N_SIGNALS];

typedef struct _MetaKmsCallbackData
{
  MetaKmsCallback callback;
  gpointer user_data;
  GDestroyNotify user_data_destroy;
} MetaKmsCallbackData;

typedef struct _MetaKmsSimpleImplSource
{
  GSource source;
  MetaKms *kms;
} MetaKmsSimpleImplSource;

typedef struct _MetaKmsFdImplSource
{
  GSource source;

  gpointer fd_tag;
  MetaKms *kms;

  MetaKmsImplTaskFunc dispatch;
  gpointer user_data;
} MetaKmsFdImplSource;

struct _MetaKms
{
  GObject parent;

  MetaBackend *backend;

  gulong hotplug_handler_id;
  gulong removed_handler_id;

  MetaKmsImpl *impl;
  gboolean in_impl_task;
  gboolean waiting_for_impl_task;

  GList *devices;

  MetaKmsUpdate *pending_update;

  GList *pending_callbacks;
  guint callback_source_id;
};

G_DEFINE_TYPE (MetaKms, meta_kms, G_TYPE_OBJECT)

MetaKmsUpdate *
meta_kms_ensure_pending_update (MetaKms *kms)
{
  if (!kms->pending_update)
    kms->pending_update = meta_kms_update_new ();

  return meta_kms_get_pending_update (kms);
}

MetaKmsUpdate *
meta_kms_get_pending_update (MetaKms *kms)
{
  return kms->pending_update;
}

static void
meta_kms_predict_states_in_impl (MetaKms       *kms,
                                 MetaKmsUpdate *update)
{
  meta_assert_in_kms_impl (kms);

  g_list_foreach (kms->devices,
                  (GFunc) meta_kms_device_predict_states_in_impl,
                  update);
}

static gpointer
meta_kms_process_update_in_impl (MetaKmsImpl  *impl,
                                 gpointer      user_data,
                                 GError      **error)
{
  g_autoptr (MetaKmsUpdate) update = user_data;
  MetaKmsFeedback *feedback;

  feedback = meta_kms_impl_process_update (impl, update);
  meta_kms_predict_states_in_impl (meta_kms_impl_get_kms (impl), update);

  return feedback;
}

static MetaKmsFeedback *
meta_kms_post_update_sync (MetaKms       *kms,
                           MetaKmsUpdate *update)
{
  meta_kms_update_seal (update);

  COGL_TRACE_BEGIN_SCOPED (MetaKmsPostUpdateSync,
                           "KMS (post update)");

  return meta_kms_run_impl_task_sync (kms,
                                      meta_kms_process_update_in_impl,
                                      update,
                                      NULL);
}

MetaKmsFeedback *
meta_kms_post_pending_update_sync (MetaKms *kms)
{
  return meta_kms_post_update_sync (kms,
                                    g_steal_pointer (&kms->pending_update));
}

static gpointer
meta_kms_discard_pending_page_flips_in_impl (MetaKmsImpl  *impl,
                                             gpointer      user_data,
                                             GError      **error)
{
  meta_kms_impl_discard_pending_page_flips (impl);
  return GINT_TO_POINTER (TRUE);
}

void
meta_kms_discard_pending_page_flips (MetaKms *kms)
{
  meta_kms_run_impl_task_sync (kms,
                               meta_kms_discard_pending_page_flips_in_impl,
                               NULL,
                               NULL);
}

static void
meta_kms_callback_data_free (MetaKmsCallbackData *callback_data)
{
  if (callback_data->user_data_destroy)
    callback_data->user_data_destroy (callback_data->user_data);
  g_slice_free (MetaKmsCallbackData, callback_data);
}

static int
flush_callbacks (MetaKms *kms)
{
  GList *l;
  int callback_count = 0;

  meta_assert_not_in_kms_impl (kms);

  for (l = kms->pending_callbacks; l; l = l->next)
    {
      MetaKmsCallbackData *callback_data = l->data;

      callback_data->callback (kms, callback_data->user_data);
      meta_kms_callback_data_free (callback_data);
      callback_count++;
    }

  g_list_free (kms->pending_callbacks);
  kms->pending_callbacks = NULL;

  return callback_count;
}

static gboolean
callback_idle (gpointer user_data)
{
  MetaKms *kms = user_data;

  flush_callbacks (kms);

  kms->callback_source_id = 0;
  return G_SOURCE_REMOVE;
}

void
meta_kms_queue_callback (MetaKms         *kms,
                         MetaKmsCallback  callback,
                         gpointer         user_data,
                         GDestroyNotify   user_data_destroy)
{
  MetaKmsCallbackData *callback_data;

  callback_data = g_slice_new0 (MetaKmsCallbackData);
  *callback_data = (MetaKmsCallbackData) {
    .callback = callback,
    .user_data = user_data,
    .user_data_destroy = user_data_destroy,
  };
  kms->pending_callbacks = g_list_append (kms->pending_callbacks,
                                          callback_data);
  if (!kms->callback_source_id)
    kms->callback_source_id = g_idle_add (callback_idle, kms);
}

int
meta_kms_flush_callbacks (MetaKms *kms)
{
  int callback_count;

  callback_count = flush_callbacks (kms);
  g_clear_handle_id (&kms->callback_source_id, g_source_remove);

  return callback_count;
}

gpointer
meta_kms_run_impl_task_sync (MetaKms              *kms,
                             MetaKmsImplTaskFunc   func,
                             gpointer              user_data,
                             GError              **error)
{
  gpointer ret;

  kms->in_impl_task = TRUE;
  kms->waiting_for_impl_task = TRUE;
  ret = func (kms->impl, user_data, error);
  kms->waiting_for_impl_task = FALSE;
  kms->in_impl_task = FALSE;

  return ret;
}

static gboolean
simple_impl_source_dispatch (GSource     *source,
                             GSourceFunc  callback,
                             gpointer     user_data)
{
  MetaKmsSimpleImplSource *simple_impl_source =
    (MetaKmsSimpleImplSource *) source;
  MetaKms *kms = simple_impl_source->kms;
  gboolean ret;

  kms->in_impl_task = TRUE;
  ret = callback (user_data);
  kms->in_impl_task = FALSE;

  return ret;
}

static GSourceFuncs simple_impl_source_funcs = {
  .dispatch = simple_impl_source_dispatch,
};

GSource *
meta_kms_add_source_in_impl (MetaKms        *kms,
                             GSourceFunc     func,
                             gpointer        user_data,
                             GDestroyNotify  user_data_destroy)
{
  GSource *source;
  MetaKmsSimpleImplSource *simple_impl_source;

  meta_assert_in_kms_impl (kms);

  source = g_source_new (&simple_impl_source_funcs,
                         sizeof (MetaKmsSimpleImplSource));
  simple_impl_source = (MetaKmsSimpleImplSource *) source;
  simple_impl_source->kms = kms;

  g_source_set_callback (source, func, user_data, user_data_destroy);
  g_source_set_ready_time (source, 0);
  g_source_attach (source, g_main_context_get_thread_default ());

  return source;
}

static gboolean
meta_kms_fd_impl_source_check (GSource *source)
{
  MetaKmsFdImplSource *fd_impl_source = (MetaKmsFdImplSource *) source;

  return g_source_query_unix_fd (source, fd_impl_source->fd_tag) & G_IO_IN;
}

static gboolean
meta_kms_fd_impl_source_dispatch (GSource     *source,
                                  GSourceFunc  callback,
                                  gpointer     user_data)
{
  MetaKmsFdImplSource *fd_impl_source = (MetaKmsFdImplSource *) source;
  MetaKms *kms = fd_impl_source->kms;
  gpointer ret;
  GError *error = NULL;

  kms->in_impl_task = TRUE;
  ret = fd_impl_source->dispatch (kms->impl,
                                  fd_impl_source->user_data,
                                  &error);
  kms->in_impl_task = FALSE;

  if (!GPOINTER_TO_INT (ret))
    {
      g_warning ("Failed to dispatch fd source: %s", error->message);
      g_error_free (error);
    }

  return G_SOURCE_CONTINUE;
}

static GSourceFuncs fd_impl_source_funcs = {
  NULL,
  meta_kms_fd_impl_source_check,
  meta_kms_fd_impl_source_dispatch
};

GSource *
meta_kms_register_fd_in_impl (MetaKms             *kms,
                              int                  fd,
                              MetaKmsImplTaskFunc  dispatch,
                              gpointer             user_data)
{
  GSource *source;
  MetaKmsFdImplSource *fd_impl_source;

  meta_assert_in_kms_impl (kms);

  source = g_source_new (&fd_impl_source_funcs, sizeof (MetaKmsFdImplSource));
  fd_impl_source = (MetaKmsFdImplSource *) source;
  fd_impl_source->dispatch = dispatch;
  fd_impl_source->user_data = user_data;
  fd_impl_source->kms = kms;
  fd_impl_source->fd_tag = g_source_add_unix_fd (source, fd,
                                                 G_IO_IN | G_IO_ERR);

  g_source_attach (source, g_main_context_get_thread_default ());

  return source;
}

gboolean
meta_kms_in_impl_task (MetaKms *kms)
{
  return kms->in_impl_task;
}

gboolean
meta_kms_is_waiting_for_impl_task (MetaKms *kms)
{
  return kms->waiting_for_impl_task;
}

static void
meta_kms_update_states_in_impl (MetaKms *kms)
{
  COGL_TRACE_BEGIN_SCOPED (MetaKmsUpdateStates,
                           "KMS (update states)");

  meta_assert_in_kms_impl (kms);

  g_list_foreach (kms->devices,
                  (GFunc) meta_kms_device_update_states_in_impl,
                  NULL);
}

static gpointer
update_states_in_impl (MetaKmsImpl  *impl,
                       gpointer      user_data,
                       GError      **error)
{
  MetaKms *kms = meta_kms_impl_get_kms (impl);;

  meta_kms_update_states_in_impl (kms);

  return GINT_TO_POINTER (TRUE);
}

static gboolean
meta_kms_update_states_sync (MetaKms  *kms,
                             GError  **error)
{
  gpointer ret;

  ret = meta_kms_run_impl_task_sync (kms, update_states_in_impl, NULL, error);
  return GPOINTER_TO_INT (ret);
}

static void
handle_hotplug_event (MetaKms *kms)
{
  g_autoptr (GError) error = NULL;

  if (!meta_kms_update_states_sync (kms, &error))
    g_warning ("Updating KMS state failed: %s", error->message);

  g_signal_emit (kms, signals[RESOURCES_CHANGED], 0);
}

static void
on_udev_hotplug (MetaUdev *udev,
                 MetaKms  *kms)
{
  handle_hotplug_event (kms);
}

static void
on_udev_device_removed (MetaUdev    *udev,
                        GUdevDevice *device,
                        MetaKms     *kms)
{
  handle_hotplug_event (kms);
}

MetaBackend *
meta_kms_get_backend (MetaKms *kms)
{
  return kms->backend;
}

static gpointer
notify_device_created_in_impl (MetaKmsImpl  *impl,
                               gpointer      user_data,
                               GError      **error)
{
  MetaKmsDevice *device = user_data;

  meta_kms_impl_notify_device_created (impl, device);

  return GINT_TO_POINTER (TRUE);
}

MetaKmsDevice *
meta_kms_create_device (MetaKms            *kms,
                        const char         *path,
                        MetaKmsDeviceFlag   flags,
                        GError            **error)
{
  MetaKmsDevice *device;

  device = meta_kms_device_new (kms, path, flags, error);
  if (!device)
    return NULL;

  meta_kms_run_impl_task_sync (kms, notify_device_created_in_impl,
                               device, NULL);

  kms->devices = g_list_append (kms->devices, device);

  return device;
}

MetaKms *
meta_kms_new (MetaBackend  *backend,
              GError      **error)
{
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaUdev *udev = meta_backend_native_get_udev (backend_native);
  MetaKms *kms;

  kms = g_object_new (META_TYPE_KMS, NULL);
  kms->backend = backend;
  kms->impl = META_KMS_IMPL (meta_kms_impl_simple_new (kms, error));
  if (!kms->impl)
    {
      g_object_unref (kms);
      return NULL;
    }

  kms->hotplug_handler_id =
    g_signal_connect (udev, "hotplug", G_CALLBACK (on_udev_hotplug), kms);
  kms->removed_handler_id =
    g_signal_connect (udev, "device-removed",
                      G_CALLBACK (on_udev_device_removed), kms);

  return kms;
}

static void
meta_kms_finalize (GObject *object)
{
  MetaKms *kms = META_KMS (object);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (kms->backend);
  MetaUdev *udev = meta_backend_native_get_udev (backend_native);
  GList *l;

  for (l = kms->pending_callbacks; l; l = l->next)
    meta_kms_callback_data_free (l->data);
  g_list_free (kms->pending_callbacks);

  g_clear_handle_id (&kms->callback_source_id, g_source_remove);

  g_list_free_full (kms->devices, g_object_unref);

  g_clear_signal_handler (&kms->hotplug_handler_id, udev);
  g_clear_signal_handler (&kms->removed_handler_id, udev);

  G_OBJECT_CLASS (meta_kms_parent_class)->finalize (object);
}

static void
meta_kms_init (MetaKms *kms)
{
}

static void
meta_kms_class_init (MetaKmsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_kms_finalize;

  signals[RESOURCES_CHANGED] =
    g_signal_new ("resources-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}
