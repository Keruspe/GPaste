/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-ui-search.h>

struct _GPasteUiSearch
{
    GtkToggleButton parent_instance;
};

G_PASTE_DEFINE_TYPE (UiSearch, ui_search, GTK_TYPE_TOGGLE_BUTTON)

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
    gtk_container_add (GTK_CONTAINER (self), gtk_image_new_from_icon_name ("edit-find-symbolic", GTK_ICON_SIZE_BUTTON));
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
    return gtk_widget_new (G_PASTE_TYPE_UI_SEARCH, NULL);
}
