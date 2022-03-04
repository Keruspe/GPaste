libgpaste_sources = [
  'gpaste/gpaste-client-item.c',
  'gpaste/gpaste-client.c',
  'gpaste/gpaste-gnome-shell-client.c',
  'gpaste/gpaste-item-enums.c',
  'gpaste/gpaste-screensaver-client.c',
  'gpaste/gpaste-settings.c',
  'gpaste/gpaste-update-enums.c',
  'gpaste/gpaste-util.c',
]

libgpaste_headers = [
  'gpaste/gpaste-client-item.h',
  'gpaste/gpaste-client.h',
  'gpaste/gpaste-gdbus-defines.h',
  'gpaste/gpaste-gdbus-macros.h',
  'gpaste/gpaste-gnome-shell-client.h',
  'gpaste/gpaste-gsettings-keys.h',
  'gpaste/gpaste-item-enums.h',
  'gpaste/gpaste-macros.h',
  'gpaste/gpaste-screensaver-client.h',
  'gpaste/gpaste-settings.h',
  'gpaste/gpaste-update-enums.h',
  'gpaste/gpaste-util.h',
]

libgpaste_symbols_file = meson.current_source_dir() / 'libgpaste.sym'

libgpaste = library(
  'gpaste',
  sources: libgpaste_sources,
  version: gpaste_soversion,
  dependencies: libgpaste_deps,
  install: true,
  install_dir: get_option('libdir'),
  link_args: ['-Wl,--version-script=' + libgpaste_symbols_file],
)

libgpaste_internal_dep = declare_dependency(
  link_with: libgpaste,
  include_directories: include_directories('.'),
)

install_headers(
  ['gpaste.h'],
  subdir: 'gpaste-' + apiversion,
)
install_headers(
  libgpaste_headers,
  subdir: 'gpaste-' + apiversion + '/gpaste',
)

if get_option('introspection')
  libgpaste_gir = gnome.generate_gir(
    libgpaste,
    sources: [ libgpaste_sources, libgpaste_headers ],
    includes: [ 'Gio-2.0', 'GLib-2.0', 'GObject-2.0' ],
    dependencies: [ libgpaste_deps, libgpaste_internal_dep ],
    header: 'gpaste.h',
    namespace: 'GPaste',
    nsversion: apiversion,
    install: true,
    extra_args: '--pkg-export=libgpaste',
  )
endif

if get_option('vapi')
  gnome.generate_vapi(
    'gpaste-' + apiversion,
    sources: [libgpaste_gir[0]],
    packages: [ 'gio-2.0', 'glib-2.0', 'gobject-2.0' ],
    install: true,
  )
endif
