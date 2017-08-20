## This file is part of GPaste.

icon32dir = $(datadir)/icons/hicolor/32x32/apps
icon48dir = $(datadir)/icons/hicolor/48x48/apps
icon128dir = $(datadir)/icons/hicolor/128x128/apps
icon256dir = $(datadir)/icons/hicolor/256x256/apps
icon512dir = $(datadir)/icons/hicolor/512x512/apps
iconsymbolicdir = $(datadir)/icons/hicolor/symbolic/apps

icon32_DATA = %D%/icons/hicolor/32x32/apps/gpaste.png
icon48_DATA = %D%/icons/hicolor/48x48/apps/gpaste.png
icon128_DATA = %D%/icons/hicolor/128x128/apps/gpaste.png
icon256_DATA = %D%/icons/hicolor/256x256/apps/gpaste.png
icon512_DATA = %D%/icons/hicolor/512x512/apps/gpaste.png
iconsymbolic_DATA = %D%/icons/hicolor/symbolic/apps/gpaste-symbolic.svg

EXTRA_DIST += $(icon32_DATA) $(icon48_DATA) $(icon128_DATA) $(icon256_DATA) \
	$(icon512_DATA) $(iconsymbolic_DATA)
