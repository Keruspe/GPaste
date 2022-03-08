gpaste_client_sources = [
  'gpaste-client.c',
]

executable(
  'gpaste-client',
  sources: gpaste_client_sources,
  install: true,
  dependencies: [ glib_dep, gtk3_dep, libgpaste_internal_dep ],
  install_dir: get_option('bindir'),
)
