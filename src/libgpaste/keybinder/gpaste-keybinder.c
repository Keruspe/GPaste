/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-keybinder.h>

#ifdef GDK_WINDOWING_WAYLAND
#  include <gdk/gdkwayland.h>
#endif
#if defined(ENABLE_X_KEYBINDER) && defined (GDK_WINDOWING_X11)
#  include <gdk/gdkx.h>
#  include <X11/extensions/XInput2.h>
#endif

#define MAX_BINDINGS 7

struct _GPasteKeybinder
{
    GObject parent_instance;
};

enum
{
    C_ACCEL,

    C_LAST_SIGNAL
};

typedef struct
{
    GSList                 *keybindings;

    GPasteSettings         *settings;
    GPasteGnomeShellClient *shell_client;
    gboolean                grabbing;
    guint64                 retries;

    guint64                 shell_watch;

    guint64                 c_signals[C_LAST_SIGNAL];
} GPasteKeybinderPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (Keybinder, keybinder, G_TYPE_OBJECT)

/***************************/
/* Internal grabbing stuff */
/***************************/

#ifdef GDK_WINDOWING_WAYLAND
static void
g_paste_keybinder_change_grab_wayland (void)
{
    g_warning ("Wayland hotkeys are currently not supported outside of gnome-shell.");
}
#endif

#if defined(ENABLE_X_KEYBINDER) && defined (GDK_WINDOWING_X11)
static void
g_paste_keybinder_change_grab_x11 (GPasteKeybinding *binding,
                                   GdkDisplay       *display,
                                   gboolean          grab)
{
    if (!g_paste_keybinding_is_active (binding))
        return;

    guchar mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
    XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };
    Display *xdisplay = GDK_DISPLAY_XDISPLAY (display);

    XISetMask (mask.mask, XI_KeyPress);

    gdk_x11_display_error_trap_push (display);

    guint64 mod_masks [] = {
        0, /* modifier only */
        GDK_MOD2_MASK, /* NumLock */
        GDK_MOD5_MASK, /* ScrollLock */
        GDK_LOCK_MASK, /* CapsLock */
        GDK_MOD2_MASK | GDK_MOD5_MASK,
        GDK_MOD2_MASK | GDK_LOCK_MASK,
        GDK_MOD5_MASK | GDK_LOCK_MASK,
        GDK_MOD2_MASK | GDK_MOD5_MASK | GDK_LOCK_MASK,
    };

    Window window = GDK_ROOT_WINDOW ();
    GdkModifierType modifiers = g_paste_keybinding_get_modifiers (binding);
    const guint32 *keycodes = g_paste_keybinding_get_keycodes (binding);

    for (guint64 i = 0; i < G_N_ELEMENTS (mod_masks); ++i) {
        XIGrabModifiers mods = { mod_masks[i] | modifiers, 0 };
        for (const guint32 *keycode = keycodes; *keycode; ++keycode)
        {
            if (grab)
            {
                XIGrabKeycode (xdisplay,
                               XIAllMasterDevices,
                               *keycode,
                               window,
                               XIGrabModeSync,
                               XIGrabModeAsync,
                               False,
                               &mask,
                               1,
                               &mods);
            }
            else
            {
                XIUngrabKeycode (xdisplay,
                                 XIAllMasterDevices,
                                 *keycode,
                                 window,
                                 1,
                                 &mods);
            }
        }
    }

    gdk_display_flush (display);
    gdk_x11_display_error_trap_pop_ignored (display);
}
#endif

static void
g_paste_keybinder_change_grab_internal (GPasteKeybinding *binding,
                                        gboolean          grab)
{
    GdkDisplay *display = gdk_display_get_default ();;

#ifdef GDK_WINDOWING_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY (display))
        g_paste_keybinder_change_grab_wayland ();
    else
#endif
#if defined(ENABLE_X_KEYBINDER) && defined (GDK_WINDOWING_X11)
    if (GDK_IS_X11_DISPLAY (display))
        g_paste_keybinder_change_grab_x11 (binding, display, grab);
    else
#endif
        g_warning ("Unsupported GDK backend, keybinder won't work.");
}

/***********************************/
/* Wrapper around GPasteKeybinding */
/***********************************/

enum
{
    C_K_REBIND,

    C_K_LAST_SIGNAL
};

