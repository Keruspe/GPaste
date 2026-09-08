// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-keybinder.h>

/* Long enough to swallow a burst of writes, short enough to go unnoticed. */
#define G_PASTE_KEYBINDER_REBIND_DELAY 250 /* ms */

struct _GPasteKeybinder
{
    GObject parent_instance;

    GHashTable                 *keybindings;  /* const gchar * (borrowed from _Keybinding) → _Keybinding * */

    GPasteSettings             *settings;
    GSignalGroup               *settings_signals;
    GPasteGlobalShortcutClient *provider;
    GSignalGroup               *provider_signals;

    guint                       rebind_source;

    gboolean                    enabled; /* the keybindings-enabled value we last applied */
};

G_PASTE_DEFINE_TYPE (Keybinder, keybinder, G_TYPE_OBJECT)

/***********************************/
/* Wrapper around GPasteKeybinding */
/***********************************/

typedef struct
{
    GPasteKeybinding *binding;
    GPasteSettings   *settings;
} _Keybinding;

static void
_keybinding_activate (_Keybinding *k)
{
    if (!g_paste_keybinding_is_active (k->binding))
        g_paste_keybinding_activate (k->binding, k->settings);
}

static void
_keybinding_deactivate (_Keybinding *k)
{
    if (g_paste_keybinding_is_active (k->binding))
        g_paste_keybinding_deactivate (k->binding);
}

static _Keybinding *
_keybinding_new (GPasteKeybinding *binding,
                 GPasteSettings   *settings)
{
    _Keybinding *k = g_new (_Keybinding, 1);

    k->binding = binding;
    k->settings = g_object_ref (settings);

    return k;
}

static void
_keybinding_free (gpointer data)
{
    _Keybinding *k = data;
    g_object_unref (k->binding);
    g_object_unref (k->settings);
    g_free (k);
}

/* Rebinding comes in bursts: an accelerator typed into the preferences writes
 * its GSettings key on every keystroke, and every write lands here. Coalesce
 * them, or a single edit costs a full ungrab and grab per keystroke -- a D-Bus
 * round trip each -- and reopens the window with nothing grabbed at all in
 * between.
 * The delay is measured from the last write of the burst, not the first: a
 * pending rebind is pushed back rather than left to fire in the middle of the
 * typing, which would cost a rebind per delay instead of one for the whole edit.
 * The source holds the keybinder weakly: dispose () is the one thing that
 * cancels it, and a reference of its own would put that out of reach. */
static gboolean
keybinder_do_rebind_all (gpointer user_data)
{
    g_autoptr (GPasteKeybinder) self = g_weak_ref_get (user_data);

    if (!self)
        return G_SOURCE_REMOVE;

    self->rebind_source = 0;

    g_paste_keybinder_activate_all (self);

    return G_SOURCE_REMOVE;
}

static void
keybinder_rebind_all (GPasteKeybinder *self)
{
    g_clear_handle_id (&self->rebind_source, g_source_remove);

    self->rebind_source = g_timeout_add_full (G_PRIORITY_DEFAULT, G_PASTE_KEYBINDER_REBIND_DELAY,
                                              keybinder_do_rebind_all,
                                              g_paste_weak_ref_new (self), g_paste_weak_ref_free);
    g_source_set_name_by_id (self->rebind_source, "[GPaste] keybindings rebind");
}

/* Any shortcut key change re-applies the whole set (a rebind reconfigures every
 * binding anyway), so one shared "rebind" listener on the settings suffices
 * instead of a GSignalGroup per binding. */
static void
on_setting_rebind (GPasteKeybinder *self,
                   const gchar     *key G_GNUC_UNUSED)
{
    keybinder_rebind_all (self);
}

static void
on_keybindings_enabled_changed (GPasteKeybinder *self,
                                GParamSpec      *pspec G_GNUC_UNUSED)
{
    /* GSettings emits changed for any write, even one that sets the value it
     * already holds: only rebind when the value we last applied no longer
     * matches. A switch flipped and flipped back inside the burst leaves the
     * rebind the first write armed standing, and it costs nothing: the set it
     * hands over is the one the provider is already holding, and it recognises
     * it and stays put. */
    if (g_paste_settings_get_keybindings_enabled (self->settings) == self->enabled)
        return;

    keybinder_rebind_all (self);
}

/**
 * g_paste_keybinder_add_keybinding:
 * @self: a #GPasteKeybinder instance
 * @binding: (transfer full): a #GPasteKeybinding instance
 *
 * Add a new keybinding
 */
G_PASTE_VISIBLE void
g_paste_keybinder_add_keybinding (GPasteKeybinder  *self,
                                  GPasteKeybinding *binding)
{
    g_return_if_fail (G_PASTE_IS_KEYBINDER (self));
    g_return_if_fail (G_PASTE_IS_KEYBINDING (binding));

    g_hash_table_insert (self->keybindings,
                         (gpointer) g_paste_keybinding_get_dconf_key (binding),
                         _keybinding_new (binding, self->settings));
}

/**
 * g_paste_keybinder_activate_all:
 * @self: a #GPasteKeybinder instance
 *
 * Activate all the managed keybindings
 *
 * Deactivates them all first, and hands the provider an empty set when the
 * keybindings-enabled setting is off: that master switch means GPaste takes no
 * global grab at all, and listens for nothing on its own either.
 */
