// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-gdbus-defines.h>

#include <gpaste-daemon/gpaste-bus.h>

struct _GPasteBus
{
    GObject parent_instance;

    GDBusConnection *connection;
    guint64          id_on_bus;
    gboolean         acquired; /* whether the name was ever actually acquired */

    GPtrArray       *objects; /* GPasteBusObject*, owned, registered on the connection */
};

G_PASTE_DEFINE_TYPE (Bus, bus, G_TYPE_OBJECT)

enum
{
    EXPORT_FAILED,
    NAME_ACQUIRED,
    NAME_LOST,

    LAST_SIGNAL
};

static guint64 signals[LAST_SIGNAL] = { 0 };

static void
g_paste_bus_register_object (GPasteBus       *self,
                             GPasteBusObject *object)
{
    g_autoptr (GError) error = NULL;

    /* Failing to export an object is a startup failure, never a takeover — even
     * though it is detected *after* the name was acquired (both daemons own the
     * name first and only add their objects from "name-acquired"). It gets its
     * own signal rather than riding on "name-lost": that one's @was_owned is
     * about a replacement, and a listener told the name was lost would report a
     * conflict with another daemon that never happened. */
    if (!g_paste_bus_object_register_on_connection (object, self->connection, &error))
    {
        /* The cause is ours to report (we are the ones holding the GError); what
         * to do about it is the listener's. */
        g_warning ("Failed to export an object on the bus: %s", (error) ? error->message : "unknown error");
        g_signal_emit (self, signals[EXPORT_FAILED], 0);
    }
}

static void
g_paste_bus_on_bus_acquired (GDBusConnection *connection,
                             const char      *name G_GNUC_UNUSED,
                             gpointer         user_data)
{
    GPasteBus *self = user_data;

    self->connection = g_object_ref (connection);

    for (guint i = 0; i < self->objects->len; ++i)
        g_paste_bus_register_object (self, g_ptr_array_index (self->objects, i));
}

static void
g_paste_bus_on_name_acquired (GDBusConnection *connection G_GNUC_UNUSED,
                              const char      *name       G_GNUC_UNUSED,
                              gpointer         user_data)
{
    GPasteBus *self = user_data;

    self->acquired = TRUE;

    g_signal_emit (self, signals[NAME_ACQUIRED], 0);
}

static void
g_paste_bus_on_name_lost (GDBusConnection *connection G_GNUC_UNUSED,
                          const char      *name       G_GNUC_UNUSED,
                          gpointer         user_data)
{
    GPasteBus *self = user_data;

    /* was-owned tells a listener whether the name was lost after we had it (a
     * takeover, e.g. the gnome-shell extension replacing us) or was never
     * acquired at all (another owner already held it: a startup failure). */
    g_signal_emit (self,
                   signals[NAME_LOST],
                   0, /* detail */
                   self->acquired);
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

    g_ptr_array_add (self->objects, g_object_ref (object));

    if (self->connection)
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

    g_return_if_fail (!self->id_on_bus);

    GBusNameOwnerFlags flags = G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT;

    if (replace)
        flags |= G_BUS_NAME_OWNER_FLAGS_REPLACE;

    self->id_on_bus = g_bus_own_name (G_BUS_TYPE_SESSION,
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

    if (self->id_on_bus)
    {
        g_bus_unown_name (self->id_on_bus);
        self->id_on_bus = 0;
    }

    for (guint i = 0; self->objects && i < self->objects->len; ++i)
        g_paste_bus_object_unregister_on_connection (g_ptr_array_index (self->objects, i));

    self->acquired = FALSE;
    g_clear_object (&self->connection);
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

    return self->connection && !g_dbus_connection_is_closed (self->connection);
}

static void
g_paste_bus_dispose (GObject *object)
{
    GPasteBus *self = G_PASTE_BUS (object);

    g_paste_bus_unown_name (self);
    g_clear_pointer (&self->objects, g_ptr_array_unref);

    G_OBJECT_CLASS (g_paste_bus_parent_class)->dispose (object);
}

static void
g_paste_bus_class_init (GPasteBusClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_bus_dispose;

    /**
     * GPasteBus::export-failed:
     * @gpaste_bus: the object on which the signal was emitted
     *
     * The "export-failed" signal is emitted when an object handed to
     * g_paste_bus_add_object() could not be registered on the connection (the
     * reason is warned about by the bus itself). The name may well be ours, but
     * that object's methods are unreachable, so a daemon is expected to treat
     * this as a startup failure — a distinct one from "name-lost", which is
     * about another daemon owning the name.
     */
    signals[EXPORT_FAILED] = g_signal_new ("export-failed",
                                           G_PASTE_TYPE_BUS,
                                           G_SIGNAL_RUN_LAST,
                                           0, /* class offset */
                                           NULL, /* accumulator */
                                           NULL, /* accumulator data */
                                           g_cclosure_marshal_VOID__VOID,
                                           G_TYPE_NONE,
                                           0);

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
    self->objects = g_ptr_array_new_with_free_func (g_object_unref);
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
