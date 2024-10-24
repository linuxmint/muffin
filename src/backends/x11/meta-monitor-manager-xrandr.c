/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001, 2002 Havoc Pennington
 * Copyright (C) 2002, 2003 Red Hat Inc.
 * Some ICCCM manager selection code derived from fvwm2,
 * Copyright (C) 2001 Dominik Vogt, Matthias Clasen, and fvwm2 team
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
 * Copyright (C) 2013 Red Hat Inc.
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION:meta-monitor-manager-xrandr
 * @title: MetaMonitorManagerXrandr
 * @short_description: A subclass of #MetaMonitorManager using XRadR
 *
 * #MetaMonitorManagerXrandr is a subclass of #MetaMonitorManager which
 * implements its functionality using the RandR X protocol.
 *
 * See also #MetaMonitorManagerKms for a native implementation using Linux DRM
 * and udev.
 */

#include "config.h"

#include "backends/x11/meta-monitor-manager-xrandr.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib-xcb.h>
#include <X11/Xlibint.h>
#include <X11/extensions/dpms.h>
#include <xcb/randr.h>

#include "backends/meta-crtc.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor-config-manager.h"
#include "backends/meta-output.h"
#include "backends/x11/meta-backend-x11.h"
#include "backends/x11/meta-crtc-xrandr.h"
#include "backends/x11/meta-gpu-xrandr.h"
#include "backends/x11/meta-output-xrandr.h"
#include "clutter/clutter.h"
#include "meta/main.h"
#include "meta/meta-x11-errors.h"

/* Look for DPI_FALLBACK in:
 * http://git.gnome.org/browse/gnome-settings-daemon/tree/plugins/xsettings/gsd-xsettings-manager.c
 * for the reasoning */
#define DPI_FALLBACK 96.0
#define RANDR_VERSION_FORMAT(major, minor) ((major * 100) + minor)
#define RANDR_TILING_MIN_VERSION RANDR_VERSION_FORMAT (1, 5)
#define RANDR_TRANSFORM_MIN_VERSION RANDR_VERSION_FORMAT (1, 3)

struct _MetaMonitorManagerXrandr
{
  MetaMonitorManager parent_instance;

  Display *xdisplay;
  int rr_event_base;
  int rr_error_base;
  int randr_version;

  xcb_timestamp_t last_xrandr_set_timestamp;

  GHashTable *tiled_monitor_atoms;

};

static MetaGpu * meta_monitor_manager_xrandr_get_gpu (MetaMonitorManagerXrandr *manager_xrandr);

struct _MetaMonitorManagerXrandrClass
{
  MetaMonitorManagerClass parent_class;
};

G_DEFINE_TYPE (MetaMonitorManagerXrandr, meta_monitor_manager_xrandr, META_TYPE_MONITOR_MANAGER);

typedef struct _MetaMonitorXrandrData
{
  Atom xrandr_name;
} MetaMonitorXrandrData;

GQuark quark_meta_monitor_xrandr_data;

Display *
meta_monitor_manager_xrandr_get_xdisplay (MetaMonitorManagerXrandr *manager_xrandr)
{
  return manager_xrandr->xdisplay;
}

uint32_t
meta_monitor_manager_xrandr_get_config_timestamp (MetaMonitorManagerXrandr *manager_xrandr)
{
  return manager_xrandr->last_xrandr_set_timestamp;
}

static GBytes *
meta_monitor_manager_xrandr_read_edid (MetaMonitorManager *manager,
                                       MetaOutput         *output)
{
  return meta_output_xrandr_read_edid (output);
}

static MetaPowerSave
x11_dpms_state_to_power_save (CARD16 dpms_state)
{
  switch (dpms_state)
    {
    case DPMSModeOn:
      return META_POWER_SAVE_ON;
    case DPMSModeStandby:
      return META_POWER_SAVE_STANDBY;
    case DPMSModeSuspend:
      return META_POWER_SAVE_SUSPEND;
    case DPMSModeOff:
      return META_POWER_SAVE_OFF;
    default:
      return META_POWER_SAVE_UNSUPPORTED;
    }
}

static void
meta_monitor_manager_xrandr_read_current_state (MetaMonitorManager *manager)
{
  MetaMonitorManagerXrandr *manager_xrandr =
    META_MONITOR_MANAGER_XRANDR (manager);
  MetaMonitorManagerClass *parent_class =
    META_MONITOR_MANAGER_CLASS (meta_monitor_manager_xrandr_parent_class);
  Display *xdisplay = meta_monitor_manager_xrandr_get_xdisplay (manager_xrandr);
  BOOL dpms_capable, dpms_enabled;
  CARD16 dpms_state;
  MetaPowerSave power_save_mode;

  dpms_capable = DPMSCapable (xdisplay);

  if (dpms_capable &&
      DPMSInfo (xdisplay, &dpms_state, &dpms_enabled) &&
      dpms_enabled)
    power_save_mode = x11_dpms_state_to_power_save (dpms_state);
  else
    power_save_mode = META_POWER_SAVE_UNSUPPORTED;

  meta_monitor_manager_power_save_mode_changed (manager, power_save_mode);

  parent_class->read_current_state (manager);
}

static void
meta_monitor_manager_xrandr_set_power_save_mode (MetaMonitorManager *manager,
						 MetaPowerSave       mode)
{
  MetaMonitorManagerXrandr *manager_xrandr = META_MONITOR_MANAGER_XRANDR (manager);
  CARD16 state;

  switch (mode) {
  case META_POWER_SAVE_ON:
    state = DPMSModeOn;
    break;
  case META_POWER_SAVE_STANDBY:
    state = DPMSModeStandby;
    break;
  case META_POWER_SAVE_SUSPEND:
    state = DPMSModeSuspend;
    break;
  case META_POWER_SAVE_OFF:
    state = DPMSModeOff;
    break;
  default:
    return;
  }

  DPMSForceLevel (manager_xrandr->xdisplay, state);
  DPMSSetTimeouts (manager_xrandr->xdisplay, 0, 0, 0);
}

