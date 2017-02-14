## This file is part of GPaste.
##
## Copyright (c) 2010-2017, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>

noinst_PROGRAMS +=            \
	bin/clipboard-watcher \
	$(NULL)

bin_clipboard_watcher_SOURCES =                   \
	%D%/clipboard-watcher/clipboard-watcher.c \
	$(NULL)

bin_clipboard_watcher_CFLAGS = \
	$(GLIB_CFLAGS)         \
	$(GTK_CFLAGS)          \
	$(NULL)

bin_clipboard_watcher_LDADD =            \
	$(builddir)/$(libgpaste_la_file) \
	$(GLIB_LIBS)                     \
	$(GTK_LIBS)                      \
	$(NULL)
