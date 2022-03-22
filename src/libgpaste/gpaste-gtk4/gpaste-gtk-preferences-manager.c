/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gtk4/gpaste-gtk-preferences-manager.h>

struct _GPasteGtkPreferencesManager
{
    GObject parent_instance;
};

enum
{
    C_SETTINGS,

    C_LAST_SIGNAL
};

typedef struct
{
    GPasteSettings *settings;

    GSList         *pages;

    guint64         c_signals[C_LAST_SIGNAL];
} GPasteGtkPreferencesManagerPrivate;

G_PASTE_GTK_DEFINE_TYPE_WITH_PRIVATE (PreferencesManager, preferences_manager, G_TYPE_OBJECT)

/**
 * g_paste_gtk_preferences_manager_get_settings:
 * @self: a #GPasteGtkPreferencesManager instance
 *
 * Returns the #GPasteSettings instance manager by @self
 *
 * Returns: (nullable) (transfer none): the #GPasteSettings instance
 */
G_PASTE_VISIBLE GPasteSettings *
g_paste_gtk_preferences_manager_get_settings (GPasteGtkPreferencesManager *self) {
    g_return_val_if_fail (G_PASTE_IS_GTK_PREFERENCES_MANAGER (self), NULL);

    GPasteGtkPreferencesManagerPrivate *priv = g_paste_gtk_preferences_manager_get_instance_private (self);

    return priv->settings;
}

/**
 * g_paste_gtk_preferences_manager_register:
 * @self: a #GPasteGtkPreferencesManager instance
 * @page: a #GPasteGtkPreferencesPage instance
 *
 * Register the page in the manager to notify for settings changes
 */
G_PASTE_VISIBLE void
g_paste_gtk_preferences_manager_register (GPasteGtkPreferencesManager *self,
                                          GPasteGtkPreferencesPage    *page)
{
    g_return_if_fail (G_PASTE_IS_GTK_PREFERENCES_MANAGER (self));
    g_return_if_fail (G_PASTE_IS_GTK_PREFERENCES_PAGE (page));

    GPasteGtkPreferencesManagerPrivate *priv = g_paste_gtk_preferences_manager_get_instance_private (self);

    priv->pages = g_slist_prepend (priv->pages, page);
}

/**
 * g_paste_gtk_preferences_manager_deregister:
 * @self: a #GPasteGtkPreferencesManager instance
 * @page: a #GPasteGtkPreferencesPage instance
 *
 * Deregister the page from the manager
 */
G_PASTE_VISIBLE void
g_paste_gtk_preferences_manager_deregister (GPasteGtkPreferencesManager *self,
                                            GPasteGtkPreferencesPage    *page)
{
    g_return_if_fail (G_PASTE_IS_GTK_PREFERENCES_MANAGER (self));
    g_return_if_fail (G_PASTE_IS_GTK_PREFERENCES_PAGE (page));

    GPasteGtkPreferencesManagerPrivate *priv = g_paste_gtk_preferences_manager_get_instance_private (self);

    priv->pages = g_slist_remove (priv->pages, page);
}

static void
g_paste_gtk_preferences_manager_setting_changed (GPasteSettings *settings,
                                                 const gchar    *key,
                                                 gpointer        user_data)
{
    GPasteGtkPreferencesManagerPrivate *priv = user_data;

    for (GSList *page = priv->pages; page; page = g_slist_next (page))
        g_paste_gtk_preferences_page_setting_changed (G_PASTE_GTK_PREFERENCES_PAGE (page->data), settings, key);
}

static void
g_paste_gtk_preferences_manager_dispose (GObject *object)
{
    GPasteGtkPreferencesManagerPrivate *priv = g_paste_gtk_preferences_manager_get_instance_private (G_PASTE_GTK_PREFERENCES_MANAGER (object));

    if (priv->settings) /* first dispose call */
    {
        g_signal_handler_disconnect (priv->settings, priv->c_signals[C_SETTINGS]);
        g_clear_object (&priv->settings);
    }

    G_OBJECT_CLASS (g_paste_gtk_preferences_manager_parent_class)->dispose (object);
}

static void
g_paste_gtk_preferences_manager_finalize (GObject *object)
{
    GPasteGtkPreferencesManagerPrivate *priv = g_paste_gtk_preferences_manager_get_instance_private (G_PASTE_GTK_PREFERENCES_MANAGER (object));

    g_slist_free (priv->pages);

    G_OBJECT_CLASS (g_paste_gtk_preferences_manager_parent_class)->finalize (object);
}

static void
g_paste_gtk_preferences_manager_class_init (GPasteGtkPreferencesManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_gtk_preferences_manager_dispose;
    object_class->finalize = g_paste_gtk_preferences_manager_finalize;
}

static void
g_paste_gtk_preferences_manager_init (GPasteGtkPreferencesManager *self)
{
    GPasteGtkPreferencesManagerPrivate *priv = g_paste_gtk_preferences_manager_get_instance_private (self);

    priv->settings = g_paste_settings_new ();
    priv->c_signals[C_SETTINGS] = g_signal_connect (priv->settings,
                                                    "changed",
                                                    G_CALLBACK (g_paste_gtk_preferences_manager_setting_changed),
                                                    priv);
}

/**
 * g_paste_gtk_preferences_manager_new:
 *
 * Create a new instance of #GPasteGtkPreferencesManager
 *
 * Returns: (nullable): a newly allocated #GPasteGtkPreferencesManager
 *                      free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteGtkPreferencesManager *
g_paste_gtk_preferences_manager_new (void)
{
    return G_PASTE_GTK_PREFERENCES_MANAGER (g_object_new (G_PASTE_TYPE_GTK_PREFERENCES_MANAGER, NULL));
}
