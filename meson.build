project('muffin', 'c', version : '6.4.1', meson_version : '>=0.56.0', license : 'GPLv2+')

mutter_plugin_api_version = '3'

split_version = meson.project_version().split('.')

# Automatically increase API version each development cycle,
# starting with 0 in 3.23.x
api_version = 0
libmutter_api_version = '@0@'.format(api_version)

# generic version requirements
fribidi_req = '>= 1.0.0'
glib_req = '>= 2.67.3'
gi_req = '>= 0.9.5'
graphene_req = '>= 1.9.3'
gtk3_req = '>= 3.19.8'
gdk_pixbuf_req = '>= 2.0'
uprof_req = '>= 0.3'
pango_req = '>= 1.2.0'
cairo_req = '>= 1.10.0'
pangocairo_req = '>= 1.20'
json_glib_req = '>= 0.12.0'
upower_glib_req = '>= 0.99.0'
xcomposite_req = '>= 0.4'
xkbcommon_req = '>= 0.4.3'
xfixes_req = '>= 3'
xi_req = '>= 1.7.4'
xrandr_req = '>= 1.5.0'
libstartup_notification_req = '>= 0.7'
libcanberra_req = '>= 0.26'
libwacom_req = '>= 0.13'
atk_req = '>= 2.5.3'

# optional version requirements
udev_req = '>= 228'
gudev_req = '>= 232'

# wayland version requirements
wayland_server_req = '>= 1.13.0'
wayland_protocols_req = '>= 1.19'

# native backend version requirements
libinput_req = '>= 1.7'
gbm_req = '>= 10.3'

# screen cast version requirements
libpipewire_req = '>= 0.3.0'

# profiler requirements
sysprof_req = '>= 3.35.2'

gnome = import('gnome')
pkg = import('pkgconfig')
i18n  = import('i18n')
cc = meson.get_compiler('c')

prefix = get_option('prefix')

bindir = join_paths(prefix, get_option('bindir'))
datadir = join_paths(prefix, get_option('datadir'))
libdir = join_paths(prefix, get_option('libdir'))
libexecdir = join_paths(prefix, get_option('libexecdir'))
includedir = join_paths(prefix, get_option('includedir'))
sysconfdir = get_option('sysconfdir')

pkgname = '@0@'.format(meson.project_name())

pkgdatadir = join_paths(datadir, pkgname)
pkglibdir = join_paths(libdir, pkgname)
pkgincludedir = join_paths(includedir, pkgname)

pcdir = join_paths(libdir, 'pkgconfig')

gettext_package = meson.project_name()
localedir = join_paths(datadir, 'locale')

libmutter_name = 'muffin'

mutter_installed_tests_datadir = join_paths(
  datadir, 'installed-tests', libmutter_name)

mutter_installed_tests_libexecdir = join_paths(
  libexecdir, 'installed-tests', libmutter_name)

m_dep = cc.find_library('m', required: true)
x11_dep = dependency('x11')
graphene_dep = dependency('graphene-gobject-1.0', version: graphene_req)
gtk3_dep = dependency('gtk+-3.0', version: gtk3_req)
gdk_pixbuf_dep = dependency('gdk-pixbuf-2.0')
pango_dep = dependency('pango', version: pango_req)
cairo_dep = dependency('cairo', version: cairo_req)
cairo_gobject_dep = dependency('cairo-gobject', version: cairo_req)
pangocairo_dep = dependency('pangocairo', version: pangocairo_req)
fribidi_dep = dependency('fribidi', version: fribidi_req)
glib_dep = dependency('glib-2.0', version: glib_req)
gio_dep = dependency('gio-unix-2.0', version: glib_req)
gio_unix_dep = dependency('gio-unix-2.0', version: glib_req)
gobject_dep = dependency('gobject-2.0', version: glib_req)
gthread_dep = dependency('gobject-2.0', version: glib_req)
gmodule_no_export_dep = dependency('gmodule-no-export-2.0', version: glib_req)
json_glib_dep = dependency('json-glib-1.0', version: json_glib_req)
cinnamon_desktop_dep = dependency('cinnamon-desktop', version: '>= 5.3')
xcomposite_dep = dependency('xcomposite', version: xcomposite_req)
xcursor_dep = dependency('xcursor')
xdamage_dep = dependency('xdamage')
xext_dep = dependency('xext')
xfixes_dep = dependency('xfixes', version: xfixes_req)
xi_dep = dependency('xi', version: xi_req)
xtst_dep = dependency('xtst')
xkbfile_dep = dependency('xkbfile')
xkeyboard_config_dep = dependency('xkeyboard-config')
xkbcommon_dep = dependency('xkbcommon', version: xkbcommon_req)
xkbcommon_x11_dep = dependency('xkbcommon-x11')
xrender_dep = dependency('xrender')
x11_xcb_dep = dependency('x11-xcb')
xrandr_dep = dependency('xrandr', version: xrandr_req)
xcb_randr_dep = dependency('xcb-randr')
xcb_res_dep = dependency('xcb-res')
xinerama_dep = dependency('xinerama')
xau_dep = dependency('xau')
ice_dep = dependency('ice')
atk_dep = dependency('atk', version: atk_req)
libcanberra_dep = dependency('libcanberra', version: libcanberra_req)
dbus_dep = dependency('dbus-1')

