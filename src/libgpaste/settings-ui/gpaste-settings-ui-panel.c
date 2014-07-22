/*
 *      This file is part of GPaste.
 *
 *      Copyright 2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-settings-ui-panel-private.h"

#include <stdlib.h>

struct _GPasteSettingsUiPanelPrivate
{
    GSList *callback_data;
    guint   current_line;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteSettingsUiPanel, g_paste_settings_ui_panel, GTK_TYPE_GRID)

#define CALLBACK_DATA_FULL(cb, rcb, w, d, d2)                                                         \
    GPasteSettingsUiPanelPrivate *priv = g_paste_settings_ui_panel_get_instance_private (self);       \
    _CallbackDataWrapper *_data = (_CallbackDataWrapper *) g_malloc0 (sizeof (_CallbackDataWrapper)); \
    CallbackDataWrapper *data = (CallbackDataWrapper *) _data;                                        \
    priv->callback_data = g_slist_prepend (priv->callback_data, _data);                               \
    _data->widget = GTK_WIDGET (w);                                                                   \
    data->callback = G_CALLBACK (cb);                                                                 \
    data->reset_cb = rcb;                                                                             \
    data->custom_data = user_data;                                                                    \
    data->user_data = (d) ? GTK_WIDGET (d) : NULL;                                                    \
    data->user_data2 = (d2) ? GTK_WIDGET (d2) : NULL

#define CALLBACK_DATA_DEFAULT(w) \
    CALLBACK_DATA_FULL (on_value_changed, on_reset, w, NULL, NULL)

#define CALLBACK_DATA_CONFIRM2(w, d, d2) \
    CALLBACK_DATA_FULL (confirm_action, NULL, w, d, d2)

#define CALLBACK_DATA_CONFIRM(w, d) \
    CALLBACK_DATA_CONFIRM2 (w, d, NULL)

#define G_PASTE_CALLBACK(cb_type)                                  \
    CallbackDataWrapper *data = (CallbackDataWrapper *) user_data; \
    ((cb_type) data->callback)

#define G_PASTE_RESET_CALLBACK()                                   \
    CallbackDataWrapper *data = (CallbackDataWrapper *) user_data; \
    (data->reset_cb)

typedef struct
{
    GCallback           callback;
    GPasteResetCallback reset_cb;
    gpointer            custom_data;
    GtkWidget          *user_data;
    GtkWidget          *user_data2;
} CallbackDataWrapper;

typedef struct
{
    CallbackDataWrapper wrap;
    GtkWidget          *widget;
    GtkWidget          *reset_widget;
    gulong              signal;
    gulong              reset_signal;
} _CallbackDataWrapper;

static void
boolean_wrapper (GObject    *object,
                 GParamSpec *pspec G_GNUC_UNUSED,
                 gpointer    user_data)
{
    G_PASTE_CALLBACK (GPasteBooleanCallback) (gtk_switch_get_active (GTK_SWITCH (object)), data->custom_data);
}

static GtkLabel *
g_paste_settings_ui_panel_add_label (GPasteSettingsUiPanel *self,
                                     const gchar           *label)
{
    GtkWidget *button_label = gtk_widget_new (GTK_TYPE_LABEL,
                                              "label",  label,
                                              "xalign", 0.0,
                                              NULL);

    GPasteSettingsUiPanelPrivate *priv = g_paste_settings_ui_panel_get_instance_private (self);

    gtk_widget_set_hexpand (button_label, TRUE);
    gtk_grid_attach (GTK_GRID (self), button_label, 0, priv->current_line++, 1, 1);

    return GTK_LABEL (button_label);
}

static gboolean
g_paste_settings_ui_panel_on_reset_pressed (GtkWidget       *widget G_GNUC_UNUSED,
                                            GdkEventButton  *event  G_GNUC_UNUSED,
                                            gpointer         user_data)
{
    G_PASTE_RESET_CALLBACK () (data->custom_data);
    return FALSE;
}

static GtkWidget *
g_paste_settings_ui_panel_make_reset_button (_CallbackDataWrapper *data)
{
    data->reset_widget = gtk_button_new_from_icon_name ("edit-delete-symbolic", GTK_ICON_SIZE_BUTTON);
    data->reset_signal = g_signal_connect (data->reset_widget,
                                           "button-press-event",
                                           G_CALLBACK (g_paste_settings_ui_panel_on_reset_pressed),
                                           data);
    if (!((CallbackDataWrapper *) data)->reset_cb)
        gtk_widget_set_sensitive (data->reset_widget, FALSE);
    return data->reset_widget;
}

/**
 * g_paste_settings_ui_panel_add_boolean_setting:
 * @self: a #GPasteSettingsUiPanel instance
 * @label: the label to display
 * @value: the deafault value
 * @on_value_changed: (closure user_data) (scope notified): the callback to call when the value changes
 * @on_reset: (closure user_data) (scope notified): the callback to call when the value is reset
 *
 * Add a new boolean settings to the current pane
 *
 * Returns: (transfer none): the #GtkSwitch we just added
 */
