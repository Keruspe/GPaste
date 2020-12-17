/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-settings-ui-widget.h>
#include <gpaste-ui-settings.h>

#include "gpaste-gtk-compat.h"

struct _GPasteUiSettings
{
    GtkMenuButton parent_instance;
};

G_PASTE_DEFINE_TYPE (UiSettings, ui_settings, GTK_TYPE_MENU_BUTTON)

static void
g_paste_ui_settings_class_init (GPasteUiSettingsClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_ui_settings_init (GPasteUiSettings *self)
{
    GtkWidget *widget = GTK_WIDGET (self);
    GtkMenuButton *menu = GTK_MENU_BUTTON (self);
    GtkWidget *popover = gtk_popover_new (GTK_WIDGET (self));
    GtkWidget *settings_widget = g_paste_settings_ui_widget_new ();

    gtk_widget_set_tooltip_text (widget, _("GPaste Settings"));
    gtk_widget_set_valign (widget, GTK_ALIGN_CENTER);

    gtk_widget_set_margin_top (settings_widget, 10);

    gtk_menu_button_set_direction (menu, GTK_ARROW_NONE);
    gtk_menu_button_set_use_popover (menu, TRUE);
    gtk_menu_button_set_popover (menu, popover);

    gtk_container_add (GTK_CONTAINER (popover), settings_widget);
    gtk_widget_show_all (settings_widget);
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
    return gtk_widget_new (G_PASTE_TYPE_UI_SETTINGS, NULL);
}