# For now always require X11 support
have_x11 = true

have_gl = get_option('opengl')
if have_gl
  gl_dep = dependency('gl')
  gl_libname = get_option('opengl_libname')
endif

have_egl = get_option('egl')
if have_egl
  egl_dep = dependency('egl')
endif

have_glx = get_option('glx')
if have_glx
  if not have_gl
    error('GLX support requires OpenGL to be enabled')
  endif
endif

have_egl_xlib = have_egl and have_x11

have_gles2 = get_option('gles2')
if have_gles2
  gles2_dep = dependency('glesv2')
  gles2_libname = get_option('gles2_libname')

  if not have_egl
    error('GLESv2 support requires EGL to be enabled')
  endif
endif

have_wayland = get_option('wayland')

if have_wayland
  wayland_server_dep = dependency('wayland-server', version: wayland_server_req)
  wayland_client_dep = dependency('wayland-client', version: wayland_server_req)
  wayland_protocols_dep = dependency('wayland-protocols',
                                     version: wayland_protocols_req)
  wayland_egl_dep = dependency('wayland-egl')

  if not have_egl
    error('Wayland support requires EGL to be enabled')
  endif
endif

have_libgudev = get_option('udev')
if have_libgudev
  libudev_dep = dependency('libudev', version: udev_req)
  gudev_dep = dependency('gudev-1.0', version: gudev_req)
endif

have_native_backend = get_option('native_backend')

if have_native_backend
  libdrm_dep = dependency('libdrm')
  libgbm_dep = dependency('gbm', version: gbm_req)
  libinput_dep = dependency('libinput', version: libinput_req)

  libsystemd_dep = dependency('libsystemd', required: false)
  if libsystemd_dep.found()
    logind_provider_dep = libsystemd_dep
  else
    logind_provider_dep = dependency('libelogind')
  endif

  if not have_egl
    error('The native backend requires EGL to be enabled')
  endif

  if not have_gles2
    error('The native backend requires GLESv2 to be enabled')
  endif

  if not have_libgudev
    error('The native backend requires udev to be enabled')
  endif
endif

have_egl_device = get_option('egl_device')

have_wayland_eglstream = get_option('wayland_eglstream')

if have_wayland_eglstream
  wayland_eglstream_protocols_dep = dependency('wayland-eglstream-protocols')
  dl_dep = cc.find_library('dl', required: true)

  if not have_wayland
    error('Wayland EGLStream support requires Wayland to be enabled')
  endif
endif

default_driver = get_option('default_driver')

have_sm = get_option('sm')
if have_sm
  sm_dep = dependency('sm')
endif

have_libwacom = get_option('libwacom')

if have_libwacom
  libwacom_dep = dependency('libwacom', version: libwacom_req)
endif

have_pango_ft2 = get_option('pango_ft2')
if have_pango_ft2
  pangoft2_dep = dependency('pangoft2')
endif

have_startup_notification = get_option('startup_notification')
if have_startup_notification
  libstartup_notification_dep = dependency('libstartup-notification-1.0',
                                           version: libstartup_notification_req)
endif

have_remote_desktop = get_option('remote_desktop')
if have_remote_desktop
  libpipewire_dep = dependency('libpipewire-0.3', version: libpipewire_req)
endif

have_introspection = get_option('introspection')
if have_introspection
  gobject_introspection_dep = dependency('gobject-introspection-1.0')

  introspection_args = [
    '--quiet',
    '-U_GNU_SOURCE',
  ]
