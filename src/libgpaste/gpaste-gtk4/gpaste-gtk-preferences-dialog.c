// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-preferences-dialog.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-pages.h>

struct _GPasteGtkPreferencesDialog
{
    AdwPreferencesDialog parent_instance;
};

typedef struct
{
    GApplication   *gapp;
    GPasteSettings *settings; /* kept alive for the bound rows' lifetime */
} GPasteGtkPreferencesDialogPrivate;

G_PASTE_GTK_DEFINE_TYPE_WITH_PRIVATE (PreferencesDialog, preferences_dialog, ADW_TYPE_PREFERENCES_DIALOG)

static void
g_paste_gtk_preferences_dialog_finalize (GObject *object)
{
    GPasteGtkPreferencesDialogPrivate *priv = g_paste_gtk_preferences_dialog_get_instance_private (G_PASTE_GTK_PREFERENCES_DIALOG (object));

    if (priv->gapp)
        g_application_release (priv->gapp);

    g_clear_object (&priv->settings);

    G_OBJECT_CLASS (g_paste_gtk_preferences_dialog_parent_class)->finalize (object);
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
    GPasteGtkPreferencesDialogPrivate *priv = g_paste_gtk_preferences_dialog_get_instance_private (self);
    /* The rows bind to (and reset through) this settings object without holding
     * a reference, so it has to outlive them. */
    GPasteSettings *settings = priv->settings = g_paste_settings_new ();

    g_autofree AdwPreferencesPage **pages = g_paste_gtk_preferences_pages_new (settings);

    for (AdwPreferencesPage **page = pages; *page; ++page)
        adw_preferences_dialog_add (win, *page);
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
