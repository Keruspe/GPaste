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

#include <gpaste-applet-ui.h>
#include <gpaste-util.h>

struct _GPasteAppletUi
{
    GtkMenuItem parent_instance;
};

G_DEFINE_TYPE (GPasteAppletUi, g_paste_applet_ui, GTK_TYPE_MENU_ITEM)

static void
g_paste_applet_ui_activate (GtkMenuItem *menu_item G_GNUC_UNUSED)
{
    g_paste_util_spawn ("Ui", NULL);

    GTK_MENU_ITEM_CLASS (g_paste_applet_ui_parent_class)->activate (menu_item);
}

static void
g_paste_applet_ui_class_init (GPasteAppletUiClass *klass)
{
    GTK_MENU_ITEM_CLASS (klass)->activate = g_paste_applet_ui_activate;
}

static void
g_paste_applet_ui_init (GPasteAppletUi *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_applet_ui_new:
 *
 * Create a new instance of #GPasteAppletUi
 *
 * Returns: a newly allocated #GPasteAppletUi
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_applet_ui_new (void)
{
    return gtk_widget_new (G_PASTE_TYPE_APPLET_UI,
                           "label", _("launch the graphical tool"),
                           NULL);
}
