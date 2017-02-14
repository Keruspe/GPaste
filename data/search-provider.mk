## This file is part of GPaste.
##
## Copyright (c) 2010-2017, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>

searchproviderdir= $(datadir)/gnome-shell/search-providers/

if ENABLE_GNOME_SHELL_EXTENSION
dist_searchprovider_DATA =                                       \
	%D%/search-provider/org.gnome.GPaste.search-provider.ini \
	$(NULL)
endif
