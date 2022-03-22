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
    G_PASTE_CALLBACK (GPasteGtkBooleanCallback) (data->settings, gtk_switch_get_active (GTK_SWITCH (object)));
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
 * @on_value_changed: (closure settings) (scope notified): the callback to call when the value changes
 * @on_reset: (closure settings) (scope notified): the callback to call when the value is reset
 *
 * Add a new boolean settings to the current pane
 *
 * Returns: (transfer none): the #GtkSwitch we just added
 */
G_PASTE_VISIBLE GtkSwitch *
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

    GtkWidget *sw = g_object_new (GTK_TYPE_SWITCH, "active", value, "valign", GTK_ALIGN_CENTER, NULL);
    AdwActionRow *row = ADW_ACTION_ROW (g_object_new (ADW_TYPE_ACTION_ROW, "title", label, "activatable-widget", sw, NULL));

    CALLBACK_DATA (sw);

    _data->c_signals[C_W_ACTION] = g_signal_connect (sw, "notify::active", G_CALLBACK (boolean_wrapper), data);
    adw_action_row_add_suffix (row, sw);
    adw_action_row_add_suffix (row, g_paste_gtk_preferences_group_make_reset_button (_data));

    adw_preferences_group_add (ADW_PREFERENCES_GROUP (self), GTK_WIDGET (row));

    return GTK_SWITCH (sw);
}

static void
range_wrapper (GtkSpinButton *spinbutton,
               gpointer       user_data)
{
    G_PASTE_CALLBACK (GPasteGtkRangeCallback) (data->settings, (guint64) gtk_spin_button_get_value (spinbutton));
}

/**
 * g_paste_gtk_preferences_group_add_range_setting:
 * @self: a #GPasteGtkPreferencesGroup instance
 * @label: the label to display
 * @value: the deafault value
 * @min: the minimal authorized value
 * @max: the maximal authorized value
 * @step: the step between proposed values
 * @on_value_changed: (closure settings) (scope notified): the callback to call when the value changes
 * @on_reset: (closure settings) (scope notified): the callback to call when the value is reset
 *
 * Add a new boolean settings to the current pane
 *
 * Returns: (transfer none): the #GtkSpinButton we just added
 */
G_PASTE_VISIBLE GtkSpinButton *
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

    GtkWidget *button = gtk_spin_button_new_with_range (min, max, step);
    GtkSpinButton *b = GTK_SPIN_BUTTON (button);
    GtkEditable *e = GTK_EDITABLE (b);
    AdwActionRow *row = ADW_ACTION_ROW (g_object_new (ADW_TYPE_ACTION_ROW, "title", label, "activatable-widget", button, NULL));

    CALLBACK_DATA (button);

    gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
    gtk_spin_button_set_value (b, value);
    gtk_editable_set_width_chars (e, 8);
    gtk_editable_set_alignment (e, 1.0);

    _data->c_signals[C_W_ACTION] = g_signal_connect (button, "value-changed", G_CALLBACK (range_wrapper), data);
    adw_action_row_add_suffix (row, button);
    adw_action_row_add_suffix (row, g_paste_gtk_preferences_group_make_reset_button (_data));

    adw_preferences_group_add (ADW_PREFERENCES_GROUP (self), GTK_WIDGET (row));

    return b;
}

static void
text_wrapper (GObject *buffer,
              gpointer user_data)
{
    G_PASTE_CALLBACK (GPasteGtkTextCallback) (data->settings, gtk_entry_buffer_get_text (GTK_ENTRY_BUFFER (buffer)));
}

/**
 * g_paste_gtk_preferences_group_add_text_setting:
 * @self: a #GPasteGtkPreferencesGroup instance
 * @label: the label to display
 * @value: the deafault value
 * @on_value_changed: (closure settings) (scope notified): the callback to call when the value changes
 * @on_reset: (closure settings) (scope notified): the callback to call when the value is reset
 *
 * Add a new text settings to the current pane
 *
 * Returns: (transfer none): the #GtkEntryBuffer from the #GtkEntry we just added
 */
G_PASTE_VISIBLE GtkEntryBuffer *
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

    GtkEntryBuffer *buffer = gtk_entry_buffer_new (value, -1);
    GtkWidget *entry = g_object_new (GTK_TYPE_ENTRY, "buffer", buffer, "valign", GTK_ALIGN_CENTER, "width-chars", 10, "xalign", 1.0, NULL);
    AdwActionRow *row = ADW_ACTION_ROW (g_object_new (ADW_TYPE_ACTION_ROW, "title", label, "activatable-widget", entry, NULL));

    CALLBACK_DATA (buffer);

    _data->c_signals[C_W_ACTION] = g_signal_connect (buffer, "notify::text", G_CALLBACK (text_wrapper), data);
    adw_action_row_add_suffix (row, entry);
    if (on_reset)
        adw_action_row_add_suffix (row, g_paste_gtk_preferences_group_make_reset_button (_data));

    adw_preferences_group_add (ADW_PREFERENCES_GROUP (self), GTK_WIDGET (row));

    return buffer;
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
