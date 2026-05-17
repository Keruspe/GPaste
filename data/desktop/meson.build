foreach component : ['Daemon', 'Preferences', 'Ui']
  untranslated_desktop_file = configure_file(
    input: 'org.gnome.GPaste.' + component + '.desktop.in.in',
    configuration: conf,
    output: 'org.gnome.GPaste.' + component + '.desktop.in',
  )

  i18n.merge_file(
    input: untranslated_desktop_file,
    po_dir: gpaste_po_dir,
    type: 'desktop',
    output: 'org.gnome.GPaste.' + component + '.desktop',
    install: true,
    install_dir: get_option('datadir') / 'applications',
  )
endforeach
