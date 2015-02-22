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

#include "gpaste-settings-ui-widget-private.h"

struct _GPasteSettingsUiWidgetPrivate
{
    GPasteSettingsUiStack *stack;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteSettingsUiWidget, g_paste_settings_ui_widget, GTK_TYPE_GRID)

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
    g_return_val_if_fail (G_PASTE_IS_SETTINGS_UI_WIDGET (self), NULL);

    GPasteSettingsUiWidgetPrivate *priv = g_paste_settings_ui_widget_get_instance_private (self);

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
    guint current_line = 0;

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
    GPasteSettingsUiWidgetPrivate *priv = g_paste_settings_ui_widget_get_instance_private (G_PASTE_SETTINGS_UI_WIDGET (self));

    if (!priv->stack)
    {
        g_object_unref (self);
        return NULL;
    }

    return self;
}
