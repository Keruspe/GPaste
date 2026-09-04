// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste/gpaste-gdbus-macros.h>
#include <gpaste/gpaste-gnome-shell-client.h>
#include <gpaste/gpaste-keybinding-provider.h>

#define G_PASTE_GNOME_SHELL_OBJECT_PATH    "/org/gnome/Shell"
#define G_PASTE_GNOME_SHELL_INTERFACE_NAME "org.gnome.Shell"

#define G_PASTE_GNOME_SHELL_GRAB_ACCELERATOR    "GrabAccelerator"
#define G_PASTE_GNOME_SHELL_GRAB_ACCELERATORS   "GrabAccelerators"
#define G_PASTE_GNOME_SHELL_UNGRAB_ACCELERATOR  "UngrabAccelerator"
#define G_PASTE_GNOME_SHELL_UNGRAB_ACCELERATORS "UngrabAccelerators"

#define G_PASTE_GNOME_SHELL_SIG_ACCELERATOR_ACTIVATED "AcceleratorActivated"

#define G_PASTE_GNOME_SHELL_INTERFACE                                                                      \
    "<node>"                                                                                               \
        "<interface  name='" G_PASTE_GNOME_SHELL_INTERFACE_NAME "'>"                                       \
            "<method name='" G_PASTE_GNOME_SHELL_GRAB_ACCELERATOR "'>"                                     \
                "<arg type='s' direction='in'  name='accelerator' />"                                      \
                "<arg type='u' direction='in'  name='modeFlags'   />"                                      \
                "<arg type='u' direction='in'  name='grabFlags'   />"                                      \
                "<arg type='u' direction='out' name='action'      />"                                      \
            "</method>"                                                                                    \
            "<method name='" G_PASTE_GNOME_SHELL_GRAB_ACCELERATORS "'>"                                    \
                "<arg type='a(suu)' direction='in'  name='accelerators' />"                                \
                "<arg type='au'     direction='out' name='actions'      />"                                \
            "</method>"                                                                                    \
            "<method name='" G_PASTE_GNOME_SHELL_UNGRAB_ACCELERATOR "'>"                                   \
                "<arg type='u' direction='in'  name='action'  />"                                          \
                "<arg type='b' direction='out' name='success' />"                                          \
            "</method>"                                                                                    \
            "<signal name='" G_PASTE_GNOME_SHELL_SIG_ACCELERATOR_ACTIVATED "'>"                            \
                "<arg name='action'     type='u' />"                                                       \
                "<arg name='parameters' type='a{sv}' />"                                                   \
            "</signal>"                                                                                    \
        "</interface>"                                                                                     \
    "</node>"

typedef struct
{
    GHashTable                  *id_to_action; /* gchar* (owned) → GUINT_TO_POINTER (guint32) */
    GPasteKeybindingAccelerator *stored;       /* the set we hold, owned — saved for re-grab */
    guint64                      generation;   /* bumped whenever the grabs we hold stop being the ones we want */
    guint64                      shell_epoch;  /* bumped whenever the shell that held them goes away */
    guint64                      call_count;   /* what the id of the next GrabAccelerators is taken from */
    guint64                      in_flight;    /* the id of the GrabAccelerators the shell has yet to answer, 0 for none */
    gboolean                     pending_grab; /* a grab was asked for while that one was in flight */
    guint64                      retries;
    guint                        retry_source;
    guint                        shell_watch;
} GPasteGnomeShellClientPrivate;

struct _GPasteGnomeShellClient
{
    GDBusProxy parent_instance;
};

static void gnome_shell_client_provider_init (GPasteKeybindingProviderInterface *iface);

G_PASTE_DEFINE_TYPE_WITH_PRIVATE_AND_INTERFACE (GnomeShellClient, gnome_shell_client, G_TYPE_DBUS_PROXY,
    G_PASTE_TYPE_KEYBINDING_PROVIDER, gnome_shell_client_provider_init)

