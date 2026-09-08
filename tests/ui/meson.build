ui_test_sources = ['test-shortcuts.c']
foreach source : gpaste_ui_sources
  if source != 'gpaste-ui.c' and source != 'gpaste-ui-window.c'
    ui_test_sources += '../../src/ui' / source
  endif
endforeach

ui_shortcuts_test = executable(
  'gpaste-ui-shortcuts-test',
  sources: ui_test_sources,
  include_directories: include_directories('../../src/ui'),
  dependencies: [ gtk4_dep, libadwaita_dep, libgpaste_internal_dep, libgpaste_gtk4_internal_dep ],
)

test('test-ui-shortcuts', ui_shortcuts_test,
  depends: gpaste_schemas,
  env: {
    'GSETTINGS_BACKEND': 'memory',
    'GSETTINGS_SCHEMA_DIR': meson.project_build_root() / 'data/gsettings',
    'GTK_A11Y': 'none',
    'GIO_USE_VFS': 'local',
    'ADW_DISABLE_PORTAL': '1',
  },
)
