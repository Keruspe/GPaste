executable(
  'gpaste-daemon',
  sources: [ 'gpaste-daemon.c' ],
  install: true,
  dependencies: [ gpaste_daemon_internal_dep ],
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