enum
{
    ACCELERATOR_ACTIVATED,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/*******************/
/* Methods / Async */
/*******************/

#define DBUS_CALL_ONE_PARAM_ASYNC(method, param_type, param_name) \
    DBUS_CALL_ONE_PARAM_ASYNC_BASE (GNOME_SHELL_CLIENT, param_type, param_name, G_PASTE_GNOME_SHELL_##method)

#define DBUS_CALL_ONE_PARAMV_ASYNC(method, paramv) \
    DBUS_CALL_ONE_PARAMV_ASYNC_BASE (GNOME_SHELL_CLIENT, paramv, G_PASTE_GNOME_SHELL_##method)

#define DBUS_CALL_THREE_PARAMS_ASYNC(method, params) \
    DBUS_CALL_THREE_PARAMS_ASYNC_BASE (GNOME_SHELL_CLIENT, params, G_PASTE_GNOME_SHELL_##method)

/****************************/
/* Methods / Async - Finish */
/****************************/

#define DBUS_ASYNC_FINISH_RET_BOOL \
    DBUS_ASYNC_FINISH_RET_BOOL_BASE (GNOME_SHELL_CLIENT)

#define DBUS_ASYNC_FINISH_RET_UINT32 \
    DBUS_ASYNC_FINISH_RET_UINT32_BASE (GNOME_SHELL_CLIENT)

/********************************/
/* Methods / Sync - With return */
/********************************/

#define DBUS_CALL_ONE_PARAM_RET_BOOL(method, param_type, param_name) \
    DBUS_CALL_ONE_PARAM_RET_BOOL_BASE (GNOME_SHELL_CLIENT, param_type, param_name, G_PASTE_GNOME_SHELL_##method)

#define DBUS_CALL_ONE_PARAMV_RET_AU(method, paramv) \
    DBUS_CALL_ONE_PARAMV_RET_AU_BASE (GNOME_SHELL_CLIENT, G_PASTE_GNOME_SHELL_##method, paramv, NULL)

#define DBUS_CALL_THREE_PARAMS_RET_UINT32(method, params) \
    DBUS_CALL_THREE_PARAMS_RET_UINT32_BASE (GNOME_SHELL_CLIENT, params, G_PASTE_GNOME_SHELL_##method)

/******************/
/* Methods / Sync */
/******************/

/**
 * g_paste_gnome_shell_client_grab_accelerator_sync:
 * @self: a #GPasteGnomeShellClient instance
 * @accelerator: a #GPasteGnomeShellAccelerator instance
 * @error: a #GError
 *
 * Grab a keybinding
 *
 * Returns: the action id corresponding
 */
G_PASTE_VISIBLE guint32
g_paste_gnome_shell_client_grab_accelerator_sync (GPasteGnomeShellClient     *self,
                                                  GPasteGnomeShellAccelerator accelerator,
                                                  GError                    **error)
{
    GVariant *accel[] = {
        g_variant_new_string (accelerator.accelerator),
        g_variant_new_uint32 (accelerator.grab_flags),
        g_variant_new_uint32 (accelerator.mode_flags)
    };
    DBUS_CALL_THREE_PARAMS_RET_UINT32 (GRAB_ACCELERATOR, accel);
}

/**
 * g_paste_gnome_shell_client_grab_accelerators_sync:
 * @self: a #GPasteGnomeShellClient instance
 * @accelerators: (array): an array of #GPasteGnomeShellAccelerator instances
 * @error: a #GError
 *
 * Grab some keybindings
 *
 * Returns: the action ids corresponding
 */
G_PASTE_VISIBLE guint32 *
g_paste_gnome_shell_client_grab_accelerators_sync (GPasteGnomeShellClient      *self,
                                                   GPasteGnomeShellAccelerator *accelerators,
                                                   GError                     **error)
{
    g_auto (GVariantBuilder) builder;
    guint64 n_accelerators = 0;

    g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);

    for (GPasteGnomeShellAccelerator *accelerator = &accelerators[0]; accelerator->accelerator; accelerator = &accelerators[++n_accelerators])
    {
        g_variant_builder_open (&builder, G_VARIANT_TYPE_TUPLE);
        g_variant_builder_add_value (&builder, g_variant_new_string (accelerator->accelerator));
        g_variant_builder_add_value (&builder, g_variant_new_uint32 (accelerator->grab_flags));
        g_variant_builder_add_value (&builder, g_variant_new_uint32 (accelerator->mode_flags));
        g_variant_builder_close (&builder);
    }

    GVariant *array = g_variant_builder_end (&builder);

    DBUS_CALL_ONE_PARAMV_RET_AU (GRAB_ACCELERATORS, array);
}

/**
 * g_paste_gnome_shell_client_ungrab_accelerator_sync:
 * @self: a #GPasteGnomeShellClient instance
 * @action: the action id corresponding to the keybinding
 * @error: a #GError
 *
 * Ungrab a keybinding
 *
 * Returns: whether the ungrab was succesful or not
 */
G_PASTE_VISIBLE gboolean
g_paste_gnome_shell_client_ungrab_accelerator_sync (GPasteGnomeShellClient *self,
                                                    guint32                 action,
                                                    GError                **error)
{
    DBUS_CALL_ONE_PARAM_RET_BOOL (UNGRAB_ACCELERATOR, uint32, action);
}

/*******************/
/* Methods / Async */
/*******************/

/**
 * g_paste_gnome_shell_client_grab_accelerator:
 * @self: a #GPasteGnomeShellClient instance
 * @accelerator: a #GPasteGnomeShellAccelerator instance
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Grab a keybinding
 */
G_PASTE_VISIBLE void
g_paste_gnome_shell_client_grab_accelerator (GPasteGnomeShellClient     *self,
                                             GPasteGnomeShellAccelerator accelerator,
                                             GAsyncReadyCallback         callback,
                                             gpointer                    user_data)
{
    GVariant *accel[] = {
        g_variant_new_string (accelerator.accelerator),
        g_variant_new_uint32 (accelerator.grab_flags),
        g_variant_new_uint32 (accelerator.mode_flags)
    };
    DBUS_CALL_THREE_PARAMS_ASYNC (GRAB_ACCELERATOR, accel);
}

/**
 * g_paste_gnome_shell_client_grab_accelerators:
 * @self: a #GPasteGnomeShellClient instance
 * @accelerators: (array): an array of #GPasteGnomeShellAccelerator instances
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Grab some keybindings
 */
G_PASTE_VISIBLE void
g_paste_gnome_shell_client_grab_accelerators (GPasteGnomeShellClient      *self,
                                              GPasteGnomeShellAccelerator *accelerators,
                                              GAsyncReadyCallback          callback,
                                              gpointer                     user_data)
{
    g_auto (GVariantBuilder) builder;
    guint64 n_accelerators = 0;

    g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);

    for (GPasteGnomeShellAccelerator *accelerator = &accelerators[0]; accelerator->accelerator; accelerator = &accelerators[++n_accelerators])
    {
        g_variant_builder_open (&builder, G_VARIANT_TYPE_TUPLE);
        g_variant_builder_add_value (&builder, g_variant_new_string (accelerator->accelerator));
        g_variant_builder_add_value (&builder, g_variant_new_uint32 (accelerator->grab_flags));
        g_variant_builder_add_value (&builder, g_variant_new_uint32 (accelerator->mode_flags));
        g_variant_builder_close (&builder);
    }

    GVariant *array = g_variant_builder_end (&builder);

    DBUS_CALL_ONE_PARAMV_ASYNC (GRAB_ACCELERATORS, array);
}

/**
 * g_paste_gnome_shell_client_ungrab_accelerator:
 * @self: a #GPasteGnomeShellClient instance
 * @action: the action id corresponding to the keybinding
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Ungrab a keybinding
 */
G_PASTE_VISIBLE void
g_paste_gnome_shell_client_ungrab_accelerator (GPasteGnomeShellClient *self,
                                               guint32                 action,
                                               GAsyncReadyCallback     callback,
                                               gpointer                user_data)
{
    DBUS_CALL_ONE_PARAM_ASYNC (UNGRAB_ACCELERATOR, uint32, action);
}

/****************************/
/* Methods / Async - Finish */
/****************************/

/**
 * g_paste_gnome_shell_client_grab_accelerator_finish:
 * @self: a #GPasteGnomeShellClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Grab a keybinding
 *
 * Returns: the action id corresultponding
 */
G_PASTE_VISIBLE guint32
g_paste_gnome_shell_client_grab_accelerator_finish (GPasteGnomeShellClient *self,
                                                    GAsyncResult           *result,
                                                    GError                **error)
{
    DBUS_ASYNC_FINISH_RET_UINT32;
}

/* The array length is the shell's own answer, and walking the reply up to the
 * number of accelerators we asked for instead trusts it to have answered with
 * exactly that many: everything internal goes through this, and the public
 * finish call is the same thing with the length thrown away. */
static guint32 *
gnome_shell_client_grab_accelerators_finish (GPasteGnomeShellClient *self,
                                             GAsyncResult           *result,
                                             guint64                *len,
                                             GError                **error)
{
    DBUS_ASYNC_FINISH_RET_AU_BASE (GNOME_SHELL_CLIENT, len);
}

/**
 * g_paste_gnome_shell_client_grab_accelerators_finish:
 * @self: a #GPasteGnomeShellClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Grab some keybindings
 *
 * Returns: the action ids corresultponding
 */
G_PASTE_VISIBLE guint32 *
g_paste_gnome_shell_client_grab_accelerators_finish (GPasteGnomeShellClient *self,
                                                     GAsyncResult           *result,
                                                     GError                **error)
{
    return gnome_shell_client_grab_accelerators_finish (self, result, NULL, error);
}

/**
 * g_paste_gnome_shell_client_ungrab_accelerator_finish:
 * @self: a #GPasteGnomeShellClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Ungrab a keybinding
 *
 * Returns: whether the ungrab was succesful or not
 */
G_PASTE_VISIBLE gboolean
g_paste_gnome_shell_client_ungrab_accelerator_finish (GPasteGnomeShellClient *self,
                                                      GAsyncResult           *result,
                                                      GError                **error)
{
    DBUS_ASYNC_FINISH_RET_BOOL;
}

/**************************/
/* GPasteKeybindingProvider */
/**************************/

typedef struct
{
    GPasteGnomeShellClient      *client;
    GStrv                        ids;         /* owned copy for mapping after async completes */
    guint64                      generation;  /* the generation this grab was issued for */
    guint64                      shell_epoch; /* the shell this grab was issued to */
    guint64                      call_id;     /* the call this context was handed to the shell with */
} _GrabAllContext;

/* UngrabAccelerators is a newer method than the per-action UngrabAccelerator,
 * and a shell that does not have it answers UnknownMethod: with the reply
 * dropped, every release -- a rebind, a stale hand-back, dispose () -- would
 * silently leave the shell holding our accelerators, as dead keys stolen from
 * every other application. A %FALSE answer is not that: it says one entry named
 * nothing the shell holds, which the stale hand-back may legitimately do. */
static void
on_accelerators_ungrabbed (GObject      *source_object,
                           GAsyncResult *res,
                           gpointer      user_data G_GNUC_UNUSED)
{
    g_autoptr (GError) error = NULL;
    g_autoptr (GVariant) ret = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object), res, &error);

    if (!ret)
    {
        g_warning ("Couldn't release keybindings with gnome-shell: %s", error->message);
        return;
    }

    gboolean success = FALSE;
    g_variant_get (ret, "(b)", &success);

    if (!success)
        g_debug ("gnome-shell did not release every accelerator we handed back");
}

/* Hand a whole set of action ids back at once: UngrabAccelerator releases a
 * single one, and going through it costs the shell as many round trips as we
 * hold grabs, on every rebind and every stale reply.
 * Addressed to a unique name on the connection rather than called on the proxy,
 * which is why UngrabAccelerators is not in the interface info above: the shell
 * that granted the ids is the one they mean, and a proxy call would hold a
 * reference on the client until its reply -- which dispose () is waiting for. */
static void
gnome_shell_client_ungrab_actions (GPasteGnomeShellClient *self,
                                   const guint32          *actions,
                                   gsize                   n_actions)
{
    if (!n_actions)
        return;

    g_auto (GVariantBuilder) builder;
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("au"));

    for (gsize i = 0; i < n_actions; i++)
        g_variant_builder_add (&builder, "u", actions[i]);

    GVariant *params[] = { g_variant_builder_end (&builder) };

    g_dbus_proxy_call (G_DBUS_PROXY (self),
                       G_PASTE_GNOME_SHELL_UNGRAB_ACCELERATORS,
                       g_variant_new_tuple (params, 1),
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,                        /* timeout */
                       NULL,                      /* cancellable */
                       on_accelerators_ungrabbed, /* callback */
                       NULL);                     /* user_data */
}

/* Release every accelerator the shell currently holds on our behalf.
 * Safe to call on a disposed client: id_to_action is then %NULL, and there is
 * nothing left to hand back. */
static void
gnome_shell_client_release_grabs (GPasteGnomeShellClient *self)
{
    GPasteGnomeShellClientPrivate *priv = g_paste_gnome_shell_client_get_instance_private (self);

    if (!priv->id_to_action || !g_hash_table_size (priv->id_to_action))
        return;

    g_autofree guint32 *actions = g_new (guint32, g_hash_table_size (priv->id_to_action));
    gsize n_actions = 0;

    GHashTableIter iter;
    gpointer key G_GNUC_UNUSED, value;
    g_hash_table_iter_init (&iter, priv->id_to_action);
    while (g_hash_table_iter_next (&iter, &key, &value))
        actions[n_actions++] = GPOINTER_TO_UINT (value);

    gnome_shell_client_ungrab_actions (self, actions, n_actions);

    g_hash_table_remove_all (priv->id_to_action);
}

static gboolean retry_grab_all (gpointer user_data);
static void gnome_shell_client_regrab_stored (GPasteGnomeShellClient *self);

static void
grab_all_cb (GObject      *source_object,
             GAsyncResult *res,
             gpointer      user_data)
{
    _GrabAllContext *ctx = user_data;
    GPasteGnomeShellClientPrivate *priv = g_paste_gnome_shell_client_get_instance_private (ctx->client);
    g_autoptr (GError) error = NULL;
    guint64 n_actions = 0;
    g_autofree guint32 *actions = gnome_shell_client_grab_accelerators_finish (
        G_PASTE_GNOME_SHELL_CLIENT (source_object), res, &n_actions, &error);

    /* Only the call we are still waiting for may say that nothing is: a shell
     * that went away with one outstanding had it abandoned there, and the reply
     * it may still send would otherwise clear the flag for the call issued to
     * its replacement -- letting the next grab go out alongside that one and
     * collide with it accelerator for accelerator. */
    if (ctx->call_id == priv->in_flight)
        priv->in_flight = 0;

    if (ctx->generation != priv->generation)
    {
        /* Superseded while in flight: whatever the shell just granted us is not
         * what we want any more, and nothing else knows those action ids, so
         * hand them back here or they stay grabbed for the rest of the session.
         * dispose () bumps the generation too, and the grabs an in-flight reply
         * brings back then are exactly the ones nothing else will ever release,
         * so this must not be conditioned on id_to_action still being around.
         * On error n_actions stays 0 and there is nothing to hand back.
         * A 0 is the shell refusing an accelerator rather than an action id, as
         * on the success path: it names nothing the shell holds, and the call
         * comes back false for the whole set on account of it. */
        g_autofree guint32 *granted = g_new (guint32, n_actions);
        gsize n_granted = 0;

        for (guint64 i = 0; i < n_actions; i++)
            if (actions[i])
                granted[n_granted++] = actions[i];

        /* To the shell that granted them and no other: a shell allocates action
         * ids from a counter that starts over with it, so the ids in a reply
         * from one that has since been replaced name the grabs the shell now on
         * the name has just granted us -- handing those back would release the
         * very accelerators the regrab that replacement triggered asked for.
         * What the old shell held went with it, and needs no releasing.
         * Which shell that is comes from the watch that drives the regrab and
         * from nowhere else: the proxy tracks the name owner under a
         * subscription of its own, dispatched independently of that one, so a
         * grab issued from the appeared handler can read an owner the proxy has
         * yet to catch up with -- and the reply, by which time it has, would
         * then be taken for a stale one and its grabs left held for good. */
        if (ctx->shell_epoch == priv->shell_epoch)
            gnome_shell_client_ungrab_actions (ctx->client, granted, n_granted);

        /* The shell answered, so the interface it was missing is up: the budget
         * belongs to the next grab, stale as this reply is. */
        if (!error)
            priv->retries = 0;
    }
    else if (error)
    {
        if (g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD) && priv->retries < 10)
        {
            /* The retry source outlives the callback that queues it, and its
             * data is this client: a reference of its own would put dispose ()
             * -- the one place that cancels the source -- out of reach, and the
             * client would grab for a provider nobody holds any more. */
            ++priv->retries;
            g_clear_handle_id (&priv->retry_source, g_source_remove);
            priv->retry_source = g_timeout_add_seconds_full (G_PRIORITY_DEFAULT, 1, retry_grab_all,
                                                             g_paste_weak_ref_new (ctx->client), g_paste_weak_ref_free);
            g_source_set_name_by_id (priv->retry_source, "[GPaste] gnome-shell grab retry");
        }
        else
        {
            priv->retries = 0;
            g_warning ("Couldn't grab keybindings with gnome-shell: %s", error->message);
        }
    }
    else
    {
        priv->retries = 0;

        gsize n_ids = g_strv_length (ctx->ids);

        if (n_actions != n_ids)
        {
            g_warning ("gnome-shell answered with %" G_GUINT64_FORMAT " action ids for %" G_GSIZE_FORMAT " accelerators",
                       n_actions, n_ids);
        }

        /* Ids granted past the accelerators we asked about map to nothing we
         * could ever look up, so hand them straight back: dropping them leaves
         * them grabbed for the rest of the session. */
        g_autofree guint32 *surplus = g_new (guint32, (n_actions > n_ids) ? n_actions - n_ids : 0);
        gsize n_surplus = 0;

        for (guint64 i = 0; i < n_actions; i++)
        {
            if (i >= n_ids)
                surplus[n_surplus++] = actions[i];
            else if (!actions[i])
            {
                /* The shell answers with no action at all for an accelerator it
                 * refused -- one another application already holds, or one it
                 * could not parse. Recording that claims a grab we don't have. */
                g_warning ("gnome-shell refused to grab the accelerator for %s", ctx->ids[i]);
            }
            else
            {
                g_hash_table_insert (priv->id_to_action,
                                     g_strdup (ctx->ids[i]),
                                     GUINT_TO_POINTER (actions[i]));
            }
        }

        gnome_shell_client_ungrab_actions (ctx->client, surplus, n_surplus);
    }

    /* A grab that came in while this call was out has been waiting for it: the
     * ids the shell was about to grant have just been handed back, and the
     * UngrabAccelerators carrying them went out on this same connection ahead
     * of the GrabAccelerators below, so the shell has released them by the time
     * it looks at the set we are asking for.
     * A reply from a call the shell going away abandoned is not the one it was
     * waiting for: the grab issued to the replacement is still out, and the
     * pending one would only be queued behind it again. */
    if (priv->pending_grab && !priv->in_flight)
    {
        priv->pending_grab = FALSE;
        gnome_shell_client_regrab_stored (ctx->client);
    }

    g_strfreev (ctx->ids);
    g_object_unref (ctx->client);
    g_free (ctx);
}

/* Ask the shell for the set we have stored, and remember which call carries it:
 * only that call's own reply says the client is free to issue another.
 * Nothing waits on that call: on_shell_vanished () abandons it outright, and the
 * reply -- stale by then -- still frees its own context and hands back whatever
 * the shell granted. */
static void
gnome_shell_client_issue_grab (GPasteGnomeShellClient *self)
{
    GPasteGnomeShellClientPrivate *priv = g_paste_gnome_shell_client_get_instance_private (self);

    /* Nothing stored is nothing to ask for. Only ungrab_all () leaves it that
     * way, and it cancels the retry that would come back through here, but the
     * set below is read straight off rather than checked. */
    if (!priv->stored)
        return;

    gsize n = g_paste_keybinding_accelerators_length (priv->stored);

    g_autofree GPasteGnomeShellAccelerator *shell_accels = g_new (GPasteGnomeShellAccelerator, n + 1);
    /* The ids alone: they are what the reply's action ids are recorded under,
     * and the accelerators go out in the call itself. */
    g_autoptr (GStrvBuilder) ids = g_strv_builder_new ();
    for (gsize i = 0; i < n; i++)
    {
        shell_accels[i] = G_PASTE_GNOME_SHELL_ACCELERATOR (priv->stored[i].accelerator);
        g_strv_builder_add (ids, priv->stored[i].id);
    }
    shell_accels[n].accelerator = NULL;

    _GrabAllContext *ctx = g_new (_GrabAllContext, 1);
    ctx->client = g_object_ref (self);
    ctx->ids = g_strv_builder_end (ids);
    ctx->generation = priv->generation;
    ctx->shell_epoch = priv->shell_epoch;
    ctx->call_id = priv->in_flight = ++priv->call_count;

    g_paste_gnome_shell_client_grab_accelerators (self, shell_accels, grab_all_cb, ctx);
}

/* Invalidate the grab still in flight, hand back what we hold, and ask the shell
 * for the set we have stored: the tail every way into the grab shares, whether
 * the set was just handed to us or is the one we were already holding. */
static void
gnome_shell_client_restart_grab (GPasteGnomeShellClient *self)
{
    GPasteGnomeShellClientPrivate *priv = g_paste_gnome_shell_client_get_instance_private (self);

    g_clear_handle_id (&priv->retry_source, g_source_remove);

    ++priv->generation;
    gnome_shell_client_release_grabs (self);

    /* One GrabAccelerators at a time. The shell refuses an accelerator that is
     * already grabbed -- by us just as much as by anyone else -- with an action
     * id of 0, and the grabs a call it has yet to answer is about to be granted
     * are ones release_grabs () cannot hand back yet, since only that answer
     * carries their ids. Asking for the same set again before it lands would
     * collide with it accelerator for accelerator and come back all zeros,
     * leaving no global shortcut at all until the next rebind. Wait for the
     * reply instead: it hands its own ids back, and grab_all_cb () then asks
     * for the set we have stored. */
    if (priv->in_flight)
    {
        priv->pending_grab = TRUE;
        return;
    }

    gnome_shell_client_issue_grab (self);
}

static void
gnome_shell_client_ungrab_all (GPasteKeybindingProvider *provider)
{
    GPasteGnomeShellClient *self = G_PASTE_GNOME_SHELL_CLIENT (provider);
    GPasteGnomeShellClientPrivate *priv = g_paste_gnome_shell_client_get_instance_private (self);

    g_clear_handle_id (&priv->retry_source, g_source_remove);

    ++priv->generation;
    gnome_shell_client_release_grabs (self);

    g_clear_pointer (&priv->stored, g_paste_keybinding_accelerators_free);
    priv->retries = 0;
    priv->pending_grab = FALSE;
}

static void
gnome_shell_client_grab_all (GPasteKeybindingProvider          *provider,
                              const GPasteKeybindingAccelerator *accels)
{
    GPasteGnomeShellClient *self = G_PASTE_GNOME_SHELL_CLIENT (provider);
    GPasteGnomeShellClientPrivate *priv = g_paste_gnome_shell_client_get_instance_private (self);

    /* Safe to call on a disposed client, as the portal provider is: the action
     * table is then %NULL, and dispose () has already handed back every grab it
     * held. Going on would ask the shell for grabs grab_all_cb () has nowhere
     * left to record, and nothing else would ever release them. */
    if (!priv->id_to_action)
        return;

    /* The complete set we hold, or the one the call still out is about to bring:
     * there is nothing to do. Retry a partially held set too, since a shortcut
     * the shell refused may now be available after its conflict was resolved. */
    if (((priv->stored && g_hash_table_size (priv->id_to_action) == g_paste_keybinding_accelerators_length (priv->stored)) ||
         priv->in_flight) &&
        g_paste_keybinding_accelerators_match (priv->stored, accels))
        return;

    gsize n = g_paste_keybinding_accelerators_length (accels);

    /* grab_all () replaces the registered set, an empty @accels included, and
     * registering an empty one leaves nothing held and nothing stored -- what
     * ungrab_all () does, down to the retry budget and the pending grab. */
    if (!n)
    {
        gnome_shell_client_ungrab_all (provider);
        return;
    }

    /* Copy the set before letting go of the one we hold: @accels is the
     * caller's, and nothing says it was not built from what is about to be
     * freed. Nothing in the tree hands us our own storage back --
     * regrab_stored () asks for the set we hold without going through here --
     * but this is the one place where that is not ours to know. */
    GPasteKeybindingAccelerator *stored = g_paste_keybinding_accelerators_copy (accels);

    g_paste_keybinding_accelerators_free (priv->stored);
    priv->stored = stored;

    gnome_shell_client_restart_grab (self);
}

/* Ask for the set we are already holding on to again: only the shell's answer
 * was lost, to an interface that was not up yet or to a restart. */
static void
gnome_shell_client_regrab_stored (GPasteGnomeShellClient *self)
{
    GPasteGnomeShellClientPrivate *priv = g_paste_gnome_shell_client_get_instance_private (self);

    /* Safe to call on a disposed client, as grab_all () is: the action table is
     * then %NULL, and there is nowhere left to record what the shell grants. */
    if (!priv->id_to_action || !priv->stored)
        return;

    /* Nothing to store: the set asked for is the one stored already, and going
     * through grab_all () would copy it over itself, string by string, on every
     * retry and every shell restart. */
    gnome_shell_client_restart_grab (self);
}

static gboolean
retry_grab_all (gpointer user_data)
{
    g_autoptr (GPasteGnomeShellClient) self = g_weak_ref_get (user_data);

    if (!self)
        return G_SOURCE_REMOVE;

    GPasteGnomeShellClientPrivate *priv = g_paste_gnome_shell_client_get_instance_private (self);

    priv->retry_source = 0;

    gnome_shell_client_regrab_stored (self);

    return G_SOURCE_REMOVE;
}

static void
gnome_shell_client_provider_init (GPasteKeybindingProviderInterface *iface)
{
    iface->grab_all   = gnome_shell_client_grab_all;
    iface->ungrab_all = gnome_shell_client_ungrab_all;
}

/****************************/
/* Shell watch / D-Bus sig  */
/****************************/

static void
on_shell_appeared (GDBusConnection *connection G_GNUC_UNUSED,
                   const gchar     *name       G_GNUC_UNUSED,
                   const gchar     *name_owner G_GNUC_UNUSED,
                   gpointer         user_data)
{
    gnome_shell_client_regrab_stored (user_data);
}

/* A shell handing the name straight to its replacement -- what
 * `gnome-shell --replace` does -- is reported as a vanish and an appear all the
 * same: g_bus_watch_name () calls the vanished handler for any old owner that
 * is not empty, before the appeared one. So everything the shell that left was
 * holding for us goes here, and the regrab that follows is the replacement's. */
static void
on_shell_vanished (GDBusConnection *connection G_GNUC_UNUSED,
                   const gchar     *name       G_GNUC_UNUSED,
                   gpointer         user_data)
{
    GPasteGnomeShellClient *self = user_data;
    GPasteGnomeShellClientPrivate *priv = g_paste_gnome_shell_client_get_instance_private (self);

    /* Whatever was waiting for a reply from the shell that just went away is
     * waiting for nothing: on_shell_appeared () asks for the stored set again.
     * The call in flight is abandoned rather than waited out -- a shell that was
     * replaced while it was outstanding may never answer it, and the replacement
     * would be asked for nothing at all until the 25 s D-Bus timeout said so.
     * The reply, should it still come, no longer carries the id of the call in
     * flight and so cannot report the one issued since as done. */
    priv->pending_grab = FALSE;
    priv->in_flight = 0;

    /* A retry queued against the shell that just went away would fire at a dead
     * name, and the grab on_shell_appeared () issues when it comes back is a new
     * attempt that deserves the full budget: carrying the counter over means a
     * login slow enough to burn the ten retries leaves every later restart of
     * the shell with no global shortcut at all, and only a warning to say so. */
    g_clear_handle_id (&priv->retry_source, g_source_remove);
    priv->retries = 0;

    /* The shell that granted whatever a reply still out is carrying is this one:
     * a reply that lands after this names grabs nobody holds any more, and the
     * action ids in it belong to a counter that starts over with the
     * replacement. */
    ++priv->shell_epoch;

    ++priv->generation;
    g_hash_table_remove_all (priv->id_to_action);
}

static void
g_paste_gnome_shell_client_g_signal (GDBusProxy  *proxy,
                                     const gchar *sender_name G_GNUC_UNUSED,
                                     const gchar *signal_name,
                                     GVariant    *parameters)
{
    GPasteGnomeShellClient *self = G_PASTE_GNOME_SHELL_CLIENT (proxy);

    if (g_paste_str_equal (signal_name, G_PASTE_GNOME_SHELL_SIG_ACCELERATOR_ACTIVATED))
    {
        GVariantIter params_iter;
        g_variant_iter_init (&params_iter, parameters);
        g_autoptr (GVariant) action_v = g_variant_iter_next_value (&params_iter);
        G_GNUC_UNUSED g_autoptr (GVariant) params = g_variant_iter_next_value (&params_iter);
        guint32 action = g_variant_get_uint32 (action_v);

        g_signal_emit (self,
                       signals[ACCELERATOR_ACTIVATED],
                       0, /* detail */
                       action,
                       NULL);

        GPasteGnomeShellClientPrivate *priv = g_paste_gnome_shell_client_get_instance_private (self);
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init (&iter, priv->id_to_action);
        while (g_hash_table_iter_next (&iter, &key, &value))
        {
            if (GPOINTER_TO_UINT (value) == action)
            {
                g_paste_keybinding_provider_emit_keybinding_activated (G_PASTE_KEYBINDING_PROVIDER (self), key);
                break;
            }
        }
    }
}

static void
g_paste_gnome_shell_client_dispose (GObject *object)
{
    GPasteGnomeShellClient *self = G_PASTE_GNOME_SHELL_CLIENT (object);
    GPasteGnomeShellClientPrivate *priv = g_paste_gnome_shell_client_get_instance_private (self);

    g_clear_handle_id (&priv->retry_source, g_source_remove);

    /* A reply still in flight is stale from here and hands itself back; the
     * grabs we already hold have nobody else left to release them. */
    ++priv->generation;
    gnome_shell_client_release_grabs (self);

    g_clear_handle_id (&priv->shell_watch, g_bus_unwatch_name);

    g_clear_pointer (&priv->id_to_action, g_hash_table_unref);
    g_clear_pointer (&priv->stored, g_paste_keybinding_accelerators_free);

    G_OBJECT_CLASS (g_paste_gnome_shell_client_parent_class)->dispose (object);
}

static void
g_paste_gnome_shell_client_class_init (GPasteGnomeShellClientClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_gnome_shell_client_dispose;
    G_DBUS_PROXY_CLASS (klass)->g_signal = g_paste_gnome_shell_client_g_signal;

    /**
     * GPasteGnomeShellClient::accelerator-activated:
     * @gnome_shell: the object on which the signal was emitted
     * @id: the id of the activated accelerator
     *
     * The "accelerator-activated" signal is emitted when gnome-shell notifies us
     * that an accelerator has been pressed.
     */
    signals[ACCELERATOR_ACTIVATED] = g_signal_new ("accelerator-activated",
                                                   G_PASTE_TYPE_GNOME_SHELL_CLIENT,
                                                   G_SIGNAL_RUN_LAST,
                                                   0, /* class offset */
                                                   NULL, /* accumulator */
                                                   NULL, /* accumulator data */
                                                   g_cclosure_marshal_VOID__UINT,
                                                   G_TYPE_NONE,
                                                   1,
                                                   G_TYPE_UINT);
}

static void
g_paste_gnome_shell_client_init (GPasteGnomeShellClient *self)
{
    GDBusProxy *proxy = G_DBUS_PROXY (self);
    g_autoptr (GError) error = NULL;
    g_autoptr (GDBusNodeInfo) gnome_shell_dbus_info = g_dbus_node_info_new_for_xml (G_PASTE_GNOME_SHELL_INTERFACE,
                                                                                    &error);
    g_assert_no_error (error);

    g_dbus_proxy_set_interface_info (proxy, gnome_shell_dbus_info->interfaces[0]);

    GPasteGnomeShellClientPrivate *priv = g_paste_gnome_shell_client_get_instance_private (self);

    priv->id_to_action = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    priv->stored = NULL;
    priv->generation = 0;
    priv->shell_epoch = 0;
    priv->call_count = 0;
    priv->in_flight = 0;
    priv->pending_grab = FALSE;
    priv->retries = 0;
    priv->retry_source = 0;
    priv->shell_watch = g_bus_watch_name (G_BUS_TYPE_SESSION,
                                          G_PASTE_GNOME_SHELL_BUS_NAME,
                                          G_BUS_NAME_WATCHER_FLAGS_NONE,
                                          on_shell_appeared,
                                          on_shell_vanished,
                                          self,
                                          NULL);
}

/**
 * g_paste_gnome_shell_client_new_sync:
 * @error: Return location for error or %NULL.
 *
 * Create a new instance of #GPasteGnomeShellClient
 *
 * Returns: a newly allocated #GPasteGnomeShellClient
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteGnomeShellClient *
g_paste_gnome_shell_client_new_sync (GError **error)
{
    CUSTOM_PROXY_NEW (GNOME_SHELL_CLIENT, GNOME_SHELL, G_PASTE_GNOME_SHELL_BUS_NAME);
}

/**
 * g_paste_gnome_shell_client_new:
 * @callback: Callback function to invoke when the proxy is ready.
 * @user_data: User data to pass to @callback.
 *
 * Create a new instance of #GPasteGnomeShellClient
 */
G_PASTE_VISIBLE void
g_paste_gnome_shell_client_new (GAsyncReadyCallback callback,
                                gpointer            user_data)
{
    CUSTOM_PROXY_NEW_ASYNC (GNOME_SHELL_CLIENT, GNOME_SHELL, G_PASTE_GNOME_SHELL_BUS_NAME);
}

/**
 * g_paste_gnome_shell_client_new_finsh:
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback function passed to the async ctor.
 * @error: Return location for error or %NULL.
 *
 * Create a new instance of #GPasteGnomeShellClient
 *
 * Returns: a newly allocated #GPasteGnomeShellClient
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteGnomeShellClient *
g_paste_gnome_shell_client_new_finish (GAsyncResult *result,
                                       GError      **error)
{
    CUSTOM_PROXY_NEW_FINISH (GNOME_SHELL_CLIENT);
}
