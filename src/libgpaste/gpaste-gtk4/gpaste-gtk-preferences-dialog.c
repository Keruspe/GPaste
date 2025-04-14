/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gtk4/gpaste-gtk-preferences-behaviour-page.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-dialog.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-history-settings-page.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-shortcuts-page.h>

struct _GPasteGtkPreferencesDialog
{
    AdwPreferencesDialog parent_instance;
};

typedef struct
{
    GApplication *gapp;
} GPasteGtkPreferencesDialogPrivate;

G_PASTE_GTK_DEFINE_TYPE_WITH_PRIVATE (PreferencesDialog, preferences_dialog, ADW_TYPE_PREFERENCES_DIALOG)

static void
g_paste_gtk_preferences_dialog_finalize (GObject *object)
{
    GPasteGtkPreferencesDialogPrivate *priv = g_paste_gtk_preferences_dialog_get_instance_private (G_PASTE_GTK_PREFERENCES_DIALOG (object));

    if (priv->gapp)
        g_application_release (priv->gapp);
}

static void
g_paste_gtk_preferences_dialog_class_init (GPasteGtkPreferencesDialogClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = g_paste_gtk_preferences_dialog_finalize;
}

static void
g_paste_gtk_preferences_dialog_init (GPasteGtkPreferencesDialog *self)
{
    AdwPreferencesDialog *win = ADW_PREFERENCES_DIALOG (self);
    g_autoptr (GPasteGtkPreferencesManager) manager = g_paste_gtk_preferences_manager_new ();
    
    adw_preferences_dialog_add (win, ADW_PREFERENCES_PAGE (g_paste_gtk_preferences_behaviour_page_new (manager)));
    adw_preferences_dialog_add (win, ADW_PREFERENCES_PAGE (g_paste_gtk_preferences_history_settings_page_new (manager)));
    adw_preferences_dialog_add (win, ADW_PREFERENCES_PAGE (g_paste_gtk_preferences_shortcuts_page_new (manager)));
}

/**
 * g_paste_gtk_preferences_dialog_new:
 * @gapp: a #GApplication instance
 *
 * Create a new instance of #GPasteGtkPreferencesDialog
 *
 * Returns: (nullable): a newly allocated #GPasteGtkPreferencesDialog
 *                      free it with g_object_unref
 */
G_PASTE_VISIBLE AdwDialog *
g_paste_gtk_preferences_dialog_new (GApplication *gapp)
{
    GPasteGtkPreferencesDialog *self = g_object_new (G_PASTE_TYPE_GTK_PREFERENCES_DIALOG, NULL);
    GPasteGtkPreferencesDialogPrivate *priv = g_paste_gtk_preferences_dialog_get_instance_private (self);

    if (gapp)
        g_application_hold (gapp);
    priv->gapp = gapp;

    return ADW_DIALOG (self);
}
