# Flag build for parallelism; see https://savannah.gnu.org/patch/?6905
.AUTOPARALLEL:

lib_LTLIBRARIES = libmuffin.la

SUBDIRS=wm-tester tools compositor/plugins

AM_CPPFLAGS=								\
	-DCLUTTER_ENABLE_EXPERIMENTAL_API \
	-DCOGL_ENABLE_EXPERIMENTAL_API \
	$(WARN_CFLAGS)							\
	$(MUFFIN_CFLAGS)						\
	-I$(srcdir)							\
	-I$(srcdir)/core						\
	-I$(srcdir)/ui							\
	-I$(srcdir)/compositor						\
	-DMUFFIN_LIBEXECDIR=\"$(libexecdir)\"				\
	-DHOST_ALIAS=\"@HOST_ALIAS@\"					\
	-DMUFFIN_LOCALEDIR=\"$(prefix)/@DATADIRNAME@/locale\"		\
	-DMUFFIN_PKGDATADIR=\"$(pkgdatadir)\"				\
	-DMUFFIN_DATADIR=\"$(datadir)\"					\
	-DG_LOG_DOMAIN=\"muffin\"					\
	-DSN_API_NOT_YET_FROZEN=1					\
	-DMUFFIN_MAJOR_VERSION=$(MUFFIN_MAJOR_VERSION)			\
	-DMUFFIN_MINOR_VERSION=$(MUFFIN_MINOR_VERSION)			\
	-DMUFFIN_MICRO_VERSION=$(MUFFIN_MICRO_VERSION)			\
	-DMUFFIN_PLUGIN_API_VERSION=$(MUFFIN_PLUGIN_API_VERSION)	\
	-DMUFFIN_PKGLIBDIR=\"$(pkglibdir)\"				\
	-DMUFFIN_PLUGIN_DIR=\"@MUFFIN_PLUGIN_DIR@\"			\
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\"

muffin_built_sources = \
	muffin-enum-types.h \
	muffin-enum-types.c

libmuffin_la_SOURCES =				\
	core/async-getprop.c			\
	core/async-getprop.h			\
	core/bell.c				\
	core/bell.h				\
	core/boxes.c				\
	core/boxes-private.h			\
	meta/boxes.h				\
	compositor/cogl-utils.c			\
	compositor/cogl-utils.h			\
	compositor/compositor.c			\
	compositor/compositor-private.h		\
	compositor/meta-background.c 		\
	compositor/meta-background.h 		\
	compositor/meta-background-actor.c	\
	compositor/meta-background-actor-private.h	\
	compositor/meta-module.c		\
	compositor/meta-module.h		\
	compositor/meta-plugin.c		\
	compositor/meta-plugin-manager.c	\
	compositor/meta-plugin-manager.h	\
	compositor/meta-shadow-factory.c	\
	compositor/meta-shadow-factory-private.h	\
	compositor/meta-shaped-texture.c	\
    compositor/meta-sync-ring.c \
    compositor/meta-sync-ring.h \
	compositor/meta-texture-rectangle.c	\
	compositor/meta-texture-rectangle.h	\
	compositor/meta-texture-tower.c		\
	compositor/meta-texture-tower.h		\
	compositor/meta-window-actor.c		\
	compositor/meta-window-actor-private.h	\
	compositor/meta-window-group.c		\
	compositor/meta-window-group.h		\
	compositor/meta-window-shape.c		\
	compositor/meta-window-shape.h		\
	compositor/region-utils.c		\
	compositor/region-utils.h		\
	meta/compositor.h			\
	meta/meta-background-actor.h		\
	meta/meta-plugin.h			\
	meta/meta-shadow-factory.h		\
	meta/meta-window-actor.h		\
	meta/compositor-muffin.h 		\
	core/above-tab-keycode.c		\
	core/constraints.c			\
	core/constraints.h			\
	core/core.c				\
	core/delete.c				\
	core/display.c				\
	core/display-private.h			\
	meta/display.h				\
	ui/draw-workspace.c			\
	ui/draw-workspace.h			\
	core/edge-resistance.c			\
	core/edge-resistance.h			\
	core/errors.c				\
	meta/errors.h				\
	core/eventqueue.c			\
	core/eventqueue.h			\
	core/frame.c				\
	core/frame.h				\
	ui/gradient.c				\
	meta/gradient.h				\
	core/group-private.h			\
	core/group-props.c			\
	core/group-props.h			\
	core/group.c				\
	meta/group.h				\
	core/iconcache.c			\
	core/iconcache.h			\
	core/keybindings.c			\
	core/keybindings-private.h		\
	core/main.c				\
	core/muffin-Xatomtype.h			\
	core/place.c				\
	core/place.h				\
	core/prefs.c				\
	meta/prefs.h				\
	core/screen.c				\
	core/screen-private.h			\
	meta/screen.h				\
	meta/types.h				\
	core/session.c				\
	core/session.h				\
	core/stack.c				\
	core/stack.h				\
	core/stack-tracker.c			\
	core/stack-tracker.h			\
	core/util.c				\
	meta/util.h				\
	core/window-props.c			\
	core/window-props.h			\
	core/window.c				\
	core/window-private.h			\
	meta/window.h				\
	core/workspace.c			\
	core/workspace-private.h		\
	core/xprops.c				\
	core/xprops.h				\
	meta/common.h				\
	core/core.h				\
	ui/ui.h					\
	inlinepixbufs.h				\
	ui/frames.c				\
	ui/frames.h				\
	ui/menu.c				\
	ui/menu.h				\
	ui/metaaccellabel.c			\
	ui/metaaccellabel.h			\
	ui/resizepopup.c			\
	ui/resizepopup.h			\
	ui/theme-parser.c			\
	ui/theme.c				\
	meta/theme.h				\
	ui/theme-private.h			\
	ui/ui.c					\
	meta/preview-widget.h			\
	ui/preview-widget.c			\
	$(muffin_built_sources)

