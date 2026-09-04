// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste/gpaste-gdbus-macros.h>
#include <gpaste/gpaste-keybinding-provider.h>
#include <gpaste-gtk4/gpaste-gtk-global-shortcut-client.h>

#include <gtk/gtk.h>

#define G_PASTE_GTK_GLOBAL_SHORTCUT_OBJECT_PATH    "/org/freedesktop/portal/desktop"
#define G_PASTE_GTK_GLOBAL_SHORTCUT_INTERFACE_NAME "org.freedesktop.portal.GlobalShortcuts"

#define G_PASTE_GTK_GLOBAL_SHORTCUT_CREATE_SESSION "CreateSession"
#define G_PASTE_GTK_GLOBAL_SHORTCUT_BIND_SHORTCUTS "BindShortcuts"

#define G_PASTE_GTK_PORTAL_REQUEST_INTERFACE_NAME "org.freedesktop.portal.Request"
#define G_PASTE_GTK_PORTAL_SESSION_INTERFACE_NAME "org.freedesktop.portal.Session"

#define G_PASTE_GTK_PORTAL_REQUEST_OBJECT_PATH G_PASTE_GTK_GLOBAL_SHORTCUT_OBJECT_PATH "/request"

#define G_PASTE_GTK_PORTAL_SIG_RESPONSE "Response"
#define G_PASTE_GTK_PORTAL_CLOSE        "Close"

#define G_PASTE_GTK_GLOBAL_SHORTCUT_SIG_ACTIVATED "Activated"

/* How long a retired CreateSession is waited on before it is given up for lost.
 * A portal that takes the call, replies to it and then never says another word
 * would otherwise keep the request -- and the reference it holds on the client
 * -- for the rest of the process. Long enough that one merely slow to get an
 * answer out of the user is not given up on. */
#define G_PASTE_GTK_PORTAL_RETIRED_REQUEST_TIMEOUT 600 /* s */

#define G_PASTE_GTK_GLOBAL_SHORTCUT_INTERFACE                                                            \
    "<node>"                                                                                             \
        "<interface name='" G_PASTE_GTK_GLOBAL_SHORTCUT_INTERFACE_NAME "'>"                              \
            "<method name='" G_PASTE_GTK_GLOBAL_SHORTCUT_CREATE_SESSION "'>"                             \
                "<arg type='a{sv}'   direction='in'  name='options' />"                                  \
                "<arg type='o'       direction='out' name='handle'  />"                                  \
            "</method>"                                                                                  \
            "<method name='" G_PASTE_GTK_GLOBAL_SHORTCUT_BIND_SHORTCUTS "'>"                             \
                "<arg type='o'         direction='in'  name='session_handle' />"                         \
                "<arg type='a(sa{sv})' direction='in'  name='shortcuts'      />"                         \
                "<arg type='s'         direction='in'  name='parent_window'  />"                         \
                "<arg type='a{sv}'     direction='in'  name='options'        />"                         \
                "<arg type='o'         direction='out' name='handle'         />"                         \
            "</method>"                                                                                  \
            "<signal name='" G_PASTE_GTK_GLOBAL_SHORTCUT_SIG_ACTIVATED "'>"                              \
                "<arg type='o'     name='session_handle' />"                                             \
                "<arg type='s'     name='shortcut_id'   />"                                              \
                "<arg type='t'     name='timestamp'     />"                                              \
                "<arg type='a{sv}' name='options'       />"                                              \
            "</signal>"                                                                                  \
        "</interface>"                                                                                   \
    "</node>"

typedef struct _PortalRequestData _PortalRequestData;

typedef struct
{
    gchar                       *session_handle;
    gchar                       *session_owner; /* the portal that answered with it, to tell a replacement apart */
    GPasteKeybindingAccelerator *shortcuts;     /* the set we hold, owned, %NULL once disposed */
    guint64                      generation;    /* bumped whenever the shortcuts we bound stop being the ones we want */
    guint64                      request_count; /* what the handle_token of the next request is built from */
    guint64                      session_count; /* what the session_handle_token of the next session is built from */
    GSList                      *requests;      /* the requests in flight, _PortalRequestData*, not owned */
} GPasteGtkGlobalShortcutClientPrivate;

struct _GPasteGtkGlobalShortcutClient
{
    GDBusProxy parent_instance;
};

static void global_shortcut_client_provider_init (GPasteKeybindingProviderInterface *iface);

G_PASTE_DEFINE_TYPE_WITH_PRIVATE_AND_INTERFACE (GtkGlobalShortcutClient, gtk_global_shortcut_client, G_TYPE_DBUS_PROXY,
    G_PASTE_TYPE_KEYBINDING_PROVIDER, global_shortcut_client_provider_init)

/**********************/
/* Shortcut variant   */
/**********************/

static gchar *
gtk_accel_to_portal_trigger (const gchar *accel)
{
    if (!accel || !*accel)
        return NULL;

    guint keyval = 0;
    GdkModifierType mods = 0;
    if (!gtk_accelerator_parse (accel, &keyval, &mods) || !keyval)
        return NULL;

    const gchar *key_name = gdk_keyval_name (keyval);
    if (!key_name)
        return NULL;

    g_autoptr (GStrvBuilder) tokens = g_strv_builder_new ();

    if (mods & GDK_CONTROL_MASK)
        g_strv_builder_add (tokens, "CTRL");
    if (mods & GDK_ALT_MASK)
        g_strv_builder_add (tokens, "ALT");
    if (mods & GDK_SHIFT_MASK)
        g_strv_builder_add (tokens, "SHIFT");
    if (mods & GDK_SUPER_MASK)
        g_strv_builder_add (tokens, "SUPER");

    g_strv_builder_take (tokens, g_ascii_strup (key_name, -1));

    g_auto (GStrv) parts = g_strv_builder_end (tokens);
    return g_strjoinv ("+", parts);
}

