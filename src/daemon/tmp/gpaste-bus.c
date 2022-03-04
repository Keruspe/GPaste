/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste/gpaste-gdbus-defines.h>

#include <gpaste-bus.h>

#include <string.h>

struct _GPasteBus
{
    GObject parent_instance;
};

typedef struct
{
    GDBusConnection          *connection;
    guint64                   id_on_bus;

    GPasteBusAcquiredCallback on_bus_acquired;
    gpointer                  user_data;
} GPasteBusPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (Bus, bus, G_TYPE_OBJECT)

enum
{
    NAME_LOST,

    LAST_SIGNAL
};

static guint64 signals[LAST_SIGNAL] = { 0 };

static void
g_paste_bus_on_bus_acquired (GDBusConnection *connection,
                             const char      *name G_GNUC_UNUSED,
                             gpointer         user_data)
{
    GPasteBus *self = user_data;
    GPasteBusPrivate *priv = g_paste_bus_get_instance_private (self);

    priv->connection = g_object_ref (connection);

    if (priv->on_bus_acquired)
        priv->on_bus_acquired (self, priv->user_data);
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

/**
 * g_paste_bus_get_connection:
 * @self: the #GPasteBus
 *
 * returns the #GDBusConnection
 *
 * Returns: (transfer none) (nullable): the connection
 */
G_PASTE_VISIBLE GDBusConnection *
g_paste_bus_get_connection (const GPasteBus *self)
{
    g_return_val_if_fail (_G_PASTE_IS_BUS (self), NULL);

    const GPasteBusPrivate *priv = _g_paste_bus_get_instance_private (self);

    return priv->connection;
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
g_paste_bus_init (GPasteBus *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_bus_new:
 * @on_bus_acquired: (closure user_data) (scope notified) (nullable): handler to invoke when name is acquired or %NULL
 *
 * Create a new instance of #GPasteBus
 *
 * Returns: a newly allocated #GPasteBus
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteBus *
g_paste_bus_new (GPasteBusAcquiredCallback on_bus_acquired,
                 gpointer                  user_data)
{
    GPasteBus *self = G_PASTE_BUS (g_object_new (G_PASTE_TYPE_BUS, NULL));
    GPasteBusPrivate *priv = g_paste_bus_get_instance_private (self);

    priv->on_bus_acquired = on_bus_acquired;
    priv->user_data = user_data;

    return self;
}
