/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-ui-about.h>
#include <gpaste-util.h>

struct _GPasteUiAbout
{
    GtkButton parent_instance;
};

typedef struct
{
    GActionGroup *action_group;
} GPasteUiAboutPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (UiAbout, ui_about, GTK_TYPE_BUTTON)

static void
g_paste_ui_about_clicked (GtkButton *button)
{
    const GPasteUiAboutPrivate *priv = _g_paste_ui_about_get_instance_private (G_PASTE_UI_ABOUT (button));

    g_action_group_activate_action (priv->action_group, "about", NULL);
}

static void
g_paste_ui_about_class_init (GPasteUiAboutClass *klass)
{
    GTK_BUTTON_CLASS (klass)->clicked = g_paste_ui_about_clicked;
}

static void
g_paste_ui_about_init (GPasteUiAbout *self)
{
    GtkWidget *widget = GTK_WIDGET (self);

    gtk_widget_set_tooltip_text (widget, _("About"));
    gtk_widget_set_valign (widget, GTK_ALIGN_CENTER);
    gtk_container_add (GTK_CONTAINER (self), gtk_image_new_from_icon_name ("dialog-information-symbolic", GTK_ICON_SIZE_BUTTON));
}

/**
 * g_paste_ui_about_new:
 * @app: The #GtkApplication
 *
 * Create a new instance of #GPasteUiAbout
 *
 * Returns: a newly allocated #GPasteUiAbout
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_about_new (GtkApplication *app)
{
    g_return_val_if_fail (GTK_IS_APPLICATION (app), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_ABOUT, NULL);
    GPasteUiAboutPrivate *priv = g_paste_ui_about_get_instance_private (G_PASTE_UI_ABOUT (self));

    priv->action_group = G_ACTION_GROUP (app);

    return self;
}
