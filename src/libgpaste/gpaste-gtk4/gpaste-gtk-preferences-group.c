// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-preferences-group.h>
#include <gpaste-gtk4/gpaste-gtk-shortcut-row.h>

struct _GPasteGtkPreferencesGroup
{
    AdwPreferencesGroup parent_instance;
};

G_PASTE_GTK_DEFINE_TYPE (PreferencesGroup, preferences_group, ADW_TYPE_PREFERENCES_GROUP)

/* Settings expose one GObject property per key (named like the key), so each
 * row binds straight to its setting with g_object_bind_property(). A right-click
 * menu restores the key's default through the same notify path. */

typedef struct
{
    GPasteSettings *settings; /* not owned: outlives the row */
    const gchar    *key;      /* not owned: a static G_PASTE_*_SETTING string */
} ResetData;

/* --- Reset a preference through a right-click menu --------------------------
 *
 * GNOME apps don't usually expose a per-setting reset; this offers a
 * "Reset to default" row context menu. */

static void
on_reset_menu_clicked (GtkButton *button,
                       gpointer   user_data)
{
    const ResetData *data = user_data;

    g_paste_settings_reset (data->settings, data->key);

    GtkWidget *popover = gtk_widget_get_ancestor (GTK_WIDGET (button), GTK_TYPE_POPOVER);
    if (popover)
        gtk_popover_popdown (GTK_POPOVER (popover));
}

/* The menu popover is built on demand and unparents itself once closed, so it
 * is never a lingering child of the row when the row is finalized. */
static void
on_reset_popover_closed (GtkPopover *popover,
                         gpointer    user_data G_GNUC_UNUSED)
{
    gtk_widget_unparent (GTK_WIDGET (popover));
}

static void
popup_reset_menu (GtkGesture *gesture,
                  GtkWidget  *row,
                  gdouble     x,
                  gdouble     y)
{
    ResetData *data = g_object_get_data (G_OBJECT (row), "gpaste-reset-data");
    const GdkRectangle rect = { (gint) x, (gint) y, 1, 1 };

    /* A plain button that resets the key directly, like the suffix button does;
     * no GAction, so there is no action-muxer lookup to get wrong. Disabled
     * while the key is already at its default. */
    GtkWidget *button = gtk_button_new_with_label (_("Reset to default"));
    gtk_button_set_has_frame (GTK_BUTTON (button), FALSE);
    gtk_widget_set_sensitive (button, !g_paste_settings_is_default (data->settings, data->key));
    g_signal_connect (button, "clicked", G_CALLBACK (on_reset_menu_clicked), data);

    GtkWidget *popover = gtk_popover_new ();
    gtk_popover_set_child (GTK_POPOVER (popover), button);
    gtk_popover_set_has_arrow (GTK_POPOVER (popover), FALSE);
    gtk_widget_set_halign (popover, GTK_ALIGN_START);
    gtk_widget_set_parent (popover, row);
    gtk_popover_set_pointing_to (GTK_POPOVER (popover), &rect);
    g_signal_connect (popover, "closed", G_CALLBACK (on_reset_popover_closed), NULL);

    gtk_gesture_set_state (gesture, GTK_EVENT_SEQUENCE_CLAIMED);
    gtk_popover_popup (GTK_POPOVER (popover));
}

static void
on_row_secondary_pressed (GtkGestureClick *gesture,
                          gint             n_press G_GNUC_UNUSED,
                          gdouble          x,
                          gdouble          y,
                          gpointer         user_data)
{
    popup_reset_menu (GTK_GESTURE (gesture), user_data, x, y);
}

static void
on_row_long_pressed (GtkGestureLongPress *gesture,
                     gdouble              x,
                     gdouble              y,
                     gpointer             user_data)
{
    popup_reset_menu (GTK_GESTURE (gesture), user_data, x, y);
}

