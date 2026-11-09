node = find_program('node', required: false)
if node.found()
  test('history-switcher', node,
    args: [files('test-history-switcher.cjs'), meson.project_source_root() / 'src/gnome-shell/historySwitcher.js'],
  )
  test('actions', node,
    args: [files('test-actions.cjs'), meson.project_source_root() / 'src/gnome-shell/actions.js'],
  )
  test('daemon', node,
    args: [files('test-daemon.cjs'), meson.project_source_root() / 'src/gnome-shell/daemon.js'],
  )
  test('extension', node,
    args: [files('test-extension.cjs'), meson.project_source_root() / 'src/gnome-shell/extension.js'],
  )
  test('indicator', node,
    args: [files('test-indicator.cjs'), meson.project_source_root() / 'src/gnome-shell/indicator.js'],
  )
  test('item-actions', node,
    args: [files('test-item-actions.cjs'), meson.project_source_root() / 'src/gnome-shell/itemActions.js'],
  )
  test('item', node,
    args: [files('test-item.cjs'), meson.project_source_root() / 'src/gnome-shell/item.js'],
  )
  test('prompt', node,
    args: [files('test-prompt.cjs'), meson.project_source_root() / 'src/gnome-shell/prompt.js'],
  )
  test('state-switch', node,
    args: [files('test-state-switch.cjs'), meson.project_source_root() / 'src/gnome-shell/stateSwitch.js'],
  )
  test('search-item', node,
    args: [files('test-search-item.cjs'), meson.project_source_root() / 'src/gnome-shell/searchItem.js'],
  )
  test('dependencies', node,
    args: [files('test-dependencies.cjs'), meson.project_source_root() / 'src/gnome-shell/dependencies.js'],
  )
endif
