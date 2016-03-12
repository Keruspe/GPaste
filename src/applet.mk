## This file is part of GPaste.
##
## Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>

if ENABLE_APPLET
pkglibexec_PROGRAMS +=    \
	bin/gpaste-applet \
	$(NULL)
endif

bin_gpaste_applet_SOURCES =        \
	%D%/applet/gpaste-applet.c \
	$(NULL)

bin_gpaste_applet_CFLAGS = \
	$(GLIB_CFLAGS)     \
	$(GTK_CFLAGS)      \
	$(NULL)

bin_gpaste_applet_LDADD =                \
	$(builddir)/$(libgpaste_la_file) \
	$(GLIB_LIBS)                     \
	$(GTK_LIBS)                      \
	$(NULL)
