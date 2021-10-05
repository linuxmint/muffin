#ifndef __CLUTTER_DEBUG_H__
#define __CLUTTER_DEBUG_H__

#include <glib.h>
#include "clutter-main.h"

G_BEGIN_DECLS

#ifdef CLUTTER_ENABLE_DEBUG

#define CLUTTER_HAS_DEBUG(type)         ((clutter_debug_flags & CLUTTER_DEBUG_##type) != FALSE)

#ifdef __GNUC__

/* Try the GCC extension for valists in macros */
#define CLUTTER_NOTE(type,x,a...)                       G_STMT_START {  \
        if (G_UNLIKELY (CLUTTER_HAS_DEBUG (type))) {                    \
          _clutter_debug_message ("[" #type "]:" G_STRLOC ": " x, ##a); \
        }                                               } G_STMT_END

#else /* !__GNUC__ */
/* Try the C99 version; unfortunately, this does not allow us to pass
 * empty arguments to the macro, which means we have to
 * do an intemediate printf.
 */
#define CLUTTER_NOTE(type,...)                          G_STMT_START {   \
        if (G_UNLIKELY (CLUTTER_HAS_DEBUG (type))) {                     \
          gchar *_fmt = g_strdup_printf (__VA_ARGS__);                   \
          _clutter_debug_message ("[" #type "]:" G_STRLOC ": %s", _fmt); \
          g_free (_fmt);                                                 \
        }                                               } G_STMT_END
#endif

#else /* !CLUTTER_ENABLE_DEBUG */

#define CLUTTER_NOTE(type,...)         G_STMT_START { } G_STMT_END
#define CLUTTER_HAS_DEBUG(type)        FALSE

#endif /* CLUTTER_ENABLE_DEBUG */

extern guint clutter_debug_flags;
extern guint clutter_pick_debug_flags;
extern guint clutter_paint_debug_flags;

void    _clutter_debug_messagev         (const char *format,
                                         va_list     var_args) G_GNUC_PRINTF (1, 0);
void    _clutter_debug_message          (const char *format,
                                         ...) G_GNUC_PRINTF (1, 2);

G_END_DECLS

#endif /* __CLUTTER_DEBUG_H__ */
