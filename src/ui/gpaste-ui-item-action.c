// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-ui-item-action.h>

typedef struct
{
    GPasteClient          *client;

    gchar                 *uuid;

    GPasteUiItemActionFunc activate_func; /* set for simple, subclass-less actions */
} GPasteUiItemActionPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (UiItemAction, ui_item_action, GTK_TYPE_BUTTON)

/**
 * g_paste_ui_item_action_set_uuid:
 * @self: a #GPasteUiItemAction instance
 * @uuid: the uuid of the corresponding item
 *
 * Track a new uuid
 */
G_PASTE_VISIBLE void
g_paste_ui_item_action_set_uuid (GPasteUiItemAction *self,
                                 const gchar        *uuid)
{
    g_return_if_fail (_G_PASTE_IS_UI_ITEM_ACTION (self));

    GPasteUiItemActionPrivate *priv = g_paste_ui_item_action_get_instance_private (self);

    g_set_str (&priv->uuid, uuid);
}

static void
g_paste_ui_item_action_real_activate (GPasteUiItemAction *self,
                                      GPasteClient       *client,
                                      const gchar        *uuid)
{
    const GPasteUiItemActionPrivate *priv = _g_paste_ui_item_action_get_instance_private (self);

    /* Default for subclass-less actions created via _new_simple. */
    if (priv->activate_func)
        priv->activate_func (client, uuid);
}

static void
g_paste_ui_item_action_clicked (GtkButton *button)
{
    GPasteUiItemAction *self = G_PASTE_UI_ITEM_ACTION (button);
    const GPasteUiItemActionPrivate *priv = _g_paste_ui_item_action_get_instance_private (self);
    GPasteUiItemActionClass *klass = G_PASTE_UI_ITEM_ACTION_GET_CLASS (self);

    if (klass->activate)
        klass->activate (self, priv->client, priv->uuid);
}

static void
g_paste_ui_item_action_dispose (GObject *object)
{
    GPasteUiItemActionPrivate *priv = g_paste_ui_item_action_get_instance_private (G_PASTE_UI_ITEM_ACTION (object));

    g_clear_object (&priv->client);
    g_clear_pointer (&priv->uuid, g_free);

    G_OBJECT_CLASS (g_paste_ui_item_action_parent_class)->dispose (object);
}

static void
g_paste_ui_item_action_class_init (GPasteUiItemActionClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_item_action_dispose;
    GTK_BUTTON_CLASS (klass)->clicked = g_paste_ui_item_action_clicked;
    klass->activate = g_paste_ui_item_action_real_activate;
}

static void
g_paste_ui_item_action_init (GPasteUiItemAction *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_ui_item_action_new:
 * @type: the type of the subclass to instantiate
 * @client: a #GPasteClient
 * @icon_name: the name of the icon to use
 * @tooltip: the tooltip to display
 *
 * Create a new instance of #GPasteUiItemAction
 *
 * Returns: a newly allocated #GPasteUiItemAction
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_item_action_new (GType         type,
                            GPasteClient *client,
                            const gchar  *icon_name,
                            const gchar  *tooltip)
{
    g_return_val_if_fail (g_type_is_a (type, G_PASTE_TYPE_UI_ITEM_ACTION), NULL);
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);

    GtkWidget *self = g_object_new (type, NULL);
    GPasteUiItemActionPrivate *priv = g_paste_ui_item_action_get_instance_private (G_PASTE_UI_ITEM_ACTION (self));
    GtkWidget *icon = gtk_image_new_from_icon_name (icon_name);

    priv->client = g_object_ref (client);

    gtk_widget_set_tooltip_text (self, tooltip);
    gtk_widget_set_margin_start (icon, 5);
    gtk_widget_set_margin_end (icon, 5);

    gtk_button_set_child (GTK_BUTTON (self), icon);

    return self;
}

/**
 * g_paste_ui_item_action_new_simple:
 * @client: a #GPasteClient
 * @icon_name: the name of the icon to use
 * @tooltip: the tooltip to display
 * @activate: (scope notified): the action to run on the tracked item when clicked
 *
 * Create a #GPasteUiItemAction that runs @activate when clicked, without
 * needing a dedicated subclass.
 *
 * Returns: a newly allocated #GPasteUiItemAction
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_item_action_new_simple (GPasteClient          *client,
                                   const gchar           *icon_name,
                                   const gchar           *tooltip,
                                   GPasteUiItemActionFunc activate)
{
    g_return_val_if_fail (activate, NULL);

    GtkWidget *self = g_paste_ui_item_action_new (G_PASTE_TYPE_UI_ITEM_ACTION, client, icon_name, tooltip);
    GPasteUiItemActionPrivate *priv = g_paste_ui_item_action_get_instance_private (G_PASTE_UI_ITEM_ACTION (self));

    priv->activate_func = activate;

    return self;
}