static void
meta_monitor_manager_xrandr_update_screen_size (MetaMonitorManagerXrandr *manager_xrandr,
                                                int                       width,
                                                int                       height,
                                                float                     scale)
{
  MetaMonitorManager *manager = META_MONITOR_MANAGER (manager_xrandr);
  MetaGpu *gpu = meta_monitor_manager_xrandr_get_gpu (manager_xrandr);
  xcb_connection_t *xcb_conn;
  xcb_generic_error_t *xcb_error;
  xcb_void_cookie_t xcb_cookie;
  Screen *screen;
  int min_width;
  int min_height;
  int max_width;
  int max_height;
  int width_mm;
  int height_mm;

  g_assert (width > 0 && height > 0 && scale > 0);

  if (manager->screen_width == width && manager->screen_height == height)
    return;

  screen = ScreenOfDisplay (manager_xrandr->xdisplay,
                            DefaultScreen (manager_xrandr->xdisplay));
  meta_gpu_xrandr_get_min_screen_size (META_GPU_XRANDR (gpu),
                                       &min_width, &min_height);
  meta_gpu_xrandr_get_max_screen_size (META_GPU_XRANDR (gpu),
                                       &max_width, &max_height);
  width = MIN (MAX (min_width, width), max_width);
  height = MIN (MAX (min_height, height), max_height);

  /* The 'physical size' of an X screen is meaningless if that screen can
   * consist of many monitors. So just pick a size that make the dpi 96.
   *
   * Firefox and Evince apparently believe what X tells them.
   */
  width_mm = (width / (DPI_FALLBACK * scale)) * 25.4 + 0.5;
  height_mm = (height / (DPI_FALLBACK * scale)) * 25.4 + 0.5;

  if (width == WidthOfScreen (screen) && height == HeightOfScreen (screen) &&
      width_mm == WidthMMOfScreen (screen) && height_mm == HeightMMOfScreen (screen))
    return;

  xcb_conn = XGetXCBConnection (manager_xrandr->xdisplay);

  xcb_grab_server (xcb_conn);

  /* Some drivers (nvidia I look at you!) might no advertise some CRTCs, so in
   * such case, we may ignore X errors here */
  xcb_cookie = xcb_randr_set_screen_size_checked (xcb_conn,
                                                  DefaultRootWindow (manager_xrandr->xdisplay),
                                                  width, height,
                                                  width_mm, height_mm);
  xcb_error = xcb_request_check (xcb_conn, xcb_cookie);
  if (!xcb_error)
    {
      manager->screen_width = width;
      manager->screen_height = height;
    }
  else
    {
      gchar buf[64];

      XGetErrorText (manager_xrandr->xdisplay, xcb_error->error_code, buf,
                     sizeof (buf) - 1);
      g_warning ("Impossible to resize screen at size %dx%d, error id %u: %s",
                 width, height, xcb_error->error_code, buf);
      g_clear_pointer (&xcb_error, free);
    }

  xcb_ungrab_server (xcb_conn);
}

static xcb_randr_rotation_t
meta_monitor_transform_to_xrandr (MetaMonitorTransform transform)
{
  switch (transform)
    {
    case META_MONITOR_TRANSFORM_NORMAL:
      return XCB_RANDR_ROTATION_ROTATE_0;
    case META_MONITOR_TRANSFORM_90:
      return XCB_RANDR_ROTATION_ROTATE_90;
    case META_MONITOR_TRANSFORM_180:
      return XCB_RANDR_ROTATION_ROTATE_180;
    case META_MONITOR_TRANSFORM_270:
      return XCB_RANDR_ROTATION_ROTATE_270;
    case META_MONITOR_TRANSFORM_FLIPPED:
      return XCB_RANDR_ROTATION_REFLECT_X | XCB_RANDR_ROTATION_ROTATE_0;
    case META_MONITOR_TRANSFORM_FLIPPED_90:
      return XCB_RANDR_ROTATION_REFLECT_X | XCB_RANDR_ROTATION_ROTATE_90;
    case META_MONITOR_TRANSFORM_FLIPPED_180:
      return XCB_RANDR_ROTATION_REFLECT_X | XCB_RANDR_ROTATION_ROTATE_180;
    case META_MONITOR_TRANSFORM_FLIPPED_270:
      return XCB_RANDR_ROTATION_REFLECT_X | XCB_RANDR_ROTATION_ROTATE_270;
    }

  g_assert_not_reached ();
  return 0;
}

static gboolean
xrandr_set_crtc_config (MetaMonitorManagerXrandr *manager_xrandr,
                        MetaCrtc                 *crtc,
                        gboolean                  save_timestamp,
                        xcb_randr_crtc_t          xrandr_crtc,
                        xcb_timestamp_t           timestamp,
                        int                       x,
                        int                       y,
                        xcb_randr_mode_t          mode,
                        xcb_randr_rotation_t      rotation,
                        xcb_randr_output_t       *outputs,
                        int                       n_outputs)
{
  xcb_timestamp_t new_timestamp;

  if (!meta_crtc_xrandr_set_config (crtc, xrandr_crtc, timestamp,
                                    x, y, mode, rotation,
                                    outputs, n_outputs,
                                    &new_timestamp))
    return FALSE;

  if (save_timestamp)
    manager_xrandr->last_xrandr_set_timestamp = new_timestamp;

  return TRUE;
}

static float
get_maximum_crtc_info_scale (MetaCrtcInfo **crtc_infos,
                             unsigned int   n_crtc_infos)
{
  float max_scale = 1.0f;
  unsigned int i;

  for (i = 0; i < n_crtc_infos; i++)
    {
      MetaCrtcInfo *crtc_info = crtc_infos[i];

      if (crtc_info->mode)
        max_scale = MAX (max_scale, crtc_info->scale);
    }

  return max_scale;
}

static gboolean
is_crtc_assignment_changed (MetaMonitorManager *monitor_manager,
                            MetaCrtc           *crtc,
                            MetaCrtcInfo      **crtc_infos,
                            unsigned int        n_crtc_infos,
                            gboolean           *weak_change)
{
  MetaLogicalMonitorLayoutMode layout_mode;
  gboolean have_scaling;
  float max_crtc_scale = 1.0f;
  float max_req_scale = 1.0f;
  unsigned int i;

  layout_mode = meta_monitor_manager_get_default_layout_mode (monitor_manager);
  have_scaling = meta_monitor_manager_get_capabilities (monitor_manager) &
                 META_MONITOR_MANAGER_CAPABILITY_NATIVE_OUTPUT_SCALING;

  if (have_scaling &&
      layout_mode == META_LOGICAL_MONITOR_LAYOUT_MODE_GLOBAL_UI_LOGICAL)
    {
      max_crtc_scale =
        meta_monitor_manager_get_maximum_crtc_scale (monitor_manager);
      max_req_scale =
        get_maximum_crtc_info_scale (crtc_infos, n_crtc_infos);
    }

  for (i = 0; i < n_crtc_infos; i++)
    {
      MetaCrtcInfo *crtc_info = crtc_infos[i];

      if (crtc_info->crtc != crtc)
        continue;

      if (meta_crtc_xrandr_is_assignment_changed (crtc, crtc_info))
        return TRUE;

      if (have_scaling)
        {
          float crtc_scale = crtc->scale;
          float req_output_scale = crtc_info->scale;

          if (layout_mode == META_LOGICAL_MONITOR_LAYOUT_MODE_PHYSICAL)
            {
              if (fmodf (crtc_scale, 1.0) == 0.0f)
                {
                  *weak_change = fabsf (crtc_scale - req_output_scale) > 0.001;
                  return FALSE;
                }
            }
          else if (layout_mode == META_LOGICAL_MONITOR_LAYOUT_MODE_GLOBAL_UI_LOGICAL)
            {
              /* In scale ui-down mode we need to check if the actual output
               * scale that will be applied to the crtc has actually changed
               * from the current value, so we need to compare the current crtc
               * scale with the scale that will be applied taking care of the
               * UI scale (max crtc scale) and of the requested maximum scale.
               * If we don't do this, we'd try to call randr calls which won't
               * ever trigger a RRScreenChangeNotify, as no actual change is
               * needed, and thus we won't ever emit a monitors-changed signal.
               */
              crtc_scale /= ceilf (max_crtc_scale);
              req_output_scale /= ceilf (max_req_scale);
            }

          if (fabsf (crtc_scale - req_output_scale) > 0.001)
            return TRUE;
        }

      return FALSE;
    }

  return !!meta_crtc_xrandr_get_current_mode (crtc);
}