static GVariant *
build_shortcuts_variant (GPasteGtkGlobalShortcutClientPrivate *priv)
{
    g_auto (GVariantBuilder) builder;
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(sa{sv})"));

    for (const GPasteKeybindingAccelerator *s = priv->shortcuts; s->id; ++s)
    {
        g_variant_builder_open (&builder, G_VARIANT_TYPE ("(sa{sv})"));
        g_variant_builder_add (&builder, "s", s->id);
        g_variant_builder_open (&builder, G_VARIANT_TYPE_VARDICT);
        if (s->accelerator)
        {
            g_autofree gchar *portal_trigger = gtk_accel_to_portal_trigger (s->accelerator);
            if (portal_trigger)
                g_variant_builder_add (&builder, "{sv}", "preferred_trigger",
                                       g_variant_new_string (portal_trigger));
        }
        if (s->description)
            g_variant_builder_add (&builder, "{sv}", "description",
                                   g_variant_new_string (s->description));
        g_variant_builder_close (&builder);
        g_variant_builder_close (&builder);
    }

    return g_variant_builder_end (&builder);
}

/**********************/
/* Async session/bind */
/**********************/

struct _PortalRequestData
{
    GWeakRef                       client;            /* weakly: see portal_request_data_new () */
    GTask                         *task;
    GDBusConnection               *connection;
    GCancellable                  *cancellable;
    gchar                         *request_path;      /* the Request we are subscribed to */
    gchar                         *owner;             /* the portal this request went to, %NULL while nobody owns the name */
    guint                          signal_id;
    guint64                        generation;        /* the generation this request was made for */
    gboolean                       awaiting_response; /* the method replied: only Response is left */
    gboolean                       responded;         /* Response came first: only the method reply is left */
    gboolean                       creates_session;   /* its Response is what a session handle arrives on */
    gboolean                       retired;           /* kept only for the session it may still be answered with */
    gboolean                       returned;          /* its task has been answered, and may not be answered twice */
    guint                          retire_source;     /* gives up on a retired request the portal never answers */
};

static _PortalRequestData *
portal_request_data_new (GPasteGtkGlobalShortcutClient *client,
                         GTask                         *task)
{
    GPasteGtkGlobalShortcutClientPrivate *priv = g_paste_gtk_global_shortcut_client_get_instance_private (client);
    _PortalRequestData *data = g_new (_PortalRequestData, 1);

    /* Weakly, and for the same reason a GSource holds the object it runs for
     * weakly: dispose () is the one thing that retires a request, and it only
     * runs once the last reference is dropped. A reference here would be one
     * the client holds on itself through the list below -- a portal slow to
     * answer, or one that never answers at all, would keep the client alive for
     * as long as it took, session and global grabs and all. */
    g_weak_ref_init (&data->client, client);
    data->task = g_object_ref (task);
    data->connection = g_object_ref (g_dbus_proxy_get_connection (G_DBUS_PROXY (client)));
    data->cancellable = g_cancellable_new ();
    data->request_path = NULL;
    /* Which portal is being asked, so that one replacing it can be told from it:
     * a request outlives neither, and a CreateSession that has yet to be
     * answered has no session handle to say the loss with. %NULL while nobody
     * owns the name -- this very call is what D-Bus-activates the portal -- and
     * filled in by the owner that turns up. */
    data->owner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (client));
    data->signal_id = 0;
    data->generation = priv->generation;
    data->awaiting_response = FALSE;
    data->responded = FALSE;
    data->creates_session = FALSE;
    data->retired = FALSE;
    data->returned = FALSE;
    data->retire_source = 0;

    /* Tracked so that a session closed under it, or a portal that goes away,
     * can retire it: nothing else would ever free a request the portal has
     * stopped answering. */
    priv->requests = g_slist_prepend (priv->requests, data);

    return data;
}

static void
portal_request_unsubscribe (_PortalRequestData *data)
{
    if (!data->signal_id)
        return;

    g_dbus_connection_signal_unsubscribe (data->connection, data->signal_id);
    data->signal_id = 0;
}

static void
portal_request_data_free (_PortalRequestData *data)
{
    g_autoptr (GPasteGtkGlobalShortcutClient) client = g_weak_ref_get (&data->client);

    /* Nothing to unlink from once the client is gone -- or once it is being
     * disposed of, GObject clearing weak locations before dispose () runs:
     * dispose () drops the list itself, and a request that outlives it frees
     * itself here without ever reading it. */
    if (client)
    {
        GPasteGtkGlobalShortcutClientPrivate *priv = g_paste_gtk_global_shortcut_client_get_instance_private (client);

        priv->requests = g_slist_remove (priv->requests, data);
    }

    g_clear_handle_id (&data->retire_source, g_source_remove);

    portal_request_unsubscribe (data);

    g_weak_ref_clear (&data->client);
    g_clear_object (&data->task);
    g_clear_object (&data->connection);
    g_clear_object (&data->cancellable);
    g_clear_pointer (&data->request_path, g_free);
    g_clear_pointer (&data->owner, g_free);
    g_free (data);
}

/* The portal builds the Request object path from our unique name and the
 * handle_token we hand it, and it is free to emit Response before the method
 * reply is dispatched -- so the path has to be predicted, and subscribed to,
 * before the call goes out. */
static gchar *
portal_request_path (GDBusConnection *connection,
                     const gchar     *token)
{
    const gchar *unique_name = g_dbus_connection_get_unique_name (connection);

    /* A connection that is not on a message bus, or has yet to be given a name,
     * has no path for us to build: %NULL says so rather than walking off the
     * front of one that is not there. */
    if (!unique_name)
        return NULL;

    /* The sender part of the path is the unique name without its leading colon
     * and with every dot turned into an underscore. */
    g_autofree gchar *sender = g_strdelimit (g_strdup (unique_name + 1), ".", '_');

    return g_strdup_printf (G_PASTE_GTK_PORTAL_REQUEST_OBJECT_PATH "/%s/%s", sender, token);
}

/* Both tokens are ours to pick and the portal builds an object path out of each,
 * so they are shaped the same way -- one namespace per counter, and never a
 * literal spelled out twice.
 * The counter is what keeps a token from ever being reused; the random half is
 * what keeps the object path it becomes from being guessed, as the portal
 * documentation asks -- a Response is only as private as the path it arrives
 * on is hard to name. */
