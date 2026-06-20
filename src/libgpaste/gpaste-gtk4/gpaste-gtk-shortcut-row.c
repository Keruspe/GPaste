// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-shortcut-row.h>

struct _GPasteGtkShortcutRow
{
    AdwActionRow parent_instance;
};

enum
{
    PROP_0,
    PROP_ACCELERATOR,

    N_PROPERTIES
};

typedef struct
{
    gchar     *accelerator;       /* always non-NULL; "" means "no shortcut" */
    GtkWidget *label;             /* GtkShortcutLabel suffix */
    gboolean   capturing;
} GPasteGtkShortcutRowPrivate;

G_PASTE_GTK_DEFINE_TYPE_WITH_PRIVATE (ShortcutRow, shortcut_row, ADW_TYPE_ACTION_ROW)

static GParamSpec *properties[N_PROPERTIES] = { NULL };

/**
 * g_paste_gtk_shortcut_row_get_accelerator:
 * @self: a #GPasteGtkShortcutRow instance
 *
 * Get the accelerator currently held by the row, in the
 * gtk_accelerator_name() form (the empty string when none is set).
 *
 * Returns: a read-only accelerator string
 */
G_PASTE_VISIBLE const gchar *
g_paste_gtk_shortcut_row_get_accelerator (GPasteGtkShortcutRow *self)
{
    g_return_val_if_fail (G_PASTE_IS_GTK_SHORTCUT_ROW (self), "");

    const GPasteGtkShortcutRowPrivate *priv = g_paste_gtk_shortcut_row_get_instance_private (self);

    return priv->accelerator;
}

static void g_paste_gtk_shortcut_row_stop_capture (GPasteGtkShortcutRow *self);

/**
 * g_paste_gtk_shortcut_row_set_accelerator:
 * @self: a #GPasteGtkShortcutRow instance
 * @accelerator: (nullable): an accelerator in gtk_accelerator_name() form, or %NULL/"" to clear
 *
 * Set the accelerator displayed by the row. Notifies the "accelerator"
 * property if the value actually changed (so bound settings do not loop back).
 */
G_PASTE_VISIBLE void
g_paste_gtk_shortcut_row_set_accelerator (GPasteGtkShortcutRow *self,
                                          const gchar          *accelerator)
{
    g_return_if_fail (G_PASTE_IS_GTK_SHORTCUT_ROW (self));

    GPasteGtkShortcutRowPrivate *priv = g_paste_gtk_shortcut_row_get_instance_private (self);
    const gchar *accel = accelerator ? accelerator : "";

    if (g_paste_str_equal (priv->accelerator, accel))
        return;

    g_set_str (&priv->accelerator, accel);
    gtk_shortcut_label_set_accelerator (GTK_SHORTCUT_LABEL (priv->label), accel);

    /* An external change (e.g. "Reset to default") while the row is armed makes
     * the in-progress capture moot; disarm it so the next key isn't captured. */
    if (priv->capturing)
        g_paste_gtk_shortcut_row_stop_capture (self);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACCELERATOR]);
}

