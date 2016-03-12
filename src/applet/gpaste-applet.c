/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-applet.h>
#include <gpaste-applet-status-icon.h>

gint
main (gint argc, gchar *argv[])
{
    G_PASTE_INIT_APPLICATION ("Applet");

    G_GNUC_UNUSED g_autoptr (GPasteApplet) applet = g_paste_applet_new (app, g_paste_applet_status_icon_new);

    return g_application_run (gapp, argc, argv);
}
