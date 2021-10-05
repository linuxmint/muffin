/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
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
 * SECTION:clutter-feature
 * @short_description: Run-time detection of Clutter features
 *
 * Parts of Clutter depend on the underlying platform, including the
 * capabilities of the backend used and the OpenGL features exposed through the
 * Clutter and COGL API.
 *
 * It is possible to ask whether Clutter has support for specific features at
 * run-time.
 */

#include "clutter-build-config.h"

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "clutter-backend-private.h"
#include "clutter-feature.h"
#include "clutter-main.h"
#include "clutter-private.h"
#include "clutter-debug.h"

#include "cogl/cogl.h"

typedef struct ClutterFeatures
{
  ClutterFeatureFlags flags;
  guint               features_set : 1;
} ClutterFeatures;

static ClutterFeatures* __features = NULL;

static ClutterFeatureFlags
clutter_features_from_cogl (void)
{
  ClutterFeatureFlags clutter_flags = 0;
  
  clutter_flags |= CLUTTER_FEATURE_SHADERS_GLSL;
  
  clutter_flags |= CLUTTER_FEATURE_OFFSCREEN;
  
  return clutter_flags;
}

gboolean
_clutter_feature_init (GError **error)
{
  ClutterMainContext *context;

  CLUTTER_NOTE (MISC, "checking features");

  if (!__features)
    {
      CLUTTER_NOTE (MISC, "allocating features data");
      __features = g_new0 (ClutterFeatures, 1);
      __features->features_set = FALSE; /* don't rely on zero-ing */
    }

  if (__features->features_set)
    return TRUE;

  context = _clutter_context_get_default ();

  /* makes sure we have a GL context; if we have, this is a no-op */
  if (!_clutter_backend_create_context (context->backend, error))
    return FALSE;

  __features->flags = (clutter_features_from_cogl ()
                    | _clutter_backend_get_features (context->backend));

  __features->features_set = TRUE;

  CLUTTER_NOTE (MISC, "features checked");

  return TRUE;
}

/**
 * clutter_feature_available:
 * @feature: a #ClutterFeatureFlags
 *
 * Checks whether @feature is available.  @feature can be a logical
 * OR of #ClutterFeatureFlags.
 *
 * Return value: %TRUE if a feature is available
 *
 * Since: 0.2
 */
gboolean
clutter_feature_available (ClutterFeatureFlags feature)
{
  if (G_UNLIKELY (!__features))
    {
      g_critical ("Unable to check features. Have you initialized Clutter?");
      return FALSE;
    }

  return (__features->flags & feature);
}

/**
 * clutter_feature_get_all:
 *
 * Returns all the supported features.
 *
 * Return value: a logical OR of all the supported features.
 *
 * Since: 0.2
 */
ClutterFeatureFlags
clutter_feature_get_all (void)
{
  if (G_UNLIKELY (!__features))
    {
      g_critical ("Unable to check features. Have you initialized Clutter?");
      return FALSE;
    }

  return __features->flags;
}