static gchar *
portal_token (guint64 count)
{
    return g_strdup_printf ("gpaste%08x%" G_GUINT64_FORMAT, g_random_int (), count);
}

/* One per request, and never reused: a path we subscribe to must not be one an
 * older request of ours could still be answered on. */
static gchar *
portal_next_request_token (GPasteGtkGlobalShortcutClientPrivate *priv)
{
    return portal_token (++priv->request_count);
}

/* A token of its own for the session: the portal builds the session's object
 * path from that one, and the session we are replacing may not be gone yet. */
static gchar *
portal_next_session_token (GPasteGtkGlobalShortcutClientPrivate *priv)
{
    return portal_token (++priv->session_count);
}

static void
portal_request_subscribe (_PortalRequestData *data,
                          const gchar        *request_path,
                          GDBusSignalCallback callback)
{
    g_set_str (&data->request_path, request_path);

    /* The portal and nobody else: with no sender to match, any peer on the bus
     * could answer the request -- with a session handle of its own, which is
     * what BindShortcuts would then be told to bind and what every Activated is
     * matched against. */
    data->signal_id = g_dbus_connection_signal_subscribe (
        data->connection, G_PASTE_GTK_GLOBAL_SHORTCUT_BUS_NAME,
        G_PASTE_GTK_PORTAL_REQUEST_INTERFACE_NAME, G_PASTE_GTK_PORTAL_SIG_RESPONSE,
        request_path, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
        callback, data, NULL);
}

/* The method reply names the request the portal actually created: the path we
 * predicted whenever handle_token was honoured, and somewhere else entirely
 * with a portal that ignored it -- Response only ever comes on the portal's
 * own choice, so the subscription follows it. */
static void
portal_request_confirm_path (_PortalRequestData *data,
                             const gchar        *request_path,
                             GDBusSignalCallback callback)
{
    data->awaiting_response = TRUE;

    if (g_paste_str_equal (data->request_path, request_path))
        return;

    portal_request_unsubscribe (data);
    portal_request_subscribe (data, request_path, callback);
}

/* The portal is free to answer the request before it replies to the call that
 * made it, and that call's callback still holds @data when it does: freeing it
 * from the Response handler would leave that callback on freed memory -- and,
 * once the block is handed to the next request, on somebody else's. Whichever
 * of the two comes last is the one that frees. */
static void
portal_request_done (_PortalRequestData *data)
{
    if (data->awaiting_response)
    {
        portal_request_data_free (data);
        return;
    }

    /* Nothing more can arrive on the request, and the method reply has nothing
     * left to say: drop the subscription and leave the free to its callback. */
    data->responded = TRUE;
    portal_request_unsubscribe (data);
}

/* Once and once only: a CreateSession whose method call failed has reported
 * that failure already, and it is kept alive for the session its Response may
 * yet carry -- there is nobody left to answer when that arrives. Every task
 * return goes through these two, so that holds for paths added later. */
static void
portal_request_take_error (_PortalRequestData *data,
                           GError             *error) /* (transfer full) */
{
    if (data->returned)
    {
        g_error_free (error);
        return;
    }

    data->returned = TRUE;
    g_task_return_error (data->task, error);
}

static void
portal_request_return_boolean (_PortalRequestData *data,
                               gboolean            value)
{
    if (data->returned)
        return;

    data->returned = TRUE;
    g_task_return_boolean (data->task, value);
}

/* Closing the session is the only way to be rid of what it bound: the portal
 * binds the shortcuts of a session once and for all -- "an application can only
 * attempt to bind shortcuts of a session once" -- so a session is spent as soon
 * as its shortcuts are not the ones we want any more, and so is the permission
 * it carries. Everything that changes the set closes the session first. */
/* A close that failed leaves the session holding its shortcuts, and the next
 * session binds its own alongside them: nothing else would ever notice. */
static void
on_session_closed (GObject      *source,
                   GAsyncResult *result,
                   gpointer      user_data G_GNUC_UNUSED)
{
    g_autoptr (GError) error = NULL;
    g_autoptr (GVariant) ret = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);

    if (!ret)
        g_warning ("GPasteGtkGlobalShortcutClient: closing the portal session failed: %s", error->message);
}

static void
close_session_handle (GDBusConnection *connection,
                      const gchar     *handle)
{
    g_dbus_connection_call (connection,
                            G_PASTE_GTK_GLOBAL_SHORTCUT_BUS_NAME,
                            handle,
                            G_PASTE_GTK_PORTAL_SESSION_INTERFACE_NAME,
                            G_PASTE_GTK_PORTAL_CLOSE,
                            NULL, /* parameters */
                            NULL, /* reply type */
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,                /* timeout */
                            NULL,              /* cancellable */
                            on_session_closed, /* callback */
                            NULL);             /* user_data */
}

static void
close_session (GPasteGtkGlobalShortcutClient *self)
{
    GPasteGtkGlobalShortcutClientPrivate *priv = g_paste_gtk_global_shortcut_client_get_instance_private (self);

    if (!priv->session_handle)
        return;

    g_autofree gchar *handle = g_steal_pointer (&priv->session_handle);

    g_clear_pointer (&priv->session_owner, g_free);

    close_session_handle (g_dbus_proxy_get_connection (G_DBUS_PROXY (self)), handle);
}

static gboolean
on_retired_request_timed_out (gpointer user_data)
{
    _PortalRequestData *data = user_data;

    data->retire_source = 0;

    /* The Response came after all, and took the task with it: what is left is a
     * method reply, which has a timeout of its own to arrive on. */
    if (data->responded)
        return G_SOURCE_REMOVE;

    portal_request_take_error (data, g_error_new (G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
                                                  "The portal never answered the request its session was to be closed by"));
    portal_request_done (data);

    return G_SOURCE_REMOVE;
}

/* Nothing is going to answer a request whose session has just been closed under
 * it, and its Response would be about shortcuts nobody wants any more: the
 * subscription and the references it holds would otherwise sit there for the
 * rest of the process, and the client they keep alive with them. */
