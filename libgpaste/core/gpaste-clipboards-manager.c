/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "gpaste-clipboards-manager-private.h"
#include "gpaste-image-item.h"
#include "gpaste-text-item.h"
#include "gpaste-uris-item.h"

#ifdef GDK_WINDOWING_WAYLAND
#  include <gdk/gdkwayland.h>
#endif
#ifdef GDK_WINDOWING_X11
#  include <gdk/gdkx.h>
#  include <X11/Xatom.h>
#  include <X11/extensions/Xfixes.h>
#endif

struct _GPasteClipboardsManagerPrivate
{
    GSList         *clipboards;
    GPasteHistory  *history;
    GPasteSettings *settings;

    GdkDisplay     *display;
    GdkWindow      *window;

    gulong          selected_signal;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteClipboardsManager, g_paste_clipboards_manager, G_TYPE_OBJECT)

#ifdef GDK_WINDOWING_X11
static gint xfixes_event_base = 0;

static Atom xa_clipboard;
#endif

/**
 * g_paste_clipboards_manager_add_clipboard:
 * @self: a #GPasteClipboardsManager instance
 * @clipboard: (transfer none): the GPasteClipboard to add
 *
 * Add a #GPasteClipboard to the #GPasteClipboardsManager
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_clipboards_manager_add_clipboard (GPasteClipboardsManager *self,
                                          GPasteClipboard         *clipboard)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (self));
    g_return_if_fail (G_PASTE_IS_CLIPBOARD (clipboard));

    GPasteClipboardsManagerPrivate *priv = self->priv;
    GtkClipboard *real = g_paste_clipboard_get_real (clipboard);

    priv->clipboards = g_slist_prepend (priv->clipboards, g_object_ref (clipboard));

    if (gtk_clipboard_wait_is_uris_available (real) ||
        gtk_clipboard_wait_is_text_available (real))
            g_paste_clipboard_set_text (clipboard);
    else if (gtk_clipboard_wait_is_image_available (real))
        g_paste_clipboard_set_image (clipboard);

    if (g_paste_clipboard_get_text (clipboard) == NULL &&
        g_paste_clipboard_get_image_checksum (clipboard) == NULL)
    {
        const GSList *history = g_paste_history_get_history (priv->history);
        if (history != NULL)
            g_paste_clipboard_select_item (clipboard, history->data);
    }
}

/**
 * g_paste_clipboards_manager_sync_from_to:
 * @self: a #GPasteClipboardsManager instance
 * @from: the Source clipboard type
 * @to: the destination clipboard type
 *
 * Sync a clipboard into another
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_clipboards_manager_sync_from_to (GPasteClipboardsManager *self,
                                         GdkAtom                  from,
                                         GdkAtom                  to)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (self));

    GPasteClipboardsManagerPrivate *priv = self->priv;
    GtkClipboard *_from = NULL, *_to = NULL;

    for (GSList *clipboard = priv->clipboards; clipboard; clipboard = g_slist_next (clipboard))
    {
        GPasteClipboard *clip = clipboard->data;
        GdkAtom cur = g_paste_clipboard_get_target (clip);

        if (cur == from)
            _from = g_paste_clipboard_get_real (clip);
        else if (cur == to)
            _to = g_paste_clipboard_get_real (clip);
    }

    if (_from && _to)
    {
        gchar *text = gtk_clipboard_wait_for_text (_from);
        if (text)
            gtk_clipboard_set_text (_to, text, -1);
    }
}

static void
g_paste_clipboards_manager_notify (GPasteClipboardsManager *self,
                                   GdkAtom                  atom)
{
    GPasteClipboardsManagerPrivate *priv = self->priv;
    GPasteHistory *history = priv->history;
    GPasteSettings *settings = priv->settings;
    const gchar *synchronized_text = NULL;
    gboolean track = ((atom != GDK_SELECTION_PRIMARY || g_paste_settings_get_primary_to_history (settings)) &&
                      g_paste_settings_get_track_changes (settings));

    for (GSList *clipboard = priv->clipboards; clipboard; clipboard = g_slist_next (clipboard))
    {
        GPasteClipboard *clip = clipboard->data;

        if (g_paste_clipboard_get_target (clip) != atom)
            continue;

        gboolean something_in_clipboard = FALSE;
        GtkSelectionData *targets = gtk_clipboard_wait_for_contents (g_paste_clipboard_get_real (clip),
                                                                     gdk_atom_intern_static_string ("TARGETS"));

        if (targets)
        {
            GPasteItem *item = NULL;
            gboolean uris_available = gtk_selection_data_targets_include_uri (targets);

            if (uris_available || gtk_selection_data_targets_include_text (targets))
            {
                /* Update our cache from the real Clipboard */
                const gchar *text = g_paste_clipboard_set_text (clip);

                /* Did we already have some contents, or did we get some now? */
                something_in_clipboard = (g_paste_clipboard_get_text (clip) != NULL);

                /* If our contents got updated */
                if (text)
                {
                    if (track)
                    {
                        if (uris_available)
                            item = G_PASTE_ITEM (g_paste_uris_item_new (text));
                        else
                            item = G_PASTE_ITEM (g_paste_text_item_new (text));
                    }

                    if (g_paste_settings_get_synchronize_clipboards (settings))
                        synchronized_text = text;
                }
            }
            else if (g_paste_settings_get_images_support (settings) && gtk_selection_data_targets_include_image (targets, FALSE))
            {
                GdkPixbuf *image = g_paste_clipboard_set_image (clip);

                something_in_clipboard = (g_paste_clipboard_get_image_checksum (clip) != NULL);

                if (image != NULL)
                {
                    if (track)
                    {
                        item = G_PASTE_ITEM (g_paste_image_item_new (image));
                        g_object_unref (image);
                    }
                }
            }

            if (item)
            {
                g_paste_history_add (history, item);
                g_object_unref (item);
            }

            gtk_selection_data_free (targets);

            if (!something_in_clipboard)
            {
                const GSList *hist = g_paste_history_get_history (history);
                if (hist != NULL)
                    g_paste_clipboard_select_item (clip, hist->data);
            }
        }
    }

    if (synchronized_text != NULL)
    {
        for (GSList *clipboard = priv->clipboards; clipboard; clipboard = g_slist_next (clipboard))
        {
            GPasteClipboard *clip = clipboard->data;
            const gchar *text = g_paste_clipboard_get_text (clip);

            if (text == NULL ||
                g_strcmp0 (text, synchronized_text) != 0)
                    g_paste_clipboard_select_text (clip, synchronized_text);
        }
    }
}

