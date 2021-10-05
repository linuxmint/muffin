#include <cogl/cogl.h>

#include "test-declarations.h"
#include "test-utils.h"
#include "cogl-config.h"

/* So we can use _COGL_STATIC_ASSERT we include the internal
 * cogl-util.h header. Since internal headers explicitly guard against
 * applications including them directly instead of including
 * <cogl/cogl.h> we define __COGL_H_INSIDE__ here to subvert those
 * guards in this case... */
#define __COGL_H_INSIDE__
#include <cogl/cogl-util.h>
#undef __COGL_H_INSIDE__

_COGL_STATIC_ASSERT (COGL_VERSION_GET_MAJOR (COGL_VERSION_ENCODE (100,
                                                                  200,
                                                                  300)) ==
                     100,
                     "Getting the major component out of a encoded version "
                     "does not work");
_COGL_STATIC_ASSERT (COGL_VERSION_GET_MINOR (COGL_VERSION_ENCODE (100,
                                                                  200,
                                                                  300)) ==
                     200,
                     "Getting the minor component out of a encoded version "
                     "does not work");
_COGL_STATIC_ASSERT (COGL_VERSION_GET_MICRO (COGL_VERSION_ENCODE (100,
                                                                  200,
                                                                  300)) ==
                     300,
                     "Getting the micro component out of a encoded version "
                     "does not work");
