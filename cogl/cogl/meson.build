cogl_cogl_includesubdir = join_paths(cogl_includesubdir, 'cogl')
cogl_cogl_includedir = join_paths(cogl_includedir, 'cogl')

cdata = configuration_data()
cdata.set('COGL_HAS_GL', have_gl)
cdata.set('CLUTTER_COGL_HAS_GL', have_gl)
cdata.set('COGL_HAS_GLX_SUPPORT', have_glx)
cdata.set('COGL_HAS_WAYLAND_EGL_SERVER_SUPPORT', have_wayland)
cdata.set('COGL_HAS_EGL_PLATFORM_XLIB_SUPPORT', have_egl_xlib)
cdata.set('COGL_HAS_EGL_SUPPORT', have_egl)
cdata.set('COGL_HAS_X11', have_x11)
cdata.set('COGL_HAS_X11_SUPPORT', have_x11)
cdata.set('COGL_HAS_XLIB', have_x11)
cdata.set('COGL_HAS_XLIB_SUPPORT', have_x11)
cdata.set('COGL_HAS_TRACING', have_profiler)

cogl_defines_h = configure_file(
  input: 'cogl-defines.h.meson',
  output: 'cogl-defines.h',
  configuration: cdata,
  install_dir: cogl_cogl_includedir,
  install: true,
)

if have_gl
  cogl_gl_header_includes = ['GL/gl.h']
elif have_gles2
  cogl_gl_header_includes = ['GLES2/gl2.h', 'GLES2/gl2ext.h']
else
  error('Neither GLES2 or OpenGL was enabled')
endif

cogl_gl_header_includes_string = ''
foreach gl_header : cogl_gl_header_includes
 cogl_gl_header_includes_string += '#include <@0@>\n'.format(gl_header)
endforeach

built_headers = []

cdata = configuration_data()
cdata.set('COGL_GL_HEADER_INCLUDES', cogl_gl_header_includes_string)

cogl_gl_header_h = configure_file(
  input: 'cogl-gl-header.h.in',
  output: 'cogl-gl-header.h',
  configuration: cdata,
  install: false,
)
built_headers += [cogl_gl_header_h]

if have_egl
  cogl_egl_includes_string = '#include <EGL/egl.h>\n#include <EGL/eglext.h>\n#include <EGL/eglmesaext.h>'
else
  cogl_egl_includes_string = ''
endif

cdata = configuration_data()
cdata.set('COGL_EGL_INCLUDES', cogl_egl_includes_string)
cogl_egl_defines_h = configure_file(
  input: 'cogl-egl-defines.h.in',
  output: 'cogl-egl-defines.h',
  configuration: cdata,
  install: false,
)
built_headers += [cogl_gl_header_h]

cogl_deprecated_headers = [
  'deprecated/cogl-material-compat.h',
  'deprecated/cogl-shader.h',
  'deprecated/cogl-clutter.h',
  'deprecated/cogl-type-casts.h',
  'deprecated/cogl-auto-texture.h',
]

cogl_headers = [
  'cogl1-context.h',
  'cogl-bitmap.h',
  'cogl-color.h',
  'cogl-context.h',
  'cogl-framebuffer.h',
  'cogl-matrix.h',
  'cogl-object.h',
  'cogl-offscreen.h',
  'cogl-onscreen.h',
  'cogl-pipeline.h',
  'cogl-pipeline-state.h',
  'cogl-pipeline-layer-state.h',
  'cogl-pixel-format.h',
  'cogl-texture.h',
  'cogl-texture-2d.h',
  'cogl-texture-2d-sliced.h',
  'cogl-types.h',
  'cogl-trace.h',
  'cogl.h',
]

cogl_nonintrospected_headers = [
  'cogl-renderer.h',
  'cogl-swap-chain.h',
  'cogl-onscreen-template.h',
  'cogl-dma-buf-handle.h',
  'cogl-display.h',
  'cogl-snippet.h',
  'cogl-index-buffer.h',
  'cogl-attribute-buffer.h',
  'cogl-indices.h',
  'cogl-attribute.h',
  'cogl-primitive.h',
  'cogl-frame-info.h',
  'cogl-output.h',
  'cogl-matrix-stack.h',
  'cogl-poll.h',
  'cogl-sub-texture.h',
  'cogl-atlas-texture.h',
  'cogl-meta-texture.h',
  'cogl-primitive-texture.h',
  'cogl-depth-state.h',
  'cogl-buffer.h',
  'cogl-pixel-buffer.h',
  'cogl-macros.h',
  'cogl-fence.h',
  'cogl-version.h',
  'cogl-gtype-private.h',
  'cogl-glib-source.h',
  'cogl-scanout.h',
]