static Atom
_gdk_atom_to_atom (GdkAtom atom)
{
    if (atom == GDK_SELECTION_CLIPBOARD)
        return xa_clipboard;
    if (atom == GDK_SELECTION_PRIMARY)
        return XA_PRIMARY;
    return 0;
}

static GdkAtom
_atom_to_gdk_atom (Atom atom)
{
    if (atom == xa_clipboard)
        return GDK_SELECTION_CLIPBOARD;
    if (atom == XA_PRIMARY)
        return GDK_SELECTION_PRIMARY;
    return 0;
}

#ifdef GDK_WINDOWING_WAYLAND
static void
g_paste_clipboards_manager_activate_one_wayland (void)
{
    g_error ("Wayland is currently not supported.");
}
#endif

#ifdef GDK_WINDOWING_X11
static void
g_paste_clipboards_manager_activate_one_x11 (GPasteClipboardsManager *self,
                                             Atom                     atom)
{
    GPasteClipboardsManagerPrivate *priv = self->priv;

    XFixesSelectSelectionInput(GDK_DISPLAY_XDISPLAY (priv->display),
                               gdk_x11_window_get_xid (priv->window),
                               atom,
                               XFixesSetSelectionOwnerNotifyMask | XFixesSelectionWindowDestroyNotifyMask | XFixesSelectionClientCloseNotifyMask);
}
#endif

static void
g_paste_clipboards_manager_activate_one (GPasteClipboardsManager *self,
                                         GdkAtom                  atom)
{
    GdkDisplay *display = self->priv->display;

#ifdef GDK_WINDOWING_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY (display))
        g_paste_clipboards_manager_activate_one_wayland ();
    else
#endif
#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_DISPLAY (display))
        g_paste_clipboards_manager_activate_one_x11 (self, _gdk_atom_to_atom (atom));
    else
#endif
        g_error ("Unsupported GDK backend.");
}

