## This file is part of GPaste.
##
## Copyright (c) 2010-2017, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>

bin_PROGRAMS +=           \
	bin/gpaste-client \
	$(NULL)

bin_gpaste_client_SOURCES =        \
	%D%/client/gpaste-client.c \
	$(NULL)

bin_gpaste_client_CFLAGS = \
	$(GLIB_CFLAGS)     \
	$(GTK_CFLAGS)      \
	$(NULL)

bin_gpaste_client_LDADD =                \
	$(builddir)/$(libgpaste_la_file) \
	$(GLIB_LIBS)                     \
	$(NULL)
