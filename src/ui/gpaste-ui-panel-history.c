// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-ui-panel-history.h>
#include <gpaste-ui-window.h>

enum
{
    DELETE,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/* The row is being looked at when the pointer is on it or the keyboard is: GTK
 * marks the whole chain from the pointer's target up, so the row is prelit
 * wherever on it the pointer is -- over the button included, which is what
 * keeps the button from going away under a click aimed at it. The keyboard's
 * half is FOCUS_VISIBLE rather than FOCUSED: a click focuses the row it picks
 * as well, and would leave the button up on it once the pointer moved on. */
#define G_PASTE_UI_PANEL_HISTORY_LOOKED_AT (GTK_STATE_FLAG_PRELIGHT | \
                                            GTK_STATE_FLAG_FOCUS_VISIBLE)

struct _GPasteUiPanelHistory
{
    AdwSidebarItem parent_instance;

    GPasteClient *client;

    gchar        *history;

    /* The revealer the delete button comes wrapped in: our suffix on the row,
     * and what shows and hides it. */
    GtkWidget    *revealer;
    /* The #GtkListBoxRow the sidebar draws us on, which is where the hover, the
     * focus and the Delete key all are. An #AdwSidebarItem is a #GObject and
     * the row is the sidebar's to build, so ours is found rather than made --
     * and found again whenever the sidebar builds another. Weak: the row holds
     * a reference on us, so it is always the first of the two to go. */
    GtkWidget    *row;
};

G_PASTE_DEFINE_TYPE (UiPanelHistory, ui_panel_history, ADW_TYPE_SIDEBAR_ITEM)

/**
 * g_paste_ui_panel_history_activate:
 * @self: a #GPasteUiPanelHistory instance
 * @origin: a widget in the window to report a failure through -- an
 *          #AdwSidebarItem is a #GObject, so this one has none of its own
 *
 * Switch to this history
 */
void
g_paste_ui_panel_history_activate (GPasteUiPanelHistory *self,
                                   GtkWidget            *origin)
{
    g_return_if_fail (G_PASTE_IS_UI_PANEL_HISTORY (self));
    g_return_if_fail (GTK_IS_WIDGET (origin));

    g_paste_client_switch_history (self->client, self->history,
                                   NULL /* cancellable */,
                                   g_paste_ui_report_void_cb,
                                   g_paste_ui_report_void (origin, g_paste_client_switch_history_finish,
                                                           _("Could not switch history")));
}

/**
 * g_paste_ui_panel_history_set_length:
 * @self: a #GPasteUiPanelHistory instance
 * @length: the length of the #GPasteHistory
 *
 * Update the displayed length of this history
 */
void
g_paste_ui_panel_history_set_length (GPasteUiPanelHistory *self,
                                     guint64               length)
{
    g_return_if_fail (G_PASTE_IS_UI_PANEL_HISTORY (self));

    g_autofree gchar *str = g_strdup_printf ("%" G_GUINT64_FORMAT, length);

    adw_sidebar_item_set_subtitle (ADW_SIDEBAR_ITEM (self), str);
}

/**
 * g_paste_ui_panel_history_get_history:
 * @self: a #GPasteUiPanelHistory instance
 *
 * Get the underlying history name
 *
 * Returns: the name of the history
 */
const gchar *
g_paste_ui_panel_history_get_history (GPasteUiPanelHistory *self)
{
    g_return_val_if_fail (G_PASTE_IS_UI_PANEL_HISTORY (self), NULL);

    return self->history;
}

static void
g_paste_ui_panel_history_sync_revealed (GPasteUiPanelHistory *self)
{
    gtk_revealer_set_reveal_child (GTK_REVEALER (self->revealer),
                                   self->row && (gtk_widget_get_state_flags (self->row) & G_PASTE_UI_PANEL_HISTORY_LOOKED_AT));
}

static void
on_row_state_flags_changed (GtkWidget    *row   G_GNUC_UNUSED,
                            GtkStateFlags flags G_GNUC_UNUSED,
                            gpointer      user_data)
{
    g_paste_ui_panel_history_sync_revealed (user_data);
}

static void
on_delete_clicked (GtkButton *button G_GNUC_UNUSED,
                   gpointer   user_data)
{
    g_signal_emit (user_data, signals[DELETE], 0);
}

static gboolean
on_delete_shortcut (GtkWidget *row  G_GNUC_UNUSED,
                    GVariant  *args G_GNUC_UNUSED,
                    gpointer   user_data)
{
    g_signal_emit (user_data, signals[DELETE], 0);

    return TRUE;
}

/* The row the sidebar drew us on, reached from the one widget of ours it holds.
 * Its map rather than the suffix being set: a sidebar that rebuilds its list
 * hands the very same suffix to a brand new row, so this is asked again every
 * time rather than answered once. */