typedef struct
{
    GPasteKeybinding       *binding;
    GPasteSettings         *settings;
    GPasteGnomeShellClient *shell_client;

    guint32                 action;

    guint64                 c_signals[C_K_LAST_SIGNAL];
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

static void
on_key_grabbed (GObject      *source_object,
                GAsyncResult *res,
                gpointer      user_data)
{
    _Keybinding *k = user_data;
    g_autoptr (GError) error = NULL;

    k->action = g_paste_gnome_shell_client_grab_accelerator_finish (G_PASTE_GNOME_SHELL_CLIENT (source_object), res, &error);

    if (error)
        g_warning ("Couldn't grab keybinding: %s", error->message);
}

static void
_keybinding_grab_gnome_shell (_Keybinding *k)
{
    if (k->action || !k->shell_client)
        return;

    GPasteGnomeShellAccelerator accel = G_PASTE_GNOME_SHELL_ACCELERATOR (g_paste_keybinding_get_accelerator (k->binding, k->settings));

    g_paste_gnome_shell_client_grab_accelerator (k->shell_client, accel, on_key_grabbed, k);
}

static void
_keybinding_grab (_Keybinding *k)
{
    _keybinding_activate (k);
    if (k->shell_client)
        _keybinding_grab_gnome_shell (k);
    else
        g_paste_keybinder_change_grab_internal (k->binding, TRUE);
}

static void
on_key_ungrabbed (GObject      *source_object,
                  GAsyncResult *res,
                  gpointer      user_data G_GNUC_UNUSED)
{
    g_autoptr (GError) error = NULL;

    g_paste_gnome_shell_client_ungrab_accelerator_finish (G_PASTE_GNOME_SHELL_CLIENT (source_object), res, &error);

    if (error)
        g_warning ("Couldn't ungrab keybinding: %s", error->message);
}

static void
_keybinding_ungrab_gnome_shell (_Keybinding *k)
{
    if (!k->action || !k->shell_client)
        return;

    g_paste_gnome_shell_client_ungrab_accelerator (k->shell_client, k->action, on_key_ungrabbed, NULL);
    k->action = 0;
}

static void
_keybinding_ungrab (_Keybinding *k)
{
    if (k->shell_client)
        _keybinding_ungrab_gnome_shell (k);
    else
        g_paste_keybinder_change_grab_internal (k->binding, FALSE);

    _keybinding_deactivate (k);
}

static void
_keybinding_rebind (_Keybinding    *k,
                    GPasteSettings *setting G_GNUC_UNUSED)
{
    _keybinding_ungrab (k);
    _keybinding_grab (k);
}

static _Keybinding *
_keybinding_new (GPasteKeybinding       *binding,
                 GPasteSettings         *settings,
                 GPasteGnomeShellClient *shell_client)
{
    _Keybinding *k = g_new (_Keybinding, 1);

    k->binding = binding;
    k->settings = g_object_ref (settings);
    k->shell_client = (shell_client) ? g_object_ref (shell_client) : NULL;

    k->action = 0;

    g_autofree gchar *detailed_signal = g_strdup_printf ("rebind::%s", g_paste_keybinding_get_dconf_key (binding));

    k->c_signals[C_K_REBIND] = g_signal_connect_swapped (settings,
                                                         detailed_signal,
                                                         G_CALLBACK (_keybinding_rebind),
                                                         k);
    return k;
}

static void
_keybinding_cleanup_shell_stuff (gpointer data,
                                 gpointer user_data G_GNUC_UNUSED)
{
    _Keybinding *k = data;
    k->action = 0;
}

static void
_keybinding_free (gpointer data,
                  gpointer user_data G_GNUC_UNUSED)
{
    _Keybinding *k = data;
    _keybinding_ungrab (k);
    g_signal_handler_disconnect (k->settings, k->c_signals[C_K_REBIND]);
    g_object_unref (k->binding);
    g_object_unref (k->settings);
    g_clear_object (&k->shell_client);
    g_free (k);
}

#define GET_BINDING(k) ((_Keybinding *) k)->binding

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
    g_return_if_fail (_G_PASTE_IS_KEYBINDER (self));
    g_return_if_fail (_G_PASTE_IS_KEYBINDING (binding));

    GPasteKeybinderPrivate *priv = g_paste_keybinder_get_instance_private (self);

    priv->keybindings = g_slist_prepend (priv->keybindings,
                                         _keybinding_new (binding, priv->settings, priv->shell_client));
}

