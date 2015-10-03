## This file is part of GPaste.
##
## Copyright 2014-2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
##
## GPaste is free software: you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation, either version 3 of the License, or
## (at your option) any later version.
##
## GPaste is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with GPaste.  If not, see <http://www.gnu.org/licenses/>.

libgpaste_la_file = lib/libgpaste.la

lib_libgpaste_la_private_headers =          \
	%D%/libgpaste/gpaste-gdbus-macros.h \
	$(NULL)

lib_libgpaste_la_misc_headers =               \
	%D%/libgpaste/gpaste-gdbus-defines.h  \
	%D%/libgpaste/gpaste-gsettings-keys.h \
	%D%/libgpaste/gpaste-macros.h         \
	%D%/libgpaste/util/gpaste-util.h      \
	$(NULL)

lib_libgpaste_la_public_headers =                                             \
	%D%/libgpaste/gpaste.h                                                \
	%D%/libgpaste/applet/gpaste-applet.h                                  \
	%D%/libgpaste/applet/gpaste-applet-about.h                            \
	%D%/libgpaste/applet/gpaste-applet-icon.h                             \
	%D%/libgpaste/applet/gpaste-applet-menu.h                             \
	%D%/libgpaste/applet/gpaste-applet-quit.h                             \
	%D%/libgpaste/applet/gpaste-applet-status-icon.h                      \
	%D%/libgpaste/applet/gpaste-applet-ui.h                               \
	%D%/libgpaste/client/gpaste-client.h                                  \
	%D%/libgpaste/core/gpaste-clipboard.h                                 \
	%D%/libgpaste/core/gpaste-clipboards-manager.h                        \
	%D%/libgpaste/core/gpaste-history.h                                   \
	%D%/libgpaste/core/gpaste-image-item.h                                \
	%D%/libgpaste/core/gpaste-item.h                                      \
	%D%/libgpaste/core/gpaste-password-item.h                             \
	%D%/libgpaste/core/gpaste-text-item.h                                 \
	%D%/libgpaste/core/gpaste-item-enums.h                                \
	%D%/libgpaste/core/gpaste-update-enums.h                              \
	%D%/libgpaste/core/gpaste-uris-item.h                                 \
	%D%/libgpaste/daemon/gpaste-bus.h                                     \
	%D%/libgpaste/daemon/gpaste-bus-object.h                              \
	%D%/libgpaste/daemon/gpaste-daemon.h                                  \
	%D%/libgpaste/daemon/gpaste-search-provider.h                         \
	%D%/libgpaste/gnome-shell-client/gpaste-gnome-shell-client.h          \
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
	%D%/libgpaste/ui/gpaste-ui-about.h                                    \
	%D%/libgpaste/ui/gpaste-ui-backup-history.h                           \
	%D%/libgpaste/ui/gpaste-ui-delete-history.h                           \
	%D%/libgpaste/ui/gpaste-ui-delete-item.h                              \
	%D%/libgpaste/ui/gpaste-ui-edit-item.h                                \
	%D%/libgpaste/ui/gpaste-ui-empty-history.h                            \
	%D%/libgpaste/ui/gpaste-ui-empty-item.h                               \
	%D%/libgpaste/ui/gpaste-ui-header.h                                   \
	%D%/libgpaste/ui/gpaste-ui-history.h                                  \
	%D%/libgpaste/ui/gpaste-ui-history-action.h                           \
	%D%/libgpaste/ui/gpaste-ui-history-actions.h                          \
	%D%/libgpaste/ui/gpaste-ui-item.h                                     \
	%D%/libgpaste/ui/gpaste-ui-item-action.h                              \
	%D%/libgpaste/ui/gpaste-ui-item-skeleton.h                            \
	%D%/libgpaste/ui/gpaste-ui-panel.h                                    \
	%D%/libgpaste/ui/gpaste-ui-panel-history.h                            \
	%D%/libgpaste/ui/gpaste-ui-reexec.h                                   \
	%D%/libgpaste/ui/gpaste-ui-search.h                                   \
	%D%/libgpaste/ui/gpaste-ui-search-bar.h                               \
	%D%/libgpaste/ui/gpaste-ui-settings.h                                 \
	%D%/libgpaste/ui/gpaste-ui-switch.h                                   \
	%D%/libgpaste/ui/gpaste-ui-upload-item.h                              \
	%D%/libgpaste/ui/gpaste-ui-window.h                                   \
	$(NULL)

