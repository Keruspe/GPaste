## This file is part of GPaste.
##
## Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>

if ENABLE_UNITY
pkglibexec_PROGRAMS +=           \
	bin/gpaste-app-indicator \
	$(NULL)
endif

bin_gpaste_app_indicator_SOURCES =                      \
	%D%/app-indicator/gpaste-applet-app-indicator.h \
	%D%/app-indicator/gpaste-applet-app-indicator.c \
	%D%/app-indicator/gpaste-app-indicator.c        \
	$(NULL)

bin_gpaste_app_indicator_CPPFLAGS =   \
	$(AM_CPPFLAGS)                \
	-I$(srcdir)/%D%/app-indicator \
	$(NULL)

bin_gpaste_app_indicator_CFLAGS = \
	$(GLIB_CFLAGS)            \
	$(GTK_CFLAGS)             \
	$(UNITY_CFLAGS)           \
	$(NULL)

bin_gpaste_app_indicator_LDADD =         \
	$(builddir)/$(libgpaste_la_file) \
	$(GLIB_LIBS)                     \
	$(GTK_LIBS)                      \
	$(UNITY_LIBS)                    \
	$(NULL)
