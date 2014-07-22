/*
 *      This file is part of GPaste.
 *
 *      Copyright 2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-applet-settings-private.h"

#include <glib/gi18n-lib.h>

G_DEFINE_TYPE (GPasteAppletSettings, g_paste_applet_settings, GTK_TYPE_MENU_ITEM)

static void
g_paste_applet_settings_activate (GtkMenuItem *menu_item G_GNUC_UNUSED)
{
    g_spawn_command_line_async (PKGLIBEXECDIR "/gpaste-settings", NULL);

    GTK_MENU_ITEM_CLASS (g_paste_applet_settings_parent_class)->activate (menu_item);
}

static void
g_paste_applet_settings_class_init (GPasteAppletSettingsClass *klass)
{
    GTK_MENU_ITEM_CLASS (klass)->activate = g_paste_applet_settings_activate;
}

static void
g_paste_applet_settings_init (GPasteAppletSettings *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_applet_settings_new:
 *
 * Create a new instance of #GPasteAppletSettings
 *
 * Returns: a newly allocated #GPasteAppletSettings
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_applet_settings_new (void)
{
    return gtk_widget_new (G_PASTE_TYPE_APPLET_SETTINGS,
                           "label", _("GPaste daemon settings"),
                           NULL);
}
