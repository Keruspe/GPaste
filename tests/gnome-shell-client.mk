## This file is part of GPaste.
##
## Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>

TESTS+=                             \
	bin/test-gnome-shell-client \
	$(NULL)

bin_test_gnome_shell_client_SOURCES =                    \
	%D%/gnome-shell-client/test-gnome-shell-client.c \
	$(NULL)

bin_test_gnome_shell_client_CFLAGS = \
	$(GLIB_CFLAGS)               \
	$(GTK_CFLAGS)                \
	$(NULL)

bin_test_gnome_shell_client_LDADD =      \
	$(builddir)/$(libgpaste_la_file) \
	$(GLIB_LIBS)                     \
	$(NULL)