G_PASTE_VISIBLE GtkSwitch *
g_paste_settings_ui_panel_add_boolean_setting (GPasteSettingsUiPanel *self,
                                               const gchar           *label,
                                               gboolean               value,
                                               GPasteBooleanCallback  on_value_changed,
                                               GPasteResetCallback    on_reset,
                                               gpointer               user_data)
{
    GtkGrid *grid = GTK_GRID (self);
    GtkLabel *button_label = g_paste_settings_ui_panel_add_label (self, label);
    GtkWidget *widget = gtk_switch_new ();
    GtkSwitch *sw = GTK_SWITCH (widget);
    CALLBACK_DATA_DEFAULT (widget);

    gtk_switch_set_active (sw, value);
    _data->signal = g_signal_connect (widget, "notify::active", G_CALLBACK (boolean_wrapper), data);
    gtk_grid_attach_next_to (grid, widget, GTK_WIDGET (button_label), GTK_POS_RIGHT, 1, 1);
    gtk_grid_attach_next_to (grid, g_paste_settings_ui_panel_make_reset_button (_data), widget, GTK_POS_RIGHT, 1, 1);

    return sw;
}

/**
 * g_paste_settings_ui_panel_add_separator:
 * @self: a #GPasteSettingsUiPanel instance
 *
 * Add a new separator to the current pane
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_settings_ui_panel_add_separator (GPasteSettingsUiPanel *self)
{
    GPasteSettingsUiPanelPrivate *priv = g_paste_settings_ui_panel_get_instance_private (self);

    gtk_grid_attach (GTK_GRID (self), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL), 0, priv->current_line++, 3, 1);
}

static void
range_wrapper (GtkSpinButton *spinbutton,
               gpointer       user_data)
{
    G_PASTE_CALLBACK (GPasteRangeCallback) (gtk_spin_button_get_value (spinbutton), data->custom_data);
}

/**
 * g_paste_settings_ui_panel_add_range_setting:
 * @self: a #GPasteSettingsUiPanel instance
 * @label: the label to display
 * @value: the deafault value
 * @min: the minimal authorized value
 * @max: the maximal authorized value
 * @step: the step between proposed values
 * @on_value_changed: (closure user_data) (scope notified): the callback to call when the value changes
 * @on_reset: (closure user_data) (scope notified): the callback to call when the value is reset
 *
 * Add a new boolean settings to the current pane
 *
 * Returns: (transfer none): the #GtkSpinButton we just added
 */
G_PASTE_VISIBLE GtkSpinButton *
g_paste_settings_ui_panel_add_range_setting (GPasteSettingsUiPanel *self,
                                             const gchar           *label,
                                             gdouble                value,
                                             gdouble                min,
                                             gdouble                max,
                                             gdouble                step,
                                             GPasteRangeCallback    on_value_changed,
                                             GPasteResetCallback    on_reset,
                                             gpointer               user_data)
{
    GtkGrid *grid = GTK_GRID (self);
    GtkLabel *button_label = g_paste_settings_ui_panel_add_label (self, label);
    GtkWidget *button = gtk_spin_button_new_with_range (min, max, step);
    GtkSpinButton *b = GTK_SPIN_BUTTON (button);
    CALLBACK_DATA_DEFAULT (button);

    gtk_widget_set_hexpand (button, TRUE);
    gtk_spin_button_set_value (b, value);
    _data->signal = g_signal_connect (GTK_SPIN_BUTTON (button), "value-changed", G_CALLBACK (range_wrapper), data);
    gtk_grid_attach_next_to (grid, button, GTK_WIDGET (button_label), GTK_POS_RIGHT, 1, 1);
    gtk_grid_attach_next_to (grid, g_paste_settings_ui_panel_make_reset_button (_data), button, GTK_POS_RIGHT, 1, 1);

    return b;
}

