/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gtk4/gpaste-gtk-preferences-group.h>

struct _GPasteGtkPreferencesGroup
{
    AdwPreferencesGroup parent_instance;
};

typedef struct
{
    GSList *callback_data;
} GPasteGtkPreferencesGroupPrivate;

G_PASTE_GTK_DEFINE_TYPE_WITH_PRIVATE (PreferencesGroup, preferences_group, ADW_TYPE_PREFERENCES_GROUP)

#define CALLBACK_DATA(w)                                                                                \
    GPasteGtkPreferencesGroupPrivate *priv = g_paste_gtk_preferences_group_get_instance_private (self); \
    _CallbackDataWrapper *_data = (_CallbackDataWrapper *) g_malloc0 (sizeof (_CallbackDataWrapper));   \
    CallbackDataWrapper *data = (CallbackDataWrapper *) _data;                                          \
    priv->callback_data = g_slist_prepend (priv->callback_data, _data);                                 \
    _data->widget = G_OBJECT (w);                                                                       \
    data->callback = G_CALLBACK (on_value_changed);                                                     \
    data->reset_cb = on_reset;                                                                          \
    data->settings = settings;

#define G_PASTE_CALLBACK(cb_type)                                  \
    CallbackDataWrapper *data = (CallbackDataWrapper *) user_data; \
    ((cb_type) data->callback)

#define G_PASTE_RESET_CALLBACK()                                   \
    CallbackDataWrapper *data = (CallbackDataWrapper *) user_data; \
    (data->reset_cb)

typedef struct
{
    GCallback              callback;
    GPasteGtkResetCallback reset_cb;
    GPasteSettings        *settings;
} CallbackDataWrapper;

enum
{
    C_W_ACTION,
    C_W_RESET,

    C_W_LAST_SIGNAL
};

typedef struct
{
    CallbackDataWrapper wrap;
    GObject            *widget;
    GtkWidget          *reset_widget;

    guint64             c_signals[C_W_LAST_SIGNAL];
} _CallbackDataWrapper;

static void
boolean_wrapper (GObject    *object,
                 GParamSpec *pspec G_GNUC_UNUSED,
                 gpointer    user_data)
{
    G_PASTE_CALLBACK (GPasteGtkBooleanCallback) (data->settings, adw_switch_row_get_active (ADW_SWITCH_ROW (object)));
}

static gboolean
g_paste_gtk_preferences_group_on_reset_clicked (GtkWidget *widget G_GNUC_UNUSED,
                                                gpointer   user_data)
{
    G_PASTE_RESET_CALLBACK () (data->settings);
    return FALSE;
}

static GtkWidget *
g_paste_gtk_preferences_group_make_reset_button (_CallbackDataWrapper *data)
{
    GtkWidget *reset_widget = data->reset_widget = gtk_button_new_from_icon_name ("edit-delete-symbolic");
    data->c_signals[C_W_RESET] = g_signal_connect (reset_widget,
                                                   "clicked",
                                                   G_CALLBACK (g_paste_gtk_preferences_group_on_reset_clicked),
                                                   data);
    if (!((CallbackDataWrapper *) data)->reset_cb)
        gtk_widget_set_sensitive (reset_widget, FALSE);
    gtk_widget_set_valign (reset_widget, GTK_ALIGN_CENTER);
    return data->reset_widget;
}

/**
 * g_paste_gtk_preferences_group_add_boolean_setting:
 * @self: a #GPasteGtkPreferencesGroup instance
 * @label: the label to display
 * @value: the deafault value
 * @on_value_changed: (scope notified): the callback to call when the value changes
 * @on_reset: (scope notified): the callback to call when the value is reset
 * @settings: a #GPasteSettings instance
 *
 * Add a new boolean settings to the current pane
 *
 * Returns: (transfer none): the #AdwSwitchRow we just added
 */
G_PASTE_VISIBLE AdwSwitchRow *
g_paste_gtk_preferences_group_add_boolean_setting (GPasteGtkPreferencesGroup *self,
                                                   const gchar               *label,
                                                   gboolean                   value,
                                                   GPasteGtkBooleanCallback   on_value_changed,
                                                   GPasteGtkResetCallback     on_reset,
                                                   GPasteSettings            *settings)
{
    g_return_val_if_fail (G_PASTE_IS_GTK_PREFERENCES_GROUP (self), NULL);
    g_return_val_if_fail (label, NULL);
    g_return_val_if_fail (on_value_changed, NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    AdwSwitchRow *row = ADW_SWITCH_ROW (adw_switch_row_new ());

    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), label);
    adw_switch_row_set_active (row, value);

    CALLBACK_DATA (row);

    _data->c_signals[C_W_ACTION] = g_signal_connect (row, "notify::active", G_CALLBACK (boolean_wrapper), data);
    adw_action_row_add_suffix (ADW_ACTION_ROW (row), g_paste_gtk_preferences_group_make_reset_button (_data));

    adw_preferences_group_add (ADW_PREFERENCES_GROUP (self), GTK_WIDGET (row));

    return row;
}

static void
range_wrapper (AdwSpinRow *spin_row,
               GParamSpec *pspec G_GNUC_UNUSED,
               gpointer    user_data)
{
    G_PASTE_CALLBACK (GPasteGtkRangeCallback) (data->settings, (guint64) adw_spin_row_get_value (spin_row));
}

