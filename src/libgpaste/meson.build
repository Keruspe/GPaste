libgpaste_soversion = '0.0.0'
libgpaste_gtk4_soversion = '1.0.1'

pkg = import('pkgconfig')

libgpaste_sources = [
  'gpaste-3/gpaste-client-item.c',
  'gpaste-3/gpaste-client.c',
  'gpaste-3/gpaste-error.c',
  'gpaste-3/gpaste-item-enums.c',
  'gpaste-3/gpaste-screensaver-client.c',
  'gpaste-3/gpaste-settings.c',
  'gpaste-3/gpaste-storage.c',
  'gpaste-3/gpaste-update-enums.c',
  'gpaste-3/gpaste-util.c',
]

libgpaste_headers = [
  'gpaste-3/gpaste-client-item.h',
  'gpaste-3/gpaste-client.h',
  'gpaste-3/gpaste-error.h',
  'gpaste-3/gpaste-gdbus-defines.h',
  'gpaste-3/gpaste-gdbus-macros.h',
  'gpaste-3/gpaste-gsettings-keys.h',
  'gpaste-3/gpaste-item-enums.h',
  'gpaste-3/gpaste-macros.h',
  'gpaste-3/gpaste-screensaver-client.h',
  'gpaste-3/gpaste-settings.h',
  'gpaste-3/gpaste-storage.h',
  'gpaste-3/gpaste-update-enums.h',
  'gpaste-3/gpaste-util.h',
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
  subdir: 'gpaste',
)
install_headers(
  libgpaste_headers,
  subdir: 'gpaste/gpaste-' + apiversion,
)

pkg.generate(
  libgpaste,
  name: 'GPaste',
  filebase: 'gpaste-' + apiversion,
  subdirs: 'gpaste',
  description: 'Library to handle and communicate with GPaste',
  requires: libgpaste_deps,
  url: 'https://github.com/Keruspe/GPaste',
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
  subdir: 'gpaste',
)
install_headers(
  libgpaste_gtk4_headers,
  subdir: 'gpaste/gpaste-gtk4',
)

pkg.generate(
  libgpaste_gtk4,
  name: 'GPasteGtk4',
  filebase: 'gpaste-gtk4',
  subdirs: 'gpaste',
  description: 'Library to manage GPaste settings and perform graphical operations',
  requires: libgpaste_gtk4_deps,
  url: 'https://github.com/Keruspe/GPaste',
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
    'gpaste-gtk4',
    sources: [libgpaste_gtk4_gir[0]],
    packages: [ libgpaste_vapi, 'gtk4', 'libadwaita-1'],
    install: true,
  )
endif

libgpaste_daemon_soversion = '0.0.0'

# What this library promises to the outside world. The gnome-shell extension is
# its only external consumer and reaches it through the GPasteDaemon-1 typelib:
# it drives GPasteBus, GPasteDaemon, GPasteSearchProvider, GPastePrompt (which
# prompt.js implements) and GPasteStorageBackend's static passphrase helpers.
#
# The rest of this list is what those headers drag in, not a separate promise:
# gpaste-daemon.h declares g_paste_daemon_new(), hence the clipboard provider,
# hence the history, hence the item hierarchy those hand around.
gpaste_daemon_public_sources = [
  'gpaste-daemon/gpaste-binary-data.c',
  'gpaste-daemon/gpaste-bus-object.c',
  'gpaste-daemon/gpaste-bus.c',
  'gpaste-daemon/gpaste-clipboard-provider.c',
  'gpaste-daemon/gpaste-daemon.c',
  'gpaste-daemon/gpaste-history.c',
  'gpaste-daemon/gpaste-item.c',
  'gpaste-daemon/gpaste-passphrase.c',
  'gpaste-daemon/gpaste-password-item.c',
  'gpaste-daemon/gpaste-prompt.c',
  'gpaste-daemon/gpaste-search-provider.c',
  'gpaste-daemon/gpaste-special-atom.c',
  'gpaste-daemon/gpaste-storage-backend.c',
  'gpaste-daemon/gpaste-storage-migration.c',
  'gpaste-daemon/gpaste-text-item.c',
]

gpaste_daemon_public_headers = [
  'gpaste-daemon/gpaste-binary-data.h',
  'gpaste-daemon/gpaste-bus-object.h',
  'gpaste-daemon/gpaste-bus.h',
  'gpaste-daemon/gpaste-clipboard-provider.h',
  'gpaste-daemon/gpaste-daemon.h',
  'gpaste-daemon/gpaste-history.h',
  'gpaste-daemon/gpaste-item.h',
  'gpaste-daemon/gpaste-passphrase.h',
  'gpaste-daemon/gpaste-password-item.h',
  'gpaste-daemon/gpaste-prompt.h',
  'gpaste-daemon/gpaste-search-provider.h',
  'gpaste-daemon/gpaste-special-atom.h',
  'gpaste-daemon/gpaste-storage-backend.h',
  'gpaste-daemon/gpaste-storage-migration.h',
  'gpaste-daemon/gpaste-text-item.h',
]

