/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-applet-ui.h>
#include <gpaste-util.h>

struct _GPasteAppletUi
{
    GtkMenuItem parent_instance;
};

G_PASTE_DEFINE_TYPE (AppletUi, applet_ui, GTK_TYPE_MENU_ITEM)

static void
g_paste_applet_ui_activate (GtkMenuItem *menu_item G_GNUC_UNUSED)
{
    g_paste_util_spawn ("Ui");

    GTK_MENU_ITEM_CLASS (g_paste_applet_ui_parent_class)->activate (menu_item);
}

static void
g_paste_applet_ui_class_init (GPasteAppletUiClass *klass)
{
    GTK_MENU_ITEM_CLASS (klass)->activate = g_paste_applet_ui_activate;
}

static void
g_paste_applet_ui_init (GPasteAppletUi *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_applet_ui_new:
 *
 * Create a new instance of #GPasteAppletUi
 *
 * Returns: a newly allocated #GPasteAppletUi
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_applet_ui_new (void)
{
    return gtk_widget_new (G_PASTE_TYPE_APPLET_UI,
                           "label", _("launch the graphical tool"),
                           NULL);
}
