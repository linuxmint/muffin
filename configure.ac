AC_PREREQ(2.50)

m4_define([muffin_major_version], [3])
m4_define([muffin_minor_version], [2])
m4_define([muffin_micro_version], [0])

m4_define([muffin_version],
          [muffin_major_version.muffin_minor_version.muffin_micro_version])

m4_define([muffin_plugin_api_version], [3])

AC_INIT([muffin], [muffin_version],
        [https://github.com/linuxmint/muffin/issues])

m4_ifdef([AX_IS_RELEASE], [AX_IS_RELEASE([always])])

AC_CONFIG_SRCDIR(src/core/display.c)
AC_CONFIG_HEADERS(config.h)

AC_CONFIG_MACRO_DIR(m4)
AM_INIT_AUTOMAKE([1.11 no-dist-gzip dist-xz tar-ustar subdir-objects])
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])],)
AM_MAINTAINER_MODE([enable])

MUFFIN_MAJOR_VERSION=muffin_major_version
MUFFIN_MINOR_VERSION=muffin_minor_version
MUFFIN_MICRO_VERSION=muffin_micro_version
MUFFIN_PLUGIN_API_VERSION=muffin_plugin_api_version
AC_SUBST(MUFFIN_MAJOR_VERSION)
AC_SUBST(MUFFIN_MINOR_VERSION)
AC_SUBST(MUFFIN_MICRO_VERSION)
AC_SUBST(MUFFIN_PLUGIN_API_VERSION)

MUFFIN_PLUGIN_DIR="$libdir/$PACKAGE/plugins"
AC_SUBST(MUFFIN_PLUGIN_DIR)

# Honor aclocal flags
AC_SUBST([ACLOCAL_AMFLAGS], ["-I $ac_macro_dir \${ACLOCAL_FLAGS}"])

GETTEXT_PACKAGE=muffin
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE",[Name of default gettext domain])

IT_PROG_INTLTOOL([0.34.90])
AC_PROG_CC
AM_PROG_CC_C_O
AC_ISC_POSIX
AC_HEADER_STDC
AC_LIBTOOL_WIN32_DLL
AM_PROG_LIBTOOL

# Sets GLIB_GENMARSHAL and GLIB_MKENUMS
AM_PATH_GLIB_2_0()

#### Integer sizes

AC_CHECK_SIZEOF(char)
AC_CHECK_SIZEOF(short)
AC_CHECK_SIZEOF(long)
AC_CHECK_SIZEOF(int)
AC_CHECK_SIZEOF(void *)
AC_CHECK_SIZEOF(long long)
AC_CHECK_SIZEOF(__int64)

## byte order
AC_C_BIGENDIAN

#### Usability of visibility attributes
gl_VISIBILITY
AC_DEFINE(LOCAL_SYMBOL, [], [attribute of the non-exported symbols])
AC_DEFINE(API_EXPORTED, [], [attribute of the symbols exported in the API])
if test $HAVE_VISIBILITY = 1; then
     AC_DEFINE(LOCAL_SYMBOL, [__attribute__ ((visibility ("hidden")))])
     AC_DEFINE(API_EXPORTED, [__attribute__ ((visibility ("default")))])
fi


CANBERRA_GTK=libcanberra-gtk3
CANBERRA_GTK_VERSION=0.26

CLUTTER_PACKAGE=clutter-1.0

MUFFIN_PC_MODULES="
   gtk+-3.0 >= 3.9.12
   gio-2.0 >= 2.25.10
   pango >= 1.14.0
   cairo >= 1.10.0
   cinnamon-desktop >= 2.4.0
   xcomposite >= 0.3 xfixes xrender xdamage
   $CLUTTER_PACKAGE >= 1.14.3
   cogl-1.0 >= 1.13.3
   gl
"

GLIB_GSETTINGS

