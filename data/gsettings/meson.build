gschema_xml = configure_file(
  input: 'org.gnome.GPaste.gschema.xml.in',
  configuration: conf,
  output: 'org.gnome.GPaste.gschema.xml',
  install: true,
  install_dir: get_option('datadir') / 'glib-2.0' / 'schemas',
)

# Compile the schema in the build directory for tests
gnome.compile_schemas(
  depend_files: gschema_xml,
)
