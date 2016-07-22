@INTLTOOL_DESKTOP_RULE@

icondir=$(pkgdatadir)/icons
icon_DATA=muffin-window-demo.png

AM_CPPFLAGS=$(WARN_CFLAGS) @MUFFIN_WINDOW_DEMO_CFLAGS@ @MUFFIN_MESSAGE_CFLAGS@ \
	-I$(top_srcdir)/src \
	-DMUFFIN_ICON_DIR=\"$(pkgdatadir)/icons\" \
	-DMUFFIN_LOCALEDIR=\"$(prefix)/@DATADIRNAME@/locale\"

muffin_message_SOURCES= 				\
	muffin-message.c

muffin_window_demo_SOURCES=				\
	muffin-window-demo.c

muffin_mag_SOURCES=					\
	muffin-mag.c

muffin_grayscale_SOURCES=				\
	muffin-grayscale.c

bin_PROGRAMS=muffin-message muffin-window-demo

## cheesy hacks I use, don't really have any business existing. ;-)
noinst_PROGRAMS=muffin-mag muffin-grayscale

muffin_message_LDADD= @MUFFIN_MESSAGE_LIBS@
muffin_window_demo_LDADD= @MUFFIN_WINDOW_DEMO_LIBS@
muffin_mag_LDADD= @MUFFIN_WINDOW_DEMO_LIBS@
muffin_grayscale_LDADD = @MUFFIN_WINDOW_DEMO_LIBS@

EXTRA_DIST=$(icon_DATA)