# Internal machinery: compiled into the library, never installed, kept out of the
# GIR and the umbrella. In-tree consumers (src/daemon, src/ui, tests) reach these
# through gpaste_daemon_headers_dep's include_directories, never the install
# prefix, so nothing in the build depends on them being shipped.
#
# New headers belong here unless the extension needs them.
gpaste_daemon_internal_sources = [
  'gpaste-daemon/gpaste-clipboard-content.c',
  'gpaste-daemon/gpaste-clipboards-manager.c',
  'gpaste-daemon/gpaste-color-item.c',
  'gpaste-daemon/gpaste-daemon-methods.c',
  'gpaste-daemon/gpaste-file-backend.c',
  'gpaste-daemon/gpaste-global-shortcut-client.c',
  'gpaste-daemon/gpaste-history-saver.c',
  'gpaste-daemon/gpaste-image-item.c',
  'gpaste-daemon/gpaste-keybinder.c',
  'gpaste-daemon/gpaste-keybinding.c',
  'gpaste-daemon/gpaste-noop-backend.c',
  'gpaste-daemon/gpaste-uris-item.c',
]

gpaste_daemon_internal_headers = [
  'gpaste-daemon/gpaste-clipboard-content.h',
  'gpaste-daemon/gpaste-clipboards-manager.h',
  'gpaste-daemon/gpaste-color-item.h',
  'gpaste-daemon/gpaste-daemon-methods.h',
  'gpaste-daemon/gpaste-file-backend.h',
  'gpaste-daemon/gpaste-global-shortcut-client.h',
  'gpaste-daemon/gpaste-history-saver.h',
  'gpaste-daemon/gpaste-image-item.h',
  'gpaste-daemon/gpaste-keybinder.h',
  'gpaste-daemon/gpaste-keybinding.h',
  'gpaste-daemon/gpaste-noop-backend.h',
  'gpaste-daemon/gpaste-uris-item.h',
]

# The GIR describes exactly the installed surface, nothing more.
gpaste_daemon_gir_sources = gpaste_daemon_public_sources
gpaste_daemon_gir_headers = gpaste_daemon_public_headers

gpaste_daemon_sources = gpaste_daemon_public_sources + gpaste_daemon_internal_sources
gpaste_daemon_headers = gpaste_daemon_public_headers + gpaste_daemon_internal_headers

# Neither libadwaita nor libgpaste-gtk4. The storage prompts were the only thing
# that ever built a dialog and they live behind GPastePrompt now; the one
# function this library still wanted from libgpaste-gtk4 — an image checksum,
# pure GDK — moved to GPasteImageItem. gtk4 itself stays, for GdkTexture, GdkRGBA
# and gtk_accelerator_parse, but the widget stack does not: gnome-shell hosts
# this library and can initialise neither it nor Adw.
gpaste_daemon_deps = [ gcr_dep, glib_dep, gtk4_dep, libgpaste_internal_dep ]

# The optional features below append to the union lists, so their headers are
# internal by construction: compiled and visible in-tree, but never installed and
# never introspected. That keeps the GIR from changing shape with the feature set.

# Optional libsodium-based encryption converter for the history storage.
if libsodium_dep.found()
  gpaste_daemon_sources += [ 'gpaste-daemon/gpaste-secret-stream-converter.c' ]
  gpaste_daemon_headers += [ 'gpaste-daemon/gpaste-secret-stream-converter.h' ]
  gpaste_daemon_deps += libsodium_dep
endif

# Optional SQLite-based incremental history storage backend.
if sqlite_dep.found()
  gpaste_daemon_sources += [ 'gpaste-daemon/gpaste-sqlite-backend.c' ]
  gpaste_daemon_headers += [ 'gpaste-daemon/gpaste-sqlite-backend.h' ]
  gpaste_daemon_deps += sqlite_dep
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
  gpaste_daemon_deps += dependency('libmutter-51')
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
  subdir: 'gpaste',
)
install_headers(
  gpaste_daemon_public_headers,
  subdir: 'gpaste/gpaste-daemon',
)

# Only gpaste-3: no public daemon header names a toolkit type, so gtk4 and gcr-4
# are private to the implementation even though the library links them. That is
# the same reason the GIR carries no toolkit namespace — gnome-shell hosts this
# library and can initialise neither GTK nor Adwaita.
pkg.generate(
  libgpaste_daemon,
  name: 'GPasteDaemon',
  filebase: 'gpaste-daemon',
  subdirs: 'gpaste',
  description: 'Library to run a GPaste daemon and manipulate its clipboard history',
  requires: libgpaste,
  url: 'https://github.com/Keruspe/GPaste',
)

if get_option('introspection')
  libgpaste_daemon_gir = gnome.generate_gir(
    libgpaste_daemon,
    sources: [ gpaste_daemon_gir_sources, gpaste_daemon_gir_headers ],
    includes: [ libgpaste_gir[0], 'Gtk-4.0', 'Gdk-4.0', 'Gio-2.0', 'GLib-2.0', 'GObject-2.0' ],
    dependencies: [ gpaste_daemon_deps, gpaste_daemon_internal_dep ],
    header: 'gpaste-daemon.h',
    namespace: 'GPasteDaemon',
    nsversion: '1',
    identifier_prefix: 'GPaste',
    symbol_prefix: 'g_paste',
    install: true,
  )
endif