static void
retire_one_request (_PortalRequestData *data,
                    gboolean            portal_is_gone)
{
    /* An answered CreateSession has nothing left to be waited on for: its
     * Response has been and gone, session and all, and only the method reply is
     * still to come -- which the cancel below brings forward. */
    if (data->creates_session && !portal_is_gone && !data->responded)
    {
        /* Not ours to drop, and not ours to cancel either: cancelling abandons
         * the wait, never the call the portal has already taken, and the handle
         * of the session it may have created arrives on this Response alone.
         * Letting the request run to it is what closes that session -- its
         * handler sees the generation has moved on and does nothing else.
         * Waiting for it is not waiting for it forever, though: a Response that
         * never comes is the one thing that would keep this request, and the
         * client it holds, around for good. */
        data->retired = TRUE;

        if (!data->retire_source)
        {
            data->retire_source = g_timeout_add_seconds (G_PASTE_GTK_PORTAL_RETIRED_REQUEST_TIMEOUT,
                                                         on_retired_request_timed_out, data);
            g_source_set_name_by_id (data->retire_source, "[GPaste] retired portal request");
        }

        return;
    }

    if (!data->awaiting_response)
    {
        /* The method call has yet to reply, and its callback is the only thing
         * that may still touch @data: cancelling the call is what brings that
         * callback -- and the free it does -- forward. */
        g_cancellable_cancel (data->cancellable);
        return;
    }

    portal_request_take_error (data, g_error_new (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                                  "The portal request was superseded before it completed"));
    portal_request_data_free (data);
}

static void
retire_request_list (const GSList *requests,
                     gboolean      portal_is_gone)
{
    for (const GSList *r = requests; r; r = r->next)
        retire_one_request (r->data, portal_is_gone);
}

static void
retire_requests (GPasteGtkGlobalShortcutClient *self,
                 gboolean                       portal_is_gone)
{
    GPasteGtkGlobalShortcutClientPrivate *priv = g_paste_gtk_global_shortcut_client_get_instance_private (self);
    /* Retiring frees, and freeing unlinks: walk a copy of the list. */
    g_autoptr (GSList) requests = g_slist_copy (priv->requests);

    retire_request_list (requests, portal_is_gone);
}

/* What a portal that is gone was holding for us: the session it created, and
 * every request it has yet to answer. One handing the bus name straight to its
 * replacement never lets go of it, so there is no %NULL owner in between to say
 * so, and a CreateSession still awaiting its Response has no session handle to
 * say it with -- only the owner it was issued to. Left alone, that request waits
 * on a Response nobody will ever send, holding its subscription and a reference
 * on the client, while the recovery guard reads it as a bind on its way. */
static gboolean
portal_was_replaced (const GPasteGtkGlobalShortcutClientPrivate *priv,
                     const gchar                                *owner)
{
    if (priv->session_owner && !g_paste_str_equal (priv->session_owner, owner))
        return TRUE;

    for (const GSList *r = priv->requests; r; r = r->next)
    {
        const _PortalRequestData *data = r->data;

        /* A cancelled request says nothing: the portal has nothing left to
         * answer it with, and it lingers only until the callback that frees it
         * runs. One of those is what a handoff already dealt with leaves
         * behind, and reading it would have the next notification take down the
         * session just created for the very owner it names.
         * Everything else counts, whatever generation it was made for: a
         * retired CreateSession still waits on a Response from the portal it
         * was issued to, and that owner is all a handoff can be told by. */
        if (g_cancellable_is_cancelled (data->cancellable))
            continue;

        if (data->owner && !g_paste_str_equal (data->owner, owner))
            return TRUE;
    }

    return FALSE;
}

/* A request that may still bind the set we hold: one made for the generation we
 * are on, still to be answered, and not one kept alive merely for a session it
 * may yet be handed. What stands in for nothing is a supersede's leftovers -- a
 * CreateSession kept only to close the session it may still be answered with, a
 * cancelled request lingering until the callback that frees it runs -- and a
 * request the portal has already answered, whose refusal has been dealt with
 * while its method reply waits for a later main-loop iteration. Reading any of
 * those as a bind on its way leaves the shortcuts bound to nothing until the
 * next settings write, both here and in the portal-restart recovery.
 * The list holds what is in flight -- a handful at the very most -- so walking
 * it per grab is cheaper than keeping a count in step with every free. */
static gboolean
has_live_request (GPasteGtkGlobalShortcutClientPrivate *priv)
{
    for (const GSList *r = priv->requests; r; r = r->next)
    {
        const _PortalRequestData *data = r->data;

        if (data->generation == priv->generation && !data->responded && !data->retired)
            return TRUE;
    }

    return FALSE;
}

/* Whether what the portal is answering is still wanted: a generation that has
 * moved on says the shortcuts the request was made for are not the ones we want
 * any more, and a client that has been disposed of says as much of every set it
 * ever held -- its session is closed, and there is nowhere left to record one. */
static gboolean
portal_request_is_current (const _PortalRequestData      *data,
                           GPasteGtkGlobalShortcutClient *client)
{
    if (!client)
        return FALSE;

    GPasteGtkGlobalShortcutClientPrivate *priv = g_paste_gtk_global_shortcut_client_get_instance_private (client);

    return data->generation == priv->generation;
}

/* Bumping the generation invalidates every request still in flight: neither
 * CreateSession nor BindShortcuts can be cancelled once the portal has taken
 * them, and a reply that lands afterwards would otherwise bind, or claim,
 * shortcuts nobody asked for. */
static void
supersede_session (GPasteGtkGlobalShortcutClient *self)
{
    GPasteGtkGlobalShortcutClientPrivate *priv = g_paste_gtk_global_shortcut_client_get_instance_private (self);

    ++priv->generation;
    retire_requests (self, FALSE);
    close_session (self);
}

static void start_bind_async (GPasteGtkGlobalShortcutClient *self, GTask *task);

