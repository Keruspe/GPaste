# We're overlinking with libgpaste here, but we need one of it's includes
# and since this isn't installed anyway it doesn't matter and is easier for us.
gnome_shell_client_test_exe = executable(
  'gpaste-gnome-shell-client-test',
  sources: 'test-gnome-shell-client.c',
  dependencies: [ glib_dep, gobject_dep, libgpaste_internal_dep ],
)

test('test-gnome-shell-client', gnome_shell_client_test_exe)
