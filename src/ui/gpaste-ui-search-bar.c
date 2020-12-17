/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (UiSearchBar, ui_search_bar, GTK_TYPE_SEARCH_BAR)

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
    g_return_val_if_fail (_G_PASTE_IS_UI_SEARCH_BAR (self), NULL);
    const GPasteUiSearchBarPrivate *priv = _g_paste_ui_search_bar_get_instance_private (self);

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
