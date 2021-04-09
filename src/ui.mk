## This file is part of GPaste.
##
## Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>

pkglibexec_PROGRAMS += \
	bin/gpaste-ui  \
	$(NULL)

bin_gpaste_ui_SOURCES =                     \
	%D%/ui/gpaste-ui-about.c            \
	%D%/ui/gpaste-ui-about.h            \
	%D%/ui/gpaste-ui-backup-history.c   \
	%D%/ui/gpaste-ui-backup-history.h   \
	%D%/ui/gpaste-ui-delete-history.c   \
	%D%/ui/gpaste-ui-delete-history.h   \
	%D%/ui/gpaste-ui-delete-item.c      \
	%D%/ui/gpaste-ui-delete-item.h      \
	%D%/ui/gpaste-ui-edit-item.c        \
	%D%/ui/gpaste-ui-edit-item.h        \
	%D%/ui/gpaste-ui-empty-history.c    \
	%D%/ui/gpaste-ui-empty-history.h    \
	%D%/ui/gpaste-ui-empty-item.c       \
	%D%/ui/gpaste-ui-empty-item.h       \
	%D%/ui/gpaste-ui-header.c           \
	%D%/ui/gpaste-ui-header.h           \
	%D%/ui/gpaste-ui-history-action.c   \
	%D%/ui/gpaste-ui-history-action.h   \
	%D%/ui/gpaste-ui-history-actions.c  \
	%D%/ui/gpaste-ui-history-actions.h  \
	%D%/ui/gpaste-ui-history.c          \
	%D%/ui/gpaste-ui-history.h          \
	%D%/ui/gpaste-ui-item-action.c      \
	%D%/ui/gpaste-ui-item-action.h      \
	%D%/ui/gpaste-ui-item-skeleton.c    \
	%D%/ui/gpaste-ui-item-skeleton.h    \
	%D%/ui/gpaste-ui-item.c             \
	%D%/ui/gpaste-ui-item.h             \
	%D%/ui/gpaste-ui-new-item.c         \
	%D%/ui/gpaste-ui-new-item.h         \
	%D%/ui/gpaste-ui-panel-history.c    \
	%D%/ui/gpaste-ui-panel-history.h    \
	%D%/ui/gpaste-ui-panel.c            \
	%D%/ui/gpaste-ui-panel.h            \
	%D%/ui/gpaste-ui-reexec.c           \
	%D%/ui/gpaste-ui-reexec.h           \
	%D%/ui/gpaste-ui-search-bar.c       \
	%D%/ui/gpaste-ui-search-bar.h       \
	%D%/ui/gpaste-ui-search.c           \
	%D%/ui/gpaste-ui-search.h           \
	%D%/ui/gpaste-ui-settings.c         \
	%D%/ui/gpaste-ui-settings.h         \
	%D%/ui/gpaste-ui-shortcuts-window.c \
	%D%/ui/gpaste-ui-shortcuts-window.h \
	%D%/ui/gpaste-ui-switch.c           \
	%D%/ui/gpaste-ui-switch.h           \
	%D%/ui/gpaste-ui-upload-item.c      \
	%D%/ui/gpaste-ui-upload-item.h      \
	%D%/ui/gpaste-ui-window.c           \
	%D%/ui/gpaste-ui-window.h           \
	%D%/ui/gpaste-ui.c                  \
	%D%/ui/tmp/gpaste-client.c \
	%D%/ui/tmp/gpaste-client.h \
	%D%/ui/tmp/gpaste-client-item.c \
	%D%/ui/tmp/gpaste-client-item.h \
	%D%/ui/tmp/gpaste-util.c \
	%D%/ui/tmp/gpaste-util.h \
	%D%/ui/tmp/gpaste-settings.c \
	%D%/ui/tmp/gpaste-settings.h \
	%D%/ui/tmp/gpaste-update-enums.c \
	%D%/ui/tmp/gpaste-update-enums.h \
	%D%/ui/tmp/gpaste-settings-ui-panel.c \
	%D%/ui/tmp/gpaste-settings-ui-panel.h \
	%D%/ui/tmp/gpaste-settings-ui-stack.c \
	%D%/ui/tmp/gpaste-settings-ui-stack.h \
	%D%/ui/tmp/gpaste-settings-ui-widget.c \
	%D%/ui/tmp/gpaste-settings-ui-widget.h \
	%D%/ui/tmp/gpaste-item-enums.c \
	%D%/ui/tmp/gpaste-item-enums.h \
	$(NULL)

bin_gpaste_ui_CFLAGS = \
	$(GLIB_CFLAGS) \
	$(GTK3_CFLAGS) \
	-I $(srcdir)/src/ui/     \
	-I $(srcdir)/src/ui/tmp/ \
	$(NULL)

bin_gpaste_ui_LDADD =                    \
	$(GLIB_LIBS)                     \
	$(GTK3_LIBS)                      \
	$(NULL)
# $(builddir)/$(libgpaste_la_file) 