static void
on_shortcuts_bound (GDBusConnection *conn   G_GNUC_UNUSED,
                    const gchar     *sender G_GNUC_UNUSED,
                    const gchar     *path   G_GNUC_UNUSED,
                    const gchar     *iface  G_GNUC_UNUSED,
                    const gchar     *sig    G_GNUC_UNUSED,
                    GVariant        *params,
                    gpointer         user_data)
{
    _PortalRequestData *data = user_data;
    g_autoptr (GPasteGtkGlobalShortcutClient) self = g_weak_ref_get (&data->client);

    /* A Response we cannot read granted us nothing, but it is not the user
     * saying no either: 2, the portal's own "ended in some other way". The
     * signature is checked here rather than left to g_variant_get (), which
     * logs a critical -- fatal under G_DEBUG=fatal-criticals, and in these very
     * tests -- and leaves @response untouched when the format string does not
     * match. These subscriptions are the connection's own, with no interface
     * info for GDBus to check the signal against, unlike the Activated the
     * proxy validates for us. */
    guint response = 2;
    g_autoptr (GVariant) results = NULL;

    if (g_variant_is_of_type (params, G_VARIANT_TYPE ("(ua{sv})")))
        g_variant_get (params, "(u@a{sv})", &response, &results);

    if (!portal_request_is_current (data, self))
    {
        /* Superseded: whoever bumped the generation closed this session, which
         * is what releases the shortcuts it just bound. A client that is gone
         * closed it in dispose (), for the same reason. */
        portal_request_take_error (data, g_error_new (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                                      "BindShortcuts was superseded before it completed"));
    }
    else if (response)
    {
        /* Denied by the user, or refused because the session had already bound
         * its shortcuts. The session is spent either way. */
        close_session (self);
        portal_request_take_error (data, g_error_new (G_IO_ERROR, G_IO_ERROR_FAILED,
                                                      "BindShortcuts portal request failed with response %u", response));
    }
    else
        portal_request_return_boolean (data, TRUE);

    portal_request_done (data);
}

static void
on_bind_method_done (GObject      *source,
                     GAsyncResult *result,
                     gpointer      user_data)
{
    _PortalRequestData *data = user_data;
    g_autoptr (GPasteGtkGlobalShortcutClient) self = g_weak_ref_get (&data->client);
    g_autoptr (GError) error = NULL;
    g_autoptr (GVariant) ret = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), result, &error);

    if (data->responded)
    {
        /* The Response beat this reply and has done everything the request had
         * left to do: letting go of @data is all this callback is still for. */
        portal_request_data_free (data);
        return;
    }

    if (!portal_request_is_current (data, self))
    {
        /* Superseded: the session this call was made for has been closed
         * already, and the live one belongs to whoever replaced us -- closing
         * that would hand back the shortcuts it has just bound. Whatever the
         * portal has left to say is about a set nobody wants. */
        portal_request_take_error (data, g_error_new (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                                      "BindShortcuts was superseded before it completed"));
        portal_request_data_free (data);
        return;
    }

    if (!ret)
    {
        close_session (self);
        portal_request_take_error (data, g_steal_pointer (&error));
        portal_request_data_free (data);
        return;
    }

    const gchar *request_path;
    g_variant_get (ret, "(&o)", &request_path);

    /* The method reply is the request's object path and nothing else: whether
     * the portal took the shortcuts -- and whether the user allowed them at
     * all -- only comes back on that request's Response signal, which we have
     * been subscribed to since before the call. */
    portal_request_confirm_path (data, request_path, on_shortcuts_bound);
}

static void
start_bind_async (GPasteGtkGlobalShortcutClient *self,
                  GTask                         *task)
{
    GPasteGtkGlobalShortcutClientPrivate *priv = g_paste_gtk_global_shortcut_client_get_instance_private (self);
    GDBusProxy *proxy = G_DBUS_PROXY (self);

    _PortalRequestData *data = portal_request_data_new (self, task);

    g_autofree gchar *token = portal_next_request_token (priv);
    g_autofree gchar *request_path = portal_request_path (data->connection, token);

    if (!request_path)
    {
        g_task_return_new_error (data->task, G_IO_ERROR, G_IO_ERROR_FAILED,
                                 "The connection the portal is reached over has no unique name");
        portal_request_data_free (data);
        return;
    }

    portal_request_subscribe (data, request_path, on_shortcuts_bound);

    g_auto (GVariantBuilder) options;
    g_variant_builder_init (&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&options, "{sv}", "handle_token", g_variant_new_string (token));

    GVariant *params[] = {
        g_variant_new_object_path (priv->session_handle),
        build_shortcuts_variant (priv),
        g_variant_new_string (""),
        g_variant_builder_end (&options)
    };

    g_dbus_proxy_call (proxy, G_PASTE_GTK_GLOBAL_SHORTCUT_BIND_SHORTCUTS,
                       g_variant_new_tuple (params, 4),
                       G_DBUS_CALL_FLAGS_NONE, -1, data->cancellable,
                       on_bind_method_done, data);
}

