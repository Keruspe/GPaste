gpaste_daemon_sources = [
  'gpaste-binary-data.c',
  'gpaste-bus-object.c',
  'gpaste-bus.c',
  'gpaste-clipboard-gdk.c',
  'gpaste-clipboard-provider.c',
  'gpaste-color-item.c',
  'gpaste-clipboards-manager.c',
  'gpaste-daemon.c',
  'gpaste-daemon-methods.c',
  'gpaste-file-backend.c',
  'gpaste-global-shortcut-client.c',
  'gpaste-history.c',
  'gpaste-history-saver.c',
  'gpaste-image-item.c',
  'gpaste-item.c',
  'gpaste-keybinder.c',
  'gpaste-keybinding.c',
  'gpaste-noop-backend.c',
  'gpaste-password-item.c',
  'gpaste-search-provider.c',
  'gpaste-special-atom.c',
  'gpaste-storage-backend.c',
  'gpaste-storage-migration.c',
  'gpaste-text-item.c',
  'gpaste-uris-item.c',
]

gpaste_daemon_headers = [
  'gpaste-binary-data.h',
  'gpaste-bus-object.h',
  'gpaste-bus.h',
  'gpaste-clipboard-gdk.h',
  'gpaste-clipboard-provider.h',
  'gpaste-color-item.h',
  'gpaste-clipboards-manager.h',
  'gpaste-daemon.h',
  'gpaste-daemon-methods.h',
  'gpaste-file-backend.h',
  'gpaste-global-shortcut-client.h',
  'gpaste-history.h',
  'gpaste-history-saver.h',
  'gpaste-image-item.h',
  'gpaste-item.h',
  'gpaste-keybinder.h',
  'gpaste-keybinding.h',
  'gpaste-noop-backend.h',
  'gpaste-password-item.h',
  'gpaste-search-provider.h',
  'gpaste-special-atom.h',
  'gpaste-storage-backend.h',
  'gpaste-text-item.h',
  'gpaste-uris-item.h',
]

gpaste_daemon_deps = [ gcr_dep, glib_dep, gtk4_dep, gtk4x11_dep, libadwaita_dep, libgpaste_internal_dep, libgpaste_gtk4_internal_dep ]

# Optional libsodium-based encryption converter for the history storage.
if libsodium_dep.found()
  gpaste_daemon_sources += [ 'gpaste-secret-stream-converter.c' ]
  gpaste_daemon_headers += [ 'gpaste-secret-stream-converter.h' ]
  gpaste_daemon_deps += libsodium_dep
endif

# Optional libsecret support to remember the passphrase in the keyring.
if libsodium_dep.found() and libsecret_dep.found()
  gpaste_daemon_sources += [ 'gpaste-storage-keyring.c' ]
  gpaste_daemon_headers += [ 'gpaste-storage-keyring.h' ]
  gpaste_daemon_deps += libsecret_dep
endif

# The mutter (MetaSelection) clipboard backend only makes sense inside
# gnome-shell, so it is built alongside the extension. It calls into libmutter,
# so the daemon links it.
if get_option('gnome-shell')
  gpaste_daemon_sources += [ 'gpaste-clipboard-meta.c' ]
  gpaste_daemon_headers += [ 'gpaste-clipboard-meta.h' ]
  gpaste_daemon_deps += dependency('libmutter-18')
endif

gpaste_daemon_headers_dep = declare_dependency(
  include_directories: include_directories('.', '../libgpaste'),
  sources: gpaste_daemon_headers,
)

gpaste_daemon_deps += gpaste_daemon_headers_dep

# Everything but main.c, as a static library so the test suite can link the
# daemon-internal types (history, items, backends) without an install.
gpaste_daemon_internal = static_library(
  'gpaste-daemon-internal',
  sources: gpaste_daemon_sources,
  dependencies: gpaste_daemon_deps,
)

gpaste_daemon_internal_dep = declare_dependency(
  link_with: gpaste_daemon_internal,
  dependencies: gpaste_daemon_deps,
)

executable(
  'gpaste-daemon',
  sources: [ 'main.c' ],
  install: true,
  dependencies: [ gpaste_daemon_internal_dep ],
  install_dir: get_option('libexecdir') / 'gpaste',
)
