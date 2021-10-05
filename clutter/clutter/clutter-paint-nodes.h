/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2011  Intel Corporation.
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
 * Author:
 *   Emmanuele Bassi <ebassi@linux.intel.com>
 */

#ifndef __CLUTTER_PAINT_NODES_H__
#define __CLUTTER_PAINT_NODES_H__

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <cogl/cogl.h>
#include <clutter/clutter-types.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_COLOR_NODE         (clutter_color_node_get_type ())
#define CLUTTER_COLOR_NODE(obj)         (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_COLOR_NODE, ClutterColorNode))
#define CLUTTER_IS_COLOR_NODE(obj)      (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_COLOR_NODE))

/**
 * ClutterColorNode:
 *
 * The #ClutterTextNode structure is an opaque
 * type whose members cannot be directly accessed.
 *
 * Since: 1.10
 */
typedef struct _ClutterColorNode                ClutterColorNode;
typedef struct _ClutterColorNodeClass           ClutterColorNodeClass;

CLUTTER_EXPORT
GType clutter_color_node_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterPaintNode *      clutter_color_node_new          (const ClutterColor    *color);

#define CLUTTER_TYPE_TEXTURE_NODE               (clutter_texture_node_get_type ())
#define CLUTTER_TEXTURE_NODE(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_TEXTURE_NODE, ClutterTextureNode))
#define CLUTTER_IS_TEXTURE_NODE(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_TEXTURE_NODE))

/**
 * ClutterTextureNode:
 *
 * The #ClutterTextNode structure is an opaque
 * type whose members cannot be directly accessed.
 *
 * Since: 1.10
 */
typedef struct _ClutterTextureNode              ClutterTextureNode;
typedef struct _ClutterTextureNodeClass         ClutterTextureNodeClass;

CLUTTER_EXPORT
GType clutter_texture_node_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterPaintNode *      clutter_texture_node_new        (CoglTexture           *texture,
                                                         const ClutterColor    *color,
                                                         ClutterScalingFilter   min_filter,
                                                         ClutterScalingFilter   mag_filter);

#define CLUTTER_TYPE_CLIP_NODE                  (clutter_clip_node_get_type ())
#define CLUTTER_CLIP_NODE(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_CLIP_NODE, ClutterClipNode))
#define CLUTTER_IS_CLIP_NODE(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_CLIP_NODE))

/**
 * ClutterClipNode:
 *
 * The #ClutterTextNode structure is an opaque
 * type whose members cannot be directly accessed.
 *
 * Since: 1.10
 */
typedef struct _ClutterClipNode                 ClutterClipNode;
typedef struct _ClutterClipNodeClass            ClutterClipNodeClass;

CLUTTER_EXPORT
GType clutter_clip_node_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterPaintNode *      clutter_clip_node_new           (void);

#define CLUTTER_TYPE_PIPELINE_NODE              (clutter_pipeline_node_get_type ())
#define CLUTTER_PIPELINE_NODE(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_PIPELINE_NODE, ClutterPipelineNode))
#define CLUTTER_IS_PIPELINE_NODE(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_PIPELINE_NODE))

/**
 * ClutterPipelineNode:
 *
 * The #ClutterTextNode structure is an opaque
 * type whose members cannot be directly accessed.
 *
 * Since: 1.10
 */
typedef struct _ClutterPipelineNode             ClutterPipelineNode;
typedef struct _ClutterPipelineNodeClass        ClutterPipelineNodeClass;

CLUTTER_EXPORT
GType clutter_pipeline_node_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterPaintNode *      clutter_pipeline_node_new       (CoglPipeline          *pipeline);

#define CLUTTER_TYPE_TEXT_NODE                  (clutter_text_node_get_type ())
#define CLUTTER_TEXT_NODE(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_TEXT_NODE, ClutterTextNode))
#define CLUTTER_IS_TEXT_NODE(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_TEXT_NODE))

/**
 * ClutterTextNode:
 *
 * The #ClutterTextNode structure is an opaque
 * type whose members cannot be directly accessed.
 *
 * Since: 1.10
 */
typedef struct _ClutterTextNode                 ClutterTextNode;
typedef struct _ClutterTextNodeClass            ClutterTextNodeClass;

CLUTTER_EXPORT
GType clutter_text_node_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterPaintNode *      clutter_text_node_new           (PangoLayout           *layout,
                                                         const ClutterColor    *color);

#define CLUTTER_TYPE_ACTOR_NODE                 (clutter_actor_node_get_type ())
#define CLUTTER_ACTOR_NODE(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_ACTOR_NODE, ClutterActorNode))
#define CLUTTER_IS_ACTOR_NODE(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_ACTOR_NODE))

/**
 * ClutterActorNode:
 *
 * The #ClutterActorNode structure is an opaque
 * type whose members cannot be directly accessed.
 */
typedef struct _ClutterActorNode ClutterActorNode;
typedef struct _ClutterActorNode ClutterActorNodeClass;

CLUTTER_EXPORT
GType clutter_actor_node_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterPaintNode * clutter_actor_node_new (ClutterActor *actor);

#define CLUTTER_TYPE_ROOT_NODE                  (clutter_root_node_get_type ())
#define CLUTTER_ROOT_NODE(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_ROOT_NODE, ClutterRootNode))
#define CLUTTER_IS_ROOT_NODE(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_ROOT_NODE))

/**
 * ClutterRootNode:
 *
 * The #ClutterRootNode structure is an opaque
 * type whose members cannot be directly accessed.
 */
typedef struct _ClutterRootNode                 ClutterRootNode;
typedef struct _ClutterPaintNodeClass           ClutterRootNodeClass;

CLUTTER_EXPORT
GType clutter_root_node_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterPaintNode *      clutter_root_node_new           (CoglFramebuffer       *framebuffer,
                                                         const ClutterColor    *clear_color,
                                                         CoglBufferBit          clear_flags);

#define CLUTTER_TYPE_LAYER_NODE                 (clutter_layer_node_get_type ())
#define CLUTTER_LAYER_NODE(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_LAYER_NODE, ClutterLayerNode))
#define CLUTTER_IS_LAYER_NODE(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_LAYER_NODE))

/*
 * ClutterLayerNode:
 *
 * The #ClutterLayerNode structure is an opaque
 * type whose members cannot be directly accessed.
 *
 * Since: 1.10
 */
typedef struct _ClutterLayerNode                ClutterLayerNode;
typedef struct _ClutterLayerNodeClass           ClutterLayerNodeClass;

CLUTTER_EXPORT
GType clutter_layer_node_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterPaintNode *      clutter_layer_node_new          (const CoglMatrix        *projection,
                                                         const cairo_rectangle_t *viewport,
                                                         float                    width,
                                                         float                    height,
                                                         guint8                   opacity);


#define CLUTTER_TYPE_TRANSFORM_NODE             (clutter_transform_node_get_type ())
#define CLUTTER_TRANSFORM_NODE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_TRANSFORM_NODE, ClutterTransformNode))
#define CLUTTER_IS_TRANSFORM_NODE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_TRANSFORM_NODE))

/*
 * ClutterTransformNode:
 *
 * The #ClutterLayerNode structure is an opaque
 * type whose members cannot be directly accessed.
 *
 * Since: 1.10
 */
typedef struct _ClutterTransformNode            ClutterTransformNode;
typedef struct _ClutterPaintNodeClass           ClutterTransformNodeClass;

CLUTTER_EXPORT
GType clutter_transform_node_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterPaintNode *      clutter_transform_node_new          (const CoglMatrix *projection);

G_END_DECLS

#endif /* __CLUTTER_PAINT_NODES_H__ */
