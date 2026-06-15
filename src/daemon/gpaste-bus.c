// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste/gpaste-gdbus-defines.h>

#include <gpaste-bus.h>

#include <string.h>

struct _GPasteBus
{
    GObject parent_instance;
};

typedef struct
{
    GDBusConnection *connection;
    guint64          id_on_bus;

    GPtrArray       *objects; /* GPasteBusObject*, owned, registered on the connection */
} GPasteBusPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (Bus, bus, G_TYPE_OBJECT)

enum
{
    NAME_LOST,

    LAST_SIGNAL
};

static guint64 signals[LAST_SIGNAL] = { 0 };

static void
g_paste_bus_register_object (GPasteBus       *self,
                             GPasteBusObject *object)
{
    const GPasteBusPrivate *priv = _g_paste_bus_get_instance_private (self);
    g_autoptr (GError) error = NULL;

    if (!g_paste_bus_object_register_on_connection (object, priv->connection, &error))
        g_signal_emit (self, signals[NAME_LOST], 0, NULL);
}

static void
g_paste_bus_on_bus_acquired (GDBusConnection *connection,
                             const char      *name G_GNUC_UNUSED,
                             gpointer         user_data)
{
    GPasteBus *self = user_data;
    GPasteBusPrivate *priv = g_paste_bus_get_instance_private (self);

    priv->connection = g_object_ref (connection);

    for (guint i = 0; i < priv->objects->len; ++i)
        g_paste_bus_register_object (self, g_ptr_array_index (priv->objects, i));
}

static void
g_paste_bus_on_name_lost (GDBusConnection *connection G_GNUC_UNUSED,
                          const char      *name       G_GNUC_UNUSED,
                          gpointer         user_data)
{
    g_signal_emit (G_PASTE_BUS (user_data),
                   signals[NAME_LOST],
                   0, /* detail */
                   NULL);
}

/**
 * g_paste_bus_add_object:
 * @self: the #GPasteBus
 * @object: (transfer none): the #GPasteBusObject to expose
 *
 * Register @object on the bus (now if the name is already owned, otherwise once
 * it is acquired). The bus keeps it alive for its own lifetime.
 */
G_PASTE_VISIBLE void
g_paste_bus_add_object (GPasteBus       *self,
                        GPasteBusObject *object)
{
    g_return_if_fail (_G_PASTE_IS_BUS (self));
    g_return_if_fail (_G_PASTE_IS_BUS_OBJECT (object));

    GPasteBusPrivate *priv = g_paste_bus_get_instance_private (self);

    g_ptr_array_add (priv->objects, g_object_ref (object));

    if (priv->connection)
        g_paste_bus_register_object (self, object);
}

/**
 * g_paste_bus_own_name:
 * @self: the #GPasteBus
 *
 * Own the bus name
 */
G_PASTE_VISIBLE void
g_paste_bus_own_name (GPasteBus *self)
{
    g_return_if_fail (_G_PASTE_IS_BUS (self));

    GPasteBusPrivate *priv = g_paste_bus_get_instance_private (self);

    g_return_if_fail (!priv->id_on_bus);

    priv->id_on_bus = g_bus_own_name (G_BUS_TYPE_SESSION,
                                      G_PASTE_BUS_NAME,
                                      G_BUS_NAME_OWNER_FLAGS_NONE,
                                      g_paste_bus_on_bus_acquired,
                                      NULL, /* on_name_acquired */
                                      g_paste_bus_on_name_lost,
                                      g_object_ref (self),
                                      g_object_unref);
}

static void
g_paste_bus_dispose (GObject *object)
{
    GPasteBusPrivate *priv = g_paste_bus_get_instance_private (G_PASTE_BUS (object));

    if (priv->connection)
    {
        g_bus_unown_name (priv->id_on_bus);
        g_clear_object (&priv->connection);
    }

    g_clear_pointer (&priv->objects, g_ptr_array_unref);

    G_OBJECT_CLASS (g_paste_bus_parent_class)->dispose (object);
}

static void
g_paste_bus_class_init (GPasteBusClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_bus_dispose;

    /**
     * GPasteDaemon::name-lost:
     * @gpaste_daemon: the object on which the signal was emitted
     *
     * The "name-lost" signal is emitted when the daemon has lost
     * its name on the bus.
     */
    signals[NAME_LOST] = g_signal_new ("name-lost",
                                       G_PASTE_TYPE_BUS,
                                       G_SIGNAL_RUN_LAST,
                                       0, /* class offset */
                                       NULL, /* accumulator */
                                       NULL, /* accumulator data */
                                       g_cclosure_marshal_VOID__VOID,
                                       G_TYPE_NONE,
                                       0);
}

static void
g_paste_bus_init (GPasteBus *self)
{
    GPasteBusPrivate *priv = g_paste_bus_get_instance_private (self);

    priv->objects = g_ptr_array_new_with_free_func (g_object_unref);
}

/**
 * g_paste_bus_new:
 *
 * Create a new instance of #GPasteBus
 *
 * Returns: a newly allocated #GPasteBus
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteBus *
g_paste_bus_new (void)
{
    return G_PASTE_BUS (g_object_new (G_PASTE_TYPE_BUS, NULL));
}