static void
text_wrapper (GtkEditable *editable,
              gpointer     user_data)
{
    G_PASTE_CALLBACK (GPasteTextCallback) (gtk_entry_get_text (GTK_ENTRY (editable)), data->custom_data);
}

/**
 * g_paste_settings_ui_panel_add_text_setting:
 * @self: a #GPasteSettingsUiPanel instance
 * @label: the label to display
 * @value: the deafault value
 * @on_value_changed: (closure user_data) (scope notified): the callback to call when the value changes
 * @on_reset: (closure user_data) (scope notified): the callback to call when the value is reset
 *
 * Add a new text settings to the current pane
 *
 * Returns: (transfer none): the #GtkEntry we just added
 */
G_PASTE_VISIBLE GtkEntry *
g_paste_settings_ui_panel_add_text_setting (GPasteSettingsUiPanel *self,
                                            const gchar           *label,
                                            const gchar           *value,
                                            GPasteTextCallback     on_value_changed,
                                            GPasteResetCallback    on_reset,
                                            gpointer               user_data)
{
    GtkGrid *grid = GTK_GRID (self);
    GtkLabel *entry_label = g_paste_settings_ui_panel_add_label (self, label);
    GtkWidget *entry = gtk_entry_new ();
    GtkEntry *e = GTK_ENTRY (entry);
    CALLBACK_DATA_DEFAULT (entry);

    gtk_widget_set_hexpand (entry, TRUE);
    gtk_entry_set_text (e, value);
    _data->signal = g_signal_connect (GTK_EDITABLE (entry), "changed", G_CALLBACK (text_wrapper), data);
    gtk_grid_attach_next_to (GTK_GRID (self), entry, GTK_WIDGET (entry_label), GTK_POS_RIGHT, 1, 1);
    if (on_reset)
        gtk_grid_attach_next_to (grid, g_paste_settings_ui_panel_make_reset_button (_data), entry, GTK_POS_RIGHT, 1, 1);

    return e;
}

static GtkButton *
g_paste_settings_ui_panel_add_confirm_button (GPasteSettingsUiPanel *self,
                                              GtkWidget             *button,
                                              GtkWidget             *attach_to)
{
    gtk_widget_set_hexpand (button, TRUE);
    gtk_grid_attach_next_to (GTK_GRID (self), button, GTK_WIDGET (attach_to), GTK_POS_RIGHT, 1, 1);

    return GTK_BUTTON (button);
}

static gboolean
text_confirm_wrapper (GtkWidget      *widget G_GNUC_UNUSED,
                      GdkEventButton *event G_GNUC_UNUSED,
                      gpointer        user_data)
{
    G_PASTE_CALLBACK (GPasteTextCallback) (gtk_entry_get_text (GTK_ENTRY (data->user_data)), data->custom_data);
    return GDK_EVENT_PROPAGATE;
}

/**
 * g_paste_settings_ui_panel_add_text_confirm_setting:
 * @self: a #GPasteSettingsUiPanel instance
 * @label: the label to display
 * @value: the deafault value
 * @on_value_changed: (closure user_data1) (scope notified): the callback to call when the value changes
 * @confirm_label: the label of the confirmation button
 * @confirm_action: (closure user_data) (scope notified): the action to perform when the button is pressed
 *
 * Add a new text settings with confirmation to the current pane
 *
 * Returns: (transfer none): the #GtkEntry we just added
 */
G_PASTE_VISIBLE GtkEntry *
g_paste_settings_ui_panel_add_text_confirm_setting (GPasteSettingsUiPanel *self,
                                                    const gchar           *label,
                                                    const gchar           *value,
                                                    GPasteTextCallback     on_value_changed,
                                                    gpointer               user_data1,
                                                    const gchar           *confirm_label,
                                                    GPasteTextCallback     confirm_action,
                                                    gpointer               user_data)
{
    GtkEntry *entry = g_paste_settings_ui_panel_add_text_setting (self, label, value, on_value_changed, NULL, user_data1);
    GtkButton *button = g_paste_settings_ui_panel_add_confirm_button (self, gtk_button_new_with_label (confirm_label), GTK_WIDGET (entry));
    CALLBACK_DATA_CONFIRM (button, entry);

    _data->signal = g_signal_connect (button, "button-press-event", G_CALLBACK (text_confirm_wrapper), data);

    return entry;
}

