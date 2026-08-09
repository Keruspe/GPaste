# The generated binding for org.gnome.GPaste2. It lives here, in the lowest
# library, because both sides need it and a GType may only be registered once in
# a process — gnome-shell loads libgpaste and libgpaste-daemon at the same time:
#
#   - GPasteDaemon2Proxy is what GPasteClient derives from, in this library;
#   - GPasteDaemon2Skeleton is what GPasteDaemon exports, in libgpaste-daemon,
#     which links this one.
#
# That second consumer is why the symbols have to be decorated: libgpaste is
# built with hidden visibility, so without G_PASTE_VISIBLE on the generated
# declarations nothing outside this library could link them. The generated
# headers are never installed and never introspected — GPasteClient is the whole
# public story, and it is hand-written.
#
# This meson.build exists only so the output lands in gpaste-3/, keeping the
# <gpaste-3/...> include style the rest of the tree uses.
libgpaste_gdbus_sources = gnome.gdbus_codegen(
  'gpaste-daemon2',
  sources: gpaste_dbus_interface_xml,
  interface_prefix: 'org.gnome.',
  namespace: 'G',
  extra_args: [
    '--glib-min-required', glib_req_version,
    '--symbol-decorator', 'G_PASTE_VISIBLE',
    '--symbol-decorator-header', 'gpaste-3/gpaste-macros.h',
  ],
)