static gboolean
is_output_assignment_changed (MetaOutput      *output,
                              MetaCrtcInfo   **crtc_infos,
                              unsigned int     n_crtc_infos,
                              MetaOutputInfo **output_infos,
                              unsigned int     n_output_infos)
{
  MetaCrtc *assigned_crtc;
  gboolean output_is_found = FALSE;
  unsigned int i;

  for (i = 0; i < n_output_infos; i++)
    {
      MetaOutputInfo *output_info = output_infos[i];

      if (output_info->output != output)
        continue;

      if (output->is_primary != output_info->is_primary)
        return TRUE;

      if (output->is_presentation != output_info->is_presentation)
        return TRUE;

      if (output->is_underscanning != output_info->is_underscanning)
        return TRUE;

      output_is_found = TRUE;
    }

  assigned_crtc = meta_output_get_assigned_crtc (output);

  if (!output_is_found)
    return assigned_crtc != NULL;

  for (i = 0; i < n_crtc_infos; i++)
    {
      MetaCrtcInfo *crtc_info = crtc_infos[i];
      unsigned int j;

      for (j = 0; j < crtc_info->outputs->len; j++)
        {
          MetaOutput *crtc_info_output =
            ((MetaOutput**) crtc_info->outputs->pdata)[j];

          if (crtc_info_output == output &&
              crtc_info->crtc == assigned_crtc)
            return FALSE;
        }
    }

  return TRUE;
}

static MetaGpu *
meta_monitor_manager_xrandr_get_gpu (MetaMonitorManagerXrandr *manager_xrandr)
{
  MetaMonitorManager *manager = META_MONITOR_MANAGER (manager_xrandr);
  MetaBackend *backend = meta_monitor_manager_get_backend (manager);

  return META_GPU (meta_backend_get_gpus (backend)->data);
}

static gboolean
is_assignments_changed (MetaMonitorManager *manager,
                        MetaCrtcInfo      **crtc_infos,
                        unsigned int        n_crtc_infos,
                        MetaOutputInfo    **output_infos,
                        unsigned int        n_output_infos,
                        gboolean           *weak_change)
{
  MetaMonitorManagerXrandr *manager_xrandr =
    META_MONITOR_MANAGER_XRANDR (manager);
  MetaGpu *gpu = meta_monitor_manager_xrandr_get_gpu (manager_xrandr);
  GList *l;

  for (l = meta_gpu_get_crtcs (gpu); l; l = l->next)
    {
      MetaCrtc *crtc = l->data;

      if (is_crtc_assignment_changed (manager, crtc, crtc_infos, n_crtc_infos, weak_change))
        return TRUE;
    }

  for (l = meta_gpu_get_outputs (gpu); l; l = l->next)
    {
      MetaOutput *output = l->data;

      if (is_output_assignment_changed (output,
                                        crtc_infos,
                                        n_crtc_infos,
                                        output_infos,
                                        n_output_infos))
        return TRUE;
    }

  if (meta_monitor_manager_get_default_layout_mode (manager) ==
      META_LOGICAL_MONITOR_LAYOUT_MODE_GLOBAL_UI_LOGICAL)
    {
      /* If nothing has changed, ensure that the crtc logical scaling matches
       * with the requested one, as in case of global UI logical layout we might
       * assume that it is in fact equal, while it's techincally different.
       * Not doing this would then cause a wrong computation of the max crtc
       * scale and thus of the UI scaling. */
      for (l = meta_gpu_get_crtcs (gpu); l; l = l->next)
        {
          MetaCrtc *crtc = l->data;
          unsigned int i;

          for (i = 0; i < n_crtc_infos; i++)
            {
              MetaCrtcInfo *crtc_info = crtc_infos[i];

              if (crtc_info->crtc == crtc)
                {
                  crtc->scale = crtc_info->scale;
                  break;
                }
            }
        }
    }

  return FALSE;
}

