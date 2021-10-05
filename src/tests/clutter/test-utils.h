#include <clutter/clutter.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

static inline ClutterActor *
clutter_test_utils_create_texture_from_file (const char  *filename,
                                             GError     **error)
{
  g_autoptr (ClutterContent) image = NULL;
  g_autoptr (GdkPixbuf) pixbuf = NULL;

  pixbuf = gdk_pixbuf_new_from_file (filename, error);
  if (!pixbuf)
    return NULL;

  image = clutter_image_new ();
  if (!clutter_image_set_data (CLUTTER_IMAGE (image),
                               gdk_pixbuf_get_pixels (pixbuf),
                               gdk_pixbuf_get_has_alpha (pixbuf)
                               ? COGL_PIXEL_FORMAT_RGBA_8888
                               : COGL_PIXEL_FORMAT_RGB_888,
                               gdk_pixbuf_get_width (pixbuf),
                               gdk_pixbuf_get_height (pixbuf),
                               gdk_pixbuf_get_rowstride (pixbuf),
                               error))
    return NULL;

  return g_object_new (CLUTTER_TYPE_ACTOR,
                       "content", image,
                       NULL);
}