lib_libgpaste_la_source_files =                                               \
	%D%/libgpaste/applet/gpaste-applet.c                                  \
	%D%/libgpaste/applet/gpaste-applet-about.c                            \
	%D%/libgpaste/applet/gpaste-applet-icon.c                             \
	%D%/libgpaste/applet/gpaste-applet-menu.c                             \
	%D%/libgpaste/applet/gpaste-applet-quit.c                             \
	%D%/libgpaste/applet/gpaste-applet-status-icon.c                      \
	%D%/libgpaste/applet/gpaste-applet-ui.c                               \
	%D%/libgpaste/client/gpaste-client.c                                  \
	%D%/libgpaste/core/gpaste-clipboard.c                                 \
	%D%/libgpaste/core/gpaste-clipboards-manager.c                        \
	%D%/libgpaste/core/gpaste-history.c                                   \
	%D%/libgpaste/core/gpaste-image-item.c                                \
	%D%/libgpaste/core/gpaste-item.c                                      \
	%D%/libgpaste/core/gpaste-password-item.c                             \
	%D%/libgpaste/core/gpaste-text-item.c                                 \
	%D%/libgpaste/core/gpaste-item-enums.c                                \
	%D%/libgpaste/core/gpaste-update-enums.c                              \
	%D%/libgpaste/core/gpaste-uris-item.c                                 \
	%D%/libgpaste/daemon/gpaste-bus.c                                     \
	%D%/libgpaste/daemon/gpaste-bus-object.c                              \
	%D%/libgpaste/daemon/gpaste-daemon.c                                  \
	%D%/libgpaste/daemon/gpaste-search-provider.c                         \
	%D%/libgpaste/gnome-shell-client/gpaste-gnome-shell-client.c          \
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
	%D%/libgpaste/ui/gpaste-ui-about.c                                    \
	%D%/libgpaste/ui/gpaste-ui-backup-history.c                           \
	%D%/libgpaste/ui/gpaste-ui-delete-history.c                           \
	%D%/libgpaste/ui/gpaste-ui-delete-item.c                              \
	%D%/libgpaste/ui/gpaste-ui-edit-item.c                                \
	%D%/libgpaste/ui/gpaste-ui-empty-history.c                            \
	%D%/libgpaste/ui/gpaste-ui-empty-item.c                               \
	%D%/libgpaste/ui/gpaste-ui-header.c                                   \
	%D%/libgpaste/ui/gpaste-ui-history.c                                  \
	%D%/libgpaste/ui/gpaste-ui-history-action.c                           \
	%D%/libgpaste/ui/gpaste-ui-history-actions.c                          \
	%D%/libgpaste/ui/gpaste-ui-item.c                                     \
	%D%/libgpaste/ui/gpaste-ui-item-action.c                              \
	%D%/libgpaste/ui/gpaste-ui-item-skeleton.c                            \
	%D%/libgpaste/ui/gpaste-ui-panel.c                                    \
	%D%/libgpaste/ui/gpaste-ui-panel-history.c                            \
	%D%/libgpaste/ui/gpaste-ui-reexec.c                                   \
	%D%/libgpaste/ui/gpaste-ui-search.c                                   \
	%D%/libgpaste/ui/gpaste-ui-search-bar.c                               \
	%D%/libgpaste/ui/gpaste-ui-settings.c                                 \
	%D%/libgpaste/ui/gpaste-ui-switch.c                                   \
	%D%/libgpaste/ui/gpaste-ui-upload-item.c                              \
	%D%/libgpaste/ui/gpaste-ui-window.c                                   \
	%D%/libgpaste/util/gpaste-util.c                                      \
	$(NULL)

if ENABLE_UNITY
lib_libgpaste_la_public_headers +=                         \
	%D%/libgpaste/applet/gpaste-applet-app-indicator.h \
	$(NULL)
lib_libgpaste_la_source_files +=                           \
	%D%/libgpaste/applet/gpaste-applet-app-indicator.c \
	$(NULL)
endif

lib_libgpaste_la_SOURCES =                  \
	$(lib_libgpaste_la_private_headers) \
	$(lib_libgpaste_la_source_files)    \
	$(NULL)

lib_libgpaste_la_CFLAGS =    \
	$(GDK_CFLAGS)        \
	$(GDK_PIXBUF_CFLAGS) \
	$(UNITY_CFLAGS)      \
	$(X11_CFLAGS)        \
	$(AM_CFLAGS)         \
	$(NULL)

lib_libgpaste_la_LIBADD =  \
	$(GDK_LIBS)        \
	$(GDK_PIXBUF_LIBS) \
	$(GTK_LIBS)        \
	$(UNITY_LIBS)      \
	$(X11_LIBS)        \
	$(AM_LIBS)         \
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
