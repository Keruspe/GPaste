/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-ui-switch.h>
#include <gpaste-util.h>

struct _GPasteUiSwitch
{
    GtkSwitch parent_instance;
};

enum
{
    C_TRACKING,

    C_LAST_SIGNAL
};

typedef struct
{
    GPasteClient *client;

    GtkWindow    *topwin;

    guint64       c_signals[C_LAST_SIGNAL];
} GPasteUiSwitchPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (UiSwitch, ui_switch, GTK_TYPE_SWITCH)

static void
on_tracking_changed (GPasteClient *client G_GNUC_UNUSED,
                     gboolean      state,
                     gpointer      user_data)
{
    GtkSwitch *sw = user_data;

    gtk_switch_set_active (sw, state);
}

static gboolean
g_paste_ui_button_press_event (GtkWidget      *widget,
                               GdkEventButton *event G_GNUC_UNUSED)
{
    const GPasteUiSwitchPrivate *priv = _g_paste_ui_switch_get_instance_private (G_PASTE_UI_SWITCH (widget));
    GtkSwitch *sw = GTK_SWITCH (widget);
    gboolean track = !gtk_switch_get_active (sw);
    gboolean changed = TRUE;

    if (!track)
    {
        changed = g_paste_util_confirm_dialog (priv->topwin, _("Stop"), _("Do you really want to stop tracking clipboard changes?"));
        track = !changed;
    }

    if (changed)
        g_paste_client_track (priv->client, track, NULL, NULL);

    return GDK_EVENT_STOP;
}

static void
g_paste_ui_switch_dispose (GObject *object)
{
    GPasteUiSwitchPrivate *priv = g_paste_ui_switch_get_instance_private (G_PASTE_UI_SWITCH (object));

    if (priv->c_signals[C_TRACKING])
    {
        g_signal_handler_disconnect (priv->client, priv->c_signals[C_TRACKING]);
        priv->c_signals[C_TRACKING] = 0;
    }

    g_clear_object (&priv->client);

    G_OBJECT_CLASS (g_paste_ui_switch_parent_class)->dispose (object);
}

static void
g_paste_ui_switch_class_init (GPasteUiSwitchClass *klass)
{
    GTK_WIDGET_CLASS (klass)->button_press_event = g_paste_ui_button_press_event;
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_switch_dispose;
}

static void
g_paste_ui_switch_init (GPasteUiSwitch *self)
{
    GtkWidget *widget = GTK_WIDGET (self);

    gtk_widget_set_tooltip_text (widget, _("Track clipboard changes"));
    gtk_widget_set_valign (widget, GTK_ALIGN_CENTER);
}

/**
 * g_paste_ui_switch_new:
 * @topwin: the main #GtkWindow
 * @client: a #GPasteClient instance
 *
 * Create a new instance of #GPasteUiSwitch
 *
 * Returns: a newly allocated #GPasteUiSwitch
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_switch_new (GtkWindow    *topwin,
                       GPasteClient *client)
{
    g_return_val_if_fail (GTK_IS_WINDOW (topwin), NULL);
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_SWITCH, NULL);
    GPasteUiSwitchPrivate *priv = g_paste_ui_switch_get_instance_private (G_PASTE_UI_SWITCH (self));

    priv->topwin = topwin;
    priv->client = g_object_ref (client);

    priv->c_signals[C_TRACKING] = g_signal_connect (G_OBJECT (priv->client),
                                                    "tracking",
                                                    G_CALLBACK (on_tracking_changed),
                                                    self);

    gtk_switch_set_active (GTK_SWITCH (self), g_paste_client_is_active (client));

    return self;
}
