project('muffin',
  'c',
  version: '3.8.0',
  meson_version: '>=0.40.0',
)
project_url = 'https://github.com/linuxmint/muffin'


###############################################################################
# Project config

i18n = import('i18n')
gnome = import('gnome')
pkgconfig = import('pkgconfig')
cc = meson.get_compiler('c')
conf = configuration_data()

plugin_api_version = 3

gettext_package = meson.project_name()
conf.set_quoted('GETTEXT_PACKAGE', gettext_package)

plugins_dir = join_paths(get_option('libdir'), meson.project_name(), 'plugins')
gnome_keybindings_dir = join_paths(get_option('datadir'), 'gnome-control-center', 'keybindings')


###############################################################################
# Dependencies

math        = cc.find_library('m')
gtk         = dependency('gtk+-3.0',        version: '>=3.9.12')
glib        = dependency('glib-2.0',        version: '>=2.37.3')
gio         = dependency('gio-2.0',         version: '>=2.25.10')
pango       = dependency('pango',           version: '>=1.14.0')
cairo       = dependency('cairo',           version: '>=1.10.0')
cinn_desktop= dependency('cinnamon-desktop',version: '>=2.4.0')
xcomposite  = dependency('xcomposite',      version: '>=0.3')
clutter     = dependency('clutter-1.0',     version: '>=1.14.3')
cogl        = dependency('cogl-1.0',        version: '>=1.13.3')
gl          = dependency('gl')

x11lib      = dependency('x11')
xcursor     = dependency('xcursor')
xdamage     = dependency('xdamage')
xext        = dependency('xext')
xfixes      = dependency('xfixes')
xrandr      = dependency('xrandr')
xrender     = dependency('xrender')
ice         = dependency('ice')

gobject_intr= dependency('gobject-introspection-1.0', version: '>=0.9.2')
conf.set('HAVE_INTROSPECTION', gobject_intr.found())



if get_option('startup-notification')
  startup_notif = dependency('libstartup-notification-1.0', version: '>=0.7')
else
  startup_notif = dependency('', required: false)
endif
conf.set('HAVE_STARTUP_NOTIFICATION', startup_notif.found())



if get_option('libcanberra')
  libcanberra = dependency('libcanberra-gtk3', version: '>=0.26')
else
  libcanberra = dependency('', required: false)
endif
conf.set('HAVE_LIBCANBERRA', libcanberra.found())


xinerama_found = false
xinerama_xfree = false
xinerama_solaris = false
if get_option('xinerama')

  if host_machine.system() == 'solaris'
    xinerama_solaris = true
    xinerama_header = 'X11/extensions/xinerama.h'

    xinerama = dependency('', required: false)
  else
    xinerama_xfree = true
    xinerama_header = 'X11/extensions/Xinerama.h'

    xinerama = dependency('xinerama')
  endif

  if not cc.has_header(xinerama_header)
    error('Header '+xinerama_header+' required for xinerama')
  endif
endif

conf.set('HAVE_XINERAMA',         xinerama_found)
conf.set('HAVE_XFREE_XINERAMA',   xinerama_xfree)
conf.set('HAVE_SOLARIS_XINERAMA', xinerama_solaris)


shape_found = false
if get_option('shape')
  shape_found = true
  if not cc.has_header('X11/extensions/shape.h')
    error('Header "X11/extensions/shape.h" required for shape')
  endif
  if not cc.has_function('XShapeQueryExtension', dependencies: xext)
    error('XExt does not provide "XShapeQueryExtension" required for shape')
  endif
endif
conf.set('HAVE_SHAPE', shape_found)


if not cc.has_header('X11/XKBlib.h')
  error('Header "X11/XKBlib.h" required for xkb')
endif
if not cc.has_function('XkbQueryExtension', dependencies: x11lib)
  error('XExt does not provide "XkbQueryExtension" required for xkb')
endif
conf.set('HAVE_XKB', true)




if not cc.has_header('X11/extensions/Xrandr.h')
  error('Header "X11/extensions/Xrandr.h" required for xrandr')
endif
if not cc.has_function('XRRUpdateConfiguration', dependencies: xrandr)
  error('XExt does not provide "XRRUpdateConfiguration" required for xrandr')
endif
conf.set('HAVE_RANDR', true)



if not cc.has_header('X11/extensions/sync.h')
  error('Header "X11/extensions/sync.h" required for xsync')
endif
if not cc.has_function('XSyncQueryExtension', dependencies: xext)
  error('XExt does not provide "XSyncQueryExtension" required for xsync')