static void
on_revealer_mapped (GtkWidget *revealer,
                    gpointer   user_data)
{
    GPasteUiPanelHistory *self = user_data;
    GtkWidget *row = gtk_widget_get_ancestor (revealer, GTK_TYPE_LIST_BOX_ROW);

    if (!row || row == self->row)
        return;

    g_set_weak_pointer (&self->row, row);

    g_signal_connect_object (row, "state-flags-changed", G_CALLBACK (on_row_state_flags_changed), self, 0);

    /* The keyboard half of the button, on the row because that is what the
     * focus is on: the button itself is not a focus target, a Tab stop between
     * two histories buying nothing the Delete key does not already offer. Delete
     * alone: GNOME has BackSpace mean back, and a whole history is a lot to lose
     * to a key pressed for that. */
    GtkEventController *shortcuts = gtk_shortcut_controller_new ();

    gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (shortcuts),
                                          gtk_shortcut_new (gtk_shortcut_trigger_create_with_aliases (GDK_KEY_Delete, 0),
                                                            gtk_callback_action_new (on_delete_shortcut, self, NULL)));
    gtk_widget_add_controller (row, shortcuts);

    g_paste_ui_panel_history_sync_revealed (self);
}

static void
g_paste_ui_panel_history_dispose (GObject *object)
{
    GPasteUiPanelHistory *self = G_PASTE_UI_PANEL_HISTORY (object);

    g_clear_object (&self->client);
    /* Registered on the row, pointing at a field of ours: left in place it
     * would have GObject write into freed memory should the row outlive us. */
    g_clear_weak_pointer (&self->row);

    G_OBJECT_CLASS (g_paste_ui_panel_history_parent_class)->dispose (object);
}

static void
g_paste_ui_panel_history_finalize (GObject *object)
{
    GPasteUiPanelHistory *self = G_PASTE_UI_PANEL_HISTORY (object);

    g_clear_pointer (&self->history, g_free);

    G_OBJECT_CLASS (g_paste_ui_panel_history_parent_class)->finalize (object);
}

static void
g_paste_ui_panel_history_class_init (GPasteUiPanelHistoryClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_ui_panel_history_dispose;
    object_class->finalize = g_paste_ui_panel_history_finalize;

    /* The button and the Delete key ask for one and the same thing, and neither
     * has the window a confirmation goes over: the panel owns both. */
    signals[DELETE] = g_signal_new ("delete",
                                    G_PASTE_TYPE_UI_PANEL_HISTORY,
                                    G_SIGNAL_RUN_LAST,
                                    0, /* class offset */
                                    NULL, /* accumulator */
                                    NULL, /* accumulator data */
                                    g_cclosure_marshal_VOID__VOID,
                                    G_TYPE_NONE,
                                    0);
}

static void
g_paste_ui_panel_history_init (GPasteUiPanelHistory *self)
{
    GtkWidget *button = gtk_button_new_from_icon_name ("edit-delete-symbolic");

    gtk_widget_set_tooltip_text (button, _("Delete"));
    /* An icon gives no accessible name, and a tooltip becomes a description. */
    gtk_accessible_update_property (GTK_ACCESSIBLE (button), GTK_ACCESSIBLE_PROPERTY_LABEL, _("Delete"), -1);
    gtk_widget_add_css_class (button, "flat");
    gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
    gtk_widget_set_can_focus (button, FALSE);
    g_signal_connect (button, "clicked", G_CALLBACK (on_delete_clicked), self);

    /* Wrapped and slid sideways at once, as an item row's buttons are: the row
     * keeps a button's height whether it is showing one or not, and takes none
     * of its width while it is not.
     *
     * Shown only under the pointer or the keyboard, which the HIG advises
     * against, a touchscreen having no hover: a button on every row, always,
     * would crowd a sidebar that is a list of names first. Touch keeps the
     * context menu, which offers Delete too. */
    GtkWidget *revealer = self->revealer = gtk_revealer_new ();

    gtk_revealer_set_transition_type (GTK_REVEALER (revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_LEFT);
    gtk_revealer_set_transition_duration (GTK_REVEALER (revealer), 0);
    gtk_revealer_set_reveal_child (GTK_REVEALER (revealer), FALSE);
    gtk_revealer_set_child (GTK_REVEALER (revealer), button);

    g_signal_connect (revealer, "map", G_CALLBACK (on_revealer_mapped), self);

    adw_sidebar_item_set_suffix (ADW_SIDEBAR_ITEM (self), revealer);
}

/**
 * g_paste_ui_panel_history_new:
 * @client: a #GPasteClient instance
 * @history: the history we represent
 * @length: how many items it holds
 *
 * Create a new instance of #GPasteUiPanelHistory
 *
 * @length is passed in rather than asked for: the listing that named this
 * history answered its size along with it, so a sidebar of them costs one call
 * rather than one per row.
 *
 * Returns: a newly allocated #GPasteUiPanelHistory
 *          free it with g_object_unref
 */
GPasteUiPanelHistory *
g_paste_ui_panel_history_new (GPasteClient *client,
                              const gchar  *history,
                              guint64       length)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (g_utf8_validate (history, -1, NULL), NULL);

    GPasteUiPanelHistory *self = g_object_new (G_PASTE_TYPE_UI_PANEL_HISTORY, NULL);

    self->client = g_object_ref (client);
    self->history = g_strdup (history);

    adw_sidebar_item_set_title (ADW_SIDEBAR_ITEM (self), history);
    g_paste_ui_panel_history_set_length (self, length);

    return self;
}
