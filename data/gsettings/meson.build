gschema_xml = configure_file(
  input: 'org.gnome.GPaste.gschema.xml.in',
  configuration: conf,
  output: 'org.gnome.GPaste.gschema.xml',
  install: true,
  install_dir: join_paths(get_option('datadir'), 'glib-2.0', 'schemas'),
)

# Compile the schema in the build directory for tests
gschema_target = custom_target(
  'compile-gsettings',
  output: 'gschemas.compiled',
  command: [
    find_program('glib-compile-schemas'),
    '--targetdir', meson.current_build_dir(),
    meson.current_build_dir(),
  ],
  depend_files: gschema_xml,
  build_by_default: true,
)
