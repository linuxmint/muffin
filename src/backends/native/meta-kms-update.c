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
 */

#include "config.h"

#include "backends/native/meta-kms-update.h"
#include "backends/native/meta-kms-update-private.h"

#include "backends/meta-display-config-shared.h"
#include "backends/native/meta-kms-plane.h"

struct _MetaKmsUpdate
{
  gboolean is_sealed;

  MetaPowerSave power_save;
  GList *mode_sets;
  GList *plane_assignments;
  GList *page_flips;
  GList *connector_properties;
  GList *crtc_gammas;
};

void
meta_kms_plane_feedback_free (MetaKmsPlaneFeedback *plane_feedback)
{
  g_error_free (plane_feedback->error);
  g_free (plane_feedback);
}

MetaKmsPlaneFeedback *
meta_kms_plane_feedback_new_take_error (MetaKmsPlane *plane,
                                        MetaKmsCrtc  *crtc,
                                        GError       *error)
{
  MetaKmsPlaneFeedback *plane_feedback;

  plane_feedback = g_new0 (MetaKmsPlaneFeedback, 1);
  *plane_feedback = (MetaKmsPlaneFeedback) {
    .plane = plane,
    .crtc = crtc,
    .error = error,
  };

  return plane_feedback;
}

MetaKmsFeedback *
meta_kms_feedback_new_passed (void)
{
  MetaKmsFeedback *feedback;

  feedback = g_new0 (MetaKmsFeedback, 1);
  *feedback = (MetaKmsFeedback) {
    .result = META_KMS_FEEDBACK_PASSED,
  };

  return feedback;
}

MetaKmsFeedback *
meta_kms_feedback_new_failed (GList  *failed_planes,
                              GError *error)
{
  MetaKmsFeedback *feedback;

  feedback = g_new0 (MetaKmsFeedback, 1);
  *feedback = (MetaKmsFeedback) {
    .result = META_KMS_FEEDBACK_FAILED,
    .error = error,
    .failed_planes = failed_planes,
  };

  return feedback;
}

void
meta_kms_feedback_free (MetaKmsFeedback *feedback)
{
  g_list_free_full (feedback->failed_planes,
                    (GDestroyNotify) meta_kms_plane_feedback_free);
  g_clear_error (&feedback->error);
  g_free (feedback);
}

MetaKmsFeedbackResult
meta_kms_feedback_get_result (MetaKmsFeedback *feedback)
{
  return feedback->result;
}

GList *
meta_kms_feedback_get_failed_planes (MetaKmsFeedback *feedback)
{
  return feedback->failed_planes;
}

const GError *
meta_kms_feedback_get_error (MetaKmsFeedback *feedback)
{
  return feedback->error;
}

static MetaKmsProperty *
meta_kms_property_new (uint32_t prop_id,
                       uint64_t value)
{
  MetaKmsProperty *prop;

  prop = g_new0 (MetaKmsProperty, 1);
  *prop = (MetaKmsProperty) {
    .prop_id = prop_id,
    .value = value,
  };

  return prop;
}

static void
meta_kms_property_free (MetaKmsProperty *prop)
{
  g_free (prop);
}

static void
meta_kms_plane_assignment_free (MetaKmsPlaneAssignment *plane_assignment)
{
  g_list_free_full (plane_assignment->plane_properties,
                    (GDestroyNotify) meta_kms_property_free);
  g_free (plane_assignment);
}

static void
meta_kms_mode_set_free (MetaKmsModeSet *mode_set)
{
  g_free (mode_set->drm_mode);
  g_list_free (mode_set->connectors);
  g_free (mode_set);
}