static void
apply_crtc_assignments (MetaMonitorManager *manager,
                        gboolean            save_timestamp,
                        MetaCrtcInfo      **crtcs,
                        unsigned int        n_crtcs,
                        MetaOutputInfo    **outputs,
                        unsigned int        n_outputs)
{
  MetaMonitorManagerXrandr *manager_xrandr = META_MONITOR_MANAGER_XRANDR (manager);
  MetaGpu *gpu = meta_monitor_manager_xrandr_get_gpu (manager_xrandr);
  MetaBackend *backend = meta_monitor_manager_get_backend (manager);
  MetaSettings *settings = meta_backend_get_settings (backend);
  MetaX11ScaleMode scale_mode = meta_settings_get_x11_scale_mode (settings);
  unsigned i, valid_crtcs;
  GList *l;
  int width, height, scaled_width, scaled_height;
  float max_scale;
  float avg_screen_scale;
  gboolean have_scaling;

  XGrabServer (manager_xrandr->xdisplay);

  have_scaling = meta_monitor_manager_get_capabilities (manager) &
                 META_MONITOR_MANAGER_CAPABILITY_NATIVE_OUTPUT_SCALING;

  /* Compute the new size of the screen (framebuffer) */
  max_scale = get_maximum_crtc_info_scale (crtcs, n_crtcs);
  width = 0; height = 0;
  scaled_width = 0; scaled_height = 0;
  avg_screen_scale = 0;
  valid_crtcs = 0;
  for (i = 0; i < n_crtcs; i++)
    {
      MetaCrtcInfo *crtc_info = crtcs[i];
      MetaCrtc *crtc = crtc_info->crtc;
      float scale = 1.0f;

      crtc->is_dirty = TRUE;

      if (crtc_info->mode == NULL)
        continue;

      if (have_scaling && scale_mode == META_X11_SCALE_MODE_UI_DOWN)
        scale = (ceilf (max_scale) / crtc_info->scale) * crtc_info->scale;
      else
        {
          scaled_width = MAX (scaled_width, crtc_info->layout.origin.x +
                              crtc_info->layout.size.width * crtc_info->scale);
          scaled_height = MAX (scaled_height, crtc_info->layout.origin.y +
                               crtc_info->layout.size.height * crtc_info->scale);
        }

      width = MAX (width, (int) roundf (crtc_info->layout.origin.x +
                                        crtc_info->layout.size.width * scale));
      height = MAX (height, (int) roundf (crtc_info->layout.origin.y +
                                          crtc_info->layout.size.height * scale));

      avg_screen_scale += (crtc_info->scale - avg_screen_scale) /
                          (float) (++valid_crtcs);
    }

  /* Second disable all newly disabled CRTCs, or CRTCs that in the previous
     configuration would be outside the new framebuffer (otherwise X complains
     loudly when resizing)
     CRTC will be enabled again after resizing the FB
  */
  for (i = 0; i < n_crtcs; i++)
    {
      MetaCrtcInfo *crtc_info = crtcs[i];
      MetaCrtc *crtc = crtc_info->crtc;
      MetaCrtcConfig *crtc_config;
      int x2, y2;

      crtc_config = crtc->config;
      if (!crtc_config)
        continue;

      x2 = (int) roundf (crtc_config->layout.origin.x +
                         crtc_config->layout.size.width);
      y2 = (int) roundf (crtc_config->layout.origin.y +
                         crtc_config->layout.size.height);

      if (!crtc_info->mode || width < scaled_width || height < scaled_height || x2 > width || y2 > height)
        {
          xrandr_set_crtc_config (manager_xrandr,
                                  crtc,
                                  save_timestamp,
                                  (xcb_randr_crtc_t) crtc->crtc_id,
                                  XCB_CURRENT_TIME,
                                  0, 0, XCB_NONE,
                                  XCB_RANDR_ROTATION_ROTATE_0,
                                  NULL, 0);
          if (have_scaling)
            meta_crtc_xrandr_set_scale (crtc,
                                        (xcb_randr_crtc_t) crtc->crtc_id, 1.0f);

          meta_crtc_unset_config (crtc);
          crtc->scale = 1.0f;
        }
    }

  /* Disable CRTCs not mentioned in the list */
  for (l = meta_gpu_get_crtcs (gpu); l; l = l->next)
    {
      MetaCrtc *crtc = l->data;

      if (crtc->is_dirty)
        {
          crtc->is_dirty = FALSE;
          continue;
        }

      if (!crtc->config)
        continue;

      xrandr_set_crtc_config (manager_xrandr,
                              crtc,
                              save_timestamp,
                              (xcb_randr_crtc_t) crtc->crtc_id,
                              XCB_CURRENT_TIME,
                              0, 0, XCB_NONE,
                              XCB_RANDR_ROTATION_ROTATE_0,
                              NULL, 0);
      if (have_scaling)
        meta_crtc_xrandr_set_scale (crtc,
                                    (xcb_randr_crtc_t) crtc->crtc_id, 1.0f);

      meta_crtc_unset_config (crtc);
      crtc->scale = 1.0f;
    }

  if (!n_crtcs)
    goto out;

  if (width > 0 && height > 0)
    {
      meta_monitor_manager_xrandr_update_screen_size (manager_xrandr,
                                                      width, height,
                                                      avg_screen_scale);
    }

  for (i = 0; i < n_crtcs; i++)
    {
      MetaCrtcInfo *crtc_info = crtcs[i];
      MetaCrtc *crtc = crtc_info->crtc;

      if (crtc_info->mode != NULL)
        {
          MetaCrtcMode *mode;
          g_autofree xcb_randr_output_t *output_ids = NULL;
          unsigned int j, n_output_ids;
          xcb_randr_rotation_t rotation;
          float scale = 1.0f;

          mode = crtc_info->mode;

          n_output_ids = crtc_info->outputs->len;
          output_ids = g_new (xcb_randr_output_t, n_output_ids);

          if (have_scaling && scale_mode != META_X11_SCALE_MODE_NONE)
            {
              scale = crtc_info->scale;

              if (scale_mode == META_X11_SCALE_MODE_UI_DOWN)
                scale /= ceilf (max_scale);
            }

          for (j = 0; j < n_output_ids; j++)
            {
              MetaOutput *output;

              output = ((MetaOutput**)crtc_info->outputs->pdata)[j];

              output->is_dirty = TRUE;
              meta_output_assign_crtc (output, crtc);

              output_ids[j] = output->winsys_id;
            }

          if (have_scaling)
            {
              if (!meta_crtc_xrandr_set_scale (crtc,
                                               (xcb_randr_crtc_t) crtc->crtc_id,
                                               scale))
                {
                  meta_warning ("Scalig CRTC %d at %f failed\n",
                                (unsigned)crtc->crtc_id, scale);
                }
            }

          rotation = meta_monitor_transform_to_xrandr (crtc_info->transform);
          if (!xrandr_set_crtc_config (manager_xrandr,
                                       crtc,
                                       save_timestamp,
                                       (xcb_randr_crtc_t) crtc->crtc_id,
                                       XCB_CURRENT_TIME,
                                       (int) roundf (crtc_info->layout.origin.x),
                                       (int) roundf (crtc_info->layout.origin.y),
                                       (xcb_randr_mode_t) mode->mode_id,
                                       rotation,
                                       output_ids, n_output_ids))
            {
              meta_warning ("Configuring CRTC %d with mode %d (%d x %d @ %f) at position %d, %d and transform %u failed\n",
                            (unsigned)(crtc->crtc_id), (unsigned)(mode->mode_id),
                            mode->width, mode->height, (float)mode->refresh_rate,
                            (int) roundf (crtc_info->layout.origin.x),
                            (int) roundf (crtc_info->layout.origin.y),
                            crtc_info->transform);
              continue;
            }

          meta_crtc_set_config (crtc,
                                &crtc_info->layout,
                                mode,
                                crtc_info->transform);
          crtc->scale = crtc_info->scale;

          if (have_scaling && scale_mode == META_X11_SCALE_MODE_UI_DOWN)
            {
              scale = (ceilf (max_scale) / crtc_info->scale) * crtc_info->scale;

              crtc->config->layout.size.width =
                roundf (crtc->config->layout.size.width * scale);
              crtc->config->layout.size.height =
                roundf (crtc->config->layout.size.height * scale);
            }
        }
    }

  for (i = 0; i < n_outputs; i++)
    {
      MetaOutputInfo *output_info = outputs[i];
      MetaOutput *output = output_info->output;

      output->is_primary = output_info->is_primary;
      output->is_presentation = output_info->is_presentation;
      output->is_underscanning = output_info->is_underscanning;

      meta_output_xrandr_apply_mode (output);
    }

  /* Disable outputs not mentioned in the list */
  for (l = meta_gpu_get_outputs (gpu); l; l = l->next)
    {
      MetaOutput *output = l->data;

      if (output->is_dirty)
        {
          output->is_dirty = FALSE;
          continue;
        }

      meta_output_unassign_crtc (output);
      output->is_primary = FALSE;
    }

out:
  XUngrabServer (manager_xrandr->xdisplay);
  XFlush (manager_xrandr->xdisplay);
}

