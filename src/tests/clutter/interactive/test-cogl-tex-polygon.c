#include <glib.h>
#include <gmodule.h>
#include <stdlib.h>
#include <clutter/clutter.h>
#include <cogl/cogl.h>

/* Coglbox declaration
 *--------------------------------------------------*/

G_BEGIN_DECLS

#define TEST_TYPE_COGLBOX test_coglbox_get_type()

#define TEST_COGLBOX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  TEST_TYPE_COGLBOX, TestCoglbox))

#define TEST_COGLBOX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  TEST_TYPE_COGLBOX, TestCoglboxClass))

#define TEST_IS_COGLBOX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  TEST_TYPE_COGLBOX))

#define TEST_IS_COGLBOX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  TEST_TYPE_COGLBOX))

#define TEST_COGLBOX_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  TEST_TYPE_COGLBOX, TestCoglboxClass))

typedef struct _TestCoglbox        TestCoglbox;
typedef struct _TestCoglboxClass   TestCoglboxClass;
typedef struct _TestCoglboxPrivate TestCoglboxPrivate;

struct _TestCoglbox
{
  ClutterActor           parent;

  /*< private >*/
  TestCoglboxPrivate *priv;
};

struct _TestCoglboxClass
{
  ClutterActorClass parent_class;

  /* padding for future expansion */
  void (*_test_coglbox1) (void);
  void (*_test_coglbox2) (void);
  void (*_test_coglbox3) (void);
  void (*_test_coglbox4) (void);
};

static GType test_coglbox_get_type (void) G_GNUC_CONST;

int
test_cogl_tex_polygon_main (int argc, char *argv[]);

const char *
test_cogl_tex_polygon_describe (void);

G_END_DECLS

/* Coglbox private declaration
 *--------------------------------------------------*/

struct _TestCoglboxPrivate
{
  CoglHandle sliced_tex, not_sliced_tex;
  gint       frame;
  gboolean   use_sliced;
  gboolean   use_linear_filtering;
};

G_DEFINE_TYPE_WITH_PRIVATE (TestCoglbox, test_coglbox, CLUTTER_TYPE_ACTOR);

#define TEST_COGLBOX_GET_PRIVATE(obj) \
((TestCoglboxPrivate *)test_coglbox_get_instance_private (TEST_COGLBOX ((obj))))

/* Coglbox implementation
 *--------------------------------------------------*/

static void
test_coglbox_fade_texture (CoglFramebuffer *framebuffer,
                           CoglPipeline    *pipeline,
                           float            x1,
                           float            y1,
                           float            x2,
                           float            y2,
                           float            tx1,
                           float            ty1,
                           float            tx2,
                           float            ty2)
{
  CoglVertexP3T2C4 vertices[4];
  CoglPrimitive *primitive;
  int i;

  vertices[0].x = x1;
  vertices[0].y = y1;
  vertices[0].z = 0;
  vertices[0].s = tx1;
  vertices[0].t = ty1;
  vertices[1].x = x1;
  vertices[1].y = y2;
  vertices[1].z = 0;
  vertices[1].s = tx1;
  vertices[1].t = ty2;
  vertices[2].x = x2;
  vertices[2].y = y2;
  vertices[2].z = 0;
  vertices[2].s = tx2;
  vertices[2].t = ty2;
  vertices[3].x = x2;
  vertices[3].y = y1;
  vertices[3].z = 0;
  vertices[3].s = tx2;
  vertices[3].t = ty1;

  for (i = 0; i < 4; i++)
    {
      CoglColor cogl_color;

      cogl_color_init_from_4ub (&cogl_color,
                                255,
                                255,
                                255,
                                ((i ^ (i >> 1)) & 1) ? 0 : 128);
      cogl_color_premultiply (&cogl_color);
      vertices[i].r = cogl_color_get_red_byte (&cogl_color);
      vertices[i].g = cogl_color_get_green_byte (&cogl_color);
      vertices[i].b = cogl_color_get_blue_byte (&cogl_color);
      vertices[i].a = cogl_color_get_alpha_byte (&cogl_color);
    }

  primitive =
    cogl_primitive_new_p3t2c4 (cogl_framebuffer_get_context (framebuffer),
                               COGL_VERTICES_MODE_TRIANGLE_FAN,
                               4,
                               vertices);
  cogl_primitive_draw (primitive, framebuffer, pipeline);
  cogl_object_unref (primitive);
}

