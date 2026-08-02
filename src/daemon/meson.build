# The GDK clipboard backend lives here rather than in libgpaste-daemon: it is
# what this executable drives, and nothing else does (the gnome-shell-hosted
# daemon uses the mutter one). It needs the X11 backend, since only X11 delivers
# global selection-ownership notifications.
executable(
  'gpaste-daemon',
  sources: [
    'gpaste-clipboard-gdk.c',
    'gpaste-daemon.c',
    'gpaste-text-content-provider.c',
  ],
  install: true,
  dependencies: [ gpaste_daemon_internal_dep, gtk4x11_dep ],
  install_dir: get_option('libexecdir') / 'gpaste',
)

# A standalone runner for the storage-backend migration / encrypted-history
# unlock (the "migrate" and "decrypt" subcommands), spawned by the GNOME Shell
# extension (which cannot run gtk/adw itself).
executable(
  'gpaste-storage',
  sources: [ 'gpaste-storage.c' ],
  install: true,
  dependencies: [ gpaste_daemon_internal_dep ],
  install_dir: get_option('libexecdir') / 'gpaste',
)
