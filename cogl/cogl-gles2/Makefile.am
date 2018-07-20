# preamble

NULL =

DISTCLEANFILES =

muffinlibdir = $(libdir)/muffin
muffinlib_LTLIBRARIES = libmuffin-cogl-gles2-@MUFFIN_PLUGIN_API_VERSION@.la

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_builddir)

AM_CFLAGS = $(COGL_DEP_CFLAGS) $(COGL_EXTRA_CFLAGS) $(MAINTAINER_CFLAGS)

libmuffin_cogl_gles2_@MUFFIN_PLUGIN_API_VERSION@_la_SOURCES = cogl-gles2-api.c
libmuffin_cogl_gles2_@MUFFIN_PLUGIN_API_VERSION@_la_LDFLAGS = \
	-no-undefined \
	-rpath $(muffinlibdir) \
	-avoid-version \
	-export-dynamic \
	-export-symbols-regex "^gl*"

coglgles2includedir = $(includedir)/muffin/cogl/cogl-gles2/GLES2
coglgles2include_HEADERS = \
	GLES2/gl2.h \
	GLES2/gl2ext.h \
	GLES2/gl2platform.h

pc_files = muffin-cogl-gles2-$(MUFFIN_PLUGIN_API_VERSION).pc

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pc_files)

DISTCLEANFILES += $(pc_files)