static void
g_paste_gtk_shortcut_row_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
    GPasteGtkShortcutRow *self = G_PASTE_GTK_SHORTCUT_ROW (object);

    switch (prop_id)
    {
    case PROP_ACCELERATOR:
        g_value_set_string (value, g_paste_gtk_shortcut_row_get_accelerator (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
g_paste_gtk_shortcut_row_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
    GPasteGtkShortcutRow *self = G_PASTE_GTK_SHORTCUT_ROW (object);

    switch (prop_id)
    {
    case PROP_ACCELERATOR:
        g_paste_gtk_shortcut_row_set_accelerator (self, g_value_get_string (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
g_paste_gtk_shortcut_row_stop_capture (GPasteGtkShortcutRow *self)
{
    GPasteGtkShortcutRowPrivate *priv = g_paste_gtk_shortcut_row_get_instance_private (self);

    priv->capturing = FALSE;
    adw_action_row_set_subtitle (ADW_ACTION_ROW (self), "");
    gtk_widget_remove_css_class (GTK_WIDGET (self), "accent");
}

static gboolean
g_paste_gtk_shortcut_row_on_key_pressed (GtkEventControllerKey *controller,
                                         guint                  keyval,
                                         guint                  keycode G_GNUC_UNUSED,
                                         GdkModifierType        state,
                                         gpointer               user_data)
{
    GPasteGtkShortcutRow *self = user_data;
    GPasteGtkShortcutRowPrivate *priv = g_paste_gtk_shortcut_row_get_instance_private (self);

    if (!priv->capturing)
        return GDK_EVENT_PROPAGATE;

    GdkModifierType mods = state & gtk_accelerator_get_default_mod_mask ();

    /* Escape (without modifiers) cancels, keeping the previous accelerator. */
    if (keyval == GDK_KEY_Escape && !mods)
    {
        g_paste_gtk_shortcut_row_stop_capture (self);
        return GDK_EVENT_STOP;
    }

    /* Backspace/Delete (without modifiers) clears the shortcut. */
    if ((keyval == GDK_KEY_BackSpace || keyval == GDK_KEY_Delete) && !mods)
    {
        g_paste_gtk_shortcut_row_set_accelerator (self, "");
        g_paste_gtk_shortcut_row_stop_capture (self);
        return GDK_EVENT_STOP;
    }

    /* Wait for a non-modifier key: a lone modifier is not a complete shortcut. */
    if (gdk_key_event_is_modifier (gtk_event_controller_get_current_event (GTK_EVENT_CONTROLLER (controller))))
        return GDK_EVENT_STOP;

    /* A modifier-less key would be grabbed globally and break ordinary typing,
     * so only accept a bare key for things that are meaningful alone (the
     * function keys); everything else needs a real modifier. */
    gboolean standalone_ok = keyval >= GDK_KEY_F1 && keyval <= GDK_KEY_F35;

    if (gtk_accelerator_valid (keyval, mods) && (mods || standalone_ok))
    {
        g_autofree gchar *name = gtk_accelerator_name (keyval, mods);

        g_paste_gtk_shortcut_row_set_accelerator (self, name);
        g_paste_gtk_shortcut_row_stop_capture (self);
    }

    /* Swallow everything else while capturing so it neither types nor navigates. */
    return GDK_EVENT_STOP;
}

static void
g_paste_gtk_shortcut_row_on_activated (AdwActionRow *row)
{
    GPasteGtkShortcutRow *self = G_PASTE_GTK_SHORTCUT_ROW (row);
    GPasteGtkShortcutRowPrivate *priv = g_paste_gtk_shortcut_row_get_instance_private (self);

    if (priv->capturing)
        return;

    priv->capturing = TRUE;
    /* translators: shown while the row waits for the user to press a shortcut */
    adw_action_row_set_subtitle (row, _("Press the new shortcut, Backspace to clear, Escape to cancel"));
    gtk_widget_add_css_class (GTK_WIDGET (self), "accent");
    gtk_widget_grab_focus (GTK_WIDGET (self));
}

static void
g_paste_gtk_shortcut_row_dispose (GObject *object)
{
    GPasteGtkShortcutRowPrivate *priv = g_paste_gtk_shortcut_row_get_instance_private (G_PASTE_GTK_SHORTCUT_ROW (object));

    g_clear_pointer (&priv->accelerator, g_free);

    G_OBJECT_CLASS (g_paste_gtk_shortcut_row_parent_class)->dispose (object);
}

static void
g_paste_gtk_shortcut_row_class_init (GPasteGtkShortcutRowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_gtk_shortcut_row_dispose;
    object_class->get_property = g_paste_gtk_shortcut_row_get_property;
    object_class->set_property = g_paste_gtk_shortcut_row_set_property;

    /**
     * GPasteGtkShortcutRow:accelerator:
     *
     * The captured accelerator, in gtk_accelerator_name() form ("" when unset).
     * Bindable, so the row can be wired straight to a settings property.
     */
    properties[PROP_ACCELERATOR] = g_param_spec_string ("accelerator", NULL, NULL, "",
                                                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

static void
g_paste_gtk_shortcut_row_init (GPasteGtkShortcutRow *self)
{
    GPasteGtkShortcutRowPrivate *priv = g_paste_gtk_shortcut_row_get_instance_private (self);

    priv->accelerator = g_strdup ("");

    priv->label = gtk_shortcut_label_new ("");
    /* translators: shown in a shortcut row when no shortcut is bound */
    gtk_shortcut_label_set_disabled_text (GTK_SHORTCUT_LABEL (priv->label), _("Disabled"));
    gtk_widget_set_valign (priv->label, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix (ADW_ACTION_ROW (self), priv->label);

    /* The row itself is the activation target. */
    gtk_widget_set_focusable (GTK_WIDGET (self), TRUE);
    gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (self), TRUE);

    /* Capture-phase key controller so the press is consumed before the list box
     * tries to navigate with it. */
    GtkEventController *key = gtk_event_controller_key_new ();
    gtk_event_controller_set_propagation_phase (key, GTK_PHASE_CAPTURE);
    g_signal_connect (key, "key-pressed", G_CALLBACK (g_paste_gtk_shortcut_row_on_key_pressed), self);
    gtk_widget_add_controller (GTK_WIDGET (self), key);

    g_signal_connect (self, "activated", G_CALLBACK (g_paste_gtk_shortcut_row_on_activated), NULL);
}

/**
 * g_paste_gtk_shortcut_row_new:
 * @title: the row title
 *
 * Create a new #GPasteGtkShortcutRow: an #AdwActionRow that captures a keyboard
 * accelerator when activated and displays it with a #GtkShortcutLabel.
 *
 * Returns: a newly allocated #GPasteGtkShortcutRow
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_gtk_shortcut_row_new (const gchar *title)
{
    g_return_val_if_fail (title, NULL);

    GtkWidget *self = g_object_new (G_PASTE_TYPE_GTK_SHORTCUT_ROW, "title", title, NULL);

    return self;
}
