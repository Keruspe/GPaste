/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2026, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gtk4/gpaste-gtk-preferences-dialog.h>

#include <gpaste-ui-settings.h>

struct _GPasteUiSettings
{
    GtkButton parent_instance;
};

G_PASTE_DEFINE_TYPE (UiSettings, ui_settings, GTK_TYPE_BUTTON)

static void
g_paste_ui_settings_clicked (GtkButton *button)
{
    GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (button));
    AdwDialog *dialog = g_paste_gtk_preferences_dialog_new (NULL);

    adw_dialog_present (dialog, GTK_WIDGET (root));
}

static void
g_paste_ui_settings_class_init (GPasteUiSettingsClass *klass)
{
    GTK_BUTTON_CLASS (klass)->clicked = g_paste_ui_settings_clicked;
}

static void
g_paste_ui_settings_init (GPasteUiSettings *self)
{
    GtkWidget *widget = GTK_WIDGET (self);

    gtk_widget_set_tooltip_text (widget, _("GPaste Settings"));
    gtk_widget_set_valign (widget, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class (widget, "flat");
    gtk_button_set_child (GTK_BUTTON (self), gtk_image_new_from_icon_name ("preferences-system-symbolic"));
}

/**
 * g_paste_ui_settings_new:
 *
 * Create a new instance of #GPasteUiSettings
 *
 * Returns: a newly allocated #GPasteUiSettings
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_settings_new (void)
{
    return g_object_new (G_PASTE_TYPE_UI_SETTINGS, NULL);
}
