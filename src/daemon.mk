## This file is part of GPaste.
##
## Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>

gpaste_daemon_binary = bin/gpaste-daemon

pkglibexec_PROGRAMS +=          \
	$(gpaste_daemon_binary) \
	$(NULL)

$(gpaste_daemon_binary): $(libgpaste_la_file)

bin_gpaste_daemon_SOURCES =        \
	%D%/daemon/gpaste-daemon.c \
	$(NULL)

bin_gpaste_daemon_CFLAGS = \
	$(GLIB_CFLAGS)     \
	$(GTK_CFLAGS)      \
	$(NULL)

bin_gpaste_daemon_LDADD =                \
	$(builddir)/$(libgpaste_la_file) \
	$(GLIB_LIBS)                     \
	$(GTK_LIBS)                      \
	$(NULL)