static void
g_paste_keybinder_grab_keybinding_func (gpointer data,
                                        gpointer user_data G_GNUC_UNUSED)
{
    _keybinding_grab (data);
}

static void
g_paste_keybinder_private_grab_all (GPasteKeybinderPrivate *priv)
{
    if (!priv->grabbing)
    {
        g_slist_foreach (priv->keybindings,
                         g_paste_keybinder_grab_keybinding_func,
                         NULL);
    }
}

static void
g_paste_keybinder_activate_keybinding_func (gpointer data,
                                            gpointer user_data G_GNUC_UNUSED)
{
    _keybinding_activate (data);
}

static void g_paste_keybinder_private_grab_all_gnome_shell (GPasteKeybinderPrivate *priv);

static gboolean
retry_grab_all_gnome_shell (gpointer user_data)
{
    g_paste_keybinder_private_grab_all_gnome_shell (user_data);

    return G_SOURCE_REMOVE;
}

static void
grab_accelerators_cb (GObject      *source_object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
    GPasteKeybinderPrivate *priv = user_data;
    g_autoptr (GError) error = NULL;
    g_autofree guint32 *actions = g_paste_gnome_shell_client_grab_accelerators_finish (G_PASTE_GNOME_SHELL_CLIENT (source_object), res, &error);

    if (error)
    {
        if (error->code == 19 && priv->retries < 10)
        {
            ++priv->retries;
            g_source_set_name_by_id (g_timeout_add_seconds (1, retry_grab_all_gnome_shell, priv), "[GPaste] gnome-shell grab");
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

        guint64 index = 0;
        for (GSList *binding = priv->keybindings; binding; binding = g_slist_next (binding))
        {
            _Keybinding *k = binding->data;
            if (g_paste_keybinding_is_active (k->binding))
                k->action = actions[index++];
        }
    }

    priv->grabbing = FALSE;
}

static void
g_paste_keybinder_private_grab_all_gnome_shell (GPasteKeybinderPrivate *priv)
{
    if (priv->grabbing || !priv->shell_client)
        return;

    priv->grabbing = TRUE;

    GPasteGnomeShellAccelerator accels[MAX_BINDINGS + 1];
    GPasteSettings *settings = priv->settings;

    guint64 index = 0;
    for (GSList *binding = priv->keybindings; binding && index < MAX_BINDINGS; binding = g_slist_next (binding))
    {
        _Keybinding *k = binding->data;
        GPasteKeybinding *keybinding = k->binding;
        if (!k->action && g_paste_keybinding_is_active (keybinding))
            accels[index++] = G_PASTE_GNOME_SHELL_ACCELERATOR (g_paste_keybinding_get_accelerator (keybinding, settings));
    }
    accels[index].accelerator = NULL;

    if (index)
    {
        g_paste_gnome_shell_client_grab_accelerators (priv->shell_client,
                                                      accels,
                                                      grab_accelerators_cb,
                                                      priv);
    }
    else
        priv->grabbing = FALSE;
}

/**
 * g_paste_keybinder_activate_all:
 * @self: a #GPasteKeybinder instance
 *
 * Activate all the managed keybindings
 */
G_PASTE_VISIBLE void
g_paste_keybinder_activate_all (GPasteKeybinder *self)
{
    g_return_if_fail (_G_PASTE_IS_KEYBINDER (self));

    GPasteKeybinderPrivate *priv = g_paste_keybinder_get_instance_private (self);

    if (priv->shell_client)
    {
        g_slist_foreach (priv->keybindings,
                         g_paste_keybinder_activate_keybinding_func,
                         NULL);
        g_paste_keybinder_private_grab_all_gnome_shell (priv);
    }
    else
        g_paste_keybinder_private_grab_all (priv);
}

static void
g_paste_keybinder_deactivate_keybinding_func (gpointer data,
                                              gpointer user_data G_GNUC_UNUSED)
{
    _keybinding_ungrab (data);
}

/**
 * g_paste_keybinder_deactivate_all:
 * @self: a #GPasteKeybinder instance
 *
 * Deactivate all the managed keybindings
 */
G_PASTE_VISIBLE void
g_paste_keybinder_deactivate_all (GPasteKeybinder *self)
{
    g_return_if_fail (_G_PASTE_IS_KEYBINDER (self));

    const GPasteKeybinderPrivate *priv = _g_paste_keybinder_get_instance_private (self);

    g_slist_foreach (priv->keybindings,
                     g_paste_keybinder_deactivate_keybinding_func,
                     NULL);
}

#ifdef GDK_WINDOWING_WAYLAND
static void
g_paste_keybinder_parse_event_wayland (void)
{
    g_warning ("Wayland hotkeys are currently not supported outside of gnome-shell.");
}
#endif

#if defined(ENABLE_X_KEYBINDER) && defined (GDK_WINDOWING_X11)
static gint
g_paste_keybinder_get_xinput_opcode (Display *display)
{
    static gint32 xinput_opcode = 0;

    if (!xinput_opcode)
    {
        gint32 major = 2, minor = 3;
        gint32 xinput_error_base;
        gint32 xinput_event_base;

        if (XQueryExtension (display,
                             "XInputExtension",
                             &xinput_opcode,
                             &xinput_error_base,
                             &xinput_event_base))
        {
            if (XIQueryVersion (display, &major, &minor) != Success)
                g_warning ("XInput 2 not found, keybinder won't work");
        }
    }

    return xinput_opcode;
}

static void
g_paste_keybinder_parse_event_x11 (XEvent                  *event,
                                   GdkModifierType         *modifiers,
                                   guint64                 *keycode)
{
    XGenericEventCookie cookie = event->xcookie;

    if (cookie.extension == g_paste_keybinder_get_xinput_opcode (NULL))
    {
        XIDeviceEvent *xi_ev = (XIDeviceEvent *) cookie.data;

        if (xi_ev->evtype == XI_KeyPress)
        {
            *modifiers = xi_ev->mods.effective;
            *keycode = xi_ev->detail;
        }
    }
}
#endif

static GdkFilterReturn
g_paste_keybinder_filter (GdkXEvent *xevent,
                          GdkEvent  *event G_GNUC_UNUSED,
                          gpointer   data)
{
    GPasteKeybinderPrivate *priv = data;
    GdkDisplay *display = gdk_display_get_default ();

    for (GList *_seat = gdk_display_list_seats (display); _seat; _seat = g_list_next (_seat))
    {
        GdkSeat *seat = _seat->data;

        if (gdk_seat_get_keyboard (seat))
            gdk_seat_ungrab (seat);
    }

    gdk_display_flush (display);

    GdkModifierType modifiers = 0;
    guint64 keycode = 0;

#ifdef GDK_WINDOWING_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY (display))
        g_paste_keybinder_parse_event_wayland ();
    else
#endif
#if defined(ENABLE_X_KEYBINDER) && defined (GDK_WINDOWING_X11)
    if (GDK_IS_X11_DISPLAY (display))
        g_paste_keybinder_parse_event_x11 ((XEvent *) xevent, &modifiers, &keycode);
    else
#endif
        g_warning ("Unsupported GDK backend, keybinder won't work.");

    for (GSList *keybinding = priv->keybindings; keybinding; keybinding = g_slist_next (keybinding))
    {
        GPasteKeybinding *real_keybinding = GET_BINDING (keybinding->data);
        if (g_paste_keybinding_is_active (real_keybinding))
            g_paste_keybinding_notify (real_keybinding, modifiers, keycode);
    }

    return GDK_FILTER_CONTINUE;
}

static void
on_accelerator_activated (GPasteGnomeShellClient *client    G_GNUC_UNUSED,
                          guint32                 action,
                          gpointer                user_data)
{
    GPasteKeybinderPrivate *priv = user_data;

    for (GSList *binding = priv->keybindings; binding; binding = g_slist_next (binding))
    {
        _Keybinding *k = binding->data;

        if (action == k->action)
        {
            GPasteKeybinding *keybinding = k->binding;
            if (g_paste_keybinding_is_active (keybinding))
                g_paste_keybinding_perform (keybinding);
            return;
        }
    }
}

static void
on_shell_appeared (GDBusConnection *connection G_GNUC_UNUSED,
                   const gchar     *name       G_GNUC_UNUSED,
                   const gchar     *name_owner G_GNUC_UNUSED,
                   gpointer         user_data)
{
    g_paste_keybinder_private_grab_all_gnome_shell (user_data);
}

static void
g_paste_keybinder_private_cleanup_shell_stuff (GPasteKeybinderPrivate *priv)
{
    g_slist_foreach (priv->keybindings, _keybinding_cleanup_shell_stuff, NULL);
}

static void
on_shell_vanished (GDBusConnection *connection G_GNUC_UNUSED,
                   const gchar     *name       G_GNUC_UNUSED,
                   gpointer         user_data)
{
    GPasteKeybinderPrivate *priv = user_data;

    g_paste_keybinder_private_cleanup_shell_stuff (priv);
    priv->grabbing = FALSE;
}

static void
g_paste_keybinder_dispose (GObject *object)
{
    GPasteKeybinder *self = G_PASTE_KEYBINDER (object);
    GPasteKeybinderPrivate *priv = g_paste_keybinder_get_instance_private (self);

    if (priv->shell_watch)
    {
        g_bus_unwatch_name (priv->shell_watch);
        priv->shell_watch = 0;
    }

    if (priv->shell_client)
    {
        g_signal_handler_disconnect (priv->shell_client, priv->c_signals[C_ACCEL]);
        g_clear_object (&priv->shell_client);
    }

    if (priv->settings)
    {
        g_clear_object (&priv->settings);
        g_paste_keybinder_deactivate_all (self);
        g_slist_foreach (priv->keybindings, _keybinding_free, NULL);
        priv->keybindings = NULL;
    }

    G_OBJECT_CLASS (g_paste_keybinder_parent_class)->dispose (object);
}

static void
g_paste_keybinder_finalize (GObject *object)
{
    GPasteKeybinderPrivate *priv = g_paste_keybinder_get_instance_private (G_PASTE_KEYBINDER (object));

    gdk_window_remove_filter (gdk_get_default_root_window (),
                              g_paste_keybinder_filter,
                              priv);

    G_OBJECT_CLASS (g_paste_keybinder_parent_class)->finalize (object);
}

static void
g_paste_keybinder_class_init (GPasteKeybinderClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_keybinder_dispose;
    object_class->finalize = g_paste_keybinder_finalize;
}

static void
g_paste_keybinder_init (GPasteKeybinder *self)
{
    GPasteKeybinderPrivate *priv = g_paste_keybinder_get_instance_private (self);

    priv->keybindings = NULL;

    gdk_window_add_filter (gdk_get_default_root_window (),
                           g_paste_keybinder_filter,
                           priv);

#if defined(ENABLE_X_KEYBINDER) && defined (GDK_WINDOWING_X11)
    /* Initialize */
    GdkDisplay *display = gdk_display_get_default ();

    if (GDK_IS_X11_DISPLAY (display))
        g_paste_keybinder_get_xinput_opcode (GDK_DISPLAY_XDISPLAY (display));
#endif
}

/**
 * g_paste_keybinder_new:
 * @settings: a #GPasteSettings instance
 * @shell_client: a #GPasteGnomeShellClient instance
 *
 * Create a new instance of #GPasteKeybinder
 *
 * Returns: a newly allocated #GPasteKeybinder
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteKeybinder *
g_paste_keybinder_new (GPasteSettings         *settings,
                       GPasteGnomeShellClient *shell_client)
{
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (!shell_client || _G_PASTE_IS_GNOME_SHELL_CLIENT (shell_client), NULL);

    GPasteKeybinder *self = G_PASTE_KEYBINDER (g_object_new (G_PASTE_TYPE_KEYBINDER, NULL));
    GPasteKeybinderPrivate *priv = g_paste_keybinder_get_instance_private (self);

    priv->settings = g_object_ref (settings);
    priv->shell_client = shell_client = NULL; //(shell_client) ? g_object_ref (shell_client) : NULL;
    priv->grabbing = FALSE;
    priv->retries = 0;

    if (shell_client)
    {
        priv->c_signals[C_ACCEL] = g_signal_connect (shell_client,
                                                     "accelerator-activated",
                                                     G_CALLBACK (on_accelerator_activated),
                                                     priv);
        priv->shell_watch = g_bus_watch_name (G_BUS_TYPE_SESSION,
                                              G_PASTE_GNOME_SHELL_BUS_NAME,
                                              G_BUS_NAME_WATCHER_FLAGS_NONE,
                                              on_shell_appeared,
                                              on_shell_vanished,
                                              priv,
                                              NULL);
    }
    else
        priv->shell_watch = 0;

    return self;
}