libmuffin_la_LDFLAGS = $(WARN_LDFLAGS) -no-undefined
libmuffin_la_LIBADD  = $(MUFFIN_LIBS) -lclutter-glx-1.0

# Headers installed for plugins; introspected information will
# be extracted into Muffin-<version>.gir
libmuffininclude_base_headers =		\
	meta/boxes.h				\
	meta/common.h				\
	meta/compositor-muffin.h		\
	meta/compositor.h			\
	meta/display.h				\
	meta/errors.h				\
	meta/gradient.h				\
	meta/group.h				\
	meta/keybindings.h			\
	meta/main.h				\
	meta/meta-background-actor.h		\
	meta/meta-plugin.h			\
	meta/meta-shaped-texture.h		\
	meta/meta-shadow-factory.h		\
	meta/meta-window-actor.h		\
	meta/prefs.h				\
	meta/screen.h				\
	meta/theme.h				\
	meta/types.h				\
	meta/util.h				\
	meta/window.h				\
	meta/workspace.h

# Excluded from scanning for introspection but installed
# atomnames.h: macros cause problems for scanning process
libmuffininclude_extra_headers =		\
	meta/preview-widget.h			\
	meta/atomnames.h

libmuffinincludedir = $(includedir)/muffin/meta

libmuffininclude_HEADERS =			\
	$(libmuffininclude_base_headers)	\
	$(libmuffininclude_extra_headers)

muffin_theme_viewer_SOURCES=  \
	ui/theme-viewer.c

bin_PROGRAMS=muffin muffin-theme-viewer

muffin_SOURCES = core/muffin.c
muffin_LDADD = $(MUFFIN_LIBS) libmuffin.la -lclutter-glx-1.0

if HAVE_INTROSPECTION
include $(INTROSPECTION_MAKEFILE)

# Since we don't make any guarantees about stability and we don't support
# parallel install, there's no real reason to change directories, filenames,
# etc. as we change the Muffin tarball version.
# 
# Change the api_version to a muffin specific number since setting it to
# 3.0 causes major dependency issues with mutter on distributions which
# auto generate typelib dependencies in RPMS
api_version = Muffin.0
#api_version = 3.0

# These files are in package-private directories, even though they may be used
# by plugins.  If you're writing a plugin, use g-ir-compiler --add-include-path
# and g-ir-compiler --includedir.
girdir = $(pkglibdir)
gir_DATA = Meta-$(api_version).gir

typelibdir = $(pkglibdir)
typelib_DATA = Meta-$(api_version).typelib

INTROSPECTION_GIRS = Meta-$(api_version).gir

