gpaste_ui_sources = [
  'gpaste-ui.c',
]

executable(
  'gpaste-ui',
  sources: gpaste_ui_sources,
  install: true,
  dependencies: [ glib_dep, gtk_dep, libgpaste_internal_dep ],
  install_dir: join_paths(get_option('libexecdir'), 'gpaste'),
)