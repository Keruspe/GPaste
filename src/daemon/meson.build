executable(
  'gpaste-daemon',
  sources: [ 'gpaste-daemon.c' ],
  install: true,
  dependencies: [ gpaste_daemon_internal_dep ],
  install_dir: get_option('libexecdir') / 'gpaste',
)

# A standalone runner for the storage-backend migration / encrypted-history
# unlock, spawned by the GNOME Shell extension (which cannot run gtk/adw itself).
executable(
  'gpaste-daemon-storage-migration',
  sources: [ 'gpaste-daemon-storage-migration.c' ],
  install: true,
  dependencies: [ gpaste_daemon_internal_dep ],
  install_dir: get_option('libexecdir') / 'gpaste',
)
