/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_GTK3_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste-gtk3.h> can be included directly."
#endif

#pragma once

#include <gpaste.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define G_PASTE_GTK_FINAL_TYPE(TypeName, type_name, TYPE_NAME, ParentTypeName) \
    G_PASTE_VISIBLE G_DECLARE_FINAL_TYPE (GPasteGtk##TypeName, g_paste_gtk_##type_name, G_PASTE, GTK_##TYPE_NAME, ParentTypeName)

#define G_PASTE_GTK_DEFINE_TYPE(TypeName, type_name, ParentTypeName) \
    G_DEFINE_TYPE (GPasteGtk##TypeName, g_paste_gtk_##type_name, ParentTypeName)

#define G_PASTE_GTK_DEFINE_TYPE_WITH_PRIVATE(TypeName, type_name, ParentTypeName) \
    G_DEFINE_TYPE_WITH_PRIVATE (GPasteGtk##TypeName, g_paste_gtk_##type_name, ParentTypeName)

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
