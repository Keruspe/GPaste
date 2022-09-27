libgpaste_soversion = '0.0.1'
libgpaste_gtk3_soversion = '0.0.1'
libgpaste_gtk4_soversion = '0.0.1'

pkg = import('pkgconfig')

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
  'gpaste-' + apiversion,
  sources: libgpaste_sources,
  version: libgpaste_soversion,
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

pkg.generate(
  libgpaste,
  name: 'GPaste',
  filebase: 'gpaste-' + apiversion,
  subdirs: 'gpaste-' + apiversion,
  description: 'Library to handle and communicate with GPaste',
  requires: libgpaste_deps,
)

libgpaste_gtk3_sources = [
  'gpaste-gtk3/gpaste-gtk-util.c',
  'gpaste-gtk3/gpaste-gtk-settings-ui-panel.c',
  'gpaste-gtk3/gpaste-gtk-settings-ui-stack.c',
  'gpaste-gtk3/gpaste-gtk-settings-ui-widget.c',
]

libgpaste_gtk3_headers = [
  'gpaste-gtk3/gpaste-gtk-macros.h',
  'gpaste-gtk3/gpaste-gtk-util.h',
  'gpaste-gtk3/gpaste-gtk-settings-ui-panel.h',
  'gpaste-gtk3/gpaste-gtk-settings-ui-stack.h',
  'gpaste-gtk3/gpaste-gtk-settings-ui-widget.h',
]

libgpaste_gtk3_symbols_file = meson.current_source_dir() / 'libgpaste-gtk3.sym'

libgpaste_gtk3_deps = [ libgpaste_internal_dep, gdk_pixbuf_dep, gdk3_dep, gtk3_dep ]

libgpaste_gtk3 = library(
  'gpaste-gtk-3',
  sources: libgpaste_gtk3_sources,
  version: libgpaste_gtk3_soversion,
  dependencies: libgpaste_gtk3_deps,
  install: true,
  install_dir: get_option('libdir'),
  link_args: ['-Wl,--version-script=' + libgpaste_gtk3_symbols_file],
)

libgpaste_gtk3_internal_dep = declare_dependency(
  link_with: libgpaste_gtk3,
  include_directories: include_directories('.'),
)

install_headers(
  ['gpaste-gtk3.h'],
  subdir: 'gpaste-' + apiversion,
)
install_headers(
  libgpaste_gtk3_headers,
  subdir: 'gpaste-' + apiversion + '/gpaste-gtk3',
)

pkg.generate(
  libgpaste_gtk3,
  name: 'GPasteGtk3',
  filebase: 'gpaste-gtk-3',
  subdirs: 'gpaste-' + apiversion,
  description: 'Library to manage GPaste settings and perform graphical operations',
  libraries: libgpaste_gtk3_deps,
)

libgpaste_gtk4_sources = [
  'gpaste-gtk4/gpaste-gtk-preferences-behaviour-page.c',
  'gpaste-gtk4/gpaste-gtk-preferences-group.c',
  'gpaste-gtk4/gpaste-gtk-preferences-history-settings-page.c',
  'gpaste-gtk4/gpaste-gtk-preferences-manager.c',
  'gpaste-gtk4/gpaste-gtk-preferences-page.c',
  'gpaste-gtk4/gpaste-gtk-preferences-shortcuts-page.c',
  'gpaste-gtk4/gpaste-gtk-preferences-widget.c',
  'gpaste-gtk4/gpaste-gtk-preferences-window.c',
  'gpaste-gtk4/gpaste-gtk-util.c',
]

libgpaste_gtk4_headers = [
  'gpaste-gtk4/gpaste-gtk-macros.h',
  'gpaste-gtk4/gpaste-gtk-preferences-behaviour-page.h',
  'gpaste-gtk4/gpaste-gtk-preferences-group.h',
  'gpaste-gtk4/gpaste-gtk-preferences-history-settings-page.h',
  'gpaste-gtk4/gpaste-gtk-preferences-manager.h',
  'gpaste-gtk4/gpaste-gtk-preferences-page.h',
  'gpaste-gtk4/gpaste-gtk-preferences-shortcuts-page.h',
  'gpaste-gtk4/gpaste-gtk-preferences-widget.h',
  'gpaste-gtk4/gpaste-gtk-preferences-window.h',
  'gpaste-gtk4/gpaste-gtk-util.h',
]

libgpaste_gtk4_symbols_file = meson.current_source_dir() / 'libgpaste-gtk4.sym'

libgpaste_gtk4_deps = [ libgpaste_internal_dep, libadwaita_dep, gtk4_dep ]

libgpaste_gtk4 = library(
  'gpaste-gtk4',
  sources: libgpaste_gtk4_sources,
  version: libgpaste_gtk4_soversion,
  dependencies: libgpaste_gtk4_deps,
  install: true,
  install_dir: get_option('libdir'),
  link_args: ['-Wl,--version-script=' + libgpaste_gtk4_symbols_file],
)

libgpaste_gtk4_internal_dep = declare_dependency(
  link_with: libgpaste_gtk4,
  include_directories: include_directories('.'),
)

install_headers(
  ['gpaste-gtk4.h'],
  subdir: 'gpaste-' + apiversion,
)
install_headers(
  libgpaste_gtk4_headers,
  subdir: 'gpaste-' + apiversion + '/gpaste-gtk4',
)

pkg.generate(
  libgpaste_gtk4,
  name: 'GPasteGtk4',
  filebase: 'gpaste-gtk-4',
  subdirs: 'gpaste-' + apiversion,
  description: 'Library to manage GPaste settings and perform graphical operations',
  libraries: libgpaste_gtk4_deps,
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
  )

  libgpaste_gtk3_gir = gnome.generate_gir(
    libgpaste_gtk3,
    sources: [ libgpaste_gtk3_sources, libgpaste_gtk3_headers ],
    includes: [ libgpaste_gir[0], 'Gtk-3.0', 'Gdk-3.0' ],
    dependencies: [ ],
    header: 'gpaste-gtk3.h',
    namespace: 'GPasteGtk',
    nsversion: '3',
    install: true,
  )

  libgpaste_gtk4_gir = gnome.generate_gir(
    libgpaste_gtk4,
    sources: [ libgpaste_gtk4_sources, libgpaste_gtk4_headers ],
    includes: [ libgpaste_gir[0], 'Adw-1', 'Gtk-4.0', 'Gdk-4.0' ],
    dependencies: [ ],
    header: 'gpaste-gtk4.h',
    namespace: 'GPasteGtk',
    nsversion: '4',
    install: true,
  )
endif

if get_option('vapi')
  libgpaste_vapi = gnome.generate_vapi(
    'gpaste-' + apiversion,
    sources: [libgpaste_gir[0]],
    packages: [ 'gio-2.0', 'glib-2.0', 'gobject-2.0' ],
    install: true,
  )

  gnome.generate_vapi(
    'gpaste-gtk-3',
    sources: [libgpaste_gtk3_gir[0]],
    packages: [ libgpaste_vapi, 'gtk+-3.0', 'gdk-3.0' ],
    install: true,
  )

  gnome.generate_vapi(
    'gpaste-gtk-4',
    sources: [libgpaste_gtk4_gir[0]],
    packages: [ libgpaste_vapi, 'gtk4', 'libadwaita-1'],
    install: true,
  )
endif
