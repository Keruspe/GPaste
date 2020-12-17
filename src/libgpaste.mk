## This file is part of GPaste.
##
## Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>

libgpaste_la_file = lib/libgpaste.la

lib_libgpaste_la_private_headers =          \
	%D%/libgpaste/gpaste-gdbus-macros.h \
	%D%/libgpaste/gpaste-gtk-compat.h   \
	$(NULL)

lib_libgpaste_la_misc_headers =               \
	%D%/libgpaste/gpaste-gdbus-defines.h  \
	%D%/libgpaste/gpaste-gsettings-keys.h \
	%D%/libgpaste/gpaste-macros.h         \
	%D%/libgpaste/util/gpaste-util.h      \
	$(NULL)

lib_libgpaste_la_public_headers =                                             \
	%D%/libgpaste/gpaste.h                                                \
	%D%/libgpaste/client/gpaste-client.h                                  \
	%D%/libgpaste/client/gpaste-client-item.h                             \
	%D%/libgpaste/core/gpaste-clipboard.h                                 \
	%D%/libgpaste/core/gpaste-clipboards-manager.h                        \
	%D%/libgpaste/core/gpaste-history.h                                   \
	%D%/libgpaste/core/gpaste-image-item.h                                \
	%D%/libgpaste/core/gpaste-item.h                                      \
	%D%/libgpaste/core/gpaste-password-item.h                             \
	%D%/libgpaste/core/gpaste-text-item.h                                 \
	%D%/libgpaste/core/gpaste-item-enums.h                                \
	%D%/libgpaste/core/gpaste-special-atom.h                              \
	%D%/libgpaste/core/gpaste-update-enums.h                              \
	%D%/libgpaste/core/gpaste-uris-item.h                                 \
	%D%/libgpaste/daemon/gpaste-bus.h                                     \
	%D%/libgpaste/daemon/gpaste-bus-object.h                              \
	%D%/libgpaste/daemon/gpaste-daemon.h                                  \
	%D%/libgpaste/daemon/gpaste-search-provider.h                         \
	%D%/libgpaste/gnome-shell-client/gpaste-gnome-shell-client.h          \
	%D%/libgpaste/io/gpaste-file-backend.h                                \
	%D%/libgpaste/io/gpaste-storage-backend.h                             \
	%D%/libgpaste/keybinder/gpaste-keybinder.h                            \
	%D%/libgpaste/keybinder/gpaste-keybinding.h                           \
	%D%/libgpaste/keybinder/gpaste-make-password-keybinding.h             \
	%D%/libgpaste/keybinder/gpaste-pop-keybinding.h                       \
	%D%/libgpaste/keybinder/gpaste-show-history-keybinding.h              \
	%D%/libgpaste/keybinder/gpaste-sync-clipboard-to-primary-keybinding.h \
	%D%/libgpaste/keybinder/gpaste-sync-primary-to-clipboard-keybinding.h \
	%D%/libgpaste/keybinder/gpaste-ui-keybinding.h                        \
	%D%/libgpaste/keybinder/gpaste-upload-keybinding.h                    \
	%D%/libgpaste/screensaver-client/gpaste-screensaver-client.h          \
	%D%/libgpaste/settings/gpaste-settings.h                              \
	%D%/libgpaste/settings-ui/gpaste-settings-ui-panel.h                  \
	%D%/libgpaste/settings-ui/gpaste-settings-ui-stack.h                  \
	%D%/libgpaste/settings-ui/gpaste-settings-ui-widget.h                 \
	$(NULL)

