// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-ui-switch.h>

typedef struct
{
    GPasteClient *client;
    GtkWindow    *topwin;
} GPasteUiSwitchData;

static void
g_paste_ui_switch_data_free (gpointer  user_data,
                             GClosure *closure G_GNUC_UNUSED)
{
    g_autofree GPasteUiSwitchData *data = user_data;

    g_clear_object (&data->client);
}

static void
on_tracking_changed (GPasteClient *client G_GNUC_UNUSED,
                     gboolean      state,
                     gpointer      user_data)
{
    GtkSwitch *sw = user_data;

    gtk_switch_set_active (sw, state);
}

typedef struct
{
    GPasteClient *client;
    gboolean      track;
} SwitchTrackData;

static void
on_track_confirmed (gboolean confirmed,
                    gpointer  user_data)
{
    g_autofree SwitchTrackData *data = user_data;
    g_autoptr (GPasteClient) client = data->client;

    if (confirmed)
        g_paste_client_track (client, data->track, NULL, NULL);
}

static void
on_gesture_pressed (GtkGestureClick *gesture,
                    gint             n_press G_GNUC_UNUSED,
                    gdouble          x       G_GNUC_UNUSED,
                    gdouble          y       G_GNUC_UNUSED,
                    gpointer         user_data)
{
    GtkWidget *widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
    GPasteUiSwitchData *data = user_data;
    GtkSwitch *sw = GTK_SWITCH (widget);
    gboolean track = !gtk_switch_get_active (sw);

    gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);

    if (!track)
    {
        SwitchTrackData *track_data = g_new (SwitchTrackData, 1);
        track_data->client = g_object_ref (data->client);
        track_data->track = track;
        g_paste_gtk_util_confirm_dialog (data->topwin, _("Stop"), _("Do you really want to stop tracking clipboard changes?"), on_track_confirmed, track_data);
    }
    else
        g_paste_client_track (data->client, track, NULL, NULL);
}

/**
 * g_paste_ui_switch_new:
 * @topwin: the main #GtkWindow
 * @client: a #GPasteClient instance
 *
 * Create a new #GtkSwitch for GPaste tracking control
 *
 * Returns: a newly allocated #GtkSwitch
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_switch_new (GtkWindow    *topwin,
                       GPasteClient *client)
{
    g_return_val_if_fail (GTK_IS_WINDOW (topwin), NULL);
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);

    GtkWidget *self = gtk_switch_new ();

    gtk_widget_set_tooltip_text (self, _("Track clipboard changes"));
    gtk_widget_set_valign (self, GTK_ALIGN_CENTER);

    GPasteUiSwitchData *data = g_new0 (GPasteUiSwitchData, 1);
    data->client = g_object_ref (client);
    data->topwin = topwin;

    GtkGesture *gesture = gtk_gesture_click_new ();
    gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (gesture), GTK_PHASE_CAPTURE);
    /* The gesture is owned by the widget; tie @data's lifetime to its closure. */
    g_signal_connect_data (gesture, "pressed", G_CALLBACK (on_gesture_pressed), data, g_paste_ui_switch_data_free, 0);
    gtk_widget_add_controller (self, GTK_EVENT_CONTROLLER (gesture));

    g_signal_connect_object (client, "tracking",
                             G_CALLBACK (on_tracking_changed), self, 0);

    gtk_switch_set_active (GTK_SWITCH (self), g_paste_client_is_active (client));

    return self;
}
