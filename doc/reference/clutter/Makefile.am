DOC_MODULE = clutter
DOC_MAIN_SGML_FILE = $(DOC_MODULE)-docs.xml
DOC_SOURCE_DIR = $(top_srcdir)/clutter/clutter

SCANGOBJ_OPTIONS =
SCAN_OPTIONS =
MKDB_OPTIONS = --xml-mode --output-format=xml --name-space=muffin-clutter
MKTMPL_OPTIONS =
FIXXREF_OPTIONS = \
	--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html/glib \
	--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html/gobject \
	--extra-dir=$(CAIRO_PREFIX)/share/gtk-doc/html/cairo \
	--extra-dir=$(PANGO_PREFIX)/share/gtk-doc/html/pango \
	--extra-dir=$(GDK_PREFIX)/share/gtk-doc/html/gdk \
	--extra-dir=$(ATK_PREFIX)/share/gtk-doc/html/atk \
	--extra-dir=../cogl/html

BUILT_HFILES = \
	clutter-enum-types.h \
	clutter-version.h \
	clutter-marshal.h

HFILE_GLOB = \
	$(top_srcdir)/clutter/clutter/*.h \
	$(top_builddir)/clutter/clutter/*.h \
	$(top_srcdir)/clutter/clutter/deprecated/*.h \
	$(top_srcdir)/clutter/clutter/x11/clutter-x11.h \
	$(top_srcdir)/clutter/clutter/x11/clutter-x11-texture-pixmap.h \
	$(top_srcdir)/clutter/clutter/egl/clutter-egl.h \
	$(top_srcdir)/clutter/clutter/wayland/clutter-wayland-compositor.h \
	$(top_srcdir)/clutter/clutter/cally/*.h

CFILE_GLOB = \
	$(top_srcdir)/clutter/clutter/*.c \
	$(top_srcdir)/clutter/clutter/cally/*.c \
	$(top_srcdir)/clutter/clutter/cogl/*.c \
	$(top_srcdir)/clutter/clutter/x11/*.c \
	$(top_srcdir)/clutter/clutter/egl/*.c \
	$(top_srcdir)/clutter/clutter/wayland/*.c \
	$(top_srcdir)/clutter/clutter/deprecated/*.c

IGNORE_HFILES = \
	config.h			\
	clutter.h 			\
	clutter-actor-meta-private.h	\
	clutter-actor-private.h		\
	clutter-backend-private.h	\
  clutter-bezier.h 		\
	clutter-cogl-compat.h		\
	clutter-color-static.h		\
	clutter-config.h		\
	clutter-constraint-private.h	\
	clutter-debug.h 		\
	clutter-deprecated.h 		\
	clutter-device-manager-private.h	\
	clutter-easing.h		\
	clutter-enum-types.h 		\
	clutter-event-translator.h	\
	clutter-flatten-effect.h	\
	clutter-gesture-action-private.h	\
	clutter-id-pool.h 		\
	clutter-keysyms.h 		\
	clutter-keysyms-compat.h	\
	clutter-keysyms-table.h 	\
	clutter-marshal.h 		\
	clutter-master-clock-default.h	\
	clutter-master-clock.h 		\
	clutter-model-private.h 	\
	clutter-paint-node-private.h	\
	clutter-paint-volume-private.h	\
	clutter-private.h 		\
	clutter-script-private.h 	\
	clutter-settings-private.h	\
	clutter-stage-manager-private.h	\
	clutter-stage-private.h		\
	clutter-stage-window.h 		\
	clutter-timeout-interval.h 	\
	cally-actor-private.h		\
	cogl 				\
	egl				\
	evdev				\
	tslib				\
	x11 				\
	wayland

EXTRA_HFILES = \
  $(top_srcdir)/clutter/clutter/x11/clutter-x11.h \
	$(top_srcdir)/clutter/clutter/x11/clutter-x11-texture-pixmap.h \
	$(top_srcdir)/clutter/clutter/egl/clutter-egl.h \
	$(top_srcdir)/clutter/clutter/wayland/clutter-wayland-compositor.h

HTML_IMAGES = \
	actor-box.png \
	actor-example.png \
	animator-key-frames.png \
	bin-layout.png \
	box-layout.png \
	constraints-example.png \
  easing-modes.png \
	event-flow.png \
	flow-layout.png \
	path-alpha-func.png \
	offscreen-redirect.png \
	table-layout.png

SVG_IMAGES = \
	animator-key-frames.svg \
  easing-modes.svg

content_files = \
	glossary.xml \
	clutter-overview.xml \
	building-clutter.xml \
	running-clutter.xml \
	migrating-ClutterAnimation.xml \
	migrating-ClutterBehaviour.xml \
	migrating-ClutterEffect.xml \
	migrating-ClutterPath.xml

expand_content_files = \
	glossary.xml \
	clutter-overview.xml \
	building-clutter.xml \
	running-clutter.xml \
	migrating-ClutterAnimation.xml \
	migrating-ClutterBehaviour.xml \
	migrating-ClutterEffect.xml \
	migrating-ClutterPath.xml

GTKDOC_CFLAGS = -I$(top_srcdir) \
	-I$(top_builddir) \
	-DCLUTTER_DISABLE_DEPRECATION_WARNINGS \
	-DCLUTTER_ENABLE_COMPOSITOR_API	\
	-DCLUTTER_ENABLE_EXPERIMENTAL_API \
	$(CLUTTER_CFLAGS)
GTKDOC_LIBS = $(top_builddir)/clutter/clutter/libmuffin-clutter-0.la $(CLUTTER_LIBS)

include $(top_srcdir)/gtk-doc.make

EXTRA_DIST += clutter.types
EXTRA_DIST += $(HTML_IMAGES) $(SVG_IMAGES)

DISTCLEANFILES = $(DOC_MODULE).types $(DOC_MODULE)-sections.txt

if ENABLE_GTK_DOC
TESTS_ENVIRONMENT = \
	DOC_MODULE=$(DOC_MODULE) \
	DOC_MAIN_SGML_FILE=$(DOC_MAIN_SGML_FILE) \
	SRCDIR=$(abs_srcdir) \
	BUILDDIR=$(abs_builddir)

#TESTS = $(GTKDOC_CHECK)
endif