cogl_nodist_headers = [
]

cogl_noop_driver_sources = [
  'driver/nop/cogl-driver-nop.c',
  'driver/nop/cogl-framebuffer-nop-private.h',
  'driver/nop/cogl-framebuffer-nop.c',
  'driver/nop/cogl-attribute-nop-private.h',
  'driver/nop/cogl-attribute-nop.c',
  'driver/nop/cogl-clip-stack-nop-private.h',
  'driver/nop/cogl-clip-stack-nop.c',
  'driver/nop/cogl-texture-2d-nop-private.h',
  'driver/nop/cogl-texture-2d-nop.c',
]

cogl_gl_prototype_headers = [
  'gl-prototypes/cogl-gles2-functions.h',
  'gl-prototypes/cogl-core-functions.h',
  'gl-prototypes/cogl-in-gles-core-functions.h',
  'gl-prototypes/cogl-in-gles2-core-functions.h',
  'gl-prototypes/cogl-glsl-functions.h',
]

cogl_common_driver_sources = [
  'driver/gl/cogl-util-gl-private.h',
  'driver/gl/cogl-util-gl.c',
  'driver/gl/cogl-framebuffer-gl-private.h',
  'driver/gl/cogl-framebuffer-gl.c',
  'driver/gl/cogl-texture-gl-private.h',
  'driver/gl/cogl-texture-gl.c',
  'driver/gl/cogl-texture-2d-gl-private.h',
  'driver/gl/cogl-texture-2d-gl.c',
  'driver/gl/cogl-attribute-gl-private.h',
  'driver/gl/cogl-attribute-gl.c',
  'driver/gl/cogl-clip-stack-gl-private.h',
  'driver/gl/cogl-clip-stack-gl.c',
  'driver/gl/cogl-buffer-gl-private.h',
  'driver/gl/cogl-buffer-gl.c',
  'driver/gl/cogl-bitmap-gl-private.h',
  'driver/gl/cogl-bitmap-gl.c',
  'driver/gl/cogl-pipeline-opengl.c',
  'driver/gl/cogl-pipeline-opengl-private.h',
  'driver/gl/cogl-pipeline-fragend-glsl.c',
  'driver/gl/cogl-pipeline-fragend-glsl-private.h',
  'driver/gl/cogl-pipeline-vertend-glsl.c',
  'driver/gl/cogl-pipeline-vertend-glsl-private.h',
  'driver/gl/cogl-pipeline-progend-glsl.c',
  'driver/gl/cogl-pipeline-progend-glsl-private.h',
]

gl_driver_sources = [
  'driver/gl/gl/cogl-driver-gl.c',
  'driver/gl/gl/cogl-texture-driver-gl.c',
]

gles_driver_sources = [
  'driver/gl/gles/cogl-driver-gles.c',
  'driver/gl/gles/cogl-texture-driver-gles.c',
]

cogl_driver_sources = [
  cogl_noop_driver_sources,
  cogl_common_driver_sources,
]

if have_gl
  cogl_driver_sources += gl_driver_sources
endif

if have_gles2
  cogl_driver_sources += gles_driver_sources
endif

