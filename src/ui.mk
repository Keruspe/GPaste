## This file is part of GPaste.
##
## Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>

pkglibexec_PROGRAMS += \
	bin/gpaste-ui  \
	$(NULL)

bin_gpaste_ui_SOURCES =    \
	%D%/ui/gpaste-ui.c \
	$(NULL)

bin_gpaste_ui_CFLAGS = \
	$(GLIB_CFLAGS) \
	$(GTK_CFLAGS)  \
	$(NULL)

bin_gpaste_ui_LDADD =                    \
	$(builddir)/$(libgpaste_la_file) \
	$(GLIB_LIBS)                     \
	$(GTK_LIBS)                      \
	$(NULL)