static void
meta_monitor_manager_xrandr_ensure_initial_config (MetaMonitorManager *manager)
{
  MetaMonitorConfigManager *config_manager =
    meta_monitor_manager_get_config_manager (manager);
  MetaMonitorsConfig *config;

  meta_monitor_manager_ensure_configured (manager);

  /*
   * Normally we don't rebuild our data structures until we see the
   * RRScreenNotify event, but at least at startup we want to have the right
   * configuration immediately.
   */
  meta_monitor_manager_read_current_state (manager);

  config = meta_monitor_config_manager_get_current (config_manager);
  meta_monitor_manager_update_logical_state_derived (manager, config);
}

static void
meta_monitor_manager_xrandr_update_screen_size_derived (MetaMonitorManager *manager,
                                                        MetaMonitorsConfig *config)
{
  MetaMonitorManagerXrandr *manager_xrandr =
    META_MONITOR_MANAGER_XRANDR (manager);
  MetaBackend *backend = meta_monitor_manager_get_backend (manager);
  MetaSettings *settings = meta_backend_get_settings (backend);
  MetaX11ScaleMode scale_mode = meta_settings_get_x11_scale_mode (settings);
  int screen_width = 0;
  int screen_height = 0;
  unsigned n_crtcs = 0;
  float average_scale = 0;
  gboolean have_scaling;
  GList *l;

  have_scaling = meta_monitor_manager_get_capabilities (manager) &
                 META_MONITOR_MANAGER_CAPABILITY_NATIVE_OUTPUT_SCALING;

  /* Compute the new size of the screen (framebuffer) */
  for (l = manager->monitors; l != NULL; l = l->next)
    {
      MetaMonitor *monitor = l->data;
      MetaOutput *output = meta_monitor_get_main_output (monitor);
      MetaCrtc *crtc = meta_output_get_assigned_crtc (output);
      graphene_rect_t *crtc_layout;
      float scale = 1.0f;

      if (!crtc || !crtc->config)
        continue;

      if (!have_scaling)
        {
          /* When scaling up we should not reduce the screen size, or X will
           * fail miserably, while we must do it when scaling down, in order to
           * increase the available screen area we can use. */
          scale = crtc->scale > 1.0f ? crtc->scale : 1.0f;
        }

      /* When computing the screen size from the crtc rects we don't have to
       * use inverted values when monitors are rotated, because this is already
       * taken in account in the crtc rectangles */
      crtc_layout = &crtc->config->layout;
      screen_width = MAX (screen_width, crtc_layout->origin.x +
                          roundf (crtc_layout->size.width * scale));
      screen_height = MAX (screen_height, crtc_layout->origin.y +
                           roundf (crtc_layout->size.height * scale));
      ++n_crtcs;

      /* This value isn't completely exact, since it doesn't take care of the
       * actual crtc sizes, however, since w're going to use this only to set
       * the MM size of the screen, and given that this value is just an
       * estimation, we don't need to be super precise. */
      average_scale += (crtc->scale - average_scale) / (float) n_crtcs;
    }

  if (screen_width > 0 && screen_height > 0)
    {
      meta_monitor_manager_xrandr_update_screen_size (manager_xrandr,
                                                      screen_width,
                                                      screen_height,
                                                      average_scale);
    }
}

static void
maybe_update_ui_scaling_factor (MetaMonitorManager *manager,
                                MetaMonitorsConfig *config)
{
  if (config->layout_mode == META_LOGICAL_MONITOR_LAYOUT_MODE_GLOBAL_UI_LOGICAL ||
      manager->layout_mode == META_LOGICAL_MONITOR_LAYOUT_MODE_GLOBAL_UI_LOGICAL)
    {
      MetaBackend *backend = meta_monitor_manager_get_backend (manager);
      MetaSettings *settings = meta_backend_get_settings (backend);

      meta_settings_update_ui_scaling_factor (settings);
    }
}

static gboolean
meta_monitor_manager_xrandr_apply_monitors_config (MetaMonitorManager      *manager,
                                                   MetaMonitorsConfig      *config,
                                                   MetaMonitorsConfigMethod method,
                                                   GError                 **error)
{
  GPtrArray *crtc_infos;
  GPtrArray *output_infos;

  if (!config)
    {
      if (!manager->in_init)
        apply_crtc_assignments (manager, TRUE, NULL, 0, NULL, 0);

      meta_monitor_manager_rebuild_derived (manager, NULL);
      return TRUE;
    }

  if (!meta_monitor_config_manager_assign (manager, config,
                                           &crtc_infos, &output_infos,
                                           error))
    return FALSE;

  if (method != META_MONITORS_CONFIG_METHOD_VERIFY)
    {
      gboolean weak_change = FALSE;

      /*
       * If the assignment has not changed, we won't get any notification about
       * any new configuration from the X server; but we still need to update
       * our own configuration, as something not applicable in Xrandr might
       * have changed locally, such as the logical monitors scale. This means we
       * must check that our new assignment actually changes anything, otherwise
       * just update the logical state.
       * If we record a weak change it means that only UI scaling needs to be
       * updated and so that we don't have to reconfigure the CRTCs, but still
       * need to update the logical state.
       */
      if (is_assignments_changed (manager,
                                  (MetaCrtcInfo **) crtc_infos->pdata,
                                  crtc_infos->len,
                                  (MetaOutputInfo **) output_infos->pdata,
                                  output_infos->len,
                                  &weak_change))
        {
          apply_crtc_assignments (manager,
                                  TRUE,
                                  (MetaCrtcInfo **) crtc_infos->pdata,
                                  crtc_infos->len,
                                  (MetaOutputInfo **) output_infos->pdata,
                                  output_infos->len);

          maybe_update_ui_scaling_factor (manager, config);
        }
      else
        {
          if (weak_change)
            maybe_update_ui_scaling_factor (manager, config);

          meta_monitor_manager_rebuild_derived (manager, config);
        }
    }

  g_ptr_array_free (crtc_infos, TRUE);
  g_ptr_array_free (output_infos, TRUE);

  return TRUE;
}