G_PASTE_VISIBLE void
g_paste_keybinder_activate_all (GPasteKeybinder *self)
{
    g_return_if_fail (G_PASTE_IS_KEYBINDER (self));

    g_autoptr (GList) values = g_hash_table_get_values (self->keybindings);

    /* The parsed accelerators go first, whatever the master switch says:
     * g_paste_keybinding_activate () caches the keycodes it parsed and
     * _keybinding_activate () skips a binding that is already active, so an
     * edited accelerator only takes effect once its binding has been through
     * here. Only the keybindings go inactive -- the provider keeps what it
     * holds until the set below reaches it, which is what lets it recognise a
     * set that has not changed. Ungrabbing first would take that away, and with
     * it the portal session whose permission the user has already given. */
    for (GList *l = values; l; l = g_list_next (l))
        _keybinding_deactivate (l->data);

    /* Remember the value we are applying rather than whether the provider ended
     * up holding anything: a grab can fail on the far side, and the setting
     * moving away from what we last applied is what a rebind answers to. */
    self->enabled = g_paste_settings_get_keybindings_enabled (self->settings);

    /* Nothing to arm under a master switch that is off, and nothing to hand over
     * but the empty set: the keybindings themselves are what we hold, and they
     * have just been deactivated. */
    const GList *armed = self->enabled ? values : NULL;

    gsize n = 0;
    for (const GList *l = armed; l; l = g_list_next (l))
    {
        _Keybinding *k = l->data;
        _keybinding_activate (k);
        if (g_paste_keybinding_is_active (k->binding))
            n++;
    }

    g_autofree GPasteKeybindingAccelerator *accels = g_new (GPasteKeybindingAccelerator, n + 1);
    gsize i = 0;
    for (const GList *l = armed; l; l = g_list_next (l))
    {
        _Keybinding *k = l->data;
        if (g_paste_keybinding_is_active (k->binding))
        {
            accels[i++] = G_PASTE_KEYBINDING_ACCELERATOR (
                g_paste_keybinding_get_dconf_key (k->binding),
                g_paste_keybinding_get_accelerator (k->binding, k->settings),
                g_paste_keybinding_get_description (k->binding));
        }
    }
    accels[i] = G_PASTE_KEYBINDING_ACCELERATOR (NULL, NULL, NULL);

    /* Handed over in a single call, the empty set of a master switch that is off
     * included: the provider replaces whatever it holds, and one asked to
     * ungrab first would have nothing left to tell a set that has not changed
     * from a new one -- which is what keeps the portal from closing a session
     * only to ask for the same one back, permission dialog and all. */
    g_paste_global_shortcut_client_grab_all (self->provider, accels);
}

static void
on_keybinding_activated (GPasteGlobalShortcutClient *provider G_GNUC_UNUSED,
                         const gchar                *id,
                         gpointer                    user_data)
{
    GPasteKeybinder *self = user_data;
    _Keybinding *k = g_hash_table_lookup (self->keybindings, id);

    if (k && g_paste_keybinding_is_active (k->binding))
        g_paste_keybinding_perform (k->binding);
}

static void
g_paste_keybinder_dispose (GObject *object)
{
    GPasteKeybinder *self = G_PASTE_KEYBINDER (object);

    g_clear_handle_id (&self->rebind_source, g_source_remove);

    if (self->settings)
    {
        g_clear_object (&self->settings_signals);
        g_clear_object (&self->settings);
        g_paste_global_shortcut_client_ungrab_all (self->provider);
        g_clear_pointer (&self->keybindings, g_hash_table_unref);
        g_clear_object (&self->provider_signals);
        g_clear_object (&self->provider);
    }

    G_OBJECT_CLASS (g_paste_keybinder_parent_class)->dispose (object);
}

static void
g_paste_keybinder_class_init (GPasteKeybinderClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_keybinder_dispose;
}

static void
g_paste_keybinder_init (GPasteKeybinder *self)
{
    self->keybindings = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, _keybinding_free);
}

/**
 * g_paste_keybinder_new:
 * @settings: a #GPasteSettings instance
 * @provider: a #GPasteGlobalShortcutClient instance
 *
 * Create a new instance of #GPasteKeybinder
 *
 * Returns: a newly allocated #GPasteKeybinder
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteKeybinder *
g_paste_keybinder_new (GPasteSettings             *settings,
                       GPasteGlobalShortcutClient *provider)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (G_PASTE_IS_GLOBAL_SHORTCUT_CLIENT (provider), NULL);

    GPasteKeybinder *self = G_PASTE_KEYBINDER (g_object_new (G_PASTE_TYPE_KEYBINDER, NULL));

    self->settings = g_object_ref (settings);
    self->provider = g_object_ref (provider);

    /* Seed the value we last applied before anything can notify us about it:
     * zero-initialised it reads as off while the setting defaults to on, and a
     * change landing before activate_all () would compare against that. */
    self->enabled = g_paste_settings_get_keybindings_enabled (settings);

    GSignalGroup *settings_signals = self->settings_signals = g_signal_group_new (G_PASTE_TYPE_SETTINGS);
    g_signal_group_connect_swapped (settings_signals, "rebind", G_CALLBACK (on_setting_rebind), self);
    g_signal_group_connect_swapped (settings_signals,
                                    "notify::" G_PASTE_KEYBINDINGS_ENABLED_SETTING,
                                    G_CALLBACK (on_keybindings_enabled_changed),
                                    self);
    g_signal_group_set_target (settings_signals, settings);

    GSignalGroup *provider_signals = self->provider_signals = g_signal_group_new (G_PASTE_TYPE_GLOBAL_SHORTCUT_CLIENT);
    g_signal_group_connect (provider_signals, "keybinding-activated", G_CALLBACK (on_keybinding_activated), self);
    g_signal_group_set_target (provider_signals, provider);

    return self;
}