cogl_sources = [
  cogl_driver_sources,

  'winsys/cogl-winsys-private.h',
  'winsys/cogl-winsys.c',
  'cogl-private.h',
  'cogl-i18n-private.h',
  'cogl-debug.h',
  'cogl-debug-options.h',
  'cogl-dma-buf-handle.c',
  'cogl-gpu-info.c',
  'cogl-gpu-info-private.h',
  'cogl-context-private.h',
  'cogl-context.c',
  'cogl-renderer-private.h',
  'cogl-renderer.h',
  'cogl-renderer.c',
  'cogl-swap-chain-private.h',
  'cogl-swap-chain.h',
  'cogl-swap-chain.c',
  'cogl-onscreen-template-private.h',
  'cogl-onscreen-template.h',
  'cogl-onscreen-template.c',
  'cogl-display-private.h',
  'cogl-display.h',
  'cogl-display.c',
  'cogl-driver.h',
  'cogl.c',
  'cogl-pixel-format.c',
  'cogl-object-private.h',
  'cogl-object.h',
  'cogl-object.c',
  'cogl-util.h',
  'cogl-util.c',
  'cogl-bitmap-private.h',
  'cogl-bitmap.c',
  'cogl-bitmap-conversion.c',
  'cogl-bitmap-packing.h',
  'cogl-primitives-private.h',
  'cogl-primitives.c',
  'cogl-bitmap-pixbuf.c',
  'cogl-clip-stack.h',
  'cogl-clip-stack.c',
  'cogl-feature-private.h',
  'cogl-feature-private.c',
  'cogl-color-private.h',
  'cogl-color.c',
  'cogl-buffer-private.h',
  'cogl-buffer.c',
  'cogl-pixel-buffer-private.h',
  'cogl-pixel-buffer.c',
  'cogl-index-buffer-private.h',
  'cogl-index-buffer.c',
  'cogl-attribute-buffer-private.h',
  'cogl-attribute-buffer.c',
  'cogl-indices-private.h',
  'cogl-indices.c',
  'cogl-attribute-private.h',
  'cogl-attribute.c',
  'cogl-primitive-private.h',
  'cogl-primitive.c',
  'cogl-matrix.c',
  'cogl-matrix-private.h',
  'cogl-matrix-stack.c',
  'cogl-matrix-stack-private.h',
  'cogl-depth-state.c',
  'cogl-depth-state-private.h',
  'cogl-node.c',
  'cogl-node-private.h',
  'cogl-pipeline.c',
  'cogl-pipeline-private.h',
  'cogl-pipeline-layer.c',
  'cogl-pipeline-layer-private.h',
  'cogl-pipeline-state.c',
  'cogl-pipeline-layer-state-private.h',
  'cogl-pipeline-layer-state.c',
  'cogl-pipeline-state-private.h',
  'cogl-pipeline-debug.c',
  'cogl-glsl-shader.c',
  'cogl-glsl-shader-private.h',
  'cogl-glsl-shader-boilerplate.h',
  'cogl-pipeline-snippet-private.h',
  'cogl-pipeline-snippet.c',
  'cogl-pipeline-cache.h',
  'cogl-pipeline-cache.c',
  'cogl-pipeline-hash-table.h',
  'cogl-pipeline-hash-table.c',
  'cogl-sampler-cache.c',
  'cogl-sampler-cache-private.h',
  'cogl-blend-string.c',
  'cogl-blend-string.h',
  'cogl-debug.c',
  'cogl-trace.c',
  'cogl-sub-texture-private.h',
  'cogl-texture-private.h',
  'cogl-texture-2d-private.h',
  'cogl-texture-2d-sliced-private.h',
  'cogl-texture-driver.h',
  'cogl-sub-texture.c',
  'cogl-texture.c',
  'cogl-texture-2d.c',
  'cogl-texture-2d-sliced.c',
  'cogl-rectangle-map.h',
  'cogl-rectangle-map.c',
  'cogl-atlas.h',
  'cogl-atlas.c',
  'cogl-atlas-texture-private.h',
  'cogl-atlas-texture.c',
  'cogl-meta-texture.c',
  'cogl-primitive-texture.c',
  'cogl-blit.h',
  'cogl-blit.c',
  'cogl-spans.h',
  'cogl-spans.c',
  'cogl-journal-private.h',
  'cogl-journal.c',
  'cogl-frame-info-private.h',
  'cogl-frame-info.c',
  'cogl-framebuffer-private.h',
  'cogl-framebuffer.c',
  'cogl-onscreen-private.h',
  'cogl-onscreen.c',
  'cogl-output-private.h',
  'cogl-output.c',
  'cogl-profile.h',
  'cogl-profile.c',
  'cogl-flags.h',
  'cogl-bitmask.h',
  'cogl-bitmask.c',
  'cogl-gtype.c',
  'cogl-gtype-private.h',
  'cogl-point-in-poly-private.h',
  'cogl-point-in-poly.c',
  'cogl-list.c',
  'cogl-list.h',
  'cogl-boxed-value.h',
  'cogl-boxed-value.c',
  'cogl-snippet-private.h',
  'cogl-snippet.c',
  'cogl-poll-private.h',
  'cogl-poll.c',
  'gl-prototypes/cogl-all-functions.h',
  'cogl-memory-stack-private.h',
  'cogl-memory-stack.c',
  'cogl-magazine-private.h',
  'cogl-magazine.c',
  'cogl-closure-list-private.h',
  'cogl-closure-list.c',
  'cogl-fence.c',
  'cogl-fence-private.h',
  'cogl-scanout.c',
  'deprecated/cogl-material-compat.c',
  'deprecated/cogl-program.c',
  'deprecated/cogl-program-private.h',
  'deprecated/cogl-auto-texture.c',
  'deprecated/cogl-shader-private.h',
  'deprecated/cogl-shader.c',
  'deprecated/cogl-clutter.c',
  'cogl-glib-source.c',
  'cogl-muffin.h',
]

