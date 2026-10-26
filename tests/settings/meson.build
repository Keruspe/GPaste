# GNOME Shell's schema, the keys GPaste reads alone, so that the extension's
# state is checked whether or not a shell is installed.
fake_shell_schema = custom_target('fake-shell-schema',
  input: 'shell-schema/org.gnome.shell.gschema.xml',
  output: 'gschemas.compiled',
  build_by_default: true,
  command: [ find_program('glib-compile-schemas'), '--targetdir', meson.current_build_dir(), meson.current_source_dir() / 'shell-schema' ],
)

settings_test_exe = executable(
  'gpaste-settings-test',
  sources: 'test-settings.c',
  dependencies: [ gio_dep, libgpaste_internal_dep ],
)

test('test-settings', settings_test_exe,
  depends: [ gpaste_schemas, fake_shell_schema ],
  env: {
    'GSETTINGS_BACKEND': 'memory',
    'GSETTINGS_SCHEMA_DIR': meson.project_build_root() / 'data/gsettings' + ':' + meson.current_build_dir(),
  },
)
