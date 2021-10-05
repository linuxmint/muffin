/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009,2010 Intel Corporation.
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
 */

#include "cogl-config.h"

#include "cogl-context-private.h"
#include "cogl-object-private.h"
#include "cogl-glsl-shader-private.h"
#include "cogl-glsl-shader-boilerplate.h"
#include "driver/gl/cogl-util-gl-private.h"
#include "deprecated/cogl-shader-private.h"

#include <glib.h>

#include <string.h>

static void _cogl_shader_free (CoglShader *shader);

COGL_HANDLE_DEFINE (Shader, shader);

#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif

static void
_cogl_shader_free (CoglShader *shader)
{
  /* Frees shader resources but its handle is not
     released! Do that separately before this! */
  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

    if (shader->gl_handle)
      GE (ctx, glDeleteShader (shader->gl_handle));

  g_slice_free (CoglShader, shader);
}

CoglHandle
cogl_create_shader (CoglShaderType type)
{
  CoglShader *shader;

  _COGL_GET_CONTEXT (ctx, NULL);

  switch (type)
    {
    case COGL_SHADER_TYPE_VERTEX:
    case COGL_SHADER_TYPE_FRAGMENT:
      break;
    default:
      g_warning ("Unexpected shader type (0x%08lX) given to "
                 "cogl_create_shader", (unsigned long) type);
      return NULL;
    }

  shader = g_slice_new (CoglShader);
  shader->gl_handle = 0;
  shader->compilation_pipeline = NULL;
  shader->type = type;

  return _cogl_shader_handle_new (shader);
}

static void
delete_shader (CoglShader *shader)
{
  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  if (shader->gl_handle)
    GE (ctx, glDeleteShader (shader->gl_handle));

  shader->gl_handle = 0;

  if (shader->compilation_pipeline)
    {
      cogl_object_unref (shader->compilation_pipeline);
      shader->compilation_pipeline = NULL;
    }
}

void
cogl_shader_source (CoglHandle   handle,
                    const char  *source)
{
  CoglShader *shader;

  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  if (!cogl_is_shader (handle))
    return;

  shader = handle;

  shader->source = g_strdup (source);
}

void
_cogl_shader_compile_real (CoglHandle handle,
                           CoglPipeline *pipeline)
{
  CoglShader *shader = handle;
  GLenum gl_type;
  GLint status;

  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  if (shader->gl_handle)
    {
      CoglPipeline *prev = shader->compilation_pipeline;

      /* XXX: currently the only things that will affect the
       * boilerplate for user shaders, apart from driver features,
       * are the pipeline layer-indices and texture-unit-indices
       */
      if (pipeline == prev ||
          _cogl_pipeline_layer_and_unit_numbers_equal (prev, pipeline))
        return;
    }

  if (shader->gl_handle)
    delete_shader (shader);

  switch (shader->type)
    {
    case COGL_SHADER_TYPE_VERTEX:
      gl_type = GL_VERTEX_SHADER;
      break;
    case COGL_SHADER_TYPE_FRAGMENT:
      gl_type = GL_FRAGMENT_SHADER;
      break;
    default:
      g_assert_not_reached ();
      break;
    }

  shader->gl_handle = ctx->glCreateShader (gl_type);

  _cogl_glsl_shader_set_source_with_boilerplate (ctx,
                                                 shader->gl_handle,
                                                 gl_type,
                                                 pipeline,
                                                 1,
                                                 (const char **)
                                                  &shader->source,
                                                 NULL);

  GE (ctx, glCompileShader (shader->gl_handle));

  shader->compilation_pipeline = cogl_object_ref (pipeline);

  GE (ctx, glGetShaderiv (shader->gl_handle, GL_COMPILE_STATUS, &status));
  if (!status)
    {
      char buffer[512];
      int len = 0;

      ctx->glGetShaderInfoLog (shader->gl_handle, 511, &len, buffer);
      buffer[len] = '\0';

      g_warning ("Failed to compile GLSL program:\n"
                 "src:\n%s\n"
                 "error:\n%s\n",
                 shader->source,
                 buffer);
    }
}

CoglShaderType
cogl_shader_get_type (CoglHandle  handle)
{
  CoglShader *shader;

  _COGL_GET_CONTEXT (ctx, COGL_SHADER_TYPE_VERTEX);

  if (!cogl_is_shader (handle))
    {
      g_warning ("Non shader handle type passed to cogl_shader_get_type");
      return COGL_SHADER_TYPE_VERTEX;
    }

  shader = handle;
  return shader->type;
}
