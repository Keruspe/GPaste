// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-macros.h>

#include <gpaste-ui-header.h>

typedef struct
{
    GtkToggleButton *favourites;
    GtkToggleButton *search;
    AdwWindowTitle  *title;
    GtkWidget       *new_item;
    GtkWidget       *menu;
    GtkWidget       *merge;  /* enters merge selection mode */
    GtkWidget       *cancel; /* leaves it */
} GPasteUiHeaderData;

/* AdwHeaderBar is G_DECLARE_FINAL_TYPE, so there is no GPasteUiHeader type to
 * check against and no instance struct to put these in: the bar carries them as
 * qdata instead. Which also means ADW_IS_HEADER_BAR() cannot tell *our* header
 * bar from any other -- the qdata being there is what says so, so look for it
 * rather than assume it. */
static GPasteUiHeaderData *
header_data (AdwHeaderBar *self)
{
    g_return_val_if_fail (ADW_IS_HEADER_BAR (self), NULL);

    GPasteUiHeaderData *data = g_object_get_data (G_OBJECT (self), "header-data");

    g_return_val_if_fail (data, NULL);

    return data;
}

/* An icon-only button names itself with its tooltip and nothing else, which
 * leaves it nameless to a screen reader: GTK feeds a tooltip in as the
 * accessible *description*, and the label it would take a name from is the
 * icon. So say it twice, once for each. */
static void
set_icon_button_label (GtkWidget   *button,
                       const gchar *label)
{
    gtk_widget_set_tooltip_text (button, label);
    gtk_accessible_update_property (GTK_ACCESSIBLE (button), GTK_ACCESSIBLE_PROPERTY_LABEL, label, -1);
}

/* A plain header button: shared icon/label/valign setup, so the toolbar's
 * simple actions don't each need spelling out. */
static GtkWidget *
header_button_new (const gchar *icon_name,
                   const gchar *label,
                   const gchar *action_name)
{
    GtkWidget *button = gtk_button_new_from_icon_name (icon_name);

    set_icon_button_label (button, label);
    gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
    gtk_actionable_set_action_name (GTK_ACTIONABLE (button), action_name);

    return button;
}

static GtkWidget *
header_toggle_new (const gchar *icon_name,
                   const gchar *label)
{
    GtkWidget *button = gtk_toggle_button_new ();

    set_icon_button_label (button, label);
    gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
    gtk_button_set_child (GTK_BUTTON (button), gtk_image_new_from_icon_name (icon_name));

    return button;
}

/* Everything the window as a whole can be asked to do, in one menu at the end of
 * the header rather than as an icon apiece: they are app-level and rare, where
 * the buttons left beside them all act on the history being shown. */
static GtkWidget *
header_menu_new (void)
{
    g_autoptr (GMenu) menu = g_menu_new ();

    /* Stateful and boolean, so it comes out as a check item. */
    g_menu_append (menu, _("Track Clipboard Changes"), "win.track-changes");

    g_autoptr (GMenu) daemon_section = g_menu_new ();
    g_menu_append (daemon_section, _("Restart Daemon"), "win.restart-daemon");
    g_menu_append_section (menu, NULL, G_MENU_MODEL (daemon_section));

    g_autoptr (GMenu) app_section = g_menu_new ();
    g_menu_append (app_section, _("Preferences"), "win.preferences");
    g_menu_append (app_section, _("Keyboard Shortcuts"), "win.show-help-overlay");
    g_menu_append (app_section, _("About GPaste"), "win.about");
    g_menu_append_section (menu, NULL, G_MENU_MODEL (app_section));

    GtkWidget *button = gtk_menu_button_new ();

    gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (button), "open-menu-symbolic");
    gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (button), G_MENU_MODEL (menu));
    set_icon_button_label (button, _("Main Menu"));
    gtk_widget_set_valign (button, GTK_ALIGN_CENTER);

    return button;
}

/**
 * g_paste_ui_header_set_subtitle:
 * @self: the header bar
 * @subtitle: the subtitle to display (current history name)
 *
 * Update the subtitle shown in the window title widget
 */
void
g_paste_ui_header_set_subtitle (AdwHeaderBar *self,
                                const gchar  *subtitle)
{
    GPasteUiHeaderData *data = header_data (self);

    if (!data)
        return;

    adw_window_title_set_subtitle (data->title, subtitle);
}

/**
 * g_paste_ui_header_get_favourites_button:
 * @self: the #AdwHeaderBar
 *
 * Get the button filtering the list down to the pinned items
 *
 * Returns: (transfer none): the #GtkToggleButton for the favourites filter
 */
GtkToggleButton *
g_paste_ui_header_get_favourites_button (AdwHeaderBar *self)
{
    GPasteUiHeaderData *data = header_data (self);

    if (!data)
        return NULL;

    return data->favourites;
}

/**
 * g_paste_ui_header_get_search_button:
 * @self: the header bar
 *
 * Get the search button
 *
 * Returns: (transfer none): the #GtkToggleButton for search
 */
GtkToggleButton *
g_paste_ui_header_get_search_button (AdwHeaderBar *self)
{
    GPasteUiHeaderData *data = header_data (self);

    if (!data)
        return NULL;

    return data->search;
}

