/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_GTK4_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste-gtk4.h> can be included directly."
#endif

#pragma once

#include <gpaste.h>

#include <adwaita.h>

G_BEGIN_DECLS

#define G_PASTE_GTK_FINAL_TYPE(TypeName, type_name, TYPE_NAME, ParentTypeName) \
    G_PASTE_VISIBLE G_DECLARE_FINAL_TYPE (GPasteGtk##TypeName, g_paste_gtk_##type_name, G_PASTE, GTK_##TYPE_NAME, ParentTypeName)

#define G_PASTE_GTK_DERIVABLE_TYPE(TypeName, type_name, TYPE_NAME, ParentTypeName) \
    G_PASTE_VISIBLE G_DECLARE_DERIVABLE_TYPE (GPasteGtk##TypeName, g_paste_gtk_##type_name, G_PASTE, GTK_##TYPE_NAME, ParentTypeName)

#define G_PASTE_GTK_DEFINE_TYPE(TypeName, type_name, ParentTypeName) \
    G_DEFINE_TYPE (GPasteGtk##TypeName, g_paste_gtk_##type_name, ParentTypeName)

#define G_PASTE_GTK_DEFINE_TYPE_WITH_PRIVATE(TypeName, type_name, ParentTypeName) \
    G_DEFINE_TYPE_WITH_PRIVATE (GPasteGtk##TypeName, g_paste_gtk_##type_name, ParentTypeName)

#define G_PASTE_GTK_INIT_APPLICATION_FULL(name, activate_cb)                                        \
    G_PASTE_INIT_GETTEXT ();                                                                        \
    AdwApplication *adw = adw_application_new ("org.gnome.GPaste." name, G_APPLICATION_FLAGS_NONE); \
    GtkApplication *app = GTK_APPLICATION (adw);                                                    \
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
        if (activate_cb)                                                                            \
        {                                                                                           \
            g_application_activate (gapp);                                                          \
            return g_application_run (gapp, argc, argv);                                            \
        }                                                                                           \
        else                                                                                        \
        {                                                                                           \
            fprintf (stderr, "GPaste " name " %s\n", _("is already running."));                     \
            exit (EXIT_FAILURE);                                                                    \
        }                                                                                           \
    }

#define G_PASTE_GTK_INIT_APPLICATION(name) \
    G_PASTE_GTK_INIT_APPLICATION_FULL (name, NULL)

G_END_DECLS