endif

have_tests = get_option('tests')
have_core_tests = false
have_cogl_tests = false
have_clutter_tests = false
have_installed_tests = false

if have_tests
  have_core_tests = get_option('core_tests')
  if have_core_tests
    if not have_wayland
      error('Tests require Wayland to be enabled')
    endif
  endif

  have_cogl_tests = get_option('cogl_tests')
  have_clutter_tests = get_option('clutter_tests')
  have_installed_tests = get_option('installed_tests')
endif

have_profiler = get_option('profiler')
if have_profiler
  sysprof_dep = dependency('sysprof-capture-3', version: sysprof_req)
endif

required_functions = [
  'ffs',
  'clz',
  'memmem',
]
foreach function : required_functions
  if not cc.has_function(function)
    error('Required function ' + function + ' missing')
  endif
endforeach

if host_machine.cpu_family() == 'x86'
  add_project_arguments('-ffloat-store', language: 'c')
endif
add_project_arguments('-D_GNU_SOURCE', language: 'c')

buildtype = get_option('buildtype')
if buildtype != 'plain'
  all_warnings = [
    '-fno-strict-aliasing',
    '-Wpointer-arith',
    '-Wmissing-declarations',
    '-Wimplicit-function-declaration',
    '-Wformat=2',
    '-Wformat-nonliteral',
    '-Wformat-security',
    '-Wstrict-prototypes',
    '-Wmissing-prototypes',
    '-Wnested-externs',
    '-Wold-style-definition',
    '-Wundef',
    '-Wunused',
    '-Wcast-align',
    '-Wmissing-noreturn',
    '-Wmissing-format-attribute',
    '-Wmissing-include-dirs',
    '-Wlogical-op',
    '-Wignored-qualifiers',
    '-Werror=redundant-decls',
    '-Werror=implicit',
    '-Werror=nonnull',
    '-Werror=init-self',
    '-Werror=main',
    '-Werror=missing-braces',
    '-Werror=sequence-point',
    '-Werror=return-type',
    '-Werror=trigraphs',
    '-Werror=array-bounds',
    '-Werror=write-strings',
    '-Werror=address',
    '-Werror=int-to-pointer-cast',
    '-Werror=pointer-to-int-cast',
    '-Werror=empty-body',
    '-Werror=write-strings',
  ]
  supported_warnings = cc.get_supported_arguments(all_warnings)
  add_project_arguments(supported_warnings, language: 'c')
endif

if get_option('debug')
  debug_c_args = [
    '-DG_ENABLE_DEBUG',
    '-fno-omit-frame-pointer'
  ]
  supported_debug_c_args = cc.get_supported_arguments(debug_c_args)
  add_project_arguments(supported_debug_c_args, language: 'c')
endif

cc.compiles('void main (void) { __builtin_ffsl (0); __builtin_popcountl (0); }')

cdata = configuration_data()
cdata.set_quoted('GETTEXT_PACKAGE', gettext_package)
cdata.set_quoted('VERSION', meson.project_version())
cdata.set_quoted('PACKAGE_VERSION', meson.project_version())

cdata.set('HAVE_EGL', have_egl)
cdata.set('HAVE_WAYLAND', have_wayland)
cdata.set('HAVE_NATIVE_BACKEND', have_native_backend)
cdata.set('HAVE_REMOTE_DESKTOP', have_remote_desktop)
cdata.set('HAVE_EGL_DEVICE', have_egl_device)
cdata.set('HAVE_WAYLAND_EGLSTREAM', have_wayland_eglstream)
cdata.set('HAVE_LIBGUDEV', have_libgudev)
cdata.set('HAVE_LIBWACOM', have_libwacom)
cdata.set('HAVE_SM', have_sm)
cdata.set('HAVE_STARTUP_NOTIFICATION', have_startup_notification)
cdata.set('HAVE_INTROSPECTION', have_introspection)
cdata.set('HAVE_PROFILER', have_profiler)

xkb_base = xkeyboard_config_dep.get_variable(pkgconfig: 'xkb_base')
cdata.set_quoted('XKB_BASE', xkb_base)

if cc.has_header_symbol('sys/prctl.h', 'prctl')
  cdata.set('HAVE_SYS_PRCTL', 1)
endif

