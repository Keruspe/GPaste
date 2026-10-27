// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-preferences-dialog.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-pages.h>

struct _GPasteGtkPreferencesDialog
{
    AdwPreferencesDialog parent_instance;

    GApplication   *gapp;     /* held for as long as the dialog is up */
    GPasteSettings *settings; /* kept alive for the bound rows' lifetime */
};

G_PASTE_GTK_DEFINE_TYPE (PreferencesDialog, preferences_dialog, ADW_TYPE_PREFERENCES_DIALOG)

static void
g_paste_gtk_preferences_dialog_dispose (GObject *object)
{
    GPasteGtkPreferencesDialog *self = G_PASTE_GTK_PREFERENCES_DIALOG (object);

    /* Chained up first: the rows the parent's dispose tears down bind to (and
     * reset through) the settings without holding a reference, so the settings
     * go after them. */
    G_OBJECT_CLASS (g_paste_gtk_preferences_dialog_parent_class)->dispose (object);

    g_clear_object (&self->settings);
    g_clear_pointer (&self->gapp, g_application_release);
}

static void
g_paste_gtk_preferences_dialog_class_init (GPasteGtkPreferencesDialogClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_gtk_preferences_dialog_dispose;
}

static void
g_paste_gtk_preferences_dialog_init (GPasteGtkPreferencesDialog *self)
{
    AdwPreferencesDialog *win = ADW_PREFERENCES_DIALOG (self);
    GPasteSettings *settings = self->settings = g_paste_settings_new ();

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

    if (gapp)
        g_application_hold (gapp);
    self->gapp = gapp;

    return ADW_DIALOG (self);
}
