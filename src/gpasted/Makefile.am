CLEANFILES = 

VALAC = valac-0.12

INCLUDES =\
	-include config.h

bin_PROGRAMS = gpasted

gpasted_SOURCES = \
	clipboard.vala \
	history.vala \
	gpasted.vala

gpasted_VALAFLAGS = \
	$(GTK_VALAFLAGS) \
	$(GIO_VALAFLAGS) \
	$(GLIB_VALAFLAGS) \
	--pkg posix

gpasted_CFLAGS = \
	$(GTK_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GLIB_CFLAGS)

gpasted_LDADD = \
	$(GTK_LIBS) \
	$(GIO_LIBS) \
	$(GLIB_LIBS)

CLEANFILES += \
	${gpasted_SOURCES:.vala=.c}

