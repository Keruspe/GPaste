gpaste_daemon_sources = [
  'gpaste-daemon.c',
]

executable(
  'gpaste-daemon',
  sources: gpaste_daemon_sources,
  dependencies: [ glib_dep, gtk_dep, libgpaste_internal_dep ],
  install: true,
  install_dir: join_paths(get_option('libexecdir'), 'gpaste'),
)