static void
add_reset_menu (GtkWidget      *row,
                GPasteSettings *settings,
                const gchar    *key)
{
    /* The settings/key the popup-on-demand menu resets; owned by the row. */
    ResetData *data = g_new0 (ResetData, 1);

    data->settings = settings;
    data->key = key;
    g_object_set_data_full (G_OBJECT (row), "gpaste-reset-data", data, g_free);

    /* Right-click (and touch long-press) anywhere on the row pops the menu. */
    GtkGesture *secondary = gtk_gesture_click_new ();
    gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (secondary), GDK_BUTTON_SECONDARY);
    g_signal_connect (secondary, "pressed", G_CALLBACK (on_row_secondary_pressed), row);
    gtk_widget_add_controller (row, GTK_EVENT_CONTROLLER (secondary));

    GtkGesture *long_press = gtk_gesture_long_press_new ();
    gtk_gesture_single_set_touch_only (GTK_GESTURE_SINGLE (long_press), TRUE);
    g_signal_connect (long_press, "pressed", G_CALLBACK (on_row_long_pressed), row);
    gtk_widget_add_controller (row, GTK_EVENT_CONTROLLER (long_press));
}

/**
 * g_paste_gtk_preferences_group_add_boolean_setting:
 * @self: a #GPasteGtkPreferencesGroup instance
 * @label: the label to display
 * @key: the settings key (and property name) this row tracks
 * @settings: a #GPasteSettings instance
 *
 * Add a new boolean setting to the current pane, bound to @key.
 *
 * Returns: (transfer none): the #AdwSwitchRow we just added
 */
G_PASTE_VISIBLE AdwSwitchRow *
g_paste_gtk_preferences_group_add_boolean_setting (GPasteGtkPreferencesGroup *self,
                                                   const gchar               *label,
                                                   const gchar               *key,
                                                   GPasteSettings            *settings)
{
    g_return_val_if_fail (G_PASTE_IS_GTK_PREFERENCES_GROUP (self), NULL);
    g_return_val_if_fail (label, NULL);
    g_return_val_if_fail (key, NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    AdwSwitchRow *row = ADW_SWITCH_ROW (adw_switch_row_new ());

    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), label);
    g_object_bind_property (settings, key, row, "active", G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
    add_reset_menu (GTK_WIDGET (row), settings, key);

    adw_preferences_group_add (ADW_PREFERENCES_GROUP (self), GTK_WIDGET (row));

    return row;
}

static gboolean
uint64_to_double (GBinding     *binding G_GNUC_UNUSED,
                  const GValue *from,
                  GValue       *to,
                  gpointer      user_data G_GNUC_UNUSED)
{
    g_value_set_double (to, (gdouble) g_value_get_uint64 (from));
    return TRUE;
}

static gboolean
double_to_uint64 (GBinding     *binding G_GNUC_UNUSED,
                  const GValue *from,
                  GValue       *to,
                  gpointer      user_data G_GNUC_UNUSED)
{
    g_value_set_uint64 (to, (guint64) g_value_get_double (from));
    return TRUE;
}

/**
 * g_paste_gtk_preferences_group_add_range_setting:
 * @self: a #GPasteGtkPreferencesGroup instance
 * @label: the label to display
 * @key: the settings key (and property name) this row tracks
 * @min: the minimal authorized value
 * @max: the maximal authorized value
 * @step: the step between proposed values
 * @settings: a #GPasteSettings instance
 *
 * Add a new range setting to the current pane, bound to @key.
 *
 * Returns: (transfer none): the #AdwSpinRow we just added
 */
G_PASTE_VISIBLE AdwSpinRow *
g_paste_gtk_preferences_group_add_range_setting (GPasteGtkPreferencesGroup *self,
                                                 const gchar               *label,
                                                 const gchar               *key,
                                                 gdouble                    min,
                                                 gdouble                    max,
                                                 gdouble                    step,
                                                 GPasteSettings            *settings)
{
    g_return_val_if_fail (G_PASTE_IS_GTK_PREFERENCES_GROUP (self), NULL);
    g_return_val_if_fail (label, NULL);
    g_return_val_if_fail (key, NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    AdwSpinRow *row = ADW_SPIN_ROW (adw_spin_row_new_with_range (min, max, step));

    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), label);
    /* The setting is a guint64, the row value a gdouble. */
    g_object_bind_property_full (settings, key, row, "value",
                                 G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL,
                                 uint64_to_double, double_to_uint64, NULL, NULL);
    add_reset_menu (GTK_WIDGET (row), settings, key);

    adw_preferences_group_add (ADW_PREFERENCES_GROUP (self), GTK_WIDGET (row));

    return row;
}

