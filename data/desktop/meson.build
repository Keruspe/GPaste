preferences_untranslated_desktop_file = configure_file(
  input: 'org.gnome.GPaste.Preferences.desktop.in.in',
  configuration: conf,
  output: 'org.gnome.GPaste.Preferences.desktop.in',
)

i18n.merge_file(
  input: preferences_untranslated_desktop_file,
  po_dir: gpaste_po_dir,
  type: 'desktop',
  output: 'org.gnome.GPaste.Preferences.desktop',
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications'),
)

ui_untranslated_desktop_file = configure_file(
  input: 'org.gnome.GPaste.Ui.desktop.in.in',
  configuration: conf,
  output: 'org.gnome.GPaste.Ui.desktop.in',
)

i18n.merge_file(
  input: ui_untranslated_desktop_file,
  po_dir: gpaste_po_dir,
  type: 'desktop',
  output: 'org.gnome.GPaste.Ui.desktop',
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications'),
)
