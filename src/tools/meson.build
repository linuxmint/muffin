
install_data('muffin-window-demo.png',
  install_dir: join_paths(get_option('datadir'), meson.project_name(), 'icons'),
)

executable('muffin-message',
  'muffin-message.c',
  include_directories: rootInclude,
  dependencies: [ gtk, x11lib, ],
  install: true,
)

executable('muffin-window-demo',
  'muffin-window-demo.c',
  include_directories: rootInclude,
  dependencies: [ gtk, x11lib, ],
  install: true,
)

executable('muffin-mag',
  'muffin-mag.c',
  include_directories: rootInclude,
  dependencies: [ gtk, math, ],
  install: false,
)

executable('muffin-grayscale',
  'muffin-grayscale.c',
  include_directories: rootInclude,
  dependencies: [ gtk, ],
  install: false,
)
