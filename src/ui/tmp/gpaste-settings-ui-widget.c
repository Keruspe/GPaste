/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-settings-ui-widget.h>

struct _GPasteSettingsUiWidget
{
    GtkGrid parent_instance;
};

typedef struct
{
    GPasteSettingsUiStack *stack;
} GPasteSettingsUiWidgetPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (SettingsUiWidget, settings_ui_widget, GTK_TYPE_GRID)

/**
 * g_paste_settings_ui_widget_get_stack:
 * @self: a #GPasteSettingsUiWidget instance
 *
 * Get the inner GtkStack from the Widget
 *
 * Returns: (transfer none): the #GtkStack
 */
G_PASTE_VISIBLE GPasteSettingsUiStack *
g_paste_settings_ui_widget_get_stack (GPasteSettingsUiWidget *self)
{
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS_UI_WIDGET (self), NULL);

    const GPasteSettingsUiWidgetPrivate *priv = _g_paste_settings_ui_widget_get_instance_private (self);

    return priv->stack;
}

static void
g_paste_settings_ui_widget_class_init (GPasteSettingsUiWidgetClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_settings_ui_widget_init (GPasteSettingsUiWidget *self)
{
    GPasteSettingsUiWidgetPrivate *priv = g_paste_settings_ui_widget_get_instance_private (self);
    GtkGrid *grid = GTK_GRID (self);
    guint64 current_line = 0;

    GPasteSettingsUiStack *stack = priv->stack = g_paste_settings_ui_stack_new ();

    if (!stack)
        return;

    g_paste_settings_ui_stack_fill (stack);

    gtk_grid_attach (grid, gtk_widget_new (GTK_TYPE_STACK_SWITCHER,
                                           "stack",  GTK_STACK (stack),
                                           "halign", GTK_ALIGN_CENTER,
                                           NULL), 0, current_line++, 1, 1);
    gtk_grid_attach (grid, GTK_WIDGET (stack), 0, current_line++, 1, 1);
}

/**
 * g_paste_settings_ui_widget_new:
 *
 * Create a new instance of #GPasteSettingsUiWidget
 *
 * Returns: (nullable): a newly allocated #GPasteSettingsUiWidget
 *                      free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_settings_ui_widget_new (void)
{
    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_SETTINGS_UI_WIDGET, NULL);
    const GPasteSettingsUiWidgetPrivate *priv = _g_paste_settings_ui_widget_get_instance_private (G_PASTE_SETTINGS_UI_WIDGET (self));

    if (!priv->stack)
    {
        g_object_unref (self);
        return NULL;
    }

    return self;
}