/**
 * g_paste_gtk_preferences_group_add_range_setting:
 * @self: a #GPasteGtkPreferencesGroup instance
 * @label: the label to display
 * @value: the deafault value
 * @min: the minimal authorized value
 * @max: the maximal authorized value
 * @step: the step between proposed values
 * @on_value_changed: (scope notified): the callback to call when the value changes
 * @on_reset: (scope notified): the callback to call when the value is reset
 * @settings: a #GPasteSettings instance
 *
 * Add a new range settings to the current pane
 *
 * Returns: (transfer none): the #AdwSpinRow we just added
 */
G_PASTE_VISIBLE AdwSpinRow *
g_paste_gtk_preferences_group_add_range_setting (GPasteGtkPreferencesGroup *self,
                                                 const gchar               *label,
                                                 gdouble                    value,
                                                 gdouble                    min,
                                                 gdouble                    max,
                                                 gdouble                    step,
                                                 GPasteGtkRangeCallback     on_value_changed,
                                                 GPasteGtkResetCallback     on_reset,
                                                 GPasteSettings            *settings)
{
    g_return_val_if_fail (G_PASTE_IS_GTK_PREFERENCES_GROUP (self), NULL);
    g_return_val_if_fail (label, NULL);
    g_return_val_if_fail (on_value_changed, NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    AdwSpinRow *row = ADW_SPIN_ROW (adw_spin_row_new_with_range (min, max, step));

    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), label);
    adw_spin_row_set_value (row, value);

    CALLBACK_DATA (row);

    _data->c_signals[C_W_ACTION] = g_signal_connect (row, "notify::value", G_CALLBACK (range_wrapper), data);
    adw_action_row_add_suffix (ADW_ACTION_ROW (row), g_paste_gtk_preferences_group_make_reset_button (_data));

    adw_preferences_group_add (ADW_PREFERENCES_GROUP (self), GTK_WIDGET (row));

    return row;
}

static void
text_wrapper (GObject    *object,
              GParamSpec *pspec G_GNUC_UNUSED,
              gpointer    user_data)
{
    G_PASTE_CALLBACK (GPasteGtkTextCallback) (data->settings, gtk_editable_get_text (GTK_EDITABLE (object)));
}

/**
 * g_paste_gtk_preferences_group_add_text_setting:
 * @self: a #GPasteGtkPreferencesGroup instance
 * @label: the label to display
 * @value: the deafault value
 * @on_value_changed: (scope notified): the callback to call when the value changes
 * @on_reset: (scope notified): the callback to call when the value is reset
 * @settings: a #GPasteSettings instance
 *
 * Add a new text settings to the current pane
 *
 * Returns: (transfer none): the #AdwEntryRow we just added
 */
G_PASTE_VISIBLE AdwEntryRow *
g_paste_gtk_preferences_group_add_text_setting (GPasteGtkPreferencesGroup *self,
                                                const gchar               *label,
                                                const gchar               *value,
                                                GPasteGtkTextCallback      on_value_changed,
                                                GPasteGtkResetCallback     on_reset,
                                                GPasteSettings            *settings)
{
    g_return_val_if_fail (G_PASTE_IS_GTK_PREFERENCES_GROUP (self), NULL);
    g_return_val_if_fail (label, NULL);
    g_return_val_if_fail (value, NULL);
    g_return_val_if_fail (on_value_changed, NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    AdwEntryRow *row = ADW_ENTRY_ROW (adw_entry_row_new ());

    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), label);
    gtk_editable_set_text (GTK_EDITABLE (row), value);

    CALLBACK_DATA (row);

    _data->c_signals[C_W_ACTION] = g_signal_connect (row, "notify::text", G_CALLBACK (text_wrapper), data);
    if (on_reset)
        adw_entry_row_add_suffix (row, g_paste_gtk_preferences_group_make_reset_button (_data));

    adw_preferences_group_add (ADW_PREFERENCES_GROUP (self), GTK_WIDGET (row));

    return row;
}

static void
clean_callback_data (gpointer data,
                     gpointer user_data G_GNUC_UNUSED)
{
    g_autofree _CallbackDataWrapper *wrap = data;

    g_signal_handler_disconnect (wrap->widget, wrap->c_signals[C_W_ACTION]);
    if (wrap->reset_widget)
        g_signal_handler_disconnect (wrap->reset_widget, wrap->c_signals[C_W_RESET]);
}

static void
g_paste_gtk_preferences_group_dispose (GObject *object)
{
    GPasteGtkPreferencesGroupPrivate *priv = g_paste_gtk_preferences_group_get_instance_private (G_PASTE_GTK_PREFERENCES_GROUP (object));

    g_slist_foreach (priv->callback_data, clean_callback_data, NULL);
    g_slist_free (priv->callback_data);
    priv->callback_data = NULL;

    G_OBJECT_CLASS (g_paste_gtk_preferences_group_parent_class)->dispose (object);
}

static void
g_paste_gtk_preferences_group_class_init (GPasteGtkPreferencesGroupClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_gtk_preferences_group_dispose;
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