static void
on_session_created (GDBusConnection *conn   G_GNUC_UNUSED,
                    const gchar     *sender G_GNUC_UNUSED,
                    const gchar     *path   G_GNUC_UNUSED,
                    const gchar     *iface  G_GNUC_UNUSED,
                    const gchar     *sig    G_GNUC_UNUSED,
                    GVariant        *params,
                    gpointer         user_data)
{
    _PortalRequestData *data = user_data;
    g_autoptr (GPasteGtkGlobalShortcutClient) self = g_weak_ref_get (&data->client);

    /* Read the way on_shortcuts_bound () reads it, and for the same reasons. */
    guint response = 2;
    g_autoptr (GVariant) results = NULL;

    if (g_variant_is_of_type (params, G_VARIANT_TYPE ("(ua{sv})")))
        g_variant_get (params, "(u@a{sv})", &response, &results);

    if (response != 0)
    {
        portal_request_take_error (data, g_error_new (G_IO_ERROR, G_IO_ERROR_FAILED,
                                                      "CreateSession portal request failed with response %u", response));
        portal_request_done (data);
        return;
    }

    /* The spec has the handle as a string and portals have been known to send
     * an object path instead: both are what g_variant_get_string () reads.
     * Anything else -- and any string that is not a path -- would reach
     * start_bind_async () as a %NULL g_variant_new_object_path (), which takes
     * the process down rather than failing the session here. */
    g_autoptr (GVariant) handle_v = g_variant_lookup_value (results, "session_handle", NULL);
    const gchar *handle = (handle_v && (g_variant_is_of_type (handle_v, G_VARIANT_TYPE_STRING) ||
                                        g_variant_is_of_type (handle_v, G_VARIANT_TYPE_OBJECT_PATH)))
                        ? g_variant_get_string (handle_v, NULL)
                        : NULL;

    if (!handle || !g_variant_is_object_path (handle))
    {
        portal_request_take_error (data, g_error_new (G_IO_ERROR, G_IO_ERROR_FAILED,
                                                      "CreateSession answered without a usable session_handle"));
        portal_request_done (data);
        return;
    }

    if (data->retired || !portal_request_is_current (data, self))
    {
        /* Superseded while the portal was asking, kept alive for this handle
         * alone after the method call failed, or answered to a client that is
         * gone: this session is nobody's, and a session we never stored has only
         * this handle to be closed by. Binding it is not on offer either -- the
         * task the bind would be reported on has been answered already, and
         * answering it twice is a critical. */
        close_session_handle (data->connection, handle);
        portal_request_take_error (data, g_error_new (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                                      "CreateSession was superseded before it completed"));
        portal_request_done (data);
        return;
    }

    GPasteGtkGlobalShortcutClientPrivate *priv = g_paste_gtk_global_shortcut_client_get_instance_private (self);

    g_set_str (&priv->session_handle, handle);

    /* Which portal answered, so that one replacing it can be told from the one
     * this session was created by: what a portal holds goes with it. */
    g_autofree gchar *owner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (self));
    g_free (priv->session_owner);
    priv->session_owner = g_steal_pointer (&owner);

    g_autoptr (GTask) task = g_object_ref (data->task);
    portal_request_done (data);

    start_bind_async (self, task);
}

static void
on_create_session_method_done (GObject      *source,
                               GAsyncResult *result,
                               gpointer      user_data)
{
    _PortalRequestData *data = user_data;
    g_autoptr (GError) error = NULL;
    g_autoptr (GVariant) ret = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), result, &error);

    if (data->responded)
    {
        /* The Response beat this reply and has done everything the request had
         * left to do, the session it came back with included. */
        portal_request_data_free (data);
        return;
    }

    if (!ret)
    {
        /* The one CreateSession we ever cancel is one the portal went away
         * under, and the session it might have created went with it. */
        gboolean portal_is_gone = g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);

        portal_request_take_error (data, g_steal_pointer (&error));

        if (portal_is_gone)
        {
            portal_request_data_free (data);
            return;
        }

        /* Any other failure says nothing about whether the portal took the
         * call -- a backend held up behind a dialog of its own answers the
         * request long after the 25 s the method itself is given -- and the
         * handle of the session it may go on to create appears in that Response
         * and nowhere else. Dropping the subscription here would leave that
         * session holding whatever it binds for the rest of the process, so
         * keep listening, under the deadline a retired request is given. */
        data->awaiting_response = TRUE;
        retire_one_request (data, FALSE);
        return;
    }

    /* The portal went away under a call it had already taken, and the reply won
     * the race with the cancel that says so: the Response this path names can
     * never come, since the portal that would send it is gone. Keeping the
     * subscription for it would hold the request -- and the client it waits on
     * -- for the rest of the process, with no deadline to end that either: the
     * retire that cancelled the call took the branch that arms none. */
    if (g_cancellable_is_cancelled (data->cancellable))
    {
        portal_request_take_error (data, g_error_new (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                                      "The portal went away before CreateSession was answered"));
        portal_request_data_free (data);
        return;
    }

    const gchar *request_path;
    g_variant_get (ret, "(&o)", &request_path);

    /* Keep listening even when the generation has moved on: the session the
     * portal is about to answer with is one only on_session_created () will
     * ever hold the handle of, and only it can close it. */
    portal_request_confirm_path (data, request_path, on_session_created);
}

static void
start_create_session_async (GPasteGtkGlobalShortcutClient *self,
                            GTask                      *task)
{
    GPasteGtkGlobalShortcutClientPrivate *priv = g_paste_gtk_global_shortcut_client_get_instance_private (self);
    GDBusProxy *proxy = G_DBUS_PROXY (self);

    _PortalRequestData *data = portal_request_data_new (self, task);
    data->creates_session = TRUE;

    g_autofree gchar *token = portal_next_request_token (priv);
    g_autofree gchar *request_path = portal_request_path (data->connection, token);

    if (!request_path)
    {
        g_task_return_new_error (data->task, G_IO_ERROR, G_IO_ERROR_FAILED,
                                 "The connection the portal is reached over has no unique name");
        portal_request_data_free (data);
        return;
    }

    portal_request_subscribe (data, request_path, on_session_created);

    g_autofree gchar *session_token = portal_next_session_token (priv);

    g_auto (GVariantBuilder) options;
    g_variant_builder_init (&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&options, "{sv}", "handle_token", g_variant_new_string (token));
    g_variant_builder_add (&options, "{sv}", "session_handle_token", g_variant_new_string (session_token));

    GVariant *params[] = { g_variant_builder_end (&options) };

    g_dbus_proxy_call (proxy, G_PASTE_GTK_GLOBAL_SHORTCUT_CREATE_SESSION,
                       g_variant_new_tuple (params, 1),
                       G_DBUS_CALL_FLAGS_NONE, -1, data->cancellable,
                       on_create_session_method_done, data);
}

/**************************/
/* GPasteKeybindingProvider */
/**************************/

