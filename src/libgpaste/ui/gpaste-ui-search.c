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

#include <gpaste-ui-search.h>

struct _GPasteUiSearch
{
    GtkToggleButton parent_instance;
};

G_DEFINE_TYPE (GPasteUiSearch, g_paste_ui_search, GTK_TYPE_TOGGLE_BUTTON)

static void
g_paste_ui_search_class_init (GPasteUiSearchClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_ui_search_init (GPasteUiSearch *self)
{
    GtkWidget *widget = GTK_WIDGET (self);

    gtk_widget_set_tooltip_text (widget, _("Search"));
    gtk_widget_set_valign (widget, GTK_ALIGN_CENTER);
}

/**
 * g_paste_ui_search_new:
 *
 * Create a new instance of #GPasteUiSearch
 *
 * Returns: a newly allocated #GPasteUiSearch
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_search_new (void)
{
    return gtk_widget_new (G_PASTE_TYPE_UI_SEARCH,
                           "image", gtk_image_new_from_icon_name ("edit-find-symbolic", GTK_ICON_SIZE_BUTTON),
                           NULL);
}