Meta-$(api_version).gir: libmuffin.la
@META_GIR@_INCLUDES = GObject-2.0 CDesktopEnums-3.0 Gdk-3.0 Gtk-3.0 Clutter-1.0 xlib-2.0 xfixes-4.0
@META_GIR@_PACKAGES = clutter-1.0 gtk+-3.0
@META_GIR@_CFLAGS = $(AM_CPPFLAGS)
@META_GIR@_LIBS = libmuffin.la
@META_GIR@_FILES =				\
	muffin-enum-types.h			\
	$(libmuffininclude_base_headers)	\
	$(filter %.c,$(libmuffin_la_SOURCES))
@META_GIR@_SCANNERFLAGS = $(WARN_SCANNERFLAGS) --warn-all --warn-error

endif

muffin_theme_viewer_LDADD= $(MUFFIN_LIBS) libmuffin.la

testboxes_SOURCES = core/testboxes.c core/boxes.c core/util.c
testgradient_SOURCES = ui/testgradient.c
testasyncgetprop_SOURCES = core/testasyncgetprop.c core/async-getprop.c

# NO-OP: work around the fact that source code tested by the programs are
# compiled for library
testasyncgetprop_CFLAGS = $(WARN_CFLAGS) $(AM_CFLAGS)
testboxes_CFLAGS = $(WARN_CFLAGS) $(AM_CFLAGS)

noinst_PROGRAMS=testboxes testgradient testasyncgetprop

testboxes_LDADD = $(MUFFIN_LIBS)
testgradient_LDADD = $(MUFFIN_LIBS) libmuffin.la
testasyncgetprop_LDADD = $(MUFFIN_LIBS)


@INTLTOOL_DESKTOP_RULE@

desktopfilesdir=$(datadir)/applications
desktopfiles_in_files=muffin.desktop.in
desktopfiles_files=$(desktopfiles_in_files:.desktop.in=.desktop)
desktopfiles_DATA = $(desktopfiles_files)

gsettings_SCHEMAS = org.cinnamon.muffin.gschema.xml
@INTLTOOL_XML_NOMERGE_RULE@
@GSETTINGS_RULES@

IMAGES=stock_maximize.png stock_minimize.png stock_delete.png
VARIABLES=stock_maximize_data $(srcdir)/stock_maximize.png \
		  stock_minimize_data $(srcdir)/stock_minimize.png \
		  stock_delete_data $(srcdir)/stock_delete.png

BUILT_SOURCES = inlinepixbufs.h
CLEANFILES =					\
	inlinepixbufs.h				\
	muffin.desktop				\
	org.cinnamon.muffin.gschema.xml		\
	$(xml_DATA)				\
	$(muffin_built_sources)			\
	$(typelib_DATA)				\
	$(gir_DATA)

inlinepixbufs.h: $(IMAGES)
	$(GDK_PIXBUF_CSOURCE) --raw --build-list $(VARIABLES) >$(srcdir)/inlinepixbufs.h

pkgconfigdir = $(libdir)/pkgconfig

pkgconfig_DATA = libmuffin.pc muffin-plugins.pc

EXTRA_DIST=$(desktopfiles_files) 	\
	$(IMAGES) 			\
	$(desktopfiles_in_files)	\
	$(xml_in_files)			\
	org.cinnamon.muffin.gschema.xml.in \
	libmuffin.pc.in \
	muffin-plugins.pc.in  \
	muffin-enum-types.h.in \
	muffin-enum-types.c.in

BUILT_SOURCES += $(muffin_built_sources)
MUFFIN_STAMP_FILES = stamp-muffin-enum-types.h
CLEANFILES += $(MUFFIN_STAMP_FILES)

muffin-enum-types.h: stamp-muffin-enum-types.h Makefile
	@true
stamp-muffin-enum-types.h: $(libmuffininclude_base_headers) muffin-enum-types.h.in
	$(AM_V_GEN) ( cd $(srcdir) && \
	  $(GLIB_MKENUMS) \
		--template muffin-enum-types.h.in \
	  $(libmuffininclude_base_headers) ) >> xgen-teth && \
	(cmp -s xgen-teth muffin-enum-types.h || cp xgen-teth muffin-enum-types.h) && \
	rm -f xgen-teth && \
	echo timestamp > $(@F)

muffin-enum-types.c: stamp-muffin-enum-types.h muffin-enum-types.c.in
	  $(AM_V_GEN) ( cd $(srcdir) && \
	  $(GLIB_MKENUMS) \
		--template muffin-enum-types.c.in \
	  $(libmuffininclude_base_headers) ) >> xgen-tetc && \
	cp xgen-tetc muffin-enum-types.c && \
	rm -f xgen-tetc