MetaKmsPlaneAssignment *
meta_kms_update_assign_plane (MetaKmsUpdate          *update,
                              MetaKmsCrtc            *crtc,
                              MetaKmsPlane           *plane,
                              uint32_t                fb_id,
                              MetaFixed16Rectangle    src_rect,
                              MetaFixed16Rectangle    dst_rect,
                              MetaKmsAssignPlaneFlag  flags)
{
  MetaKmsPlaneAssignment *plane_assignment;

  g_assert (!meta_kms_update_is_sealed (update));

  plane_assignment = g_new0 (MetaKmsPlaneAssignment, 1);
  *plane_assignment = (MetaKmsPlaneAssignment) {
    .update = update,
    .crtc = crtc,
    .plane = plane,
    .fb_id = fb_id,
    .src_rect = src_rect,
    .dst_rect = dst_rect,
    .flags = flags,
  };

  update->plane_assignments = g_list_prepend (update->plane_assignments,
                                              plane_assignment);

  return plane_assignment;
}

MetaKmsPlaneAssignment *
meta_kms_update_unassign_plane (MetaKmsUpdate *update,
                                MetaKmsCrtc   *crtc,
                                MetaKmsPlane  *plane)
{
  MetaKmsPlaneAssignment *plane_assignment;

  g_assert (!meta_kms_update_is_sealed (update));

  plane_assignment = g_new0 (MetaKmsPlaneAssignment, 1);
  *plane_assignment = (MetaKmsPlaneAssignment) {
    .update = update,
    .crtc = crtc,
    .plane = plane,
    .fb_id = 0,
  };

  update->plane_assignments = g_list_prepend (update->plane_assignments,
                                              plane_assignment);

  return plane_assignment;
}

void
meta_kms_update_mode_set (MetaKmsUpdate   *update,
                          MetaKmsCrtc     *crtc,
                          GList           *connectors,
                          drmModeModeInfo *drm_mode)
{
  MetaKmsModeSet *mode_set;

  g_assert (!meta_kms_update_is_sealed (update));

  mode_set = g_new0 (MetaKmsModeSet, 1);
  *mode_set = (MetaKmsModeSet) {
    .crtc = crtc,
    .connectors = connectors,
    .drm_mode = drm_mode ? g_memdup (drm_mode, sizeof *drm_mode) : NULL,
  };

  update->mode_sets = g_list_prepend (update->mode_sets, mode_set);
}

void
meta_kms_update_set_connector_property (MetaKmsUpdate    *update,
                                        MetaKmsConnector *connector,
                                        uint32_t          prop_id,
                                        uint64_t          value)
{
  MetaKmsConnectorProperty *prop;

  g_assert (!meta_kms_update_is_sealed (update));

  prop = g_new0 (MetaKmsConnectorProperty, 1);
  *prop = (MetaKmsConnectorProperty) {
    .connector = connector,
    .prop_id = prop_id,
    .value = value,
  };

  update->connector_properties = g_list_prepend (update->connector_properties,
                                                 prop);
}

static void
meta_kms_crtc_gamma_free (MetaKmsCrtcGamma *gamma)
{
  g_free (gamma->red);
  g_free (gamma->green);
  g_free (gamma->blue);
  g_free (gamma);
}

void
meta_kms_update_set_crtc_gamma (MetaKmsUpdate  *update,
                                MetaKmsCrtc    *crtc,
                                int             size,
                                const uint16_t *red,
                                const uint16_t *green,
                                const uint16_t *blue)
{
  MetaKmsCrtcGamma *gamma;

  g_assert (!meta_kms_update_is_sealed (update));

  gamma = g_new0 (MetaKmsCrtcGamma, 1);
  *gamma = (MetaKmsCrtcGamma) {
    .crtc = crtc,
    .size = size,
    .red = g_memdup (red, size * sizeof *red),
    .green = g_memdup (green, size * sizeof *green),
    .blue = g_memdup (blue, size * sizeof *blue),
  };

  update->crtc_gammas = g_list_prepend (update->crtc_gammas, gamma);
}

void
meta_kms_update_page_flip (MetaKmsUpdate                 *update,
                           MetaKmsCrtc                   *crtc,
                           const MetaKmsPageFlipFeedback *feedback,
                           gpointer                       user_data)
{
  MetaKmsPageFlip *page_flip;

  g_assert (!meta_kms_update_is_sealed (update));

  page_flip = g_new0 (MetaKmsPageFlip, 1);
  *page_flip = (MetaKmsPageFlip) {
    .crtc = crtc,
    .feedback = feedback,
    .user_data = user_data,
  };

  update->page_flips = g_list_prepend (update->page_flips, page_flip);
}