static void
meta_monitor_manager_xrandr_change_backlight (MetaMonitorManager *manager,
					      MetaOutput         *output,
					      gint                value)
{
  meta_output_xrandr_change_backlight (output, value);
}

static void
meta_monitor_manager_xrandr_get_crtc_gamma (MetaMonitorManager  *manager,
					    MetaCrtc            *crtc,
					    gsize               *size,
					    unsigned short     **red,
					    unsigned short     **green,
					    unsigned short     **blue)
{
  MetaMonitorManagerXrandr *manager_xrandr = META_MONITOR_MANAGER_XRANDR (manager);
  XRRCrtcGamma *gamma;

  gamma = XRRGetCrtcGamma (manager_xrandr->xdisplay, (XID)crtc->crtc_id);

  *size = gamma->size;
  *red = g_memdup2 (gamma->red, sizeof (unsigned short) * gamma->size);
  *green = g_memdup2 (gamma->green, sizeof (unsigned short) * gamma->size);
  *blue = g_memdup2 (gamma->blue, sizeof (unsigned short) * gamma->size);

  XRRFreeGamma (gamma);
}

static void
meta_monitor_manager_xrandr_set_crtc_gamma (MetaMonitorManager *manager,
					    MetaCrtc           *crtc,
					    gsize               size,
					    unsigned short     *red,
					    unsigned short     *green,
					    unsigned short     *blue)
{
  MetaMonitorManagerXrandr *manager_xrandr = META_MONITOR_MANAGER_XRANDR (manager);
  XRRCrtcGamma *gamma;

  gamma = XRRAllocGamma (size);
  memcpy (gamma->red, red, sizeof (unsigned short) * size);
  memcpy (gamma->green, green, sizeof (unsigned short) * size);
  memcpy (gamma->blue, blue, sizeof (unsigned short) * size);

  XRRSetCrtcGamma (manager_xrandr->xdisplay, (XID)crtc->crtc_id, gamma);

  XRRFreeGamma (gamma);
}

static MetaMonitorXrandrData *
meta_monitor_xrandr_data_from_monitor (MetaMonitor *monitor)
{
  MetaMonitorXrandrData *monitor_xrandr_data;

  monitor_xrandr_data = g_object_get_qdata (G_OBJECT (monitor),
                                            quark_meta_monitor_xrandr_data);
  if (monitor_xrandr_data)
    return monitor_xrandr_data;

  monitor_xrandr_data = g_new0 (MetaMonitorXrandrData, 1);
  g_object_set_qdata_full (G_OBJECT (monitor),
                           quark_meta_monitor_xrandr_data,
                           monitor_xrandr_data,
                           g_free);

  return monitor_xrandr_data;
}

static void
meta_monitor_manager_xrandr_increase_monitor_count (MetaMonitorManagerXrandr *manager_xrandr,
                                                    Atom                      name_atom)
{
  int count;

  count =
    GPOINTER_TO_INT (g_hash_table_lookup (manager_xrandr->tiled_monitor_atoms,
                                          GSIZE_TO_POINTER (name_atom)));

  count++;
  g_hash_table_insert (manager_xrandr->tiled_monitor_atoms,
                       GSIZE_TO_POINTER (name_atom),
                       GINT_TO_POINTER (count));
}

static int
meta_monitor_manager_xrandr_decrease_monitor_count (MetaMonitorManagerXrandr *manager_xrandr,
                                                    Atom                      name_atom)
{
  int count;

  count =
    GPOINTER_TO_SIZE (g_hash_table_lookup (manager_xrandr->tiled_monitor_atoms,
                                           GSIZE_TO_POINTER (name_atom)));
  g_assert (count > 0);

  count--;
  g_hash_table_insert (manager_xrandr->tiled_monitor_atoms,
                       GSIZE_TO_POINTER (name_atom),
                       GINT_TO_POINTER (count));

  return count;
}

static void
meta_monitor_manager_xrandr_tiled_monitor_added (MetaMonitorManager *manager,
                                                 MetaMonitor        *monitor)
{
  MetaMonitorManagerXrandr *manager_xrandr = META_MONITOR_MANAGER_XRANDR (manager);
  MetaMonitorTiled *monitor_tiled = META_MONITOR_TILED (monitor);
  const char *product;
  char *name;
  uint32_t tile_group_id;
  MetaMonitorXrandrData *monitor_xrandr_data;
  Atom name_atom;
  XRRMonitorInfo *xrandr_monitor_info;
  GList *outputs;
  GList *l;
  int i;
  xcb_connection_t *xcb_conn;
  xcb_void_cookie_t cookie;

  if (!(meta_monitor_manager_get_capabilities (manager) &
        META_MONITOR_MANAGER_CAPABILITY_TILING))
    return;

  product = meta_monitor_get_product (monitor);
  tile_group_id = meta_monitor_tiled_get_tile_group_id (monitor_tiled);

  if (product)
    name = g_strdup_printf ("%s-%d", product, tile_group_id);
  else
    name = g_strdup_printf ("Tiled-%d", tile_group_id);

  name_atom = XInternAtom (manager_xrandr->xdisplay, name, False);
  g_free (name);

  monitor_xrandr_data = meta_monitor_xrandr_data_from_monitor (monitor);
  monitor_xrandr_data->xrandr_name = name_atom;

  meta_monitor_manager_xrandr_increase_monitor_count (manager_xrandr,
                                                      name_atom);

  outputs = meta_monitor_get_outputs (monitor);
  xrandr_monitor_info = XRRAllocateMonitor (manager_xrandr->xdisplay,
                                            g_list_length (outputs));
  xrandr_monitor_info->name = name_atom;
  xrandr_monitor_info->primary = meta_monitor_is_primary (monitor);
  xrandr_monitor_info->automatic = True;
  for (l = outputs, i = 0; l; l = l->next, i++)
    {
      MetaOutput *output = l->data;

      xrandr_monitor_info->outputs[i] = output->winsys_id;
    }

  xcb_conn = XGetXCBConnection (manager_xrandr->xdisplay);
  cookie = xcb_randr_delete_monitor_checked (xcb_conn,
                                             DefaultRootWindow (manager_xrandr->xdisplay),
                                             name_atom);
  free (xcb_request_check (xcb_conn, cookie)); /* ignore DeleteMonitor errors */
  XRRSetMonitor (manager_xrandr->xdisplay,
                 DefaultRootWindow (manager_xrandr->xdisplay),
                 xrandr_monitor_info);
  XRRFreeMonitors (xrandr_monitor_info);
}

