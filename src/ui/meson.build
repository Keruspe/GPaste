gpaste_ui_sources = [
  'gpaste-ui-about.c',
  'gpaste-ui-delete-item.c',
  'gpaste-ui-edit-item.c',
  'gpaste-ui-header.c',
  'gpaste-ui-history.c',
  'gpaste-ui-item-action.c',
  'gpaste-ui-item-skeleton.c',
  'gpaste-ui-item.c',
  'gpaste-ui-new-item.c',
  'gpaste-ui-panel-history.c',
  'gpaste-ui-panel.c',
  'gpaste-ui-reexec.c',
  'gpaste-ui-search-bar.c',
  'gpaste-ui-search.c',
  'gpaste-ui-settings.c',
  'gpaste-ui-shortcuts-window.c',
  'gpaste-ui-switch.c',
  'gpaste-ui-upload-item.c',
  'gpaste-ui-window.c',
  'gpaste-ui.c',
]

gpaste_ui_headers = [
  'gpaste-ui-about.h',
  'gpaste-ui-delete-item.h',
  'gpaste-ui-edit-item.h',
  'gpaste-ui-header.h',
  'gpaste-ui-history.h',
  'gpaste-ui-item-action.h',
  'gpaste-ui-item-skeleton.h',
  'gpaste-ui-item.h',
  'gpaste-ui-new-item.h',
  'gpaste-ui-panel-history.h',
  'gpaste-ui-panel.h',
  'gpaste-ui-reexec.h',
  'gpaste-ui-search-bar.h',
  'gpaste-ui-search.h',
  'gpaste-ui-settings.h',
  'gpaste-ui-shortcuts-window.h',
  'gpaste-ui-switch.h',
  'gpaste-ui-upload-item.h',
  'gpaste-ui-window.h',
]

gpaste_ui_headers_dep = declare_dependency(
  include_directories: include_directories('../libgpaste'),
  sources: gpaste_ui_headers,
)

executable(
  'gpaste-ui',
  sources: gpaste_ui_sources,
  install: true,
  dependencies: [ glib_dep, gtk4_dep, pango_dep, libadwaita_dep, libgpaste_internal_dep, libgpaste_gtk4_internal_dep, gpaste_ui_headers_dep ],
  install_dir: get_option('libexecdir') / 'gpaste',
)
