/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-ui-history-action.h>
#include <gpaste-ui-history-actions.h>

typedef struct
{
    GPasteClient           *client;
    GPasteSettings         *settings;
    GPasteUiHistoryActions *actions;

    GtkWindow              *rootwin;

    gchar                  *history;
} GPasteUiHistoryActionPrivate;

G_PASTE_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (UiHistoryAction, ui_history_action, GTK_TYPE_BUTTON)

/**
 * g_paste_ui_history_action_set_history:
 * @self: a #GPasteUiHistoryAction instance
 * @history: the history to delete
 *
 * Set the history to delete
 */
G_PASTE_VISIBLE void
g_paste_ui_history_action_set_history (GPasteUiHistoryAction *self,
                                       const gchar           *history)
{
    g_return_if_fail (_G_PASTE_IS_UI_HISTORY_ACTION (self));

    GPasteUiHistoryActionPrivate *priv = g_paste_ui_history_action_get_instance_private (G_PASTE_UI_HISTORY_ACTION (self));

    g_free (priv->history);
    priv->history = g_strdup (history);
}

static gboolean
g_paste_ui_history_action_button_press_event (GtkWidget      *widget,
                                              GdkEventButton *event G_GNUC_UNUSED)
{
    GPasteUiHistoryAction *self = G_PASTE_UI_HISTORY_ACTION (widget);
    GPasteUiHistoryActionClass *klass = G_PASTE_UI_HISTORY_ACTION_GET_CLASS (self);
    const GPasteUiHistoryActionPrivate *priv = _g_paste_ui_history_action_get_instance_private (self);
    gboolean ret;

    if (priv->history && klass->activate)
        ret = klass->activate (self, priv->client, priv->settings, priv->rootwin, priv->history);
    else
        ret = GTK_WIDGET_CLASS (g_paste_ui_history_action_parent_class)->button_press_event (widget, event);

    g_paste_ui_history_actions_set_relative_to (priv->actions, NULL);

    return ret;
}

static void
g_paste_ui_history_action_dispose (GObject *object)
{
    GPasteUiHistoryActionPrivate *priv = g_paste_ui_history_action_get_instance_private (G_PASTE_UI_HISTORY_ACTION (object));

    g_clear_object (&priv->client);
    g_clear_object (&priv->settings);

    G_OBJECT_CLASS (g_paste_ui_history_action_parent_class)->dispose (object);
}

static void
g_paste_ui_history_action_finalize (GObject *object)
{
    const GPasteUiHistoryActionPrivate *priv = _g_paste_ui_history_action_get_instance_private (G_PASTE_UI_HISTORY_ACTION (object));

    g_free (priv->history);

    G_OBJECT_CLASS (g_paste_ui_history_action_parent_class)->finalize (object);
}

static void
g_paste_ui_history_action_class_init (GPasteUiHistoryActionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_ui_history_action_dispose;
    object_class->finalize = g_paste_ui_history_action_finalize;

    GTK_WIDGET_CLASS (klass)->button_press_event = g_paste_ui_history_action_button_press_event;
}

static void
g_paste_ui_history_action_init (GPasteUiHistoryAction *self)
{
    GtkWidget *button = GTK_WIDGET (self);

    gtk_widget_set_margin_start (button, 5);
    gtk_widget_set_margin_end (button, 5);
}

/**
 * g_paste_ui_history_action_new:
 * @type: the type of the subclass to instantiate
 * @client: a #GPasteClient
 * @settings: a #GPasteSettings
 * @actions: a #GPasteUiHistoryActions
 * @rootwin: the main #GtkWindow
 *
 * Create a new instance of #GPasteUiHistoryAction
 *
 * Returns: a newly allocated #GPasteUiHistoryAction
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_history_action_new (GType           type,
                               GPasteClient   *client,
                               GPasteSettings *settings,
                               GtkWidget      *actions,
                               GtkWindow      *rootwin,
                               const gchar    *label)
{
    g_return_val_if_fail (g_type_is_a (type, G_PASTE_TYPE_UI_HISTORY_ACTION), NULL);
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (_G_PASTE_IS_UI_HISTORY_ACTIONS (actions), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    GtkWidget *self = gtk_widget_new (type,
                                      "width-request",  200,
                                      "height-request", 30,
                                      NULL);
    GPasteUiHistoryActionPrivate *priv = g_paste_ui_history_action_get_instance_private (G_PASTE_UI_HISTORY_ACTION (self));

    priv->client = g_object_ref (client);
    priv->settings = g_object_ref (settings);
    priv->actions = G_PASTE_UI_HISTORY_ACTIONS (actions);
    priv->rootwin = rootwin;

    gtk_button_set_label (GTK_BUTTON (self), label);

    return self;
}