static void
on_provider_bind_done (GObject      *source   G_GNUC_UNUSED,
                       GAsyncResult *result,
                       gpointer      user_data G_GNUC_UNUSED)
{
    g_autoptr (GError) error = NULL;

    /* A task can come back false without an error of its own, and a superseded
     * request is not a failure either: something else took over. */
    if (!g_task_propagate_boolean (G_TASK (result), &error) && error &&
        !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("GPasteGtkGlobalShortcutClient: binding the global shortcuts failed: %s", error->message);
}

/* Bind the shortcuts we hold: a session of their own, then the one chance that
 * session gets to bind them. Whatever was around before was for another set --
 * or for a portal that has since restarted -- so it goes first: a session left
 * open would keep holding shortcuts nobody asked for any more, and a request
 * still after one would store its handle over ours. */
static void
start_grab_async (GPasteGtkGlobalShortcutClient *self)
{
    supersede_session (self);

    /* No source object: the task outlives neither the client nor the requests it
     * is handed between, and a reference on the client is exactly what the
     * requests hold weakly to keep dispose () within reach. */
    g_autoptr (GTask) task = g_task_new (NULL, NULL, on_provider_bind_done, NULL);

    start_create_session_async (self, task);
}

static void
global_shortcut_client_grab_all (GPasteKeybindingProvider          *provider,
                                  const GPasteKeybindingAccelerator *accels)
{
    GPasteGtkGlobalShortcutClient *self = G_PASTE_GTK_GLOBAL_SHORTCUT_CLIENT (provider);
    GPasteGtkGlobalShortcutClientPrivate *priv = g_paste_gtk_global_shortcut_client_get_instance_private (self);

    /* Safe to call on a disposed client, as the gnome-shell provider is: the
     * shortcuts are then %NULL, and dispose () has already closed the session
     * that held them. */
    if (!priv->shortcuts)
        return;

    /* A write of an accelerator that did not change is a rebind all the same,
     * and rebinding the very set that is already bound would close the session
     * holding it only to ask the portal for another -- putting its permission
     * dialog back in front of the user, on backends that do not remember the
     * grant, over a set nothing has changed. */
    if ((priv->session_handle || has_live_request (priv)) &&
        g_paste_keybinding_accelerators_match (priv->shortcuts, accels))
        return;

    /* Copied before what we hold is let go of: @accels is the caller's, and
     * nothing says it was not built from the set about to be freed. */
    GPasteKeybindingAccelerator *shortcuts = g_paste_keybinding_accelerators_copy (accels);

    g_paste_keybinding_accelerators_free (priv->shortcuts);
    priv->shortcuts = shortcuts;


    /* The set we are registering is not the one the current session bound, and
     * that session cannot be told about it: start over with a new one, or with
     * none at all when there is nothing left to bind. */
    if (priv->shortcuts[0].id)
        start_grab_async (self);
    else
        supersede_session (self);
}

static void
global_shortcut_client_ungrab_all (GPasteKeybindingProvider *provider)
{
    GPasteGtkGlobalShortcutClient *self = G_PASTE_GTK_GLOBAL_SHORTCUT_CLIENT (provider);
    GPasteGtkGlobalShortcutClientPrivate *priv = g_paste_gtk_global_shortcut_client_get_instance_private (self);

    /* Safe to call on a disposed client: see grab_all (). */
    if (!priv->shortcuts)
        return;

    g_paste_keybinding_accelerators_free (priv->shortcuts);
    /* Nothing to bind, and a set that is still ours: only a disposed client has
     * none at all. */
    priv->shortcuts = g_new0 (GPasteKeybindingAccelerator, 1);

    /* Rebinding an empty set would leave the session's shortcuts exactly where
     * they are: closing it is what hands them back. */
    supersede_session (self);
}

static void
global_shortcut_client_provider_init (GPasteKeybindingProviderInterface *iface)
{
    iface->grab_all   = global_shortcut_client_grab_all;
    iface->ungrab_all = global_shortcut_client_ungrab_all;
}

/**********************/
/* D-Bus signal       */
/**********************/

static void
g_paste_gtk_global_shortcut_client_g_signal (GDBusProxy  *proxy,
                                             const gchar *sender_name G_GNUC_UNUSED,
                                             const gchar *signal_name,
                                             GVariant    *parameters)
{
    GPasteGtkGlobalShortcutClient *self = G_PASTE_GTK_GLOBAL_SHORTCUT_CLIENT (proxy);

    if (g_paste_str_equal (signal_name, G_PASTE_GTK_GLOBAL_SHORTCUT_SIG_ACTIVATED))
    {
        GPasteGtkGlobalShortcutClientPrivate *priv = g_paste_gtk_global_shortcut_client_get_instance_private (self);
        const gchar *session_handle;
        const gchar *shortcut_id;
        guint64 timestamp G_GNUC_UNUSED;
        g_autoptr (GVariant) options = NULL;
        g_variant_get (parameters, "(&o&st@a{sv})",
                       &session_handle, &shortcut_id, &timestamp, &options);

        if (g_paste_str_equal (session_handle, priv->session_handle))
            g_paste_keybinding_provider_emit_keybinding_activated (G_PASTE_KEYBINDING_PROVIDER (self), shortcut_id);
    }
}

/* The portal takes the sessions and every request in flight with it when it
 * goes: nothing is left to close, and the Response the request is waiting for
 * can never come -- it would hold its subscription, and the client it keeps
 * alive with it, for the rest of the process.
 * Idempotent: a handoff reported as a %NULL owner and then a new one runs this
 * twice, and the second pass finds nothing left to retire or close. */
static void
portal_lost (GPasteGtkGlobalShortcutClient *self)
{
    GPasteGtkGlobalShortcutClientPrivate *priv = g_paste_gtk_global_shortcut_client_get_instance_private (self);

    ++priv->generation;
    retire_requests (self, TRUE);
    g_clear_pointer (&priv->session_handle, g_free);
    g_clear_pointer (&priv->session_owner, g_free);
}

static void
on_portal_name_owner_changed (GPasteGtkGlobalShortcutClient *self,
                              GParamSpec                    *pspec     G_GNUC_UNUSED,
                              gpointer                       user_data G_GNUC_UNUSED)
{
    GPasteGtkGlobalShortcutClientPrivate *priv = g_paste_gtk_global_shortcut_client_get_instance_private (self);
    g_autofree gchar *owner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (self));

    /* A portal handing the name straight to its replacement never lets go of
     * it, so there is no %NULL in between to say that what it held is gone: an
     * owner that is not the one our session and our requests were made under
     * says it just as well. Left alone, the handle would go on naming a session
     * the portal now on the name knows nothing about -- one no Activated can
     * ever match, and one the guard below would take for a session we still
     * have. */
    if (!owner || portal_was_replaced (priv, owner))
        portal_lost (self);

    if (!owner)
        return;

    /* A request made while nobody owned the name -- our own call is what
     * D-Bus-activated the portal -- is being answered by the owner that has
     * just turned up: without it recorded, the handoff after this one would
     * have nothing to tell it apart from. */
    for (const GSList *r = priv->requests; r; r = r->next)
    {
        _PortalRequestData *data = r->data;

        if (!data->owner)
            data->owner = g_strdup (owner);
    }

    /* Back, without the session that held our shortcuts: the set we still hold
     * is bound to nothing until a new session binds it, and nothing else is
     * ever going to ask for one.
     * A session, or a request made for the generation we are on, is one this
     * owner change did not take -- the name appearing because our own
     * CreateSession D-Bus-activated the portal is exactly that -- and starting
     * over would close the session that request is about to be answered with,
     * taking the permission dialog the user is looking at down with it. */
    if (priv->shortcuts && priv->shortcuts[0].id && !priv->session_handle && !has_live_request (priv))
        start_grab_async (self);
}

