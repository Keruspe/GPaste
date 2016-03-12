## This file is part of GPaste.
##
## Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>

autostartdir = $(sysconfdir)/xdg/autostart
applicationsdir = $(datadir)/applications

gpaste_applet_desktop_file = %D%/desktop/org.gnome.GPaste.Applet.desktop
gpaste_app_indicator_desktop_file = %D%/desktop/org.gnome.GPaste.AppIndicator.desktop
gpaste_ui_desktop_file = %D%/desktop/org.gnome.GPaste.Ui.desktop

all_desktop_files =                          \
	$(gpaste_applet_desktop_file)        \
	$(gpaste_app_indicator_desktop_file) \
	$(gpaste_ui_desktop_file)            \
	$(NULL)

nodist_autostart_DATA = \
	$(NULL)

if ENABLE_APPLET
nodist_autostart_DATA +=              \
	$(gpaste_applet_desktop_file) \
	$(NULL)
endif

if ENABLE_UNITY
nodist_autostart_DATA +=                     \
	$(gpaste_app_indicator_desktop_file) \
	$(NULL)
endif

nodist_applications_DATA =        \
	$(gpaste_ui_desktop_file) \
	$(nodist_autostart_DATA)  \
	$(NULL)

SUFFIXES += .desktop.in.in .desktop.in .desktop
.desktop.in.in.desktop.in:
	@ $(MKDIR_P) $(@D)
	$(AM_V_GEN) $(SED) -e 's,[@]pkglibexecdir[@],$(pkglibexecdir),g' < $< > $@
.desktop.in.desktop:
	@ $(MKDIR_P) $(@D)
	$(AM_V_GEN) $(MSGFMT) --desktop --template $< -o $@ -d $(top_builddir)/po/

EXTRA_DIST +=                                        \
	$(all_desktop_files:.desktop=.desktop.in.in) \
	$(NULL)

CLEANFILES +=                                     \
	$(all_desktop_files)                      \
	$(all_desktop_files:.desktop=.desktop.in) \
	$(NULL)