void
meta_kms_update_custom_page_flip (MetaKmsUpdate                 *update,
                                  MetaKmsCrtc                   *crtc,
                                  const MetaKmsPageFlipFeedback *feedback,
                                  gpointer                       user_data,
                                  MetaKmsCustomPageFlipFunc      custom_page_flip_func,
                                  gpointer                       custom_page_flip_user_data)
{
  MetaKmsPageFlip *page_flip;

  g_assert (!meta_kms_update_is_sealed (update));

  page_flip = g_new0 (MetaKmsPageFlip, 1);
  *page_flip = (MetaKmsPageFlip) {
    .crtc = crtc,
    .feedback = feedback,
    .user_data = user_data,
    .custom_page_flip_func = custom_page_flip_func,
    .custom_page_flip_user_data = custom_page_flip_user_data,
  };

  update->page_flips = g_list_prepend (update->page_flips, page_flip);
}

void
meta_kms_plane_assignment_set_plane_property (MetaKmsPlaneAssignment *plane_assignment,
                                              uint32_t                prop_id,
                                              uint64_t                value)
{
  MetaKmsProperty *plane_prop;

  g_assert (!meta_kms_update_is_sealed (plane_assignment->update));

  plane_prop = meta_kms_property_new (prop_id, value);

  plane_assignment->plane_properties =
    g_list_prepend (plane_assignment->plane_properties, plane_prop);
}

void
meta_kms_plane_assignment_set_cursor_hotspot (MetaKmsPlaneAssignment *plane_assignment,
                                              int                     x,
                                              int                     y)
{
  plane_assignment->cursor_hotspot.is_valid = TRUE;
  plane_assignment->cursor_hotspot.x = x;
  plane_assignment->cursor_hotspot.y = y;
}

MetaKmsPlaneAssignment *
meta_kms_update_get_primary_plane_assignment (MetaKmsUpdate *update,
                                              MetaKmsCrtc   *crtc)
{
  GList *l;

  for (l = meta_kms_update_get_plane_assignments (update); l; l = l->next)
    {
      MetaKmsPlaneAssignment *plane_assignment = l->data;

      if (plane_assignment->crtc == crtc)
        return plane_assignment;
    }

  return NULL;
}

GList *
meta_kms_update_get_plane_assignments (MetaKmsUpdate *update)
{
  return update->plane_assignments;
}

GList *
meta_kms_update_get_mode_sets (MetaKmsUpdate *update)
{
  return update->mode_sets;
}

GList *
meta_kms_update_get_page_flips (MetaKmsUpdate *update)
{
  return update->page_flips;
}

GList *
meta_kms_update_get_connector_properties (MetaKmsUpdate *update)
{
  return update->connector_properties;
}

GList *
meta_kms_update_get_crtc_gammas (MetaKmsUpdate *update)
{
  return update->crtc_gammas;
}

void
meta_kms_update_seal (MetaKmsUpdate *update)
{
  update->is_sealed = TRUE;
}

gboolean
meta_kms_update_is_sealed (MetaKmsUpdate *update)
{
  return update->is_sealed;
}

MetaKmsUpdate *
meta_kms_update_new (void)
{
  return g_new0 (MetaKmsUpdate, 1);
}

void
meta_kms_update_free (MetaKmsUpdate *update)
{
  g_list_free_full (update->plane_assignments,
                    (GDestroyNotify) meta_kms_plane_assignment_free);
  g_list_free_full (update->mode_sets,
                    (GDestroyNotify) meta_kms_mode_set_free);
  g_list_free_full (update->page_flips, g_free);
  g_list_free_full (update->connector_properties, g_free);
  g_list_free_full (update->crtc_gammas, (GDestroyNotify) meta_kms_crtc_gamma_free);

  g_free (update);
}
