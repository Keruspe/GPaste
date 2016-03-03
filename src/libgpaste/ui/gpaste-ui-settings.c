/*
 *      This file is part of GPaste.
 *
 *      Copyright 2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <gpaste-settings-ui-widget.h>
#include <gpaste-ui-settings.h>

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
