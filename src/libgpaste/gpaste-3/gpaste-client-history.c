// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-client-history.h>

struct _GPasteClientHistory
{
    GObject parent_instance;

    gchar  *name;
    guint64 size;
};

G_PASTE_DEFINE_TYPE (ClientHistory, client_history, G_TYPE_OBJECT)

/**
 * g_paste_client_history_get_name:
 * @self: a #GPasteClientHistory instance
 *
 * Returns the name of the history
 */
G_PASTE_VISIBLE const gchar *
g_paste_client_history_get_name (GPasteClientHistory *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT_HISTORY (self), NULL);

    return self->name;
}

/**
 * g_paste_client_history_get_size:
 * @self: a #GPasteClientHistory instance
 *
 * Returns how many items the history held when it was listed
 */
G_PASTE_VISIBLE guint64
g_paste_client_history_get_size (GPasteClientHistory *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT_HISTORY (self), 0);

    return self->size;
}

static void
g_paste_client_history_finalize (GObject *object)
{
    GPasteClientHistory *self = G_PASTE_CLIENT_HISTORY (object);

    g_free (self->name);

    G_OBJECT_CLASS (g_paste_client_history_parent_class)->finalize (object);
}

static void
g_paste_client_history_class_init (GPasteClientHistoryClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = g_paste_client_history_finalize;
}

static void
g_paste_client_history_init (GPasteClientHistory *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_client_history_new:
 * @name: the name of the history
 * @size: how many items it holds
 *
 * Create a new instance of #GPasteClientHistory
 *
 * Returns: (transfer full): a newly allocated #GPasteClientHistory
 *                           free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteClientHistory *
g_paste_client_history_new (const gchar *name,
                            guint64      size)
{
    g_return_val_if_fail (g_utf8_validate (name, -1, NULL), NULL);

    GPasteClientHistory *self = g_object_new (G_PASTE_TYPE_CLIENT_HISTORY, NULL);

    self->name = g_strdup (name);
    self->size = size;

    return self;
}