endif
conf.set('HAVE_XSYNC', true)


conf.set('HAVE_XCURSOR', true)


x_libs = [
  x11lib,
  xcursor,
  xdamage,
  xext,
  xfixes,
  xrandr,
  xrender,
  ice,

  xinerama,
]


if get_option('sm')
  sm = dependency('sm')
  if not cc.has_header('X11/SM/SMlib.h')
    error('Header "X11/SM/SMlib.h" required for sm', dependency: sm)
  endif
  if not cc.has_function('SmcSaveYourselfDone', dependencies: sm)
    error('XExt does not provide "SmcSaveYourselfDone" required for sm')
  endif

else
  sm = dependency('', required: false)
endif

conf.set('HAVE_SM', sm.found())



conf.set('HAVE_BACKTRACE', cc.has_function('backtrace'))

zenity = find_program('zenity')
###############################################################################
# Paths
datadir    = join_paths(get_option('datadir'))))
pkgdatadir = join_paths(get_option('datadir')),meson.project_name()))
icon_dir   = join_paths(get_option('datadir')),meson.project_name(),'icons'))
pkglibdir  = join_paths(get_option('libdir')), meson.project_name()))
plugin_dir = join_paths(get_option('libdir')), meson.project_name(), 'plugins'))
libexecdir = join_paths(get_option('libexecdir')))
localedir  = join_paths(get_option('localedir')))

conf.set_quoted('MUFFIN_DATADIR',   join_paths(get_option('prefix'), datadir)
conf.set_quoted('MUFFIN_ICON_DIR',  join_paths(get_option('prefix'), icon_dir)
conf.set_quoted('MUFFIN_LIBEXECDIR',join_paths(get_option('prefix'), libexecdir)
conf.set_quoted('MUFFIN_LOCALEDIR', join_paths(get_option('prefix'), localedir)
conf.set_quoted('MUFFIN_PKGDATADIR',join_paths(get_option('prefix'), pkgdatadir)
conf.set_quoted('MUFFIN_PKGLIBDIR', join_paths(get_option('prefix'), pkglibdir)
conf.set_quoted('MUFFIN_PLUGIN_DIR',join_paths(get_option('prefix'), plugin_dir)

# Build options

conf.set_quoted('PACKAGE',      meson.project_name())
conf.set_quoted('VERSION',      meson.project_version())
conf.set_quoted('G_LOG_DOMAIN', meson.project_name())

version = meson.project_version().split('.')
conf.set('MUFFIN_MAJOR_VERSION',     version[0].to_int())
conf.set('MUFFIN_MICRO_VERSION',     version[1].to_int())
conf.set('MUFFIN_MINOR_VERSION',     version[2].to_int())
conf.set('MUFFIN_PLUGIN_API_VERSION',plugin_api_version)


conf.set('CLUTTER_ENABLE_EXPERIMENTAL_API', 1)
conf.set('COGL_ENABLE_EXPERIMENTAL_API',    1)

conf.set('SN_API_NOT_YET_FROZEN',    1)

conf.set('LOCAL_SYMBOL', '__attribute__ ((visibility ("hidden")))')
conf.set('API_EXPORTED', '__attribute__ ((visibility ("default")))')


add_project_arguments([
    '-DHAVE_CONFIG_H',
    '-Wno-deprecated-declarations',
  ],
  language: 'c',
)

configure_file(
  output: 'config.h',
  configuration: conf,
)



rootInclude = include_directories('.')

subdir('po')
subdir('src')
subdir('data')
subdir('doc')


final_message = [
   'muffin-@0@'.format(meson.project_version()),
   '',
   '    prefix:                   @0@'.format(get_option('prefix')),
   '    source code location:     @0@'.format(meson.source_root()),
   '    compiler:                 @0@'.format(cc.get_id()),
   '',
   '    XFree86 Xinerama:         @0@'.format('use_xfree_xinerama'),
   '    Solaris Xinerama:         @0@'.format('use_solaris_xinerama'),
   '    Startup notification:     @0@'.format('have_startup_notification'),
   '    libcanberra:              @0@'.format('have_libcanberra'),
   '    Introspection:            @0@'.format('found_introspection'),
   '    Session management:       @0@'.format('found_sm'),
   '    Shape extension:          @0@'.format('found_shape'),
   '    Xsync:                    @0@'.format('found_xsync'),
   '    Xcursor:                  @0@'.format('have_xcursor'),
   '',
]
message('\n'.join(final_message))
