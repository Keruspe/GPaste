// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste/gpaste-gdbus-defines.h>

#include <gpaste-daemon/gpaste-bus.h>

struct _GPasteBus
{
    GObject parent_instance;
};

typedef struct
{
    GDBusConnection *connection;
    guint64          id_on_bus;
    gboolean         acquired; /* whether the name was ever actually acquired */

    GPtrArray       *objects; /* GPasteBusObject*, owned, registered on the connection */
} GPasteBusPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (Bus, bus, G_TYPE_OBJECT)

enum
{
    NAME_ACQUIRED,
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

    /* Failing to export an object is a startup failure, never a takeover — even
     * though it is now detected *after* the name was acquired (both daemons own
     * the name first and only add their objects from "name-acquired"), so
     * priv->acquired must not be forwarded here: a listener would then flush,
     * exit successfully and report a replacement that never happened. */
    if (!g_paste_bus_object_register_on_connection (object, priv->connection, &error))
    {
        g_warning ("Failed to export an object on the bus: %s", (error) ? error->message : "unknown error");
        g_signal_emit (self, signals[NAME_LOST], 0, FALSE);
    }
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
g_paste_bus_on_name_acquired (GDBusConnection *connection G_GNUC_UNUSED,
                              const char      *name       G_GNUC_UNUSED,
                              gpointer         user_data)
{
    GPasteBus *self = user_data;
    GPasteBusPrivate *priv = g_paste_bus_get_instance_private (self);

    priv->acquired = TRUE;

    g_signal_emit (self, signals[NAME_ACQUIRED], 0);
}

static void
g_paste_bus_on_name_lost (GDBusConnection *connection G_GNUC_UNUSED,
                          const char      *name       G_GNUC_UNUSED,
                          gpointer         user_data)
{
    GPasteBus *self = user_data;
    const GPasteBusPrivate *priv = _g_paste_bus_get_instance_private (self);

    /* was-owned tells a listener whether the name was lost after we had it (a
     * takeover, e.g. the gnome-shell extension replacing us) or was never
     * acquired at all (another owner already held it: a startup failure). */
    g_signal_emit (self,
                   signals[NAME_LOST],
                   0, /* detail */
                   priv->acquired);
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
 * g_paste_bus_own_name_full:
 * @self: the #GPasteBus
 * @replace: whether to evict any current owner of the name
 *
 * Own the bus name. The name is always owned with
 * %G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT so another GPaste daemon (typically
 * the one hosted in the gnome-shell extension, or a manually started
 * `gpaste-daemon --replace`) can take over cleanly. When @replace is %TRUE,
 * %G_BUS_NAME_OWNER_FLAGS_REPLACE is added too, so we evict whoever currently
 * holds the name (the takeover side of that same handover).
 */
G_PASTE_VISIBLE void
g_paste_bus_own_name_full (GPasteBus *self,
                           gboolean   replace)
{
    g_return_if_fail (_G_PASTE_IS_BUS (self));

    GPasteBusPrivate *priv = g_paste_bus_get_instance_private (self);

    g_return_if_fail (!priv->id_on_bus);

    GBusNameOwnerFlags flags = G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT;

    if (replace)
        flags |= G_BUS_NAME_OWNER_FLAGS_REPLACE;

    priv->id_on_bus = g_bus_own_name (G_BUS_TYPE_SESSION,
                                      G_PASTE_BUS_NAME,
                                      flags,
                                      g_paste_bus_on_bus_acquired,
                                      g_paste_bus_on_name_acquired,
                                      g_paste_bus_on_name_lost,
                                      g_object_ref (self),
                                      g_object_unref);
}

/**
 * g_paste_bus_unown_name:
 * @self: the #GPasteBus
 *
 * Release the bus name previously owned with g_paste_bus_own_name_full().
 *
 * g_bus_own_name() holds a reference on @self until the name is unowned, so this
 * has to be called for the bus to be finalized (merely dropping the caller's
 * reference is not enough). Gated on the owner id rather than the connection so
 * the name is released even when it was never acquired (e.g. another owner held
 * it). Safe to call when the name was never owned and more than once.
 *
 * The exported objects are unregistered too: their registration owns a reference
 * on them, so a host whose connection outlives the daemon (gnome-shell) would
 * otherwise keep the old objects exported forever and the next daemon's
 * registration on the same paths would fail. They stay in @self's object list, so
 * re-owning the name re-exports them.
 */
G_PASTE_VISIBLE void
g_paste_bus_unown_name (GPasteBus *self)
{
    g_return_if_fail (_G_PASTE_IS_BUS (self));

    GPasteBusPrivate *priv = g_paste_bus_get_instance_private (self);

    if (priv->id_on_bus)
    {
        g_bus_unown_name (priv->id_on_bus);
        priv->id_on_bus = 0;
    }

    for (guint i = 0; priv->objects && i < priv->objects->len; ++i)
        g_paste_bus_object_unregister_on_connection (g_ptr_array_index (priv->objects, i));

    priv->acquired = FALSE;
    g_clear_object (&priv->connection);
}

/**
 * g_paste_bus_is_connected:
 * @self: the #GPasteBus
 *
 * Whether the bus still holds a live D-Bus connection. This lets a "name-lost"
 * handler tell a deliberate takeover (another owner replaced us while the bus is
 * up, so the connection is still open) from the session bus connection simply
 * dropping (where the name is lost only because the connection went away), which
 * warrant very different responses: standing down for good versus reconnecting.
 *
 * Returns: %TRUE if a non-closed connection is held
 */
G_PASTE_VISIBLE gboolean
g_paste_bus_is_connected (GPasteBus *self)
{
    g_return_val_if_fail (_G_PASTE_IS_BUS (self), FALSE);

    const GPasteBusPrivate *priv = _g_paste_bus_get_instance_private (self);

    return priv->connection && !g_dbus_connection_is_closed (priv->connection);
}

static void
g_paste_bus_dispose (GObject *object)
{
    GPasteBus *self = G_PASTE_BUS (object);
    GPasteBusPrivate *priv = g_paste_bus_get_instance_private (self);

    g_paste_bus_unown_name (self);
    g_clear_pointer (&priv->objects, g_ptr_array_unref);

    G_OBJECT_CLASS (g_paste_bus_parent_class)->dispose (object);
}

static void
g_paste_bus_class_init (GPasteBusClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_bus_dispose;

    /**
     * GPasteBus::name-acquired:
     * @gpaste_bus: the object on which the signal was emitted
     *
     * The "name-acquired" signal is emitted when the daemon has acquired its
     * name on the bus, so name-dependent startup (e.g. loading the history) can
     * begin only once we actually own the name.
     */
    signals[NAME_ACQUIRED] = g_signal_new ("name-acquired",
                                           G_PASTE_TYPE_BUS,
                                           G_SIGNAL_RUN_LAST,
                                           0, /* class offset */
                                           NULL, /* accumulator */
                                           NULL, /* accumulator data */
                                           g_cclosure_marshal_VOID__VOID,
                                           G_TYPE_NONE,
                                           0);

    /**
     * GPasteBus::name-lost:
     * @gpaste_bus: the object on which the signal was emitted
     * @was_owned: %TRUE if the name was lost after being acquired (a takeover),
     *             %FALSE if it was never acquired (a startup failure)
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
                                       g_cclosure_marshal_VOID__BOOLEAN,
                                       G_TYPE_NONE,
                                       1, /* number of params */
                                       G_TYPE_BOOLEAN);
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