static void
meta_monitor_manager_xrandr_tiled_monitor_removed (MetaMonitorManager *manager,
                                                   MetaMonitor        *monitor)
{
  MetaMonitorManagerXrandr *manager_xrandr = META_MONITOR_MANAGER_XRANDR (manager);
  MetaMonitorXrandrData *monitor_xrandr_data;
  Atom monitor_name;

  int monitor_count;

  if (!(meta_monitor_manager_get_capabilities (manager) &
        META_MONITOR_MANAGER_CAPABILITY_TILING))
    return;

  monitor_xrandr_data = meta_monitor_xrandr_data_from_monitor (monitor);
  monitor_name = monitor_xrandr_data->xrandr_name;
  monitor_count =
    meta_monitor_manager_xrandr_decrease_monitor_count (manager_xrandr,
                                                        monitor_name);

  if (monitor_count == 0)
    XRRDeleteMonitor (manager_xrandr->xdisplay,
                      DefaultRootWindow (manager_xrandr->xdisplay),
                      monitor_name);
}

static void
meta_monitor_manager_xrandr_init_monitors (MetaMonitorManagerXrandr *manager_xrandr)
{
  MetaMonitorManager *manager = META_MONITOR_MANAGER (manager_xrandr);
  XRRMonitorInfo *m;
  int n, i;

  if (!(meta_monitor_manager_get_capabilities (manager) &
        META_MONITOR_MANAGER_CAPABILITY_TILING))
    return;

  /* delete any tiled monitors setup, as mutter will want to recreate
     things in its image */
  m = XRRGetMonitors (manager_xrandr->xdisplay,
                      DefaultRootWindow (manager_xrandr->xdisplay),
                      FALSE, &n);
  if (n == -1)
    return;

  for (i = 0; i < n; i++)
    {
      if (m[i].noutput > 1)
        XRRDeleteMonitor (manager_xrandr->xdisplay,
                          DefaultRootWindow (manager_xrandr->xdisplay),
                          m[i].name);
    }
  XRRFreeMonitors (m);
}

static gboolean
meta_monitor_manager_xrandr_is_transform_handled (MetaMonitorManager  *manager,
                                                  MetaCrtc            *crtc,
                                                  MetaMonitorTransform transform)
{
  g_warn_if_fail ((crtc->all_transforms & transform) == transform);

  return TRUE;
}

static MetaMonitorScalesConstraint
get_scale_constraints (MetaMonitorManager *manager)
{
  MetaMonitorScalesConstraint constraints = 0;

  if (meta_monitor_manager_get_capabilities (manager) &
      META_MONITOR_MANAGER_CAPABILITY_GLOBAL_SCALE_REQUIRED)
    constraints |= META_MONITOR_SCALES_CONSTRAINT_NO_FRAC;

  return constraints;
}

static float
meta_monitor_manager_xrandr_calculate_monitor_mode_scale (MetaMonitorManager           *manager,
                                                          MetaLogicalMonitorLayoutMode  layout_mode,
                                                          MetaMonitor                  *monitor,
                                                          MetaMonitorMode              *monitor_mode)
{
  return meta_monitor_calculate_mode_scale (monitor, monitor_mode,
                                            get_scale_constraints (manager));
}

static float *
meta_monitor_manager_xrandr_calculate_supported_scales (MetaMonitorManager           *manager,
                                                        MetaLogicalMonitorLayoutMode  layout_mode,
                                                        MetaMonitor                  *monitor,
                                                        MetaMonitorMode              *monitor_mode,
                                                        int                          *n_supported_scales)
{
  return meta_monitor_calculate_supported_scales (monitor, monitor_mode,
                                                  get_scale_constraints (manager),
                                                  n_supported_scales);
}

static MetaMonitorManagerCapability
meta_monitor_manager_xrandr_get_capabilities (MetaMonitorManager *manager)
{
  MetaMonitorManagerCapability capabilities;
  MetaMonitorManagerXrandr *xrandr_manager = META_MONITOR_MANAGER_XRANDR (manager);
  MetaBackend *backend = meta_monitor_manager_get_backend (manager);
  MetaSettings *settings = meta_backend_get_settings (backend);

  capabilities = META_MONITOR_MANAGER_CAPABILITY_NONE;

  if (xrandr_manager->randr_version >= RANDR_TILING_MIN_VERSION)
    capabilities |= META_MONITOR_MANAGER_CAPABILITY_TILING;

  if (xrandr_manager->randr_version >= RANDR_TRANSFORM_MIN_VERSION)
    capabilities |= META_MONITOR_MANAGER_CAPABILITY_NATIVE_OUTPUT_SCALING;

  if (meta_settings_is_experimental_feature_enabled (settings,
        META_EXPERIMENTAL_FEATURE_X11_RANDR_FRACTIONAL_SCALING))
    {
      capabilities |= META_MONITOR_MANAGER_CAPABILITY_LAYOUT_MODE;
    }
  else
    {
      capabilities |= META_MONITOR_MANAGER_CAPABILITY_GLOBAL_SCALE_REQUIRED;
    }

  return capabilities;
}

static gboolean
meta_monitor_manager_xrandr_get_max_screen_size (MetaMonitorManager *manager,
                                                 int                *max_width,
                                                 int                *max_height)
{
  MetaMonitorManagerXrandr *manager_xrandr =
    META_MONITOR_MANAGER_XRANDR (manager);
  MetaGpu *gpu = meta_monitor_manager_xrandr_get_gpu (manager_xrandr);

  meta_gpu_xrandr_get_max_screen_size (META_GPU_XRANDR (gpu),
                                       max_width, max_height);

  return TRUE;
}

static void
scale_mode_changed (MetaSettings       *settings,
                    MetaMonitorManager *manager)
{
  if (!(meta_monitor_manager_get_capabilities (manager) &
        META_MONITOR_MANAGER_CAPABILITY_NATIVE_OUTPUT_SCALING))
    return;

  if (!meta_settings_is_experimental_feature_enabled (settings,
      META_EXPERIMENTAL_FEATURE_X11_RANDR_FRACTIONAL_SCALING))
    return;

  meta_monitor_manager_on_hotplug (manager);
  meta_settings_update_ui_scaling_factor (settings);
}

static MetaLogicalMonitorLayoutMode
meta_monitor_manager_xrandr_get_default_layout_mode (MetaMonitorManager *manager)
{
  MetaMonitorManagerCapability capabilities =
    meta_monitor_manager_get_capabilities (manager);

  if ((capabilities & META_MONITOR_MANAGER_CAPABILITY_NATIVE_OUTPUT_SCALING) &&
      (capabilities & META_MONITOR_MANAGER_CAPABILITY_LAYOUT_MODE))
    {
      MetaBackend *backend = meta_monitor_manager_get_backend (manager);
      MetaSettings *settings = meta_backend_get_settings (backend);
      MetaX11ScaleMode scale_mode = meta_settings_get_x11_scale_mode (settings);

      if (scale_mode == META_X11_SCALE_MODE_UI_DOWN)
        return META_LOGICAL_MONITOR_LAYOUT_MODE_GLOBAL_UI_LOGICAL;
      else if (scale_mode == META_X11_SCALE_MODE_UP)
        return META_LOGICAL_MONITOR_LAYOUT_MODE_LOGICAL;
    }

  return META_LOGICAL_MONITOR_LAYOUT_MODE_PHYSICAL;
}

