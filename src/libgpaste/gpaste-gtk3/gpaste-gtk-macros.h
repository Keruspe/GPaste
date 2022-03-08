/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#ifndef __GPASTE_GTK_MACROS_H__
#define __GPASTE_GTK_MACROS_H__

#include <gpaste/gpaste-macros.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define G_PASTE_GTK_INIT_APPLICATION_FULL(name, activate_cb)                                        \
    G_PASTE_INIT_GETTEXT ();                                                                        \
    GtkApplication *app = gtk_application_new ("org.gnome.GPaste." name, G_APPLICATION_FLAGS_NONE); \
    GApplication *gapp = G_APPLICATION (app);                                                       \
    g_autoptr (GError) error = NULL;                                                                \
    G_APPLICATION_GET_CLASS (gapp)->activate = activate_cb;                                         \
    g_application_register (gapp, NULL, &error);                                                    \
    if (error)                                                                                      \
    {                                                                                               \
        fprintf (stderr, "%s: %s\n", _("Failed to register the gtk application"), error->message);  \
        return EXIT_FAILURE;                                                                        \
    }                                                                                               \
    if (g_application_get_is_remote (gapp))                                                         \
    {                                                                                               \
        if (G_APPLICATION_GET_CLASS (gapp)->activate)                                               \
        {                                                                                           \
            g_application_activate (gapp);                                                          \
            return g_application_run (gapp, argc, argv);                                            \
        }                                                                                           \
        else                                                                                        \
        {                                                                                           \
            fprintf (stderr, "GPaste " name " %s\n", _("is already running."));                     \
            exit (EXIT_FAILURE);                                                                    \
        }                                                                                           \
    }                                                                                               \
    g_object_set (gtk_settings_get_default (), "gtk-application-prefer-dark-theme", TRUE, NULL)

#define G_PASTE_GTK_INIT_APPLICATION(name) \
    G_PASTE_GTK_INIT_APPLICATION_FULL (name, NULL)

G_END_DECLS

#endif /*__GPASTE_MACROS_H__*/