if have_x11
  cogl_nonintrospected_headers += [
    'winsys/cogl-texture-pixmap-x11.h',
    'cogl-xlib.h',
  ]
  cogl_sources += [
    'cogl-x11-renderer-private.h',
    'cogl-xlib-private.h',
    'cogl-xlib-renderer-private.h',
    'cogl-xlib-renderer.c',
    'winsys/cogl-texture-pixmap-x11-private.h',
    'winsys/cogl-texture-pixmap-x11.c',
  ]
  cogl_headers += [
    'cogl-xlib-renderer.h'
  ]
endif

if have_glx
  cogl_nonintrospected_headers += [
    'winsys/cogl-glx.h',
  ]
  cogl_sources += [
    'winsys/cogl-glx-display-private.h',
    'winsys/cogl-glx-renderer-private.h',
    'winsys/cogl-winsys-glx-feature-functions.h',
    'winsys/cogl-winsys-glx-private.h',
    'winsys/cogl-winsys-glx.c',
  ]
endif

if have_wayland
  cogl_nonintrospected_headers += [
    'cogl-wayland-server.h',
  ]
endif

if have_egl
  cogl_nonintrospected_headers += [
    'cogl-egl.h',
    cogl_egl_defines_h,
  ]
  cogl_sources += [
    'cogl-egl-private.h',
    'winsys/cogl-winsys-egl.c',
    'winsys/cogl-winsys-egl-feature-functions.h',
    'winsys/cogl-winsys-egl-private.h',
  ]
endif

if have_egl_xlib
  cogl_sources += [
    'winsys/cogl-winsys-egl-x11.c',
    'winsys/cogl-winsys-egl-x11-private.h',
  ]
endif

cogl_introspected_headers = [
  cogl_headers,
  cogl_deprecated_headers,
]

cogl_headers_all = [
  cogl_introspected_headers,
  cogl_nonintrospected_headers,
  cogl_deprecated_headers,
]

cogl_test_deps = []

if have_cogl_tests
  cogl_test_deps += [libmuffin_cogl_test_fixtures_dep]
endif

libmuffin_cogl_name = 'muffin-cogl-' + libmuffin_api_version
libmuffin_cogl = shared_library(libmuffin_cogl_name,
  sources: [cogl_sources, cogl_headers_all],
  version: '0.0.0',
  soversion: 0,
  c_args: cogl_c_args,
  include_directories: cogl_includepath,
  dependencies: [cogl_deps, cogl_test_deps],
  gnu_symbol_visibility: 'hidden',
  install_rpath: pkglibdir,
  install_dir: pkglibdir,
  install: true,
)
libmuffin_cogl_dep = declare_dependency(
  dependencies: [cogl_deps],
  link_with: libmuffin_cogl,
)

if have_introspection
  libmuffin_cogl_gir = gnome.generate_gir(libmuffin_cogl,
    sources: cogl_introspected_headers,
    nsversion: libmuffin_api_version,
    namespace: 'Cogl',
    includes: ['cairo-1.0', 'GL-1.0', 'GObject-2.0', 'Graphene-1.0'],
    dependencies: [cogl_deps],
    extra_args: introspection_args + [
      '-UCOGL_COMPILATION',
      '-D__COGL_H_INSIDE__',
      '-D__COGL_XLIB_H_INSIDE__',
      '-D__COGL_EGL_H_INSIDE__',
      '-D__COGL_GLX_H_INSIDE__',
      '-DCOGL_GIR_SCANNING',
    ],
    header: 'cogl/cogl.h',
    install_dir_gir: pkglibdir,
    install_dir_typelib: pkglibdir,
    install: true
  )
endif

install_headers([
    cogl_headers,
    cogl_nonintrospected_headers,
  ],
  subdir: cogl_cogl_includesubdir)

install_headers([
    cogl_deprecated_headers,
  ],
  subdir: join_paths(cogl_cogl_includesubdir, 'deprecated'))

install_headers(cogl_gl_prototype_headers,
  subdir: join_paths(cogl_cogl_includesubdir, 'gl-prototypes'))

pkg.generate(libmuffin_cogl,
  name: 'Cogl',
  filebase: libmuffin_cogl_name,
  description: 'An object oriented GL/GLES Abstraction/Utility Layer in muffin',
  libraries: [m_dep],
  subdirs: join_paths(pkgname, 'cogl'),
  requires: [cogl_pkg_deps],
  version: meson.project_version(),
  variables: [
    'apiversion=' + libmuffin_api_version,
  ],
  install_dir: pcdir,
)