/**
 * g_paste_ui_header_get_merge_button:
 * @self: the header bar
 *
 * Get the button that enters merge selection mode
 *
 * Returns: (transfer none): the merge #GtkButton
 */
GtkWidget *
g_paste_ui_header_get_merge_button (AdwHeaderBar *self)
{
    GPasteUiHeaderData *data = header_data (self);

    if (!data)
        return NULL;

    return data->merge;
}

/**
 * g_paste_ui_header_get_cancel_button:
 * @self: the header bar
 *
 * Get the button that leaves merge selection mode
 *
 * Returns: (transfer none): the cancel #GtkButton
 */
GtkWidget *
g_paste_ui_header_get_cancel_button (AdwHeaderBar *self)
{
    GPasteUiHeaderData *data = header_data (self);

    if (!data)
        return NULL;

    return data->cancel;
}

/**
 * g_paste_ui_header_set_selection_mode:
 * @self: the header bar
 * @selection_mode: whether merge selection mode is active
 *
 * Switch the header between its normal look and the selection-mode look: the
 * controls that act on the history give way to Cancel, and the title says how
 * many items are picked.
 */
void
g_paste_ui_header_set_selection_mode (AdwHeaderBar *self,
                                      gboolean      selection_mode)
{
    GPasteUiHeaderData *data = header_data (self);

    if (!data)
        return;

    if (!selection_mode)
        adw_window_title_set_title (data->title, PACKAGE_NAME);
    else
    {
        /* Both toggles are about to leave the bar, and a filter left on would
         * keep narrowing the rows being picked from with nothing on screen left
         * to turn it off. */
        gtk_toggle_button_set_active (data->search, FALSE);
        gtk_toggle_button_set_active (data->favourites, FALSE);
    }

    /* What the mode looks like is what is left in the bar: everything that does
     * not belong to picking items goes, which is how GNOME says "you are in a
     * mode" without a style class. (There is none to reach for anyway --
     * libadwaita defines .selection-mode for check buttons alone.) */
    gtk_widget_set_visible (data->new_item, !selection_mode);
    gtk_widget_set_visible (GTK_WIDGET (data->search), !selection_mode);
    gtk_widget_set_visible (GTK_WIDGET (data->favourites), !selection_mode);
    gtk_widget_set_visible (data->menu, !selection_mode);
    gtk_widget_set_visible (data->merge, !selection_mode);
    gtk_widget_set_visible (data->cancel, selection_mode);
}

/**
 * g_paste_ui_header_set_selection_count:
 * @self: the header bar
 * @count: the number of selected items
 *
 * Update the title to reflect how many items are selected for merging.
 */
void
g_paste_ui_header_set_selection_count (AdwHeaderBar *self,
                                       guint         count)
{
    GPasteUiHeaderData *data = header_data (self);

    if (!data)
        return;
    g_autofree gchar *title = g_strdup_printf (ngettext ("%u selected", "%u selected", count), count);

    adw_window_title_set_title (data->title, title);
}

/**
 * g_paste_ui_header_new:
 *
 * Create a new #AdwHeaderBar configured for GPaste. Every control it carries
 * drives a "win." action, so the window owns the state and the header owns only
 * how it is shown.
 *
 * Returns: a newly allocated #AdwHeaderBar
 *          free it with g_object_unref
 */
GtkWidget *
g_paste_ui_header_new (void)
{
    GtkWidget *self = adw_header_bar_new ();
    AdwHeaderBar *bar = ADW_HEADER_BAR (self);

    GtkWidget *new_item = header_button_new ("document-new-symbolic", _("New Item"), "win.new-item");
    GtkWidget *search = header_toggle_new ("edit-find-symbolic", _("Search"));
    GtkWidget *favourites = header_toggle_new ("starred-symbolic", _("Show Only Pinned Items"));
    GtkWidget *merge = header_button_new ("edit-select-all-symbolic", _("Select Items to Merge"), NULL);
    GtkWidget *menu = header_menu_new ();
    GtkWidget *title = adw_window_title_new (PACKAGE_NAME, NULL);

    GtkWidget *cancel = gtk_button_new_with_label (_("Cancel"));
    gtk_widget_set_valign (cancel, GTK_ALIGN_CENTER);
    gtk_widget_set_visible (cancel, FALSE);

    GPasteUiHeaderData *data = g_new0 (GPasteUiHeaderData, 1);
    data->favourites = GTK_TOGGLE_BUTTON (favourites);
    data->search = GTK_TOGGLE_BUTTON (search);
    data->title = ADW_WINDOW_TITLE (title);
    data->new_item = new_item;
    data->menu = menu;
    data->merge = merge;
    data->cancel = cancel;

    g_object_set_data_full (G_OBJECT (self), "header-data", data, g_free);

    adw_header_bar_set_title_widget (bar, title);
    adw_header_bar_pack_start (bar, new_item);
    adw_header_bar_pack_start (bar, cancel);
    adw_header_bar_pack_end (bar, menu);
    adw_header_bar_pack_end (bar, merge);
    adw_header_bar_pack_end (bar, favourites);
    adw_header_bar_pack_end (bar, search);

    return self;
}