static void
meta_monitor_manager_xrandr_constructed (GObject *object)
{
  MetaMonitorManagerXrandr *manager_xrandr =
    META_MONITOR_MANAGER_XRANDR (object);
  MetaMonitorManager *manager = META_MONITOR_MANAGER (manager_xrandr);
  MetaBackend *backend = meta_monitor_manager_get_backend (manager);
  MetaBackendX11 *backend_x11 = META_BACKEND_X11 (backend);
  MetaSettings *settings = meta_backend_get_settings (backend);

  manager_xrandr->xdisplay = meta_backend_x11_get_xdisplay (backend_x11);

  if (!XRRQueryExtension (manager_xrandr->xdisplay,
			  &manager_xrandr->rr_event_base,
			  &manager_xrandr->rr_error_base))
    {
      return;
    }
  else
    {
      int major_version, minor_version;
      /* We only use ScreenChangeNotify, but GDK uses the others,
	 and we don't want to step on its toes */
      XRRSelectInput (manager_xrandr->xdisplay,
		      DefaultRootWindow (manager_xrandr->xdisplay),
		      RRScreenChangeNotifyMask
		      | RRCrtcChangeNotifyMask
		      | RROutputPropertyNotifyMask);

      XRRQueryVersion (manager_xrandr->xdisplay, &major_version,
                       &minor_version);
      manager_xrandr->randr_version = RANDR_VERSION_FORMAT (major_version,
                                                            minor_version);
      if (manager_xrandr->randr_version >= RANDR_TILING_MIN_VERSION)
        manager_xrandr->tiled_monitor_atoms = g_hash_table_new (NULL, NULL);

      meta_monitor_manager_xrandr_init_monitors (manager_xrandr);
    }

  g_signal_connect_object (settings, "x11-scale-mode-changed",
                           G_CALLBACK (scale_mode_changed), manager_xrandr, 0);

  G_OBJECT_CLASS (meta_monitor_manager_xrandr_parent_class)->constructed (object);
}

static void
meta_monitor_manager_xrandr_finalize (GObject *object)
{
  MetaMonitorManagerXrandr *manager_xrandr = META_MONITOR_MANAGER_XRANDR (object);

  g_hash_table_destroy (manager_xrandr->tiled_monitor_atoms);

  G_OBJECT_CLASS (meta_monitor_manager_xrandr_parent_class)->finalize (object);
}

static void
meta_monitor_manager_xrandr_init (MetaMonitorManagerXrandr *manager_xrandr)
{
}

static void
meta_monitor_manager_xrandr_class_init (MetaMonitorManagerXrandrClass *klass)
{
  MetaMonitorManagerClass *manager_class = META_MONITOR_MANAGER_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_monitor_manager_xrandr_finalize;
  object_class->constructed = meta_monitor_manager_xrandr_constructed;

  manager_class->read_edid = meta_monitor_manager_xrandr_read_edid;
  manager_class->read_current_state = meta_monitor_manager_xrandr_read_current_state;
  manager_class->ensure_initial_config = meta_monitor_manager_xrandr_ensure_initial_config;
  manager_class->apply_monitors_config = meta_monitor_manager_xrandr_apply_monitors_config;
  manager_class->update_screen_size_derived = meta_monitor_manager_xrandr_update_screen_size_derived;
  manager_class->set_power_save_mode = meta_monitor_manager_xrandr_set_power_save_mode;
  manager_class->change_backlight = meta_monitor_manager_xrandr_change_backlight;
  manager_class->get_crtc_gamma = meta_monitor_manager_xrandr_get_crtc_gamma;
  manager_class->set_crtc_gamma = meta_monitor_manager_xrandr_set_crtc_gamma;
  manager_class->tiled_monitor_added = meta_monitor_manager_xrandr_tiled_monitor_added;
  manager_class->tiled_monitor_removed = meta_monitor_manager_xrandr_tiled_monitor_removed;
  manager_class->is_transform_handled = meta_monitor_manager_xrandr_is_transform_handled;
  manager_class->calculate_monitor_mode_scale = meta_monitor_manager_xrandr_calculate_monitor_mode_scale;
  manager_class->calculate_supported_scales = meta_monitor_manager_xrandr_calculate_supported_scales;
  manager_class->get_capabilities = meta_monitor_manager_xrandr_get_capabilities;
  manager_class->get_max_screen_size = meta_monitor_manager_xrandr_get_max_screen_size;
  manager_class->get_default_layout_mode = meta_monitor_manager_xrandr_get_default_layout_mode;

  quark_meta_monitor_xrandr_data =
    g_quark_from_static_string ("-meta-monitor-xrandr-data");
}

gboolean
meta_monitor_manager_xrandr_handle_xevent (MetaMonitorManagerXrandr *manager_xrandr,
					   XEvent                   *event)
{
  MetaMonitorManager *manager = META_MONITOR_MANAGER (manager_xrandr);
  MetaGpu *gpu = meta_monitor_manager_xrandr_get_gpu (manager_xrandr);
  MetaGpuXrandr *gpu_xrandr;
  XRRScreenResources *resources;
  gboolean is_hotplug;
  gboolean is_our_configuration;

  if ((event->type - manager_xrandr->rr_event_base) != RRScreenChangeNotify)
    return FALSE;

  XRRUpdateConfiguration (event);

  meta_monitor_manager_read_current_state (manager);

  gpu_xrandr = META_GPU_XRANDR (gpu);
  resources = meta_gpu_xrandr_get_resources (gpu_xrandr);

  is_hotplug = resources->timestamp < resources->configTimestamp;
  is_our_configuration = (resources->timestamp ==
                          manager_xrandr->last_xrandr_set_timestamp);
  if (is_hotplug)
    {
      meta_monitor_manager_on_hotplug (manager);
    }
  else
    {
      MetaMonitorsConfig *config;

      if (is_our_configuration)
        {
          MetaMonitorConfigManager *config_manager =
            meta_monitor_manager_get_config_manager (manager);

          config = meta_monitor_config_manager_get_current (config_manager);
        }
      else
        {
          config = NULL;
        }

      meta_monitor_manager_rebuild_derived (manager, config);
    }

  return TRUE;
}
