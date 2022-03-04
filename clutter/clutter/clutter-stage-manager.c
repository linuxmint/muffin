/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2008 OpenedHand
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
 * Author: Emmanuele Bassi <ebassi@linux.intel.com>
 */

/**
 * SECTION:clutter-stage-manager
 * @short_description: Maintains the list of stages
 *
 * #ClutterStageManager is a singleton object, owned by Clutter, which
 * maintains the list of currently active stages
 *
 * Every newly-created #ClutterStage will cause the emission of the
 * #ClutterStageManager::stage-added signal; once a #ClutterStage has
 * been destroyed, the #ClutterStageManager::stage-removed signal will
 * be emitted
 *
 * #ClutterStageManager is available since Clutter 0.8
 */

#include "clutter-build-config.h"

#include "clutter-stage-manager-private.h"

#include "clutter-marshal.h"
#include "clutter-debug.h"
#include "clutter-private.h"

enum
{
  PROP_0,
  PROP_DEFAULT_STAGE
};

enum
{
  STAGE_ADDED,
  STAGE_REMOVED,

  LAST_SIGNAL
};

static guint manager_signals[LAST_SIGNAL] = { 0, };
static ClutterStage *default_stage = NULL;

G_DEFINE_TYPE (ClutterStageManager, clutter_stage_manager, G_TYPE_OBJECT);

static void
clutter_stage_manager_get_property (GObject    *gobject,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  switch (prop_id)
    {
    case PROP_DEFAULT_STAGE:
      g_value_set_object (value, default_stage);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_stage_manager_dispose (GObject *gobject)
{
  ClutterStageManager *stage_manager;

  stage_manager = CLUTTER_STAGE_MANAGER (gobject);

  g_slist_free_full (stage_manager->stages,
                     (GDestroyNotify) clutter_actor_destroy);
  stage_manager->stages = NULL;

  G_OBJECT_CLASS (clutter_stage_manager_parent_class)->dispose (gobject);
}

static void
clutter_stage_manager_class_init (ClutterStageManagerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose      = clutter_stage_manager_dispose;
  gobject_class->get_property = clutter_stage_manager_get_property;

  /**
   * ClutterStageManager:default-stage:
   *
   * The default stage used by Clutter.
   *
   * Since: 0.8
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DEFAULT_STAGE,
                                   g_param_spec_object ("default-stage",
                                                        "Default Stage",
                                                        "The default stage",
                                                        CLUTTER_TYPE_STAGE,
                                                        CLUTTER_PARAM_READABLE));

  /**
   * ClutterStageManager::stage-added:
   * @stage_manager: the object which received the signal
   * @stage: the added stage
   *
   * The ::stage-added signal is emitted each time a new #ClutterStage
   * has been added to the stage manager.
   *
   * Since: 0.8
   */
  manager_signals[STAGE_ADDED] =
    g_signal_new ("stage-added",
                  G_OBJECT_CLASS_TYPE (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterStageManagerClass, stage_added),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_STAGE);
  /**
   * ClutterStageManager::stage-removed:
   * @stage_manager: the object which received the signal
   * @stage: the removed stage
   *
   * The ::stage-removed signal is emitted each time a #ClutterStage
   * has been removed from the stage manager.
   *
   * Since: 0.8
   */
  manager_signals[STAGE_REMOVED] =
    g_signal_new ("stage-removed",
                  G_OBJECT_CLASS_TYPE (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterStageManagerClass, stage_removed),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_STAGE);
}

static void
clutter_stage_manager_init (ClutterStageManager *stage_manager)
{
}

/**
 * clutter_stage_manager_get_default:
 *
 * Returns the default #ClutterStageManager.
 *
 * Return value: (transfer none): the default stage manager instance. The returned
 *   object is owned by Clutter and you should not reference or unreference it.
 *
 * Since: 0.8
 */
ClutterStageManager *
clutter_stage_manager_get_default (void)
{
  ClutterMainContext *context = _clutter_context_get_default ();

  if (G_UNLIKELY (context->stage_manager == NULL))
    context->stage_manager = g_object_new (CLUTTER_TYPE_STAGE_MANAGER, NULL);

  return context->stage_manager;
}

/*< private >
 * _clutter_stage_manager_set_default_stage:
 * @stage_manager: a #ClutterStageManager
 * @stage: a #ClutterStage
 *
 * Sets @stage as the default stage
 *
 * A no-op if there already is a default stage
 */
void
_clutter_stage_manager_set_default_stage (ClutterStageManager *stage_manager,
                                          ClutterStage        *stage)
{
  if (G_UNLIKELY (default_stage == NULL))
    {
      default_stage = stage;

      /* the default stage is immediately realized */
      clutter_actor_realize (CLUTTER_ACTOR (stage));

      g_object_notify (G_OBJECT (stage_manager), "default-stage");
    }
}

/**
 * clutter_stage_manager_get_default_stage:
 * @stage_manager: a #ClutterStageManager
 *
 * Returns the default #ClutterStage.
 *
 * Return value: (transfer none): the default stage. The returned object
 *   is owned by Clutter and you should never reference or unreference it
 *
 * Since: 0.8
 */
ClutterStage *
clutter_stage_manager_get_default_stage (ClutterStageManager *stage_manager)
{
  return default_stage;
}

/**
 * clutter_stage_manager_list_stages:
 * @stage_manager: a #ClutterStageManager
 *
 * Lists all currently used stages.
 *
 * Return value: (transfer container) (element-type Clutter.Stage): a newly
 *   allocated list of #ClutterStage objects. Use g_slist_free() to
 *   deallocate it when done.
 *
 * Since: 0.8
 */
GSList *
clutter_stage_manager_list_stages (ClutterStageManager *stage_manager)
{
  return g_slist_copy (stage_manager->stages);
}

/**
 * clutter_stage_manager_peek_stages:
 * @stage_manager: a #ClutterStageManager
 *
 * Lists all currently used stages.
 *
 * Return value: (transfer none) (element-type Clutter.Stage): a pointer
 *   to the internal list of #ClutterStage objects. The returned list
 *   is owned by the #ClutterStageManager and should never be modified
 *   or freed
 *
 * Since: 1.0
 */
const GSList *
clutter_stage_manager_peek_stages (ClutterStageManager *stage_manager)
{
  return stage_manager->stages;
}

void
_clutter_stage_manager_add_stage (ClutterStageManager *stage_manager,
                                  ClutterStage        *stage)
{
  if (g_slist_find (stage_manager->stages, stage))
    {
      g_warning ("Trying to add a stage to the list of managed stages, "
                 "but it is already in it, aborting.");
      return;
    }

  g_object_ref_sink (stage);

  stage_manager->stages = g_slist_append (stage_manager->stages, stage);

  g_signal_emit (stage_manager, manager_signals[STAGE_ADDED], 0, stage);
}

void
_clutter_stage_manager_remove_stage (ClutterStageManager *stage_manager,
                                     ClutterStage        *stage)
{
  /* this might be called multiple times from a ::dispose, so it
   * needs to just return without warning
   */
  if (!g_slist_find (stage_manager->stages, stage))
    return;

  stage_manager->stages = g_slist_remove (stage_manager->stages, stage);

  /* if the default stage is being destroyed then we unset the pointer */
  if (default_stage == stage)
    default_stage = NULL;

  g_signal_emit (stage_manager, manager_signals[STAGE_REMOVED], 0, stage);

  g_object_unref (stage);
}
