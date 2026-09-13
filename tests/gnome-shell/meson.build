node = find_program('node', required: false)
if node.found()
  test('history-switcher', node,
    args: [files('test-history-switcher.cjs'), meson.project_source_root() / 'src/gnome-shell/historySwitcher.js'],
  )
endif
