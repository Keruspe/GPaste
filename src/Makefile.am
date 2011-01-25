CLEANFILES = 

SUBDIRS = gpasted

VALAC = valac-0.12

INCLUDES =\
	-include config.h

bin_PROGRAMS = gpaste

gpaste_SOURCES = \
	gpaste.vala

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

