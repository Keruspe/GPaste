ACLOCAL_AMFLAGS  = -I m4 ${ACLOCAL_FLAGS}

CLEANFILES = 

VALAC = valac-0.12

INCLUDES =\
	-include config.h

bin_PROGRAMS = gpasted gpaste

gpasted_SOURCES = \
	src/gpasted/clipboard.vala \
	src/gpasted/history.vala \
	src/gpasted/gpasted.vala

gpasted_VALAFLAGS = \
	$(GTK_VALAFLAGS) \
	$(GIO_VALAFLAGS) \
	$(GLIB_VALAFLAGS) \
	--pkg posix

gpasted_CFLAGS = \
	$(GTK_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GLIB_CFLAGS)

CLEANFILES += \
	${gpasted_SOURCES:.vala=.c}

gpasted_LDADD = \
	$(GTK_LIBS) \
	$(GIO_LIBS) \
	$(GLIB_LIBS)

gpaste_SOURCES = \
	src/gpaste.vala

gpaste_VALAFLAGS = \
	$(GIO_VALAFLAGS) \
	$(GLIB_VALAFLAGS) \
	--pkg posix

gpaste_CFLAGS = \
	$(GIO_CFLAGS) \
	$(GLIB_CFLAGS)

gpaste_LDADD = \
	$(GIO_LIBS) \
	$(GLIB_LIBS)

CLEANFILES += \
	${gpaste_SOURCES:.vala=.c}