lib_libgpaste_la_source_files =                                               \
	%D%/libgpaste/client/gpaste-client.c                                  \
	%D%/libgpaste/client/gpaste-client-item.c                             \
	%D%/libgpaste/core/gpaste-clipboard.c                                 \
	%D%/libgpaste/core/gpaste-clipboards-manager.c                        \
	%D%/libgpaste/core/gpaste-history.c                                   \
	%D%/libgpaste/core/gpaste-image-item.c                                \
	%D%/libgpaste/core/gpaste-item.c                                      \
	%D%/libgpaste/core/gpaste-password-item.c                             \
	%D%/libgpaste/core/gpaste-text-item.c                                 \
	%D%/libgpaste/core/gpaste-item-enums.c                                \
	%D%/libgpaste/core/gpaste-special-atom.c                              \
	%D%/libgpaste/core/gpaste-update-enums.c                              \
	%D%/libgpaste/core/gpaste-uris-item.c                                 \
	%D%/libgpaste/daemon/gpaste-bus.c                                     \
	%D%/libgpaste/daemon/gpaste-bus-object.c                              \
	%D%/libgpaste/daemon/gpaste-daemon.c                                  \
	%D%/libgpaste/daemon/gpaste-search-provider.c                         \
	%D%/libgpaste/gnome-shell-client/gpaste-gnome-shell-client.c          \
	%D%/libgpaste/io/gpaste-file-backend.c                                \
	%D%/libgpaste/io/gpaste-storage-backend.c                             \
	%D%/libgpaste/keybinder/gpaste-keybinder.c                            \
	%D%/libgpaste/keybinder/gpaste-keybinding.c                           \
	%D%/libgpaste/keybinder/gpaste-make-password-keybinding.c             \
	%D%/libgpaste/keybinder/gpaste-pop-keybinding.c                       \
	%D%/libgpaste/keybinder/gpaste-show-history-keybinding.c              \
	%D%/libgpaste/keybinder/gpaste-sync-clipboard-to-primary-keybinding.c \
	%D%/libgpaste/keybinder/gpaste-sync-primary-to-clipboard-keybinding.c \
	%D%/libgpaste/keybinder/gpaste-ui-keybinding.c                        \
	%D%/libgpaste/keybinder/gpaste-upload-keybinding.c                    \
	%D%/libgpaste/screensaver-client/gpaste-screensaver-client.c          \
	%D%/libgpaste/settings/gpaste-settings.c                              \
	%D%/libgpaste/settings-ui/gpaste-settings-ui-panel.c                  \
	%D%/libgpaste/settings-ui/gpaste-settings-ui-stack.c                  \
	%D%/libgpaste/settings-ui/gpaste-settings-ui-widget.c                 \
	%D%/libgpaste/util/gpaste-util.c                                      \
	$(NULL)

lib_libgpaste_la_SOURCES =                  \
	$(lib_libgpaste_la_private_headers) \
	$(lib_libgpaste_la_source_files)    \
	$(NULL)

lib_libgpaste_la_CPPFLAGS =       \
	$(AM_CPPFLAGS)            \
	-DG_LOG_DOMAIN=\"GPaste\" \
	$(NULL)

lib_libgpaste_la_CFLAGS =    \
	$(GDK_CFLAGS)        \
	$(GDK_PIXBUF_CFLAGS) \
	$(GLIB_CFLAGS)       \
	$(GTK_CFLAGS)        \
	$(X11_CFLAGS)        \
	$(NULL)

lib_libgpaste_la_LIBADD =  \
	$(GDK_LIBS)        \
	$(GDK_PIXBUF_LIBS) \
	$(GLIB_LIBS)       \
	$(GTK_LIBS)        \
	$(X11_LIBS)        \
	$(NULL)

libgpaste_symbols_file = $(srcdir)/%D%/libgpaste/libgpaste.sym

lib_libgpaste_la_LDFLAGS =                             \
	-version-info $(LIBGPASTE_LT_VERSION)          \
	-Wl,--version-script=$(libgpaste_symbols_file) \
	$(NULL)

lib_libgpaste_la_DEPENDENCIES =   \
	$(libgpaste_symbols_file) \
	$(NULL)

pkginclude_HEADERS +=                      \
	$(lib_libgpaste_la_misc_headers)   \
	$(lib_libgpaste_la_public_headers) \
	$(NULL)

lib_LTLIBRARIES +=           \
	$(libgpaste_la_file) \
	$(NULL)

EXTRA_DIST +=                     \
	$(libgpaste_symbols_file) \
	$(NULL)
