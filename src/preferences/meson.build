gpaste_preferences_sources = [
  'gpaste-preferences.c',
]

executable(
  'gpaste-preferences',
  sources: gpaste_preferences_sources,
  install: true,
  dependencies: [ glib_dep, gtk4_dep, libadwaita_dep, libgpaste_internal_dep, libgpaste_gtk4_internal_dep ],
  install_dir: join_paths(get_option('libexecdir'), 'gpaste'),
)