/**
 * g_paste_gtk_preferences_group_add_text_setting:
 * @self: a #GPasteGtkPreferencesGroup instance
 * @label: the label to display
 * @key: the settings key (and property name) this row tracks
 * @settings: a #GPasteSettings instance
 *
 * Add a new text setting to the current pane, bound to @key.
 *
 * Returns: (transfer none): the #AdwEntryRow we just added
 */
G_PASTE_VISIBLE AdwEntryRow *
g_paste_gtk_preferences_group_add_text_setting (GPasteGtkPreferencesGroup *self,
                                                const gchar               *label,
                                                const gchar               *key,
                                                GPasteSettings            *settings)
{
    g_return_val_if_fail (G_PASTE_IS_GTK_PREFERENCES_GROUP (self), NULL);
    g_return_val_if_fail (label, NULL);
    g_return_val_if_fail (key, NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    AdwEntryRow *row = ADW_ENTRY_ROW (adw_entry_row_new ());

    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), label);
    g_object_bind_property (settings, key, row, "text", G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
    add_reset_menu (GTK_WIDGET (row), settings, key);

    adw_preferences_group_add (ADW_PREFERENCES_GROUP (self), GTK_WIDGET (row));

    return row;
}

/**
 * g_paste_gtk_preferences_group_add_shortcut_setting:
 * @self: a #GPasteGtkPreferencesGroup instance
 * @label: the label to display
 * @key: the settings key (and property name) this row tracks
 * @settings: a #GPasteSettings instance
 *
 * Add a new keyboard-shortcut setting to the current pane, bound to @key. The
 * row captures the key combination the user presses rather than having them
 * type the accelerator by hand.
 *
 * Returns: (transfer none): the #GPasteGtkShortcutRow we just added
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_gtk_preferences_group_add_shortcut_setting (GPasteGtkPreferencesGroup *self,
                                                    const gchar               *label,
                                                    const gchar               *key,
                                                    GPasteSettings            *settings)
{
    g_return_val_if_fail (G_PASTE_IS_GTK_PREFERENCES_GROUP (self), NULL);
    g_return_val_if_fail (label, NULL);
    g_return_val_if_fail (key, NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    GtkWidget *row = g_paste_gtk_shortcut_row_new (label);

    g_object_bind_property (settings, key, row, "accelerator", G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
    add_reset_menu (row, settings, key);

    adw_preferences_group_add (ADW_PREFERENCES_GROUP (self), row);

    return row;
}

/**
 * g_paste_gtk_preferences_group_add_button:
 * @self: a #GPasteGtkPreferencesGroup instance
 * @label: the label to display
 * @activated: (scope forever) (closure user_data): the callback to run when the row is activated
 * @user_data: user data for @activated
 *
 * Add a button-like row to the current pane that runs @activated when clicked.
 * Unlike the setting rows, this one is not bound to a key.
 *
 * Returns: (transfer none): the #AdwButtonRow we just added
 */
G_PASTE_VISIBLE AdwButtonRow *
g_paste_gtk_preferences_group_add_button (GPasteGtkPreferencesGroup *self,
                                          const gchar               *label,
                                          GCallback                  activated,
                                          gpointer                   user_data)
{
    g_return_val_if_fail (G_PASTE_IS_GTK_PREFERENCES_GROUP (self), NULL);
    g_return_val_if_fail (label, NULL);

    AdwButtonRow *row = ADW_BUTTON_ROW (adw_button_row_new ());

    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), label);
    if (activated)
        g_signal_connect (row, "activated", activated, user_data);

    adw_preferences_group_add (ADW_PREFERENCES_GROUP (self), GTK_WIDGET (row));

    return row;
}

static void
g_paste_gtk_preferences_group_class_init (GPasteGtkPreferencesGroupClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_gtk_preferences_group_init (GPasteGtkPreferencesGroup *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_gtk_preferences_group_new:
 * @title: The title of the preferences group
 *
 * Create a new instance of #GPasteGtkPreferencesGroup
 *
 * Returns: a newly allocated #GPasteGtkPreferencesGroup
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteGtkPreferencesGroup *
g_paste_gtk_preferences_group_new (const gchar *title) {
    g_return_val_if_fail (title, NULL);

    return G_PASTE_GTK_PREFERENCES_GROUP (g_object_new (G_PASTE_TYPE_GTK_PREFERENCES_GROUP, "title", title, NULL));
}
