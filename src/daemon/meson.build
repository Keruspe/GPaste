gpaste_daemon_sources = [
  'gpaste-binary-data.c',
  'gpaste-bus-object.c',
  'gpaste-bus.c',
  'gpaste-clipboard.c',
  'gpaste-color-item.c',
  'gpaste-clipboards-manager.c',
  'gpaste-daemon.c',
  'main.c',
  'gpaste-file-backend.c',
  'gpaste-history.c',
  'gpaste-image-item.c',
  'gpaste-item.c',
  'gpaste-keybinder.c',
  'gpaste-keybinding.c',
  'gpaste-password-item.c',
  'gpaste-search-provider.c',
  'gpaste-special-atom.c',
  'gpaste-storage-backend.c',
  'gpaste-text-item.c',
  'gpaste-uris-item.c',
]

gpaste_daemon_headers = [
  'gpaste-binary-data.h',
  'gpaste-bus-object.h',
  'gpaste-bus.h',
  'gpaste-clipboard.h',
  'gpaste-color-item.h',
  'gpaste-clipboards-manager.h',
  'gpaste-daemon.h',
  'gpaste-file-backend.h',
  'gpaste-history.h',
  'gpaste-image-item.h',
  'gpaste-item.h',
  'gpaste-keybinder.h',
  'gpaste-keybinding.h',
  'gpaste-password-item.h',
  'gpaste-search-provider.h',
  'gpaste-special-atom.h',
  'gpaste-storage-backend.h',
  'gpaste-text-item.h',
  'gpaste-uris-item.h',
]

gpaste_daemon_headers_dep = declare_dependency(
  include_directories: include_directories('.', '../libgpaste'),
  sources: gpaste_daemon_headers,
)

executable(
  'gpaste-daemon',
  sources: gpaste_daemon_sources,
  install: true,
  dependencies: [ gcr_dep, glib_dep, gtk4_dep, gtk4x11_dep, libadwaita_dep, libgpaste_internal_dep, libgpaste_gtk4_internal_dep, gpaste_daemon_headers_dep ],
  install_dir: get_option('libexecdir') / 'gpaste',
)
