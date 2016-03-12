/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-applet.h>
#include <gpaste-applet-app-indicator.h>

gint
main (gint argc, gchar *argv[])
{
    G_PASTE_INIT_APPLICATION ("AppIndicator");

    G_GNUC_UNUSED g_autoptr (GPasteApplet) applet = g_paste_applet_new (app, g_paste_applet_app_indicator_new);

    return g_application_run (gapp, argc, argv);
}