/****************/
/* GObject glue */
/****************/

static void
g_paste_gtk_global_shortcut_client_dispose (GObject *object)
{
    GPasteGtkGlobalShortcutClient *self = G_PASTE_GTK_GLOBAL_SHORTCUT_CLIENT (object);
    GPasteGtkGlobalShortcutClientPrivate *priv = g_paste_gtk_global_shortcut_client_get_instance_private (self);

    /* Out of the client before a single request is retired, rather than emptied
     * afterwards: weak locations are cleared before dispose () runs, so a
     * request freed below cannot take itself out of this list, and a list
     * holding freed requests is one nothing may be left able to walk. What is
     * not freed is a retired request waiting on a Response we will not be here
     * for; it frees itself, finding no client to unlink from. */
    g_autoptr (GSList) requests = g_steal_pointer (&priv->requests);

    ++priv->generation;
    retire_request_list (requests, FALSE);
    close_session (self);

    g_clear_pointer (&priv->shortcuts, g_paste_keybinding_accelerators_free);
    g_clear_pointer (&priv->session_owner, g_free);

    G_OBJECT_CLASS (g_paste_gtk_global_shortcut_client_parent_class)->dispose (object);
}

static void
g_paste_gtk_global_shortcut_client_class_init (GPasteGtkGlobalShortcutClientClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_gtk_global_shortcut_client_dispose;
    G_DBUS_PROXY_CLASS (klass)->g_signal = g_paste_gtk_global_shortcut_client_g_signal;
}

static void
g_paste_gtk_global_shortcut_client_init (GPasteGtkGlobalShortcutClient *self)
{
    GDBusProxy *proxy = G_DBUS_PROXY (self);
    g_autoptr (GError) error = NULL;
    g_autoptr (GDBusNodeInfo) dbus_info = g_dbus_node_info_new_for_xml (G_PASTE_GTK_GLOBAL_SHORTCUT_INTERFACE,
                                                                        &error);
    g_assert_no_error (error);

    g_dbus_proxy_set_interface_info (proxy, dbus_info->interfaces[0]);

    GPasteGtkGlobalShortcutClientPrivate *priv = g_paste_gtk_global_shortcut_client_get_instance_private (self);
    priv->session_handle = NULL;
    priv->session_owner = NULL;
    priv->shortcuts = g_new0 (GPasteKeybindingAccelerator, 1);
    priv->generation = 0;
    priv->request_count = 0;
    priv->session_count = 0;
    priv->requests = NULL;

    g_signal_connect (self, "notify::g-name-owner", G_CALLBACK (on_portal_name_owner_changed), NULL);
}

/**
 * g_paste_gtk_global_shortcut_client_new_sync:
 * @error: Return location for error or %NULL.
 *
 * Create a new instance of #GPasteGtkGlobalShortcutClient
 *
 * Returns: a newly allocated #GPasteGtkGlobalShortcutClient
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteGtkGlobalShortcutClient *
g_paste_gtk_global_shortcut_client_new_sync (GError **error)
{
    CUSTOM_PROXY_NEW (GTK_GLOBAL_SHORTCUT_CLIENT, GTK_GLOBAL_SHORTCUT, G_PASTE_GTK_GLOBAL_SHORTCUT_BUS_NAME);
}

/**
 * g_paste_gtk_global_shortcut_client_new:
 * @callback: Callback function to invoke when the proxy is ready.
 * @user_data: User data to pass to @callback.
 *
 * Create a new instance of #GPasteGtkGlobalShortcutClient
 */
G_PASTE_VISIBLE void
g_paste_gtk_global_shortcut_client_new (GAsyncReadyCallback callback,
                                    gpointer            user_data)
{
    CUSTOM_PROXY_NEW_ASYNC (GTK_GLOBAL_SHORTCUT_CLIENT, GTK_GLOBAL_SHORTCUT, G_PASTE_GTK_GLOBAL_SHORTCUT_BUS_NAME);
}

/**
 * g_paste_gtk_global_shortcut_client_new_finish:
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback function passed to the async ctor.
 * @error: Return location for error or %NULL.
 *
 * Create a new instance of #GPasteGtkGlobalShortcutClient
 *
 * Returns: a newly allocated #GPasteGtkGlobalShortcutClient
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteGtkGlobalShortcutClient *
g_paste_gtk_global_shortcut_client_new_finish (GAsyncResult *result,
                                           GError      **error)
{
    CUSTOM_PROXY_NEW_FINISH (GTK_GLOBAL_SHORTCUT_CLIENT);
}
