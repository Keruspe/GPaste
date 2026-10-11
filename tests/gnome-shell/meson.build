node = find_program('node', required: false)
if node.found()
  test('history-switcher', node,
    args: [files('test-history-switcher.cjs'), meson.project_source_root() / 'src/gnome-shell/historySwitcher.js'],
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
  test('state-switch', node,
    args: [files('test-state-switch.cjs'), meson.project_source_root() / 'src/gnome-shell/stateSwitch.js'],
  )
endif
