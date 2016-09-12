
pkglibdir=@MUFFIN_PLUGIN_DIR@

AM_CPPFLAGS=$(WARN_CFLAGS) @MUFFIN_CFLAGS@ -I $(top_srcdir)/src -DMUFFIN_LIBEXECDIR=\"$(libexecdir)\" -DHOST_ALIAS=\"@HOST_ALIAS@\" -DMUFFIN_LOCALEDIR=\"$(prefix)/@DATADIRNAME@/locale\" -DMUFFIN_PKGDATADIR=\"$(pkgdatadir)\" -DMUFFIN_DATADIR=\"$(datadir)\" -DG_LOG_DOMAIN=\"muffin\" -DSN_API_NOT_YET_FROZEN=1 -DMUFFIN_MAJOR_VERSION=$(MUFFIN_MAJOR_VERSION) -DMUFFIN_MINOR_VERSION=$(MUFFIN_MINOR_VERSION) -DMUFFIN_MICRO_VERSION=$(MUFFIN_MICRO_VERSION) -DMUFFIN_PLUGIN_API_VERSION=$(MUFFIN_PLUGIN_API_VERSION) -DMUFFIN_PLUGIN_DIR=\"@MUFFIN_PLUGIN_DIR@\"

default_la_CFLAGS   = $(WARN_CFLAGS) -fPIC
default_la_SOURCES  = default.c
default_la_LDFLAGS  = $(WARN_LDFLAGS) -module -avoid-version -no-undefined
default_la_LIBADD   = @CLUTTER_LIBS@

pkglib_LTLIBRARIES = default.la

# post-install hook to remove the .la and .a files we are not interested in
# (There is no way to stop libtool generating static libs locally, and we
# cannot do this globally because of libmuffin-private.so).
install-exec-hook:
	-rm $(DESTDIR)$(pkglibdir)/*.a
	-rm $(DESTDIR)$(pkglibdir)/*.la

# Since we removed the .la file, 'make uninstall' doesn't work properly,
# since it counts on libtool to remove the .la files, so just kill the
# .so file manually.
uninstall-local:
	-rm -f $(DESTDIR)$(pkglibdir)/default.so
