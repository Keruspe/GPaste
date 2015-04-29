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

#include <gpaste-ui-search-bar.h>

struct _GPasteUiSearchBar
{
    GtkSearchBar parent_instance;
};

typedef struct
{
    GtkSearchEntry *entry;
} GPasteUiSearchBarPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiSearchBar, g_paste_ui_search_bar, GTK_TYPE_SEARCH_BAR)

/**
 * g_paste_ui_search_bar_get_entry:
 *
 * Get the #GtkSearchEntry
 *
 * Returns: (transfer none): the #GtkSearchEntry
 */
G_PASTE_VISIBLE GtkSearchEntry *
g_paste_ui_search_bar_get_entry (const GPasteUiSearchBar *self)
{
    g_return_val_if_fail (G_PASTE_IS_UI_SEARCH_BAR (self), NULL);
    GPasteUiSearchBarPrivate *priv = g_paste_ui_search_bar_get_instance_private (self);

    return priv->entry;
}

static void
g_paste_ui_search_bar_class_init (GPasteUiSearchBarClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_ui_search_bar_init (GPasteUiSearchBar *self)
{
    GPasteUiSearchBarPrivate *priv = g_paste_ui_search_bar_get_instance_private (self);
    GtkWidget *entry = gtk_search_entry_new ();

    priv->entry = GTK_SEARCH_ENTRY (entry);

    gtk_container_add (GTK_CONTAINER (self), entry);
}

/**
 * g_paste_ui_search_bar_new:
 *
 * Create a new instance of #GPasteUiSearchBar
 *
 * Returns: a newly allocated #GPasteUiSearchBar
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_search_bar_new (void)
{
    return gtk_widget_new (G_PASTE_TYPE_UI_SEARCH_BAR,
                           NULL);
}
