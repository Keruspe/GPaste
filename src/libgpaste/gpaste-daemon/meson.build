# gnome-shell's search provider interface, generated for GPasteSearchProvider.
#
# Unlike the daemon interface this one has a single consumer in a single library,
# so its symbols stay hidden: no decorator, nothing to export.
#
# As in gpaste-3/, this meson.build exists only to keep the output under
# gpaste-daemon/ so the <gpaste-daemon/...> include style holds.
gpaste_daemon_gdbus_sources = gnome.gdbus_codegen(
  'gpaste-shell-search-provider2',
  sources: files('org.gnome.Shell.SearchProvider2.xml'),
  interface_prefix: 'org.gnome.Shell.',
  namespace: 'G',
  extra_args: [ '--glib-min-required', glib_req_version ],
)
