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
  dependencies: [ gtk4_dep, gpaste_test_env_dep, libadwaita_dep, libgpaste_internal_dep, libgpaste_gtk4_internal_dep ],
)

test('test-ui-shortcuts', ui_shortcuts_test, depends: [gpaste_schemas, test_bus_supervisor])

ui_password_test = executable(
  'gpaste-ui-password-test',
  sources: ['test-password-dialog.c'],
  include_directories: include_directories('../../src/ui'),
  dependencies: [gtk4_dep, gpaste_test_env_dep, libadwaita_dep, libgpaste_internal_dep, libgpaste_gtk4_internal_dep],
)

test('test-ui-password', ui_password_test, depends: [gpaste_schemas, test_bus_supervisor])

ui_edit_item_test = executable(
  'gpaste-ui-edit-item-test',
  sources: ['test-edit-item.c'],
  include_directories: include_directories('../../src/ui'),
  dependencies: [gtk4_dep, gpaste_test_env_dep, libadwaita_dep, libgpaste_internal_dep, libgpaste_gtk4_internal_dep],
)

test('test-ui-edit-item', ui_edit_item_test, depends: [gpaste_schemas, test_bus_supervisor])

ui_text_dialog_test = executable(
  'gpaste-ui-text-dialog-test',
  sources: ['test-text-dialog.c'],
  dependencies: [gtk4_dep, gpaste_test_env_dep, libadwaita_dep, libgpaste_internal_dep, libgpaste_gtk4_internal_dep],
)

test('test-ui-text-dialog', ui_text_dialog_test, depends: [gpaste_schemas, test_bus_supervisor])