/**
 * g_paste_clipboards_manager_activate:
 * @self: a #GPasteClipboardsManager instance
 *
 * Activate the #GPasteClipboardsManager
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_clipboards_manager_activate (GPasteClipboardsManager *self)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (self));

    for (GSList *clipboard = self->priv->clipboards; clipboard; clipboard = g_slist_next (clipboard))
        g_paste_clipboards_manager_activate_one (self, g_paste_clipboard_get_target (clipboard->data));
}

/**
 * g_paste_clipboards_manager_select:
 * @self: a #GPasteClipboardsManager instance
 * @item: the #GPasteItem to select
 *
 * Select a new #GPasteItem
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_clipboards_manager_select (GPasteClipboardsManager *self,
                                   GPasteItem              *item)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (self));
    g_return_if_fail (G_PASTE_IS_ITEM (item));

    GPasteClipboardsManagerPrivate *priv = self->priv;

    g_paste_history_add (priv->history, item);
    for (GSList *clipboard = priv->clipboards; clipboard; clipboard = g_slist_next (clipboard))
        g_paste_clipboard_select_item (clipboard->data, item);
}

static gboolean
on_item_selected (GPasteClipboardsManager *self,
                  GPasteItem              *item,
                  gpointer                 user_data G_GNUC_UNUSED)
{
    g_paste_clipboards_manager_select (self, item);

    return TRUE;
}

static GdkFilterReturn
g_paste_clipboards_manager_filter (GdkXEvent *xevent,
                                   GdkEvent  *event G_GNUC_UNUSED,
                                   gpointer   data)
{
    XGenericEventCookie cookie = ((XEvent *) xevent)->xcookie;

    if (cookie.type == xfixes_event_base + XFixesSelectionNotify)
    {
        XFixesSelectionNotifyEvent *xf_ev = (XFixesSelectionNotifyEvent *) xevent;
        g_paste_clipboards_manager_notify (G_PASTE_CLIPBOARDS_MANAGER (data),
                                           _atom_to_gdk_atom (xf_ev->selection));
    }

    return GDK_FILTER_CONTINUE;
}

static void
g_paste_clipboards_manager_dispose (GObject *object)
{
    GPasteClipboardsManager *self = G_PASTE_CLIPBOARDS_MANAGER (object);
    GPasteClipboardsManagerPrivate *priv = self->priv;
    GPasteSettings *settings = priv->settings;

    if (settings)
    {
        g_signal_handler_disconnect (settings, priv->selected_signal);
        g_object_unref (settings);
        g_object_unref (priv->history);
        priv->settings = NULL;
    }

    G_OBJECT_CLASS (g_paste_clipboards_manager_parent_class)->dispose (object);
}

static void
g_paste_clipboards_manager_finalize (GObject *object)
{
    GPasteClipboardsManagerPrivate *priv = G_PASTE_CLIPBOARDS_MANAGER (object)->priv;

    g_slist_free_full (priv->clipboards,
                       g_object_unref);

    G_OBJECT_CLASS (g_paste_clipboards_manager_parent_class)->finalize (object);
}

static void
g_paste_clipboards_manager_class_init (GPasteClipboardsManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_clipboards_manager_dispose;
    object_class->finalize = g_paste_clipboards_manager_finalize;
}

#ifdef GDK_WINDOWING_WAYLAND
static void
g_paste_clipboards_manager_init_wayland (void)
{
    g_error ("Wayland is currently not supported.");
}
#endif

#ifdef GDK_WINDOWING_X11
static void
g_paste_clipboards_manager_init_x11 (Display *display)
{
    if (!xfixes_event_base)
    {
        xa_clipboard = XInternAtom (display, "CLIPBOARD", False);

        gint xfixes_error_base;

        if (!XFixesQueryExtension (display,
                                   &xfixes_event_base,
                                   &xfixes_error_base))
            g_error ("XFixes 5 not found, GPaste won't work");
    }
}
#endif

static void
g_paste_clipboards_manager_init (GPasteClipboardsManager *self)
{
    GPasteClipboardsManagerPrivate *priv = self->priv = g_paste_clipboards_manager_get_instance_private (self);

    priv->clipboards = NULL;

    GdkDisplay *display = priv->display = gdk_display_get_default ();
    GdkWindow *window = priv->window = gdk_get_default_root_window ();

#ifdef GDK_WINDOWING_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY (display))
        g_paste_clipboards_manager_init_wayland ();
    else
#endif
#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_DISPLAY (display))
        g_paste_clipboards_manager_init_x11 (GDK_DISPLAY_XDISPLAY (display));
    else
#endif
        g_error ("Unsupported GDK backend.");

    gdk_window_add_filter (window,
                           g_paste_clipboards_manager_filter,
                           self);
}

/**
 * g_paste_clipboards_manager_new:
 * @history: (transfer none): a #GPasteHistory instance
 * @settings: (transfer none): a #GPasteSettings instance
 *
 * Create a new instance of #GPasteClipboardsManager
 *
 * Returns: a newly allocated #GPasteClipboardsManager
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteClipboardsManager *
g_paste_clipboards_manager_new (GPasteHistory  *history,
                                GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (history), NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    GPasteClipboardsManager *self = g_object_new (G_PASTE_TYPE_CLIPBOARDS_MANAGER, NULL);
    GPasteClipboardsManagerPrivate *priv = self->priv;

    priv->history = g_object_ref (history);
    priv->settings = g_object_ref (settings);

    priv->selected_signal = g_signal_connect_swapped (G_OBJECT (history),
                                                      "selected",
                                                      G_CALLBACK (on_item_selected),
                                                      self);

    return self;
}
