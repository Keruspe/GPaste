shell_extension_files = [
  'aboutItem.js',
  'actionButton.js',
  'actionButtonActor.js',
  'actions.js',
  'checkerBypass.js',
  'deleteButton.js',
  'deleteItemPart.js',
  'dependencies.js',
  'dummyHistoryItem.js',
  'emptyHistoryItem.js',
  'extension.js',
  'indicator.js',
  'item.js',
  'padding.js',
  'pageItem.js',
  'pageSwitcher.js',
  'prefs.js',
  'searchItem.js',
  'stateSwitch.js',
  'statusIcon.js',
  'uiItem.js',
]

configured_shell_ext_metadata = configure_file(
  input: 'metadata.json.in',
  configuration: conf,
  output: 'metadata.json',
)

install_data(
  shell_extension_files,
  configured_shell_ext_metadata,
  install_dir: join_paths(get_option('datadir'), 'gnome-shell/extensions/GPaste@gnome-shell-extensions.gnome.org'),
)