have_xwayland_initfd = false
if have_wayland
  xwayland_dep = dependency('xwayland', required: false)

  xwayland_path = get_option('xwayland_path')
  if xwayland_path == ''
    if xwayland_dep.found()
      xwayland_path = xwayland_dep.get_pkgconfig_variable('xwayland')
    else
      xwayland_path = find_program('Xwayland').full_path()
    endif
  endif
  cdata.set_quoted('XWAYLAND_PATH', xwayland_path)

  # For Xwayland authority file generation.
  if cc.has_header_symbol('sys/random.h', 'getrandom')
    cdata.set('HAVE_SYS_RANDOM', 1)
  elif cc.has_header_symbol('linux/random.h', 'getrandom')
    cdata.set('HAVE_LINUX_RANDOM', 1)
  else
    error('Required function getrandom not found')
  endif

  # For Xwayland -initfd usage
  use_initfd = get_option('xwayland_initfd')
  if xwayland_dep.found()
    xwayland_supports_initfd = xwayland_dep.get_pkgconfig_variable('have_initfd') == 'true'
  else
    xwayland_options = run_command(xwayland_path, '-help')
    xwayland_supports_initfd = xwayland_options.stderr().contains('-initfd')
  endif

  if use_initfd.auto()
    have_xwayland_initfd = xwayland_supports_initfd
  else
    have_xwayland_initfd = use_initfd.enabled()
  endif

  if (have_xwayland_initfd)
    cdata.set('HAVE_XWAYLAND_INITFD', 1)
  endif
endif

#xwayland_grab_default_access_rules = get_option('xwayland_grab_default_access_rules')
xwayland_grab_default_access_rules = ''

cdata.set_quoted('XWAYLAND_GRAB_DEFAULT_ACCESS_RULES',
                 xwayland_grab_default_access_rules)

cdata.set_quoted('MUTTER_PLUGIN_DIR', join_paths(pkglibdir, 'plugins'))
cdata.set_quoted('MUTTER_LOCALEDIR', localedir)
cdata.set_quoted('MUTTER_LIBEXECDIR', libexecdir)
cdata.set_quoted('MUTTER_PKGDATADIR', pkgdatadir)

config_h = configure_file(
  input: 'config.h.meson',
  output: 'config.h',
  configuration: cdata
)

top_includepath = include_directories('.')

subdir('cogl')
subdir('clutter')
subdir('data')
subdir('src')
subdir('po')
subdir('doc/man')

meson.add_install_script('meson/meson-postinstall.sh')

output = [
  '',
  '',
  '   Mutter ' + meson.project_version(),
  '  ===============',
  '',
  '    Prefix....................... ' + prefix,
  '    libexecdir................... ' + libexecdir,
  '    pkgdatadir................... ' + pkgdatadir,
  '',
  '    Rendering APIs:',
  '',
  '        OpenGL................... ' + have_gl.to_string(),
  '        GLES2.................... ' + have_gles2.to_string(),
  '        EGL...................... ' + have_egl.to_string(),
  '        GLX...................... ' + have_glx.to_string(),
  '',
  '    Options:',
  '',
  '        Wayland.................. ' + have_wayland.to_string(),
  '        Wayland EGLStream........ ' + have_wayland_eglstream.to_string(),
  '        Native Backend........... ' + have_native_backend.to_string(),
  '        EGL Device............... ' + have_egl_device.to_string(),
  '        Default driver........... ' + default_driver,
  '        Remote desktop........... ' + have_remote_desktop.to_string(),
  '        gudev.................... ' + have_libgudev.to_string(),
  '        Wacom.................... ' + have_libwacom.to_string(),
  '        SM....................... ' + have_sm.to_string(),
  '        Startup notification..... ' + have_startup_notification.to_string(),
  '        Introspection............ ' + have_introspection.to_string(),
  '        Profiler................. ' + have_profiler.to_string(),
  '        Xwayland initfd.......... ' + have_xwayland_initfd.to_string(),
  '',
  '    Tests:',
  '',
  '        Enabled.................. ' + have_tests.to_string(),
  '        Core tests............... ' + have_core_tests.to_string(),
  '        Cogl tests............... ' + have_cogl_tests.to_string(),
  '        Clutter tests............ ' + have_clutter_tests.to_string(),
  '        Installed tests.......... ' + have_installed_tests.to_string(),
  '',
  '  Now type \'ninja -C ' + meson.project_build_root() + '\' to build ' + meson.project_name(),
  '',
  '',
]
message('\n'.join(output))
