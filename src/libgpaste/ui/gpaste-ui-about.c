/*
 *      This file is part of GPaste.
 *
 *      Copyright 2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include <gpaste-ui-about.h>
#include <gpaste-util.h>

struct _GPasteUiAbout
{
    GtkButton parent_instance;
};

typedef struct
{
    GtkWindow *topwin;
} GPasteUiAboutPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiAbout, g_paste_ui_about, GTK_TYPE_BUTTON)

static void
g_paste_ui_about_clicked (GtkButton *button)
{
    GPasteUiAboutPrivate *priv = g_paste_ui_about_get_instance_private ((GPasteUiAbout *) button);

    g_paste_util_show_about_dialog (priv->topwin);
}

static void
g_paste_ui_about_class_init (GPasteUiAboutClass *klass)
{
    GTK_BUTTON_CLASS (klass)->clicked = g_paste_ui_about_clicked;
}

static void
g_paste_ui_about_init (GPasteUiAbout *self)
{
    gtk_widget_set_tooltip_text (GTK_WIDGET (self), _("About"));
}

/**
 * g_paste_ui_about_new:
 * @topwin: The toplevel #GtkWindow
 *
 * Create a new instance of #GPasteUiAbout
 *
 * Returns: a newly allocated #GPasteUiAbout
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_about_new (GtkWindow *topwin)
{
    g_return_val_if_fail (GTK_IS_WINDOW (topwin), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_ABOUT,
                                      "image", gtk_image_new_from_icon_name ("dialog-information-symbolic", GTK_ICON_SIZE_BUTTON),
                                      NULL);
    GPasteUiAboutPrivate *priv = g_paste_ui_about_get_instance_private ((GPasteUiAbout *) self);

    priv->topwin = topwin;

    return self;
}
