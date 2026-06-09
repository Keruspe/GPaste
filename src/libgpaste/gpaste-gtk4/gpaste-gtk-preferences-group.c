// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-preferences-group.h>

struct _GPasteGtkPreferencesGroup
{
    AdwPreferencesGroup parent_instance;
};

G_PASTE_GTK_DEFINE_TYPE (PreferencesGroup, preferences_group, ADW_TYPE_PREFERENCES_GROUP)

/* Settings expose one GObject property per key (named like the key), so each
 * row binds straight to its setting with g_object_bind_property(). The reset
 * suffix restores the key's default through the same notify path. */

typedef struct
{
    GPasteSettings *settings; /* not owned: outlives the row */
    const gchar    *key;      /* not owned: a static G_PASTE_*_SETTING string */
} ResetData;

static void
on_reset_clicked (GtkButton *button G_GNUC_UNUSED,
                  gpointer   user_data)
{
    const ResetData *data = user_data;

    g_paste_settings_reset (data->settings, data->key);
}

static void
reset_data_free (gpointer  data,
                 GClosure *closure G_GNUC_UNUSED)
{
    g_free (data);
}

static void
add_reset_button (GtkWidget      *row,
                  GPasteSettings *settings,
                  const gchar    *key)
{
    GtkWidget *button = gtk_button_new_from_icon_name ("edit-delete-symbolic");

    gtk_widget_add_css_class (button, "flat");
    gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text (button, _("Reset to default"));

    ResetData *data = g_new0 (ResetData, 1);
    data->settings = settings;
    data->key = key;
    g_signal_connect_data (button, "clicked", G_CALLBACK (on_reset_clicked), data, reset_data_free, 0);

    /* AdwEntryRow is not an AdwActionRow, so it has its own suffix API. */
    if (ADW_IS_ENTRY_ROW (row))
        adw_entry_row_add_suffix (ADW_ENTRY_ROW (row), button);
    else
        adw_action_row_add_suffix (ADW_ACTION_ROW (row), button);
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
    add_reset_button (GTK_WIDGET (row), settings, key);

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
    add_reset_button (GTK_WIDGET (row), settings, key);

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
    add_reset_button (GTK_WIDGET (row), settings, key);

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
