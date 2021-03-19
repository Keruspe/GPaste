libgpaste_sources = [
  'client/gpaste-client-item.c',
  'client/gpaste-client.c',
  'core/gpaste-clipboard.c',
  'core/gpaste-clipboards-manager.c',
  'core/gpaste-history.c',
  'core/gpaste-image-item.c',
  'core/gpaste-item-enums.c',
  'core/gpaste-item.c',
  'core/gpaste-password-item.c',
  'core/gpaste-special-atom.c',
  'core/gpaste-text-item.c',
  'core/gpaste-update-enums.c',
  'core/gpaste-uris-item.c',
  'daemon/gpaste-bus-object.c',
  'daemon/gpaste-bus.c',
  'daemon/gpaste-daemon.c',
  'daemon/gpaste-search-provider.c',
  'gnome-shell-client/gpaste-gnome-shell-client.c',
  'io/gpaste-file-backend.c',
  'io/gpaste-storage-backend.c',
  'keybinder/gpaste-keybinder.c',
  'keybinder/gpaste-keybinding.c',
  'keybinder/gpaste-make-password-keybinding.c',
  'keybinder/gpaste-pop-keybinding.c',
  'keybinder/gpaste-show-history-keybinding.c',
  'keybinder/gpaste-sync-clipboard-to-primary-keybinding.c',
  'keybinder/gpaste-sync-primary-to-clipboard-keybinding.c',
  'keybinder/gpaste-ui-keybinding.c',
  'keybinder/gpaste-upload-keybinding.c',
  'screensaver-client/gpaste-screensaver-client.c',
  'settings-ui/gpaste-settings-ui-panel.c',
  'settings-ui/gpaste-settings-ui-stack.c',
  'settings-ui/gpaste-settings-ui-widget.c',
  'settings/gpaste-settings.c',
  'util/gpaste-util.c',
]

libgpaste_headers = [
  'client/gpaste-client-item.h',
  'client/gpaste-client.h',
  'core/gpaste-clipboard.h',
  'core/gpaste-clipboards-manager.h',
  'core/gpaste-history.h',
  'core/gpaste-image-item.h',
  'core/gpaste-item-enums.h',
  'core/gpaste-item.h',
  'core/gpaste-password-item.h',
  'core/gpaste-special-atom.h',
  'core/gpaste-text-item.h',
  'core/gpaste-update-enums.h',
  'core/gpaste-uris-item.h',
  'daemon/gpaste-bus-object.h',
  'daemon/gpaste-bus.h',
  'daemon/gpaste-daemon.h',
  'daemon/gpaste-search-provider.h',
  'gnome-shell-client/gpaste-gnome-shell-client.h',
  'gpaste-gdbus-defines.h',
  'gpaste-gsettings-keys.h',
  'gpaste-macros.h',
  'gpaste.h',
  'io/gpaste-file-backend.h',
  'io/gpaste-storage-backend.h',
  'keybinder/gpaste-keybinder.h',
  'keybinder/gpaste-keybinding.h',
  'keybinder/gpaste-make-password-keybinding.h',
  'keybinder/gpaste-pop-keybinding.h',
  'keybinder/gpaste-show-history-keybinding.h',
  'keybinder/gpaste-sync-clipboard-to-primary-keybinding.h',
  'keybinder/gpaste-sync-primary-to-clipboard-keybinding.h',
  'keybinder/gpaste-ui-keybinding.h',
  'keybinder/gpaste-upload-keybinding.h',
  'screensaver-client/gpaste-screensaver-client.h',
  'settings-ui/gpaste-settings-ui-panel.h',
  'settings-ui/gpaste-settings-ui-stack.h',
  'settings-ui/gpaste-settings-ui-widget.h',
  'settings/gpaste-settings.h',
  'util/gpaste-util.h',
]

libgpaste_inc = include_directories(
  '.',
  'client',
  'core',
  'daemon',
  'gnome-shell-client',
  'io',
  'keybinder',
  'screensaver-client',
  'settings',
  'settings-ui',
  'ui',
  'util',
)

libgpaste_symbols_file = meson.current_source_dir() / 'libgpaste.sym'

libgpaste = library(
  'gpaste',
  sources: libgpaste_sources,
  version: gpaste_soversion,
  dependencies: libgpaste_deps,
  install: true,
  install_dir: get_option('libdir'),
  include_directories : libgpaste_inc,
  link_args: ['-Wl,--version-script=' + libgpaste_symbols_file],
)

libgpaste_internal_dep = declare_dependency(
  link_with: libgpaste,
  include_directories: libgpaste_inc,
)

install_headers(
  libgpaste_headers,
  subdir: 'gpaste'
)

if get_option('introspection')
  libgpaste_gir = gnome.generate_gir(
    libgpaste,
    sources: [ libgpaste_sources, libgpaste_headers ],
    includes: [ 'GdkPixbuf-2.0', 'Gio-2.0', 'GObject-2.0', 'Gtk-3.0' ],
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
  sources: libgpaste_gir[0],
  packages: [ 'gdk-pixbuf-2.0', 'gio-2.0', 'glib-2.0', 'gobject-2.0', 'gtk+-3.0' ],
  install: true,
  )
endif