static void
test_coglbox_triangle_texture (CoglFramebuffer *framebuffer,
                               CoglHandle       material,
                               int              tex_width,
                               int              tex_height,
                               float            x,
                               float            y,
                               float            tx1,
                               float            ty1,
                               float            tx2,
                               float            ty2,
                               float            tx3,
                               float            ty3)
{
  CoglVertexP3T2 vertices[3];
  CoglPrimitive *primitive;

  vertices[0].x = x + tx1 * tex_width;
  vertices[0].y = y + ty1 * tex_height;
  vertices[0].z = 0;
  vertices[0].s = tx1;
  vertices[0].t = ty1;

  vertices[1].x = x + tx2 * tex_width;
  vertices[1].y = y + ty2 * tex_height;
  vertices[1].z = 0;
  vertices[1].s = tx2;
  vertices[1].t = ty2;

  vertices[2].x = x + tx3 * tex_width;
  vertices[2].y = y + ty3 * tex_height;
  vertices[2].z = 0;
  vertices[2].s = tx3;
  vertices[2].t = ty3;

  primitive = cogl_primitive_new_p3t2 (cogl_framebuffer_get_context (framebuffer),
                                       COGL_VERTICES_MODE_TRIANGLE_FAN,
                                       3,
                                       vertices);
  cogl_primitive_draw (primitive, framebuffer, material);
  cogl_object_unref (primitive);
}

static void
test_coglbox_paint (ClutterActor        *self,
                    ClutterPaintContext *paint_context)
{
  TestCoglboxPrivate *priv = TEST_COGLBOX_GET_PRIVATE (self);
  CoglHandle tex_handle = priv->use_sliced ? priv->sliced_tex
                                           : priv->not_sliced_tex;
  int tex_width = cogl_texture_get_width (tex_handle);
  int tex_height = cogl_texture_get_height (tex_handle);
  CoglFramebuffer *framebuffer =
    clutter_paint_context_get_framebuffer (paint_context);
  CoglHandle material = cogl_material_new ();

  cogl_material_set_layer (material, 0, tex_handle);

  cogl_material_set_layer_filters (material, 0,
                                   priv->use_linear_filtering
                                   ? COGL_MATERIAL_FILTER_LINEAR :
                                   COGL_MATERIAL_FILTER_NEAREST,
                                   priv->use_linear_filtering
                                   ? COGL_MATERIAL_FILTER_LINEAR :
                                   COGL_MATERIAL_FILTER_NEAREST);

  cogl_framebuffer_push_matrix (framebuffer);
  cogl_framebuffer_translate (framebuffer, tex_width / 2, 0, 0);
  cogl_framebuffer_rotate (framebuffer, priv->frame, 0, 1, 0);
  cogl_framebuffer_translate (framebuffer, -tex_width / 2, 0, 0);

  /* Draw a hand and refect it */
  cogl_framebuffer_draw_textured_rectangle (framebuffer, material,
                                            0, 0, tex_width, tex_height,
                                            0, 0, 1, 1);
  test_coglbox_fade_texture (framebuffer, material,
                             0, tex_height,
			     tex_width, (tex_height * 3 / 2),
			     0.0, 1.0,
			     1.0, 0.5);

  cogl_framebuffer_pop_matrix (framebuffer);

  cogl_framebuffer_push_matrix (framebuffer);
  cogl_framebuffer_translate (framebuffer, tex_width * 3 / 2 + 60, 0, 0);
  cogl_framebuffer_rotate (framebuffer, priv->frame, 0, 1, 0);
  cogl_framebuffer_translate (framebuffer, -tex_width / 2 - 10, 0, 0);

  /* Draw the texture split into two triangles */
  test_coglbox_triangle_texture (framebuffer, material,
                                 tex_width, tex_height,
				 0, 0,
				 0, 0,
				 0, 1,
				 1, 1);
  test_coglbox_triangle_texture (framebuffer, material,
                                 tex_width, tex_height,
				 20, 0,
				 0, 0,
				 1, 0,
				 1, 1);

  cogl_framebuffer_pop_matrix (framebuffer);

  cogl_object_unref (material);
}

static void
test_coglbox_finalize (GObject *object)
{
  G_OBJECT_CLASS (test_coglbox_parent_class)->finalize (object);
}

static void
test_coglbox_dispose (GObject *object)
{
  TestCoglboxPrivate *priv;

  priv = TEST_COGLBOX_GET_PRIVATE (object);
  cogl_object_unref (priv->not_sliced_tex);
  cogl_object_unref (priv->sliced_tex);

  G_OBJECT_CLASS (test_coglbox_parent_class)->dispose (object);
}

static void
test_coglbox_init (TestCoglbox *self)
{
  TestCoglboxPrivate *priv;
  GError *error = NULL;
  gchar *file;

  self->priv = priv = TEST_COGLBOX_GET_PRIVATE (self);

  priv->use_linear_filtering = FALSE;
  priv->use_sliced = FALSE;

  file = g_build_filename (TESTS_DATADIR, "redhand.png", NULL);
  priv->sliced_tex =
    cogl_texture_new_from_file  (file,
                                 COGL_TEXTURE_NONE,
                                 COGL_PIXEL_FORMAT_ANY,
                                 &error);
  if (priv->sliced_tex == NULL)
    {
      if (error)
        {
          g_warning ("Texture loading failed: %s", error->message);
          g_error_free (error);
          error = NULL;
        }
      else
        g_warning ("Texture loading failed: <unknown>");
    }

  priv->not_sliced_tex =
    cogl_texture_new_from_file (file,
                                COGL_TEXTURE_NO_SLICING,
                                COGL_PIXEL_FORMAT_ANY,
                                &error);
  if (priv->not_sliced_tex == NULL)
    {
      if (error)
        {
          g_warning ("Texture loading failed: %s", error->message);
          g_error_free (error);
        }
      else
        g_warning ("Texture loading failed: <unknown>");
    }

  g_free (file);
}