static GtkWidget *
g_paste_settings_ui_panel_make_combo_box_text (const GStrv *labels)
{
    GtkWidget *combo_box = gtk_combo_box_text_new ();

    for (unsigned int i = 0; labels[i]; ++i)
        gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo_box), labels[i][0], labels[i][1]);
    gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), 0);

    return combo_box;
}

static gboolean
multi_action_wrapper (GtkWidget       *widget G_GNUC_UNUSED,
                      GdkEventButton  *event G_GNUC_UNUSED,
                      gpointer         user_data)
{
    G_PASTE_CALLBACK (GPasteMultiActionCallback) (gtk_combo_box_get_active_id (GTK_COMBO_BOX (data->user_data)),
                                                  gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (data->user_data2)),
                                                  data->custom_data);
    return GDK_EVENT_PROPAGATE;
}

/**
 * g_paste_settings_ui_panel_add_multi_action_setting:
 * @self: a #GPasteSettingsUiPanel instance
 * @action_labels: the labels to display as the available actions
 * @confirm_label: the label of the confirmation button
 * @confirm_action: (closure user_data) (scope notified): the action to perform when the button is pressed
 *
 * Add a new multi action settings to the current pane
 *
 * Returns: (transfer none): the #GtkComboBoxText we just added as the targets
 */
G_PASTE_VISIBLE GtkComboBoxText *
g_paste_settings_ui_panel_add_multi_action_setting (GPasteSettingsUiPanel    *self,
                                                    gchar ** const           *action_labels,
                                                    const gchar              *confirm_label,
                                                    GPasteMultiActionCallback confirm_action,
                                                    gpointer                  user_data)
{
    GtkWidget *actions = g_paste_settings_ui_panel_make_combo_box_text (action_labels);
    GtkWidget *targets = gtk_combo_box_text_new_with_entry ();
    GtkWidget *_button = gtk_button_new_with_label (confirm_label);
    CALLBACK_DATA_CONFIRM2 (_button, actions, targets);

    gtk_grid_attach (GTK_GRID (self), actions, 0, priv->current_line++, 1, 1);
    gtk_grid_attach_next_to (GTK_GRID (self), targets, actions, GTK_POS_RIGHT, 1, 1);

    GtkButton *button = g_paste_settings_ui_panel_add_confirm_button (self, _button, targets);

    _data->signal = g_signal_connect (button, "button-press-event", G_CALLBACK (multi_action_wrapper), data);

    return GTK_COMBO_BOX_TEXT (targets);
}

static void
clean_callback_data (gpointer data,
                     gpointer user_data G_GNUC_UNUSED)
{
    G_PASTE_CLEANUP_FREE _CallbackDataWrapper *wrap = data;

    g_signal_handler_disconnect (wrap->widget, wrap->signal);
    if (wrap->reset_widget)
        g_signal_handler_disconnect (wrap->reset_widget, wrap->reset_signal);
}

static void
g_paste_settings_ui_panel_dispose (GObject *object)
{
    GPasteSettingsUiPanelPrivate *priv = g_paste_settings_ui_panel_get_instance_private (G_PASTE_SETTINGS_UI_PANEL (object));

    g_slist_foreach (priv->callback_data, clean_callback_data, NULL);
    g_slist_free (priv->callback_data);
    priv->callback_data = NULL;

    G_OBJECT_CLASS (g_paste_settings_ui_panel_parent_class)->dispose (object);
}

static void
g_paste_settings_ui_panel_class_init (GPasteSettingsUiPanelClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_settings_ui_panel_dispose;
}

static void
g_paste_settings_ui_panel_init (GPasteSettingsUiPanel *self)
{
    GPasteSettingsUiPanelPrivate *priv = g_paste_settings_ui_panel_get_instance_private (self);

    priv->callback_data = NULL;
    priv->current_line = 0;

    GtkGrid *grid = GTK_GRID (self);

    gtk_grid_set_column_spacing (grid, 10);
    gtk_grid_set_row_spacing (grid, 10);
}

/**
 * g_paste_settings_ui_panel_new:
 *
 * Create a new instance of #GPasteSettingsUiPanel
 *
 * Returns: a newly allocated #GPasteSettingsUiPanel
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteSettingsUiPanel *
g_paste_settings_ui_panel_new (void) {
    return G_PASTE_SETTINGS_UI_PANEL (gtk_widget_new (G_PASTE_TYPE_SETTINGS_UI_PANEL, NULL));
}