AC_ARG_ENABLE(verbose-mode,
  AC_HELP_STRING([--disable-verbose-mode],
                 [disable muffin's ability to do verbose logging, for embedded/size-sensitive custom builds]),,
  enable_verbose_mode=yes)
dnl '

if test x$enable_verbose_mode = xyes; then
    AC_DEFINE(WITH_VERBOSE_MODE,1,[Build with verbose mode support])
fi

AC_ARG_ENABLE([gtk-doc],
  AC_HELP_STRING([--enable-gtk-doc],
                 [use gtk-doc to build documentation [[default=yes]]]),,
  enable_gtk_doc=yes)

AC_ARG_ENABLE(sm,
  AC_HELP_STRING([--disable-sm],
                 [disable muffin's session management support, for embedded/size-sensitive custom non-GNOME builds]),,
  enable_sm=auto)
dnl '


AC_ARG_ENABLE(startup-notification,
  AC_HELP_STRING([--disable-startup-notification],
                 [disable muffin's startup notification support, for embedded/size-sensitive custom non-GNOME builds]),,
  enable_startup_notification=auto)
dnl '

AC_ARG_WITH(libcanberra,
  AC_HELP_STRING([--without-libcanberra],
                 [disable the use of libcanberra for playing sounds]),,
  with_libcanberra=auto)

AC_ARG_ENABLE(xsync,
  AC_HELP_STRING([--disable-xsync],
                 [disable muffin's use of the XSync extension]),,
  enable_xsync=auto)
dnl '

AC_ARG_ENABLE(shape,
  AC_HELP_STRING([--disable-shape],
                 [disable muffin's use of the shaped window extension]),,
  enable_shape=auto)
dnl '

## try definining HAVE_BACKTRACE
AC_CHECK_HEADERS(execinfo.h, [AC_CHECK_FUNCS(backtrace)])

AM_GLIB_GNU_GETTEXT

## here we get the flags we'll actually use
PKG_CHECK_MODULES(ALL, glib-2.0 >= 2.37.3)
PKG_CHECK_MODULES(MUFFIN_MESSAGE, gtk+-3.0)
PKG_CHECK_MODULES(MUFFIN_WINDOW_DEMO, gtk+-3.0)

# Unconditionally use this dir to avoid a circular dep with gnomecc
GNOME_KEYBINDINGS_KEYSDIR="${datadir}/gnome-control-center/keybindings"
AC_SUBST(GNOME_KEYBINDINGS_KEYSDIR)

STARTUP_NOTIFICATION_VERSION=0.7
AC_MSG_CHECKING([Startup notification library >= $STARTUP_NOTIFICATION_VERSION])
if $PKG_CONFIG --atleast-version $STARTUP_NOTIFICATION_VERSION libstartup-notification-1.0; then
   have_startup_notification=yes
else
   have_startup_notification=no
fi
AC_MSG_RESULT($have_startup_notification)

if test x$enable_startup_notification = xyes; then
   have_startup_notification=yes
   echo "startup-notification support forced on"
elif test x$enable_startup_notification = xauto; then
   true
else
   have_startup_notification=no
fi

if test x$have_startup_notification = xyes; then
  echo "Building with libstartup-notification"
  MUFFIN_PC_MODULES="$MUFFIN_PC_MODULES libstartup-notification-1.0 >= $STARTUP_NOTIFICATION_VERSION"
  AC_DEFINE(HAVE_STARTUP_NOTIFICATION, , [Building with startup notification support])
else
  echo "Building without libstartup-notification"
fi

have_libcanberra=no
AC_MSG_CHECKING([libcanberra-gtk])
if test x$with_libcanberra = xno ; then
  AC_MSG_RESULT([disabled])
else
  if $PKG_CONFIG --exists $CANBERRA_GTK '>=' $CANBERRA_GTK_VERSION; then
    have_libcanberra=yes
    AC_MSG_RESULT(yes)
    MUFFIN_PC_MODULES="$MUFFIN_PC_MODULES $CANBERRA_GTK"
    AC_DEFINE([HAVE_LIBCANBERRA], 1, [Building with libcanberra for playing sounds])
  else
    AC_MSG_RESULT(no)
    if test x$with_libcanberra = xyes ; then
      AC_MSG_ERROR([libcanberra forced and libcanberra-gtk was not found])
    fi
  fi
fi

INTROSPECTION_VERSION=0.9.2
GOBJECT_INTROSPECTION_CHECK([$INTROSPECTION_VERSION])

if test x$found_introspection != xno; then
  AC_DEFINE(HAVE_INTROSPECTION, 1, [Define if GObject introspection is available])
  MUFFIN_PC_MODULES="$MUFFIN_PC_MODULES gobject-introspection-1.0"
  # Since we don't make any guarantees about stability and we don't support
  # parallel install, there's no real reason to change directories, filenames,
  # etc. as we change the Muffin tarball version. Note that this must match
  # api_version in src/Makefile.am

  # This is set to a magic number which is different from the API which
  # Mutter uses.  Otherwise there will be dependency conflicts in RPM
  # based distributions that automatically resolve typelib file dependencies.
  META_GIR=Meta_Muffin_0_gir
  # META_GIR=[Meta_]muffin_major_version[_]muffin_minor_version[_gir]
  AC_SUBST(META_GIR)
fi

AC_MSG_CHECKING([Xcursor])
if $PKG_CONFIG xcursor; then
     have_xcursor=yes
  else
     have_xcursor=no
  fi
  AC_MSG_RESULT($have_xcursor)

if test x$have_xcursor = xyes; then
  echo "Building with Xcursor"
  MUFFIN_PC_MODULES="$MUFFIN_PC_MODULES xcursor"
  AC_DEFINE(HAVE_XCURSOR, , [Building with Xcursor support])
fi

PKG_CHECK_MODULES(MUFFIN, $MUFFIN_PC_MODULES)

# This is used for plugins
AC_SUBST(CLUTTER_PACKAGE)
PKG_CHECK_MODULES(CLUTTER, $CLUTTER_PACKAGE)

AC_PATH_XTRA

ALL_X_LIBS="$X_LIBS $X_PRE_LIBS -lX11 $X_EXTRA_LIBS"

# Check for Xinerama extension (Solaris impl or Xfree impl)
muffin_save_cppflags="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $X_CFLAGS"

AC_ARG_ENABLE(xinerama,
  AC_HELP_STRING([--disable-xinerama],
                 [disable muffin's use of the Xinerama extension]),
  try_xinerama=$enable_xinerama,try_xinerama=yes)
dnl '

use_solaris_xinerama=no
use_xfree_xinerama=no
if test "${try_xinerama}" != no; then
    case "$host" in
        *-*-solaris*)
            # Check for solaris
            use_solaris_xinerama=yes
            AC_CHECK_LIB(Xext, XineramaGetInfo,
                         use_solaris_xinerama=yes, use_solaris_xinerama=no,
                         $ALL_X_LIBS)
            if test "x$use_solaris_xinerama" = "xyes"; then
                AC_CHECK_HEADER(X11/extensions/xinerama.h,
                                if test -z "`echo $ALL_X_LIBS | grep "\-lXext" 2> /dev/null`"; then
                                    X_EXTRA_LIBS="-lXext $X_EXTRA_LIBS"
                                fi
                                AC_DEFINE(HAVE_SOLARIS_XINERAMA, , [Have Solaris-style Xinerama])
                                AC_DEFINE(HAVE_XINERAMA, , [Have some version of Xinerama]),
                                use_solaris_xinerama=no,
                                [#include <X11/Xlib.h>])
            fi
            AC_MSG_CHECKING(for Xinerama support on Solaris)
            AC_MSG_RESULT($use_solaris_xinerama);
            ;;
        *)
            # Check for XFree
            use_xfree_xinerama=yes
            AC_CHECK_LIB(Xinerama, XineramaQueryExtension,
                [AC_CHECK_HEADER(X11/extensions/Xinerama.h,
                                 X_EXTRA_LIBS="-lXinerama $X_EXTRA_LIBS"
                                 if test -z "`echo $ALL_X_LIBS | grep "\-lXext" 2> /dev/null`"; then
                                     X_EXTRA_LIBS="-lXext $X_EXTRA_LIBS"
                                 fi
                                 AC_DEFINE(HAVE_XFREE_XINERAMA, , [Have XFree86-style Xinerama])
                                 AC_DEFINE(HAVE_XINERAMA,, [Have some version of Xinerama]),
                                 use_xfree_xinerama=no,
                                 [#include <X11/Xlib.h>])],
                use_xfree_xinerama=no, -lXext $ALL_X_LIBS)
            AC_MSG_CHECKING(for Xinerama support on XFree86)
            AC_MSG_RESULT($use_xfree_xinerama);
            ;;
    esac
fi

CPPFLAGS="$muffin_save_cppflags"

SHAPE_LIBS=
found_shape=no
AC_CHECK_LIB(Xext, XShapeQueryExtension,
               [AC_CHECK_HEADER(X11/extensions/shape.h,
                                SHAPE_LIBS=-lXext found_shape=yes)],
               , $ALL_X_LIBS)

if test x$enable_shape = xno; then
   found_shape=no
fi

if test x$enable_shape = xyes; then
   if test "$found_shape" = "no"; then
      AC_MSG_ERROR([--enable-shape forced and Shape not found])
      exit 1
   fi
fi

if test "x$found_shape" = "xyes"; then
   AC_DEFINE(HAVE_SHAPE, , [Have the shape extension library])
fi

found_xkb=no
AC_CHECK_LIB(X11, XkbQueryExtension,
               [AC_CHECK_HEADER(X11/XKBlib.h,
                                found_xkb=yes)],
	, $ALL_X_LIBS)

if test "x$found_xkb" = "xyes"; then
   AC_DEFINE(HAVE_XKB, , [Have keyboard extension library])
fi


RANDR_LIBS=
found_randr=no
AC_CHECK_LIB(Xrandr, XRRUpdateConfiguration,
               [AC_CHECK_HEADER(X11/extensions/Xrandr.h,
                                RANDR_LIBS=-lXrandr found_randr=yes,,
				[#include <X11/Xlib.h>])],
               , -lXrender -lXext $ALL_X_LIBS)

if test "x$found_randr" = "xyes"; then
   AC_DEFINE(HAVE_RANDR, , [Have the Xrandr extension library])
fi

XSYNC_LIBS=
found_xsync=no
AC_CHECK_LIB(Xext, XSyncQueryExtension,
               [AC_CHECK_HEADER(X11/extensions/sync.h,
                                found_xsync=yes,,
				[#include <X11/Xlib.h>])],
               , $ALL_X_LIBS)

if test x$enable_xsync = xno; then
   found_xsync=no
fi

if test x$enable_xsync = xyes; then
   if test "$found_xsync" = "no"; then
      AC_MSG_ERROR([--enable-xsync forced and XSync not found])
      exit 1
   fi
fi

if test "x$found_xsync" = "xyes"; then
   XSYNC_LIBS=-lXext
   AC_DEFINE(HAVE_XSYNC, , [Have the Xsync extension library])
fi

MUFFIN_LIBS="$MUFFIN_LIBS $XSYNC_LIBS $RANDR_LIBS $SHAPE_LIBS $X_LIBS $X_PRE_LIBS -lX11 $X_EXTRA_LIBS -lm"
MUFFIN_MESSAGE_LIBS="$MUFFIN_MESSAGE_LIBS $X_LIBS $X_PRE_LIBS -lX11 $X_EXTRA_LIBS"
MUFFIN_WINDOW_DEMO_LIBS="$MUFFIN_WINDOW_DEMO_LIBS $X_LIBS $X_PRE_LIBS -lX11 $X_EXTRA_LIBS -lm"
MUFFIN_PROPS_LIBS="$MUFFIN_PROPS_LIBS $X_LIBS $X_PRE_LIBS -lX11 $X_EXTRA_LIBS"

found_sm=no
case "$MUFFIN_LIBS" in
 *-lSM*)
    found_sm=yes
    ;;
 *)
  AC_CHECK_LIB(SM, SmcSaveYourselfDone,
               [AC_CHECK_HEADERS(X11/SM/SMlib.h,
                                MUFFIN_LIBS="-lSM -lICE $MUFFIN_LIBS" found_sm=yes)],
               , $MUFFIN_LIBS)
    ;;
esac

if test x$enable_sm = xno; then
   found_sm=no
fi

if test x$enable_sm = xyes; then
   if test "$found_sm" = "no"; then
      AC_MSG_ERROR([--enable-sm forced and -lSM not found])
      exit 1
   fi
fi

if test "$found_sm" = "yes"; then
   AC_DEFINE(HAVE_SM, , [Building with SM support])
fi

AM_CONDITIONAL(HAVE_SM, test "$found_sm" = "yes")

HOST_ALIAS=$host_alias
AC_SUBST(HOST_ALIAS)

AC_PATH_PROG(GDK_PIXBUF_CSOURCE, gdk-pixbuf-csource, no)

if test x"$GDK_PIXBUF_CSOURCE" = xno; then
  AC_MSG_ERROR([gdk-pixbuf-csource executable not found in your path - should be installed with GTK])
fi

AC_SUBST(GDK_PIXBUF_CSOURCE)

AC_PATH_PROG(ZENITY, zenity, no)
if test x"$ZENITY" = xno; then
  AC_MSG_ERROR([zenity not found in your path - needed for dialogs])
fi

AC_ARG_ENABLE(debug,
	[  --enable-debug		enable debugging],,
	enable_debug=no)
if test "x$enable_debug" = "xyes"; then
	CFLAGS="$CFLAGS -g -O"
fi

# Use gnome-doc-utils:
GNOME_DOC_INIT([0.8.0])

GTK_DOC_CHECK([1.15], [--flavour no-tmpl])

AC_CHECK_DECL([GL_EXT_x11_sync_object],
              [],
              [AC_MSG_ERROR([GL_EXT_x11_sync_object definition not found, please update your GL headers])],
              [#include <GL/glx.h>])

m4_ifdef([AX_COMPILER_FLAGS],
         [AX_COMPILER_FLAGS([WARN_CFLAGS],[WARN_LDFLAGS])])

AC_CONFIG_FILES([
Makefile
data/Makefile
doc/Makefile
doc/man/Makefile
doc/reference/Makefile
doc/reference/muffin-docs.sgml
src/Makefile
src/wm-tester/Makefile
src/libmuffin.pc
src/muffin-plugins.pc
src/tools/Makefile
src/compositor/plugins/Makefile
po/Makefile.in
])

AC_OUTPUT

if test x$enable_verbose_mode = xno; then
        echo "*** WARNING WARNING WARNING WARNING WARNING"
        echo "*** Building without verbose mode"
        echo "*** This means there's no way to debug muffin problems."
        echo "*** Please build normal desktop versions of muffin"
        echo "*** with verbose mode enabled so users can use it when they report bugs."
fi

dnl ==========================================================================
echo "
muffin-$VERSION

	prefix:                   ${prefix}
	source code location:	  ${srcdir}
	compiler:		  ${CC}

	XFree86 Xinerama:         ${use_xfree_xinerama}
	Solaris Xinerama:         ${use_solaris_xinerama}
	Startup notification:     ${have_startup_notification}
	libcanberra:              ${have_libcanberra}
	Introspection:            ${found_introspection}
	Session management:       ${found_sm}
	Shape extension:          ${found_shape}
	Xsync:                    ${found_xsync}
	Xcursor:                  ${have_xcursor}
"

