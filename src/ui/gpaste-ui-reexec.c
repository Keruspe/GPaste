/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-ui-reexec.h>
#include <gpaste-util.h>

struct _GPasteUiReexec
{
    GtkButton parent_instance;
};

typedef struct
{
    GPasteClient *client;

    GtkWindow    *topwin;
} GPasteUiReexecPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (UiReexec, ui_reexec, GTK_TYPE_BUTTON)

static void
g_paste_ui_reexec_clicked (GtkButton *button)
{
    const GPasteUiReexecPrivate *priv = _g_paste_ui_reexec_get_instance_private (G_PASTE_UI_REEXEC (button));

    if (g_paste_util_confirm_dialog (priv->topwin, _("Restart"), _("Do you really want to restart the daemon?")))
        g_paste_client_reexecute (priv->client, NULL, NULL);
}

static void
g_paste_ui_reexec_dispose (GObject *object)
{
    GPasteUiReexecPrivate *priv = g_paste_ui_reexec_get_instance_private (G_PASTE_UI_REEXEC (object));

    g_clear_object (&priv->client);

    G_OBJECT_CLASS (g_paste_ui_reexec_parent_class)->dispose (object);
}

static void
g_paste_ui_reexec_class_init (GPasteUiReexecClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_reexec_dispose;
    GTK_BUTTON_CLASS (klass)->clicked = g_paste_ui_reexec_clicked;
}

static void
g_paste_ui_reexec_init (GPasteUiReexec *self)
{
    GtkWidget *widget = GTK_WIDGET (self);

    gtk_widget_set_tooltip_text (widget, _("Restart the daemon"));
    gtk_widget_set_valign (widget, GTK_ALIGN_CENTER);
    gtk_container_add (GTK_CONTAINER (self), gtk_image_new_from_icon_name ("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON));
}

/**
 * g_paste_ui_reexec_new:
 * @topwin: the main #GtkWindow
 * @client: a #GPasteClient instance
 *
 * Create a new instance of #GPasteUiReexec
 *
 * Returns: a newly allocated #GPasteUiReexec
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_reexec_new (GtkWindow    *topwin,
                       GPasteClient *client)
{
    g_return_val_if_fail (GTK_IS_WINDOW (topwin), NULL);
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_REEXEC,NULL);
    GPasteUiReexecPrivate *priv = g_paste_ui_reexec_get_instance_private (G_PASTE_UI_REEXEC (self));

    priv->topwin = topwin;
    priv->client = g_object_ref (client);

    return self;
}
