libgpaste_soversion = '0.1.0'
libgpaste_gtk4_soversion = '1.0.0'

pkg = import('pkgconfig')

libgpaste_sources = [
  'gpaste/gpaste-client-item.c',
  'gpaste/gpaste-client.c',
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
  'gpaste/gpaste-gsettings-keys.h',
  'gpaste/gpaste-item-enums.h',
  'gpaste/gpaste-macros.h',
  'gpaste/gpaste-screensaver-client.h',
  'gpaste/gpaste-settings.h',
  'gpaste/gpaste-update-enums.h',
  'gpaste/gpaste-util.h',
]

libgpaste = library(
  'gpaste-' + apiversion,
  sources: libgpaste_sources,
  version: libgpaste_soversion,
  dependencies: libgpaste_deps,
  install: true,
  install_dir: get_option('libdir'),
  # Exports are the symbols marked G_PASTE_VISIBLE; everything else stays hidden.
  gnu_symbol_visibility: 'hidden',
  link_args: ['-lc'],
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


libgpaste_gtk4_sources = [
  'gpaste-gtk4/gpaste-gtk-preferences-behaviour-page.c',
  'gpaste-gtk4/gpaste-gtk-preferences-dialog.c',
  'gpaste-gtk4/gpaste-gtk-preferences-group.c',
  'gpaste-gtk4/gpaste-gtk-preferences-history-settings-page.c',
  'gpaste-gtk4/gpaste-gtk-preferences-images-page.c',
  'gpaste-gtk4/gpaste-gtk-preferences-page.c',
  'gpaste-gtk4/gpaste-gtk-preferences-shortcuts-page.c',
  'gpaste-gtk4/gpaste-gtk-preferences-widget.c',
  'gpaste-gtk4/gpaste-gtk-shortcut-row.c',
  'gpaste-gtk4/gpaste-gtk-util.c',
]

libgpaste_gtk4_headers = [
  'gpaste-gtk4/gpaste-gtk-macros.h',
  'gpaste-gtk4/gpaste-gtk-preferences-behaviour-page.h',
  'gpaste-gtk4/gpaste-gtk-preferences-dialog.h',
  'gpaste-gtk4/gpaste-gtk-preferences-group.h',
  'gpaste-gtk4/gpaste-gtk-preferences-history-settings-page.h',
  'gpaste-gtk4/gpaste-gtk-preferences-images-page.h',
  'gpaste-gtk4/gpaste-gtk-preferences-page.h',
  'gpaste-gtk4/gpaste-gtk-preferences-shortcuts-page.h',
  'gpaste-gtk4/gpaste-gtk-preferences-widget.h',
  'gpaste-gtk4/gpaste-gtk-shortcut-row.h',
  'gpaste-gtk4/gpaste-gtk-util.h',
]

libgpaste_gtk4_deps = [ libgpaste_internal_dep, libadwaita_dep, gtk4_dep ]

libgpaste_gtk4 = library(
  'gpaste-gtk4',
  sources: libgpaste_gtk4_sources,
  version: libgpaste_gtk4_soversion,
  dependencies: libgpaste_gtk4_deps,
  install: true,
  install_dir: get_option('libdir'),
  gnu_symbol_visibility: 'hidden',
  link_args: ['-lc'],
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
    'gpaste-gtk-4',
    sources: [libgpaste_gtk4_gir[0]],
    packages: [ libgpaste_vapi, 'gtk4', 'libadwaita-1'],
    install: true,
  )
endif

libgpaste_daemon_soversion = '0.0.0'

# The introspectable, always-built core of the daemon library. Optional bits
# (mutter clipboard, libsodium converter, libsecret keyring) are appended below
# but kept out of the GIR/umbrella so introspection stays toolkit-clean.
gpaste_daemon_gir_sources = [
  'gpaste-daemon/gpaste-binary-data.c',
  'gpaste-daemon/gpaste-bus-object.c',
  'gpaste-daemon/gpaste-bus.c',
  'gpaste-daemon/gpaste-clipboard-content.c',
  'gpaste-daemon/gpaste-clipboard-gdk.c',
  'gpaste-daemon/gpaste-clipboard-provider.c',
  'gpaste-daemon/gpaste-clipboards-manager.c',
  'gpaste-daemon/gpaste-color-item.c',
  'gpaste-daemon/gpaste-daemon-methods.c',
  'gpaste-daemon/gpaste-daemon.c',
  'gpaste-daemon/gpaste-file-backend.c',
  'gpaste-daemon/gpaste-global-shortcut-client.c',
  'gpaste-daemon/gpaste-history-saver.c',
  'gpaste-daemon/gpaste-history.c',
  'gpaste-daemon/gpaste-image-item.c',
  'gpaste-daemon/gpaste-item.c',
  'gpaste-daemon/gpaste-keybinder.c',
  'gpaste-daemon/gpaste-keybinding.c',
  'gpaste-daemon/gpaste-noop-backend.c',
  'gpaste-daemon/gpaste-password-item.c',
  'gpaste-daemon/gpaste-search-provider.c',
  'gpaste-daemon/gpaste-special-atom.c',
  'gpaste-daemon/gpaste-sqlite-backend.c',
  'gpaste-daemon/gpaste-storage-backend.c',
  'gpaste-daemon/gpaste-storage-migration.c',
  'gpaste-daemon/gpaste-text-item.c',
  'gpaste-daemon/gpaste-uris-item.c',
]

gpaste_daemon_gir_headers = [
  'gpaste-daemon/gpaste-binary-data.h',
  'gpaste-daemon/gpaste-bus-object.h',
  'gpaste-daemon/gpaste-bus.h',
  'gpaste-daemon/gpaste-clipboard-content.h',
  'gpaste-daemon/gpaste-clipboard-gdk.h',
  'gpaste-daemon/gpaste-clipboard-provider.h',
  'gpaste-daemon/gpaste-clipboards-manager.h',
  'gpaste-daemon/gpaste-color-item.h',
  'gpaste-daemon/gpaste-daemon-methods.h',
  'gpaste-daemon/gpaste-daemon.h',
  'gpaste-daemon/gpaste-file-backend.h',
  'gpaste-daemon/gpaste-global-shortcut-client.h',
  'gpaste-daemon/gpaste-history-saver.h',
  'gpaste-daemon/gpaste-history.h',
  'gpaste-daemon/gpaste-image-item.h',
  'gpaste-daemon/gpaste-item.h',
  'gpaste-daemon/gpaste-keybinder.h',
  'gpaste-daemon/gpaste-keybinding.h',
  'gpaste-daemon/gpaste-noop-backend.h',
  'gpaste-daemon/gpaste-password-item.h',
  'gpaste-daemon/gpaste-search-provider.h',
  'gpaste-daemon/gpaste-special-atom.h',
  'gpaste-daemon/gpaste-sqlite-backend.h',
  'gpaste-daemon/gpaste-storage-backend.h',
  'gpaste-daemon/gpaste-storage-migration.h',
  'gpaste-daemon/gpaste-text-item.h',
  'gpaste-daemon/gpaste-uris-item.h',
]

gpaste_daemon_sources = gpaste_daemon_gir_sources
gpaste_daemon_headers = gpaste_daemon_gir_headers

gpaste_daemon_deps = [ gcr_dep, glib_dep, gtk4_dep, gtk4x11_dep, libadwaita_dep, libgpaste_internal_dep, libgpaste_gtk4_internal_dep, sqlite3_dep ]

# Optional libsodium-based encryption converter for the history storage.
if libsodium_dep.found()
  gpaste_daemon_sources += [ 'gpaste-daemon/gpaste-secret-stream-converter.c' ]
  gpaste_daemon_headers += [ 'gpaste-daemon/gpaste-secret-stream-converter.h' ]
  gpaste_daemon_deps += libsodium_dep
endif

# Optional libsecret support to remember the passphrase in the keyring.
if libsodium_dep.found() and libsecret_dep.found()
  gpaste_daemon_sources += [ 'gpaste-daemon/gpaste-storage-keyring.c' ]
  gpaste_daemon_headers += [ 'gpaste-daemon/gpaste-storage-keyring.h' ]
  gpaste_daemon_deps += libsecret_dep
endif

# Optional libpwquality support to rate the passphrase strength in the prompt.
if libsodium_dep.found() and libpwquality_dep.found()
  gpaste_daemon_deps += libpwquality_dep
endif

# The mutter (MetaSelection) clipboard backend only makes sense inside
# gnome-shell, so it is built alongside the extension. It calls into libmutter,
# so the daemon links it.
if get_option('gnome-shell')
  gpaste_daemon_sources += [ 'gpaste-daemon/gpaste-clipboard-meta.c' ]
  gpaste_daemon_headers += [ 'gpaste-daemon/gpaste-clipboard-meta.h' ]
  gpaste_daemon_deps += dependency('libmutter-18')
endif

gpaste_daemon_headers_dep = declare_dependency(
  include_directories: include_directories('.'),
  sources: gpaste_daemon_headers,
)

gpaste_daemon_deps += gpaste_daemon_headers_dep

libgpaste_daemon = library(
  'gpaste-daemon',
  sources: gpaste_daemon_sources,
  version: libgpaste_daemon_soversion,
  dependencies: gpaste_daemon_deps,
  install: true,
  install_dir: get_option('libdir'),
  # Exports are the symbols marked G_PASTE_VISIBLE. Optional-feature symbols
  # (encryption, libsecret, the mutter clipboard) are only compiled when the
  # feature is enabled, so they export exactly when present without the version
  # script having to reference symbols that may be undefined.
  gnu_symbol_visibility: 'hidden',
  link_args: ['-lc'],
)

gpaste_daemon_internal_dep = declare_dependency(
  link_with: libgpaste_daemon,
  dependencies: gpaste_daemon_deps,
)

install_headers(
  ['gpaste-daemon.h'],
  subdir: 'gpaste-' + apiversion,
)
install_headers(
  gpaste_daemon_headers,
  subdir: 'gpaste-' + apiversion + '/gpaste-daemon',
)

if get_option('introspection')
  libgpaste_daemon_gir = gnome.generate_gir(
    libgpaste_daemon,
    sources: [ gpaste_daemon_gir_sources, gpaste_daemon_gir_headers ],
    includes: [ libgpaste_gir[0], libgpaste_gtk4_gir[0], 'Adw-1', 'Gtk-4.0', 'Gdk-4.0', 'Gio-2.0', 'GLib-2.0', 'GObject-2.0' ],
    dependencies: [ gpaste_daemon_deps, gpaste_daemon_internal_dep ],
    header: 'gpaste-daemon.h',
    namespace: 'GPasteDaemon',
    nsversion: '1',
    identifier_prefix: 'GPaste',
    symbol_prefix: 'g_paste',
    install: true,
  )
endif
