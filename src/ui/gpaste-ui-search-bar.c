/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-ui-search-bar.h>

/**
 * g_paste_ui_search_bar_get_entry:
 * @self: a #GtkSearchBar
 *
 * Get the #GtkSearchEntry
 *
 * Returns: (transfer none): the #GtkSearchEntry
 */
G_PASTE_VISIBLE GtkSearchEntry *
g_paste_ui_search_bar_get_entry (GtkSearchBar *self)
{
    g_return_val_if_fail (GTK_IS_SEARCH_BAR (self), NULL);

    return GTK_SEARCH_ENTRY (g_object_get_data (G_OBJECT (self), "entry"));
}

/**
 * g_paste_ui_search_bar_new:
 *
 * Create a new #GtkSearchBar for GPaste
 *
 * Returns: a newly allocated #GtkSearchBar
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_search_bar_new (void)
{
    GtkWidget *self = gtk_search_bar_new ();
    GtkWidget *entry = gtk_search_entry_new ();

    g_object_set_data (G_OBJECT (self), "entry", entry);
    gtk_search_bar_set_child (GTK_SEARCH_BAR (self), entry);
    gtk_search_bar_connect_entry (GTK_SEARCH_BAR (self), GTK_EDITABLE (entry));

    return self;
}