static void
test_coglbox_class_init (TestCoglboxClass *klass)
{
  GObjectClass      *gobject_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class   = CLUTTER_ACTOR_CLASS (klass);

  gobject_class->finalize     = test_coglbox_finalize;
  gobject_class->dispose      = test_coglbox_dispose;
  actor_class->paint          = test_coglbox_paint;
}

static ClutterActor*
test_coglbox_new (void)
{
  return g_object_new (TEST_TYPE_COGLBOX, NULL);
}

static void
frame_cb (ClutterTimeline *timeline,
	  gint             elapsed_msecs,
	  gpointer         data)
{
  TestCoglboxPrivate *priv = TEST_COGLBOX_GET_PRIVATE (data);
  gdouble progress = clutter_timeline_get_progress (timeline);

  priv->frame = 360.0 * progress;
  clutter_actor_queue_redraw (CLUTTER_ACTOR (data));
}

static void
update_toggle_text (ClutterText *button, gboolean val)
{
  clutter_text_set_text (button, val ? "Enabled" : "Disabled");
}

static gboolean
on_toggle_click (ClutterActor *button, ClutterEvent *event,
		 gboolean *toggle_val)
{
  update_toggle_text (CLUTTER_TEXT (button), *toggle_val = !*toggle_val);

  return TRUE;
}

static ClutterActor *
make_toggle (const char *label_text, gboolean *toggle_val)
{
  ClutterActor *group = clutter_group_new ();
  ClutterActor *label = clutter_text_new_with_text ("Sans 14", label_text);
  ClutterActor *button = clutter_text_new_with_text ("Sans 14", "");

  clutter_actor_set_reactive (button, TRUE);

  update_toggle_text (CLUTTER_TEXT (button), *toggle_val);

  clutter_actor_set_position (button, clutter_actor_get_width (label) + 10, 0);
  clutter_container_add (CLUTTER_CONTAINER (group), label, button, NULL);

  g_signal_connect (button, "button-press-event", G_CALLBACK (on_toggle_click),
		    toggle_val);

  return group;
}

G_MODULE_EXPORT int
test_cogl_tex_polygon_main (int argc, char *argv[])
{
  ClutterActor     *stage;
  ClutterActor     *coglbox;
  ClutterActor     *filtering_toggle;
  ClutterActor     *slicing_toggle;
  ClutterActor     *note;
  ClutterTimeline  *timeline;
  ClutterColor      blue = { 0x30, 0x30, 0xff, 0xff };

  if (clutter_init (&argc, &argv) != CLUTTER_INIT_SUCCESS)
    return 1;

  /* Stage */
  stage = clutter_stage_new ();
  clutter_stage_set_color (CLUTTER_STAGE (stage), &blue);
  clutter_actor_set_size (stage, 640, 480);
  clutter_stage_set_title (CLUTTER_STAGE (stage), "Cogl Texture Polygon");
  g_signal_connect (stage, "destroy", G_CALLBACK (clutter_main_quit), NULL);

  /* Cogl Box */
  coglbox = test_coglbox_new ();
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), coglbox);

  /* Timeline for animation */
  timeline = clutter_timeline_new (6000);
  clutter_timeline_set_loop (timeline, TRUE);
  g_signal_connect (timeline, "new-frame", G_CALLBACK (frame_cb), coglbox);
  clutter_timeline_start (timeline);

  /* Labels for toggling settings */
  slicing_toggle = make_toggle ("Texture slicing: ",
				&(TEST_COGLBOX_GET_PRIVATE (coglbox)
				  ->use_sliced));
  clutter_actor_set_position (slicing_toggle, 0,
			      clutter_actor_get_height (stage)
			      - clutter_actor_get_height (slicing_toggle));
  filtering_toggle = make_toggle ("Linear filtering: ",
				  &(TEST_COGLBOX_GET_PRIVATE (coglbox)
				    ->use_linear_filtering));
  clutter_actor_set_position (filtering_toggle, 0,
			      clutter_actor_get_y (slicing_toggle)
			      - clutter_actor_get_height (filtering_toggle));
  note = clutter_text_new_with_text ("Sans 10", "<- Click to change");
  clutter_actor_set_position (note,
			      clutter_actor_get_width (filtering_toggle) + 10,
			      (clutter_actor_get_height (stage)
			       + clutter_actor_get_y (filtering_toggle)) / 2
			      - clutter_actor_get_height (note) / 2);

  clutter_container_add (CLUTTER_CONTAINER (stage),
			 slicing_toggle,
			 filtering_toggle,
			 note,
			 NULL);

  clutter_actor_show (stage);

  clutter_main ();

  return 0;
}

G_MODULE_EXPORT const char *
test_cogl_tex_polygon_describe (void)
{
  return "Texture polygon primitive.";
}
