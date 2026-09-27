// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-bus.h>
#include <gpaste-test-env.h>

/* Reach the window's banner, actions and setup. */
#include <gpaste-ui-window.c>
#include <gpaste-ui-item.h>

#include <gpaste-3/gpaste-daemon3.h>
#include <gpaste-3/gpaste-gdbus-defines.h>

static gboolean have_display;
/* One for the whole binary: an application stays exported on the bus for as
 * long as anything holds it, which a closed window may still do. */
static GtkApplication *app;

/* A daemon stood in for by the generated skeleton on a connection of its own,
 * owning the name before it exports anything, as both real daemons do. It
 * lists what @listing says, and holds @size items of text. */
typedef struct
{
    GDBusConnection       *connection;
    GPasteDaemon3         *skeleton;
    guint                  owner;
    const gchar           *listing;
    guint64                size;
    /* Answer the pinned items and searches as a daemon gone would: unlike the
     * holds below, there is no invocation the test could answer later, a
     * search going out each time the filter changes. */
    gboolean               filters_gone;
    gboolean               allow_replacement;
    gboolean               replace;
    guint                  listings;
    guint                  sizings;
    guint                  filters;
    /* Set to hold these calls rather than answer them, the invocation kept
     * for the test to answer: its reference is the handler's to keep. */
    gboolean               hold_sizes;
    GDBusMethodInvocation *held_size;
    gboolean               hold_listings;
    GDBusMethodInvocation *held_listing;
    gboolean               hold_upload;
    GDBusMethodInvocation *held_upload;
    guint                  uploads;
} FakeDaemon;

#define LISTING_TWO "[('" G_PASTE_DEFAULT_HISTORY "', uint64 0), ('work', 2)]"
#define LISTING_ONE "[('" G_PASTE_DEFAULT_HISTORY "', uint64 0)]"
#define LISTING_THREE "[('" G_PASTE_DEFAULT_HISTORY "', uint64 0), ('work', 2), ('other', 1)]"

static gboolean
on_list_histories (GPasteDaemon3         *skeleton,
                   GDBusMethodInvocation *invocation,
                   gpointer               user_data)
{
    FakeDaemon *daemon = user_data;

    ++daemon->listings;

    if (daemon->hold_listings)
        daemon->held_listing = invocation;
    else
        g_paste_daemon3_complete_list_histories (skeleton, invocation, g_variant_new_parsed (daemon->listing));

    return TRUE;
}

static gboolean
on_get_history_size (GPasteDaemon3         *skeleton,
                     GDBusMethodInvocation *invocation,
                     gpointer               user_data)
{
    FakeDaemon *daemon = user_data;

    ++daemon->sizings;

    if (daemon->hold_sizes)
        daemon->held_size = invocation;
    else
        g_paste_daemon3_complete_get_history_size (skeleton, invocation, daemon->size);

    return TRUE;
}

/* What the bus answers a call whose recipient disconnected: a call answered
 * this way is one its daemon left without answering. */
static void
return_gone (GDBusMethodInvocation *invocation)
{
    g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR, G_DBUS_ERROR_NO_REPLY, "gone");
}

/* A search and the pinned items alike: nothing matches, nothing is pinned --
 * or, with filters_gone, the daemon gone without answering. */
static gboolean
on_get_favourites (GPasteDaemon3         *skeleton G_GNUC_UNUSED,
                   GDBusMethodInvocation *invocation,
                   gpointer               user_data)
{
    FakeDaemon *daemon = user_data;

    ++daemon->filters;
    if (daemon->filters_gone)
        return_gone (invocation);
    else
        g_dbus_method_invocation_return_value (invocation, g_variant_new_parsed ("(@a(ssubas) [],)"));

    return TRUE;
}

static gboolean
on_fake_search (GPasteDaemon3         *skeleton,
                GDBusMethodInvocation *invocation,
                const gchar           *query G_GNUC_UNUSED,
                gpointer               user_data)
{
    return on_get_favourites (skeleton, invocation, user_data);
}

#define UPLOADED_URL "https://paste.rs/x"

/* What the daemon keeps of the address -- the history, or the clipboard alone --
 * is its own business (g_paste_daemon_methods_copy_uploaded ()), which nothing
 * here stands in for: the window only reports how the upload went. */
static gboolean
on_upload_and_copy (GPasteDaemon3         *skeleton,
                    GDBusMethodInvocation *invocation,
                    const gchar           *uuid G_GNUC_UNUSED,
                    gpointer               user_data)
{
    FakeDaemon *daemon = user_data;

    ++daemon->uploads;

    if (daemon->hold_upload)
        daemon->held_upload = invocation;
    else
        g_paste_daemon3_complete_upload_and_copy (skeleton, invocation, UPLOADED_URL);

    return TRUE;
}

static gboolean
on_get_item_at_index (GPasteDaemon3         *skeleton,
                      GDBusMethodInvocation *invocation,
                      guint64                index,
                      gpointer               user_data G_GNUC_UNUSED)
{
    g_autofree gchar *uuid = g_strdup_printf ("00000000-0000-4000-8000-%012" G_GUINT64_FORMAT, index);
    g_autofree gchar *value = g_strdup_printf ("value %" G_GUINT64_FORMAT, index);

    g_paste_daemon3_complete_get_item_at_index (skeleton, invocation,
                                                g_variant_new ("(ssub@as)", uuid, value, G_PASTE_ITEM_KIND_TEXT, FALSE,
                                                               g_variant_new_strv (NULL, 0)));

    return TRUE;
}

static void
fake_daemon_start (FakeDaemon *daemon)
{
    g_autoptr (GError) error = NULL;

    if (!daemon->listing)
        daemon->listing = LISTING_TWO;

    daemon->connection = g_paste_test_bus_connect (g_getenv ("DBUS_SESSION_BUS_ADDRESS"));
    /* Not queued for the name once replaced: a real daemon quits then
     * (on_name_lost () in src/daemon/gpaste-daemon.c), so it never gets the
     * name back from its successor going. */
    GBusNameOwnerFlags flags = (daemon->allow_replacement ? G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT | G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE : G_BUS_NAME_OWNER_FLAGS_NONE) |
                              (daemon->replace ? G_BUS_NAME_OWNER_FLAGS_REPLACE : G_BUS_NAME_OWNER_FLAGS_NONE);

    daemon->owner = g_bus_own_name_on_connection (daemon->connection, G_PASTE_BUS_NAME, flags,
                                                  NULL, NULL, NULL, NULL);

    daemon->skeleton = g_paste_daemon3_skeleton_new ();
    g_signal_connect (daemon->skeleton, "handle-list-histories", G_CALLBACK (on_list_histories), daemon);
    g_signal_connect (daemon->skeleton, "handle-get-history-size", G_CALLBACK (on_get_history_size), daemon);
    g_signal_connect (daemon->skeleton, "handle-get-item-at-index", G_CALLBACK (on_get_item_at_index), daemon);
    g_signal_connect (daemon->skeleton, "handle-search", G_CALLBACK (on_fake_search), daemon);
    g_signal_connect (daemon->skeleton, "handle-upload-and-copy", G_CALLBACK (on_upload_and_copy), daemon);
    g_signal_connect (daemon->skeleton, "handle-get-favourites", G_CALLBACK (on_get_favourites), daemon);
    g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (daemon->skeleton), daemon->connection,
                                      G_PASTE_DAEMON_OBJECT_PATH, &error);
    g_assert_no_error (error);
    g_paste_daemon3_set_history (daemon->skeleton, G_PASTE_DEFAULT_HISTORY);
}

/* Gone as a real daemon goes, its names and its object off the bus at once:
 * the connection closed first. Unexported first, it would still own the name
 * for a moment, and a call landing then would read an error no real daemon
 * gives -- no object at the path -- which the window reports as a failure. */
static void
fake_daemon_stop (FakeDaemon *daemon)
{
    g_dbus_connection_close_sync (daemon->connection, NULL, NULL);
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (daemon->skeleton));
    g_clear_object (&daemon->skeleton);
    g_clear_handle_id (&daemon->owner, g_bus_unown_name);
    g_clear_object (&daemon->connection);
}

/* The calls the window sends the daemon's name that are its own to make -- a
 * search, the pinned items -- counted as they leave, which is the one place to
 * see them while nobody owns the name. The filter runs on GDBus' worker
 * thread. */
typedef struct
{
    gint searches;
    gint favourites;
} Outgoing;

static GDBusMessage *
count_outgoing (GDBusConnection *connection G_GNUC_UNUSED,
                GDBusMessage    *message,
                gboolean         incoming,
                gpointer         user_data)
{
    Outgoing *outgoing = user_data;

    if (incoming ||
        g_dbus_message_get_message_type (message) != G_DBUS_MESSAGE_TYPE_METHOD_CALL ||
        !g_paste_str_equal (g_dbus_message_get_destination (message), G_PASTE_BUS_NAME))
        return message;

    const gchar *member = g_dbus_message_get_member (message);

    if (g_paste_str_equal (member, "Search"))
        g_atomic_int_inc (&outgoing->searches);
    else if (g_paste_str_equal (member, "GetFavourites"))
        g_atomic_int_inc (&outgoing->favourites);

    return message;
}

static void
wait_for_presence (GPasteClient        *client,
                   GPasteDaemonPresence presence)
{
    g_paste_test_bus_wait_for_enum (client, "daemon-presence", presence);
}

static void
drain (void)
{
    while (g_main_context_iteration (NULL, FALSE));
}

static GtkWidget *
find_widget (GtkWidget *widget,
             GType      type)
{
    if (G_TYPE_CHECK_INSTANCE_TYPE (widget, type))
        return widget;

    for (GtkWidget *child = gtk_widget_get_first_child (widget); child; child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *found = find_widget (child, type);

        if (found)
            return found;
    }

    return NULL;
}

static AdwStatusPage *
find_status_page (GtkWidget *widget)
{
    return ADW_STATUS_PAGE (find_widget (widget, ADW_TYPE_STATUS_PAGE));
}

/* The sidebar's row for @history, borrowed from the sidebar's model. */
static AdwSidebarItem *
sidebar_item (GPasteUiWindow *window,
              const gchar    *history)
{
    g_autoptr (GtkSelectionModel) items = adw_sidebar_get_items (ADW_SIDEBAR (find_widget (window->panel, ADW_TYPE_SIDEBAR)));

    for (guint i = 0; i < g_list_model_get_n_items (G_LIST_MODEL (items)); ++i)
    {
        g_autoptr (AdwSidebarItem) item = g_list_model_get_item (G_LIST_MODEL (items), i);

        if (g_paste_str_equal (adw_sidebar_item_get_title (item), history))
            return item;
    }

    return NULL;
}

static gboolean
sidebar_lists (GPasteUiWindow *window,
               const gchar    *history)
{
    return sidebar_item (window, history) != NULL;
}

static gboolean
action_enabled (GPasteUiWindow *window,
                const gchar    *name)
{
    return g_action_group_get_action_enabled (G_ACTION_GROUP (window), name);
}

static gboolean
history_has_focus (GPasteUiWindow *window)
{
    GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (window));

    return focus && gtk_widget_is_ancestor (focus, GTK_WIDGET (window->history));
}

static gboolean
history_list_has_focus (GPasteUiWindow *window)
{
    GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (window));
    GtkWidget *list = find_widget (GTK_WIDGET (window->history), GTK_TYPE_LIST_VIEW);

    return focus && (focus == list || gtk_widget_is_ancestor (focus, list));
}

static gboolean
sidebar_dropped (gconstpointer window,
                 gconstpointer history)
{
    return !sidebar_lists ((GPasteUiWindow *) window, history);
}

static gboolean
sidebar_has_history (gconstpointer window,
                     gconstpointer history)
{
    return sidebar_lists ((GPasteUiWindow *) window, history);
}

static gboolean
history_rows_are (gconstpointer window,
                  gconstpointer count)
{
    GtkWidget *view = find_widget (GTK_WIDGET (((GPasteUiWindow *) window)->history), GTK_TYPE_LIST_VIEW);

    return g_list_model_get_n_items (G_LIST_MODEL (gtk_list_view_get_model (GTK_LIST_VIEW (view)))) == GPOINTER_TO_UINT (count);
}

/* GTK lets go of a row that went away on a later frame, not at once: until it
 * does, the window still names that row, rows standing in for it later getting
 * the focus back on their own. */
static gboolean
focus_moved_away (gconstpointer window,
                  gconstpointer arg G_GNUC_UNUSED)
{
    return !history_has_focus ((GPasteUiWindow *) window);
}

static gboolean
focus_returned (gconstpointer window,
                gconstpointer arg G_GNUC_UNUSED)
{
    return history_has_focus ((GPasteUiWindow *) window);
}

static gboolean
history_is_work (gconstpointer client,
                 gconstpointer arg G_GNUC_UNUSED)
{
    g_autofree gchar *history = g_paste_client_get_history_name ((GPasteClient *) client);

    return g_paste_str_equal (history, "work");
}

/* The rows are back once the list is shown in place of the status page. */
static gboolean
list_shown (gconstpointer window,
            gconstpointer arg G_GNUC_UNUSED)
{
    return gtk_widget_get_mapped (find_widget (GTK_WIDGET (((GPasteUiWindow *) window)->history), GTK_TYPE_LIST_VIEW));
}

static GPasteUiWindow *
test_window (void)
{
    return g_object_ref_sink (g_object_new (G_PASTE_TYPE_UI_WINDOW, "application", app, NULL));
}

/* What the window showed the moment the daemon was said to be starting, read
 * from inside that notify -- after the window's own handler, connected first --
 * so that the end of the client's grace second, which a loaded machine can
 * reach before the test does, cannot have moved it on. */
typedef struct
{
    gboolean     seen;
    gboolean     revealed;
    const gchar *title;
    gboolean     new_item;
    gboolean     panel;
    gboolean     search_mode;
    const gchar *search_text;
} StartingSnapshot;

static void
on_presence_snapshot (GPasteClient *client,
                      GParamSpec   *pspec G_GNUC_UNUSED,
                      gpointer      user_data)
{
    GPasteUiWindow *window = user_data;
    StartingSnapshot *snapshot = g_object_get_data (G_OBJECT (window), "starting-snapshot");

    if (snapshot->seen || g_paste_client_get_daemon_presence (client) != G_PASTE_DAEMON_PRESENCE_STARTING)
        return;

    snapshot->seen = TRUE;
    snapshot->revealed = adw_banner_get_revealed (window->banner);
    snapshot->title = g_intern_string (adw_banner_get_title (window->banner));
    snapshot->new_item = action_enabled (window, "new-item");
    snapshot->panel = gtk_widget_get_sensitive (window->panel);
    snapshot->search_mode = gtk_search_bar_get_search_mode (window->search_bar);
    snapshot->search_text = g_intern_string (gtk_editable_get_text (GTK_EDITABLE (window->search_entry)));
}

/* The window, the list and the sidebar across a daemon that is not there, turns
 * up, and goes away again. */
static void
test_follow (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteUiWindow) window = test_window ();
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    FakeDaemon daemon = { 0 };

    g_assert_no_error (error);

    /* Opening the window asks for a daemon, which the bus has none of. */
    g_paste_ui_window_setup (window, client);
    g_assert_true (adw_banner_get_revealed (window->banner));
    g_assert_cmpstr (adw_banner_get_title (window->banner), ==, _("Connecting to GPaste…"));
    g_assert_null (adw_banner_get_button_label (window->banner));
    g_assert_nonnull (adw_status_page_get_paintable (find_status_page (GTK_WIDGET (window->history))));

    /* Nothing that would act on a daemon is on offer without one. */
    g_assert_false (action_enabled (window, "new-item"));
    g_assert_false (action_enabled (window, "track-changes"));
    g_assert_true (action_enabled (window, "preferences"));
    g_assert_false (gtk_widget_get_sensitive (window->panel));
    g_assert_false (gtk_widget_get_sensitive (GTK_WIDGET (window->history)));

    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_ABSENT);
    g_assert_cmpstr (adw_banner_get_title (window->banner), ==, _("Couldn’t connect to GPaste"));
    g_assert_cmpstr (adw_banner_get_button_label (window->banner), ==, _("Retry"));
    g_assert_cmpstr (adw_status_page_get_title (find_status_page (GTK_WIDGET (window->history))), ==, _("History Unavailable"));
    g_assert_null (adw_status_page_get_paintable (find_status_page (GTK_WIDGET (window->history))));

    /* A daemon turning up lists the histories and the history both, and the
     * banner slides away still saying what it said. */
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_test_bus_wait_for_count (&daemon.listings, 1);
    g_paste_test_bus_wait_for_count (&daemon.sizings, 1);
    g_assert_false (adw_banner_get_revealed (window->banner));
    g_assert_cmpstr (adw_banner_get_title (window->banner), ==, _("Couldn’t connect to GPaste"));
    g_assert_true (action_enabled (window, "new-item"));
    g_assert_true (gtk_widget_get_sensitive (window->panel));
    g_assert_true (gtk_widget_get_sensitive (GTK_WIDGET (window->history)));

    /* One going away takes the offers back, but not the search the user
     * typed: a daemon away for a moment would otherwise throw it away. */
    StartingSnapshot snapshot = { 0 };

    g_object_set_data (G_OBJECT (window), "starting-snapshot", &snapshot);
    g_signal_connect_object (client, "notify::daemon-presence", G_CALLBACK (on_presence_snapshot), window, 0);
    gtk_search_bar_set_search_mode (window->search_bar, TRUE);
    gtk_editable_set_text (GTK_EDITABLE (window->search_entry), "value");
    g_paste_test_bus_wait_for_count (&daemon.filters, 1);
    fake_daemon_stop (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_STARTING);
    g_assert_true (snapshot.seen);
    g_assert_true (snapshot.revealed);
    g_assert_cmpstr (snapshot.title, ==, _("Connecting to GPaste…"));
    g_assert_false (snapshot.new_item);
    g_assert_false (snapshot.panel);
    g_assert_true (snapshot.search_mode);
    g_assert_cmpstr (snapshot.search_text, ==, "value");

    /* The next daemon is asked the same search, once: the list asks it as
     * the presence reaches ready, and nothing asks again after. */
    daemon.filters = 0;
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_test_bus_wait_for_count (&daemon.filters, 1);
    g_paste_test_bus_pump (200);
    g_assert_cmpuint (daemon.filters, ==, 1);
    g_assert_true (gtk_search_bar_get_search_mode (window->search_bar));
    g_assert_cmpstr (gtk_editable_get_text (GTK_EDITABLE (window->search_entry)), ==, "value");

    gtk_window_destroy (GTK_WINDOW (window));
    drain ();
    fake_daemon_stop (&daemon);
}

/* A window presented for a proxy that could not be built, then retried into a
 * daemon that is already there: the banner has nothing left to say. */
static void
test_retried_client (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GPasteUiWindow) window = test_window ();
    FakeDaemon daemon = { 0 };

    /* What on_banner_retry () leaves: a new client on its way, over a window
     * on_client_ready () already presented for the failure. */
    window->connecting = TRUE;
    window->initialized = TRUE;
    g_paste_ui_window_update_banner (window);
    g_assert_cmpstr (adw_banner_get_title (window->banner), ==, _("Connecting to GPaste…"));

    /* Built before the daemon is there: the stand-in answers on this thread's
     * main context, which a synchronous call made to it would block. */
    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);

    g_assert_no_error (error);
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);

    window->connecting = FALSE;
    g_paste_ui_window_setup (window, client);
    g_assert_false (adw_banner_get_revealed (window->banner));

    gtk_window_destroy (GTK_WINDOW (window));
    drain ();
    fake_daemon_stop (&daemon);
}

/* Escape with the search bar open and no daemon closes the bar, the focus
 * being on none of what is insensitive: the search is not kept for a daemon
 * that is not there, and emptying the entry asks nothing of anyone, the next
 * daemon included (on_escape ()). */
static void
test_search_closed_away (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteUiWindow) window = test_window ();
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    FakeDaemon daemon = { 0 };
    Outgoing outgoing = { 0 };

    g_assert_no_error (error);
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_ui_window_setup (window, client);
    gtk_search_bar_set_search_mode (window->search_bar, TRUE);
    gtk_editable_set_text (GTK_EDITABLE (window->search_entry), "value");
    g_paste_test_bus_wait_for_count (&daemon.filters, 1);
    drain ();

    GDBusConnection *connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (client));
    guint filter = g_dbus_connection_add_filter (connection, count_outgoing, &outgoing, NULL);

    fake_daemon_stop (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_STARTING);
    g_assert_true (gtk_search_bar_get_search_mode (window->search_bar));

    g_assert_true (on_escape (GTK_WIDGET (window), NULL, NULL));
    g_assert_false (gtk_search_bar_get_search_mode (window->search_bar));
    g_assert_cmpstr (gtk_editable_get_text (GTK_EDITABLE (window->search_entry)), ==, "");
    drain ();

    daemon.filters = 0;
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_test_bus_wait_for_count (&daemon.listings, 1);
    g_paste_test_bus_pump (200);
    g_dbus_connection_flush_sync (connection, NULL, &error);
    g_assert_no_error (error);
    g_assert_cmpint (g_atomic_int_get (&outgoing.searches), ==, 0);
    g_assert_cmpuint (daemon.filters, ==, 0);
    g_dbus_connection_remove_filter (connection, filter);

    gtk_window_destroy (GTK_WINDOW (window));
    drain ();
    fake_daemon_stop (&daemon);
}

/* With no daemon, nothing the window does asks for one: not typing, not a
 * search reaching the list. */
static void
test_nothing_asked (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteUiWindow) window = test_window ();
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    FakeDaemon daemon = { 0 };
    Outgoing outgoing = { 0 };

    /* Built before the daemon is there, as in test_retried_client (). */
    g_assert_no_error (error);
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_ui_window_setup (window, client);
    g_assert_true (gtk_search_bar_get_key_capture_widget (window->search_bar) == GTK_WIDGET (window));

    /* The Pinned filter on and a search typed: the search bar stays open
     * while the daemon is away, though emptying its entry is a search of its
     * own (/ui/daemon-presence/search-closed-away). */
    gtk_toggle_button_set_active (g_paste_ui_header_get_favourites_button (window->header), TRUE);
    gtk_search_bar_set_search_mode (window->search_bar, TRUE);
    gtk_editable_set_text (GTK_EDITABLE (window->search_entry), "value");
    g_paste_test_bus_wait_for_count (&daemon.filters, 2);
    drain ();

    GDBusConnection *connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (client));
    guint filter = g_dbus_connection_add_filter (connection, count_outgoing, &outgoing, NULL);

    fake_daemon_stop (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_STARTING);
    drain ();

    /* With the daemon gone, typing does not open the search, and a search
     * asked for all the same stops at the list. */
    g_assert_null (gtk_search_bar_get_key_capture_widget (window->search_bar));
    g_paste_ui_history_search (window->history, "value");
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_ABSENT);
    g_paste_ui_history_search (window->history, "");
    drain ();

    g_dbus_connection_flush_sync (connection, NULL, &error);
    g_assert_no_error (error);
    g_assert_cmpint (g_atomic_int_get (&outgoing.favourites), ==, 0);
    g_assert_cmpint (g_atomic_int_get (&outgoing.searches), ==, 0);
    g_assert_cmpstr (adw_status_page_get_title (find_status_page (GTK_WIDGET (window->history))), ==, _("History Unavailable"));
    g_dbus_connection_remove_filter (connection, filter);

    /* A daemon back gives typing back its search. */
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_assert_true (gtk_search_bar_get_key_capture_widget (window->search_bar) == GTK_WIDGET (window));

    gtk_window_destroy (GTK_WINDOW (window));
    drain ();
    fake_daemon_stop (&daemon);
}

/* The sidebar's entry holding the focus when the daemon goes: it is taken off
 * the entry before the sidebar goes insensitive, or GTK would leave the entry
 * believing it still has it -- and warn, which a test aborts on. */
static void
test_entry_focus_released (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteUiWindow) window = test_window ();
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    FakeDaemon daemon = { 0 };

    g_assert_no_error (error);
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_ui_window_setup (window, client);
    g_paste_test_bus_wait_for_count (&daemon.listings, 1);
    drain ();

    GtkWidget *entry = find_widget (window->panel, GTK_TYPE_TEXT);

    g_assert_true (gtk_widget_grab_focus (entry));

    fake_daemon_stop (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_STARTING);
    /* Past the frame GTK would warn on. */
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_ABSENT);
    g_assert_false (gtk_widget_has_focus (entry));

    gtk_window_destroy (GTK_WINDOW (window));
    drain ();
}

/* What the entry's own focus controller believes, which is what GTK leaves
 * stale when it drops the focus of an insensitive widget. */
static gboolean
entry_believes_focused (GtkWidget *text)
{
    g_autoptr (GListModel) controllers = gtk_widget_observe_controllers (text);

    for (guint i = 0; i < g_list_model_get_n_items (controllers); ++i)
    {
        g_autoptr (GObject) controller = g_list_model_get_item (controllers, i);

        if (GTK_IS_EVENT_CONTROLLER_FOCUS (controller) && gtk_event_controller_focus_is_focus (GTK_EVENT_CONTROLLER_FOCUS (controller)))
            return TRUE;
    }

    return FALSE;
}

static gboolean
widget_mapped (gconstpointer widget,
               gconstpointer arg G_GNUC_UNUSED)
{
    return gtk_widget_get_mapped ((GtkWidget *) widget);
}

static gboolean
entry_let_go (gconstpointer text,
              gconstpointer arg G_GNUC_UNUSED)
{
    return !entry_believes_focused ((GtkWidget *) text);
}

/* The Merge popover's entry holding the focus when the picks drop below two --
 * the daemon going, which empties the list, or a history change: it is let go
 * of before the button goes insensitive, or its own focus controller goes on
 * believing it has the focus -- which GTK warns about when the popover is next
 * opened. */
static void
test_merge_entry_focus_released (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteUiWindow) window = test_window ();
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    FakeDaemon daemon = { 0 };

    g_assert_no_error (error);
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_ui_window_setup (window, client);
    drain ();

    GtkMenuButton *merge = GTK_MENU_BUTTON (window->merge_button);

    /* A popover on Xvfb, which has no compositor, gets GDK warning about its
     * frame timings -- nothing the window does, and nothing this test is about,
     * which asserts on the entry's controller rather than on a warning. So
     * warnings are not fatal while the popover is up; the structured log GTK
     * uses ignores g_test_log_set_fatal_handler (). */
    GLogLevelFlags fatal = g_log_set_always_fatal (G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);

    /* In selection mode, as the Merge button is only shown there. */
    on_enter_selection_mode (NULL, window);
    g_signal_emit_by_name (window->history, "selection-changed", 2);
    gtk_menu_button_popup (merge);
    g_paste_test_bus_wait_until (widget_mapped, window->merge_entry, NULL);
    g_assert_true (gtk_widget_get_mapped (window->merge_entry));
    g_assert_true (gtk_widget_grab_focus (window->merge_entry));

    GtkWidget *text = find_widget (window->merge_entry, GTK_TYPE_TEXT);

    g_assert_true (entry_believes_focused (text));

    g_signal_emit_by_name (window->history, "selection-changed", 0);
    g_paste_test_bus_wait_until (entry_let_go, text, NULL);
    g_assert_false (entry_believes_focused (text));
    g_log_set_always_fatal (fatal);

    gtk_window_destroy (GTK_WINDOW (window));
    drain ();
    fake_daemon_stop (&daemon);
}

/* A window closed while the list's size request is out: the list outlives the
 * sidebar, the reply holding it, and the reply must not reach for the sidebar
 * gone with the window (see the panel field of GPasteUiHistory). */
static void
test_reply_after_close (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteUiWindow) window = test_window ();
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    FakeDaemon daemon = { 0 };

    g_assert_no_error (error);
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_ui_window_setup (window, client);
    g_paste_test_bus_wait_for_count (&daemon.sizings, 1);
    drain ();

    daemon.hold_sizes = TRUE;
    daemon.sizings = 0;
    g_paste_ui_history_search (window->history, "");
    g_paste_test_bus_wait_for_count (&daemon.sizings, 1);

    /* Destroyed and let go of, as the application does: the list stands on,
     * held by the reply, and the sidebar does not. */
    GtkWidget *panel = window->panel;

    g_object_add_weak_pointer (G_OBJECT (panel), (gpointer *) &panel);
    gtk_window_destroy (GTK_WINDOW (window));
    g_clear_object (&window);
    drain ();
    g_assert_null (panel);

    g_paste_daemon3_complete_get_history_size (daemon.skeleton, g_steal_pointer (&daemon.held_size), 3);
    g_dbus_connection_flush_sync (daemon.connection, NULL, &error);
    g_assert_no_error (error);
    g_paste_test_bus_round_trip (g_dbus_proxy_get_connection (G_DBUS_PROXY (client)), daemon.connection);

    fake_daemon_stop (&daemon);
}

/* A listing asked for under one history and answered after a switch to
 * another: the sidebar keeps the row of the history current now selected. */
static void
test_listing_after_switch (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteUiWindow) window = test_window ();
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    FakeDaemon daemon = { .listing = LISTING_TWO };

    g_assert_no_error (error);
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_ui_window_setup (window, client);
    g_paste_test_bus_wait_for_count (&daemon.listings, 1);
    g_paste_test_bus_round_trip (g_dbus_proxy_get_connection (G_DBUS_PROXY (client)), daemon.connection);

    daemon.hold_listings = TRUE;
    daemon.listings = 0;
    g_paste_daemon3_emit_raw_histories_changed (daemon.skeleton);
    g_paste_test_bus_wait_for_count (&daemon.listings, 1);

    g_paste_daemon3_set_history (daemon.skeleton, "work");
    g_paste_test_bus_wait_until (history_is_work, client, NULL);
    drain ();

    g_paste_daemon3_complete_list_histories (daemon.skeleton, g_steal_pointer (&daemon.held_listing), g_variant_new_parsed (LISTING_TWO));
    g_paste_test_bus_round_trip (g_dbus_proxy_get_connection (G_DBUS_PROXY (client)), daemon.connection);

    AdwSidebarItem *selected = adw_sidebar_get_selected_item (ADW_SIDEBAR (find_widget (window->panel, ADW_TYPE_SIDEBAR)));

    g_assert_nonnull (selected);
    g_assert_cmpstr (adw_sidebar_item_get_title (selected), ==, "work");

    gtk_window_destroy (GTK_WINDOW (window));
    drain ();
    fake_daemon_stop (&daemon);
}

/* A delayed older listing cannot undo the rows installed by a later one. */
static void
test_listing_overtaken (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteUiWindow) window = test_window ();
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    FakeDaemon daemon = { .listing = LISTING_TWO };

    g_assert_no_error (error);
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_ui_window_setup (window, client);
    g_paste_test_bus_wait_for_count (&daemon.listings, 1);
    g_paste_test_bus_wait_until (sidebar_has_history, window, "work");

    daemon.hold_listings = TRUE;
    daemon.listings = 0;
    g_paste_daemon3_emit_raw_histories_changed (daemon.skeleton);
    g_paste_test_bus_wait_for_count (&daemon.listings, 1);

    daemon.hold_listings = FALSE;
    g_paste_daemon3_emit_raw_histories_changed (daemon.skeleton);
    g_paste_test_bus_wait_for_count (&daemon.listings, 2);
    g_paste_test_bus_round_trip (g_dbus_proxy_get_connection (G_DBUS_PROXY (client)), daemon.connection);
    g_assert_true (sidebar_lists (window, "work"));

    g_paste_daemon3_complete_list_histories (daemon.skeleton, g_steal_pointer (&daemon.held_listing), g_variant_new_parsed (LISTING_ONE));
    g_paste_test_bus_round_trip (g_dbus_proxy_get_connection (G_DBUS_PROXY (client)), daemon.connection);
    g_assert_true (sidebar_lists (window, "work"));

    gtk_window_destroy (GTK_WINDOW (window));
    drain ();
    fake_daemon_stop (&daemon);
}

/* A listing still out when its daemon goes is given up on: the bus answers it
 * with an error once that daemon is off it, which a listing still standing
 * would report, critically -- fatal in a test. Nothing announces that report
 * not coming, hence the pump. */
static void
test_listing_abandoned (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteUiWindow) window = test_window ();
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    FakeDaemon daemon = { .listing = LISTING_TWO };

    g_assert_no_error (error);
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_ui_window_setup (window, client);
    g_paste_test_bus_wait_for_count (&daemon.listings, 1);
    drain ();

    daemon.hold_listings = TRUE;
    daemon.listings = 0;
    g_paste_daemon3_emit_raw_histories_changed (daemon.skeleton);
    g_paste_test_bus_wait_for_count (&daemon.listings, 1);

    /* Dropped unanswered, the connection going with it. */
    g_clear_object (&daemon.held_listing);
    fake_daemon_stop (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_ABSENT);
    g_paste_test_bus_pump (300);

    gtk_window_destroy (GTK_WINDOW (window));
    drain ();
}

/* A window closed while the sidebar's listing is out takes the sidebar with
 * it: the listing holds it weakly, so nothing keeps the panel past the window,
 * and its dispose () gives the listing up (g_paste_ui_panel_refresh ()). The
 * answer the stand-in sends after goes to a call already given up on. */
static void
test_listing_after_close (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteUiWindow) window = test_window ();
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    FakeDaemon daemon = { .listing = LISTING_TWO, .hold_listings = TRUE };
    GtkWidget *panel = NULL;

    g_assert_no_error (error);
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_ui_window_setup (window, client);
    g_paste_test_bus_wait_for_count (&daemon.listings, 1);
    g_set_weak_pointer (&panel, window->panel);

    gtk_window_destroy (GTK_WINDOW (window));
    g_clear_object (&window);
    drain ();
    g_assert_null (panel);

    g_paste_daemon3_complete_list_histories (daemon.skeleton, g_steal_pointer (&daemon.held_listing), g_variant_new_parsed (LISTING_TWO));
    g_paste_test_bus_round_trip (g_dbus_proxy_get_connection (G_DBUS_PROXY (client)), daemon.connection);
    fake_daemon_stop (&daemon);
}

/* A daemon coming back with fewer histories than it left with: the sidebar
 * drops the ones it no longer lists. */
static void
test_listing_pruned (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteUiWindow) window = test_window ();
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    FakeDaemon daemon = { .listing = LISTING_TWO };

    /* Built before the daemon is there, as in test_retried_client (). */
    g_assert_no_error (error);
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_ui_window_setup (window, client);
    g_paste_test_bus_wait_for_count (&daemon.listings, 1);
    g_paste_test_bus_wait_until (sidebar_has_history, window, "work");

    fake_daemon_stop (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_STARTING);
    daemon.listing = LISTING_ONE;
    daemon.listings = 0;
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_test_bus_wait_until (sidebar_dropped, window, "work");
    g_assert_false (sidebar_lists (window, "work"));
    g_assert_true (sidebar_lists (window, G_PASTE_DEFAULT_HISTORY));

    gtk_window_destroy (GTK_WINDOW (window));
    drain ();
    fake_daemon_stop (&daemon);
}

/* GDBus answers the proxy's GetAll with the method call itself when the vtable
 * has no get_property (), which is what lets this successor sit on it: until
 * it answers, the proxy goes on addressing the daemon standing down. */
static void
on_held_get_all (GDBusConnection       *connection     G_GNUC_UNUSED,
                 const gchar           *sender         G_GNUC_UNUSED,
                 const gchar           *object_path    G_GNUC_UNUSED,
                 const gchar           *interface_name G_GNUC_UNUSED,
                 const gchar           *method_name    G_GNUC_UNUSED,
                 GVariant              *parameters     G_GNUC_UNUSED,
                 GDBusMethodInvocation *invocation,
                 gpointer               user_data)
{
    *(GDBusMethodInvocation **) user_data = invocation;
}

static gboolean
sidebar_enabled (GPasteUiWindow *window,
                 const gchar    *history)
{
    return adw_sidebar_item_get_enabled (sidebar_item (window, history));
}

static gboolean
sidebar_row_enabled (gconstpointer window,
                     gconstpointer history)
{
    return sidebar_enabled ((GPasteUiWindow *) window, history);
}

/* A daemon taking the name over straight from another is a presence edge like
 * any other (/client/presence/handoff), and the window treats it as one.
 *
 * While the proxy still addresses the daemon standing down, nothing is asked
 * of it, and the listing it had out says nothing once answered, the edge
 * having given it up. The sidebar's rows stay, but none the new
 * daemon has not named can be clicked, a click on one it lacks creating that
 * history there; the listing naming it gives it back, the same row in the
 * same place, and the new daemon is listed and sized once. */
static void
test_listing_handoff (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteUiWindow) window = test_window ();
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    FakeDaemon old = { .listing = LISTING_TWO, .size = 2, .allow_replacement = TRUE };
    FakeDaemon successor = { .listing = LISTING_THREE, .size = 1, .replace = TRUE, .hold_sizes = TRUE, .hold_listings = TRUE };
    GDBusConnection *bus = NULL;

    g_assert_no_error (error);
    bus = g_dbus_proxy_get_connection (G_DBUS_PROXY (client));
    fake_daemon_start (&old);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_test_bus_wait_for_owner (G_DBUS_PROXY (client), old.connection);
    g_paste_ui_window_setup (window, client);
    g_paste_test_bus_wait_for_count (&old.listings, 1);
    g_paste_test_bus_wait_until (sidebar_has_history, window, "work");
    g_paste_test_bus_wait_until (history_rows_are, window, GUINT_TO_POINTER (2));

    /* Held, so that a row dropped and added again could not come back at the
     * same address and pass for the one kept. */
    g_autoptr (AdwSidebarItem) work = g_object_ref (sidebar_item (window, "work"));

    old.hold_listings = TRUE;
    old.listings = 0;
    g_signal_emit_by_name (client, "histories-changed");
    g_paste_test_bus_wait_for_count (&old.listings, 1);
    g_assert_nonnull (old.held_listing);

    /* A successor owning the name and sitting on the proxy's GetAll. */
    static const GDBusInterfaceVTable vtable = { on_held_get_all, NULL, NULL, { 0 } };
    GDBusMethodInvocation *get_all = NULL;
    g_autoptr (GDBusConnection) holder = g_paste_test_bus_connect (g_getenv ("DBUS_SESSION_BUS_ADDRESS"));
    guint registration = g_dbus_connection_register_object (holder, G_PASTE_DAEMON_OBJECT_PATH,
                                                            g_paste_daemon3_interface_info (),
                                                            &vtable, &get_all, NULL, &error);

    g_assert_no_error (error);

    guint holder_owner = g_bus_own_name_on_connection (holder, G_PASTE_BUS_NAME,
                                                       G_BUS_NAME_OWNER_FLAGS_REPLACE | G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT | G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE,
                                                       NULL, NULL, NULL, NULL);

    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_STARTING);
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &get_all, NULL);
    g_assert_false (sidebar_enabled (window, "work"));
    g_assert_true (sidebar_enabled (window, G_PASTE_DEFAULT_HISTORY));

    /* The old listing answering now, as a daemon that kept nothing: given
     * up on, it prunes nothing. */
    g_paste_daemon3_complete_list_histories (old.skeleton, g_steal_pointer (&old.held_listing), g_variant_new_parsed (LISTING_ONE));
    g_paste_test_bus_round_trip (bus, old.connection);
    g_assert_true (sidebar_item (window, "work") == work);

    /* What the old daemon says meanwhile reaches nobody
     * (g_paste_client_g_signal ()); nor would anything ask it if it did, which
     * the client announcing the changes itself shows. */
    old.listings = 0;
    old.sizings = 0;
    g_paste_daemon3_emit_raw_histories_changed (old.skeleton);
    g_paste_daemon3_emit_raw_update (old.skeleton, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, "", 0);
    g_signal_emit_by_name (client, "histories-changed");
    g_signal_emit_by_name (client, "update", G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, "", (guint64) 0);
    g_paste_test_bus_round_trip (bus, old.connection);
    g_assert_cmpuint (old.listings, ==, 0);
    g_assert_cmpuint (old.sizings, ==, 0);

    /* A context menu left up on the row that was just disabled acts on
     * nothing (g_paste_ui_panel_get_menu_history ()): a backup would open
     * its dialog. */
    AdwSidebar *sidebar = ADW_SIDEBAR (find_widget (window->panel, ADW_TYPE_SIDEBAR));

    g_signal_emit_by_name (sidebar, "setup-menu", sidebar_item (window, "work"));
    g_assert_true (gtk_widget_activate_action (window->panel, "panel.backup-history", NULL));
    drain ();
    g_assert_null (adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (window)));

    /* A serving successor takes the name over from the one that held. */
    g_dbus_method_invocation_return_value (g_steal_pointer (&get_all), g_variant_new_parsed ("(@a{sv} {},)"));
    fake_daemon_start (&successor);
    g_paste_test_bus_wait_for_owner (G_DBUS_PROXY (client), successor.connection);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_test_bus_wait_for_count (&successor.listings, 1);
    g_paste_test_bus_wait_for_count (&successor.sizings, 1);
    g_assert_false (sidebar_enabled (window, "work"));
    g_assert_true (history_rows_are (window, GUINT_TO_POINTER (0)));

    /* The successor's current history is named by the daemon there now, listed
     * or not (on_history_changed ()): its row can be clicked before the
     * listing says so. */
    g_paste_daemon3_set_history (successor.skeleton, "work");
    g_paste_test_bus_wait_until (sidebar_row_enabled, window, "work");
    g_assert_true (sidebar_enabled (window, "work"));

    g_paste_daemon3_complete_list_histories (successor.skeleton, g_steal_pointer (&successor.held_listing), g_variant_new_parsed (LISTING_THREE));
    g_paste_daemon3_complete_get_history_size (successor.skeleton, g_steal_pointer (&successor.held_size), successor.size);
    g_paste_test_bus_wait_until (sidebar_has_history, window, "other");
    g_paste_test_bus_wait_until (history_rows_are, window, GUINT_TO_POINTER (1));
    g_paste_test_bus_round_trip (bus, successor.connection);
    g_assert_true (sidebar_item (window, "work") == work);
    g_assert_true (sidebar_enabled (window, "work"));
    g_assert_cmpuint (successor.listings, ==, 1);
    g_assert_cmpuint (successor.sizings, ==, 1);

    /* The same menu acts on the row once it can be clicked. */
    g_assert_true (gtk_widget_activate_action (window->panel, "panel.backup-history", NULL));
    drain ();
    g_assert_nonnull (adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (window)));
    adw_dialog_force_close (adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (window)));

    gtk_window_destroy (GTK_WINDOW (window));
    drain ();
    fake_daemon_stop (&successor);
    g_dbus_connection_close_sync (holder, NULL, NULL);
    g_dbus_connection_unregister_object (holder, registration);
    g_bus_unown_name (holder_owner);
    fake_daemon_stop (&old);
}

/* A read failing because its daemon left without answering -- a crash with
 * the call out -- is no failure to report: no warning, no "Could Not Load
 * History", no "No Pinned Items", no critical from the sidebar, the presence
 * saying what comes next (g_paste_client_is_daemon_gone_error ()). Warnings
 * and criticals being fatal, either report would abort the case. */
static void
test_listing_daemon_gone (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteUiWindow) window = test_window ();
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    FakeDaemon daemon = { .size = 1, .hold_sizes = TRUE, .hold_listings = TRUE };

    g_assert_no_error (error);
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_ui_window_setup (window, client);
    g_paste_test_bus_wait_for_count (&daemon.sizings, 1);
    g_paste_test_bus_wait_for_count (&daemon.listings, 1);

    AdwStatusPage *status = find_status_page (GTK_WIDGET (window->history));
    g_autofree gchar *title = g_strdup (adw_status_page_get_title (status));
    GdkPaintable *paintable = adw_status_page_get_paintable (status);

    return_gone (g_steal_pointer (&daemon.held_size));
    return_gone (g_steal_pointer (&daemon.held_listing));
    g_paste_test_bus_round_trip (g_dbus_proxy_get_connection (G_DBUS_PROXY (client)), daemon.connection);
    /* The page waiting for rows stays as it was. */
    g_assert_cmpstr (adw_status_page_get_title (status), ==, title);
    g_assert_true (adw_status_page_get_paintable (status) == paintable);

    /* Nor is a filter read failing so an answer that nothing is pinned. */
    daemon.filters_gone = TRUE;
    g_paste_ui_history_set_favourites (window->history, TRUE);
    g_paste_test_bus_wait_for_count (&daemon.filters, 1);
    g_paste_test_bus_round_trip (g_dbus_proxy_get_connection (G_DBUS_PROXY (client)), daemon.connection);
    g_assert_cmpstr (adw_status_page_get_title (status), !=, _("No Pinned Items"));

    gtk_window_destroy (GTK_WINDOW (window));
    drain ();
    fake_daemon_stop (&daemon);
}

/* The proxy can say a daemon is ready, its history cached, before it has
 * learnt who owns the name: learning it is no handoff, so the sidebar and the
 * list ask that daemon once, not again when its owner turns up (the bus
 * announced no owner straight to another: on_name_owner_changed ()). */
static void
test_owner_learned (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteUiWindow) window = test_window ();
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    FakeDaemon daemon = { .size = 1 };

    g_assert_no_error (error);
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_ui_window_setup (window, client);
    g_paste_test_bus_wait_for_owner (G_DBUS_PROXY (client), daemon.connection);
    g_paste_test_bus_wait_until (list_shown, window, NULL);
    g_paste_test_bus_round_trip (g_dbus_proxy_get_connection (G_DBUS_PROXY (client)), daemon.connection);
    g_assert_cmpuint (daemon.listings, ==, 1);
    g_assert_cmpuint (daemon.sizings, ==, 1);

    gtk_window_destroy (GTK_WINDOW (window));
    drain ();
    fake_daemon_stop (&daemon);
}

/* A successor whose history size cannot be read leaves no rows to keep
 * showing: the list says so, with Retry, rather than a spinner forever, and a
 * retry that succeeds brings the rows and the list's focus back. */
static void
test_listing_owner_size_error (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteUiWindow) window = test_window ();
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    FakeDaemon old = { .size = 2, .allow_replacement = TRUE };
    FakeDaemon successor = { .size = 1, .replace = TRUE, .hold_sizes = TRUE };

    g_assert_no_error (error);
    fake_daemon_start (&old);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_ui_window_setup (window, client);
    g_paste_test_bus_wait_until (history_rows_are, window, GUINT_TO_POINTER (2));
    gtk_widget_grab_focus (find_widget (GTK_WIDGET (window->history), GTK_TYPE_LIST_VIEW));
    g_assert_true (history_has_focus (window));

    fake_daemon_start (&successor);
    g_paste_test_bus_wait_for_owner (G_DBUS_PROXY (client), successor.connection);
    g_paste_test_bus_wait_for_count (&successor.sizings, 1);
    g_assert_true (history_rows_are (window, GUINT_TO_POINTER (0)));

    AdwStatusPage *status = find_status_page (GTK_WIDGET (window->history));
    GLogLevelFlags fatal = g_log_set_always_fatal (G_LOG_LEVEL_ERROR);

    for (guint attempt = 0; attempt < 2; ++attempt)
    {
        g_assert_nonnull (successor.held_size);
        g_dbus_method_invocation_return_dbus_error (g_steal_pointer (&successor.held_size),
                                                     "org.gnome.GPaste.Test.Size", "size failed");
        g_paste_test_bus_round_trip (g_dbus_proxy_get_connection (G_DBUS_PROXY (client)), successor.connection);
        g_assert_cmpint (g_paste_client_get_daemon_presence (client), ==, G_PASTE_DAEMON_PRESENCE_READY);
        g_assert_true (history_rows_are (window, GUINT_TO_POINTER (0)));
        g_assert_cmpstr (adw_status_page_get_title (status), ==, _("Could Not Load History"));
        g_assert_null (adw_status_page_get_paintable (status));

        GtkWidget *retry = adw_status_page_get_child (status);

        g_assert_nonnull (retry);
        g_assert_true (gtk_widget_get_visible (retry));
        g_assert_true (gtk_widget_grab_focus (retry));
        g_signal_emit_by_name (retry, "clicked");
        g_paste_test_bus_wait_for_count (&successor.sizings, attempt + 2);
        g_assert_nonnull (adw_status_page_get_paintable (status));
    }

    g_log_set_always_fatal (fatal);
    successor.hold_sizes = FALSE;
    g_paste_daemon3_complete_get_history_size (successor.skeleton, g_steal_pointer (&successor.held_size), successor.size);
    g_paste_test_bus_wait_until (history_rows_are, window, GUINT_TO_POINTER (1));
    g_assert_false (gtk_widget_get_visible (GTK_WIDGET (status)));
    g_paste_test_bus_wait_until (focus_returned, window, NULL);
    g_assert_true (history_list_has_focus (window));

    gtk_window_destroy (GTK_WINDOW (window));
    drain ();
    fake_daemon_stop (&successor);
    fake_daemon_stop (&old);
}

/* The list that had the focus when the daemon went has it again once the
 * daemon is back and its rows are. */
static void
test_focus_restored (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteUiWindow) window = test_window ();
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    FakeDaemon daemon = { .size = 2 };

    /* Built before the daemon is there, as in test_retried_client (). */
    g_assert_no_error (error);
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_ui_window_setup (window, client);
    g_paste_test_bus_wait_for_count (&daemon.sizings, 1);
    drain ();
    gtk_widget_grab_focus (find_widget (GTK_WIDGET (window->history), GTK_TYPE_LIST_VIEW));
    g_assert_true (history_has_focus (window));
    g_assert_false (focus_moved_away (window, NULL));

    fake_daemon_stop (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_STARTING);
    g_paste_test_bus_wait_until (focus_moved_away, window, NULL);
    g_assert_false (history_has_focus (window));

    daemon.sizings = 0;
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_test_bus_wait_until (focus_returned, window, NULL);
    g_assert_true (history_has_focus (window));

    /* But not from where the user put it while the daemon was away. */
    GtkWidget *elsewhere = GTK_WIDGET (g_paste_ui_header_get_favourites_button (window->header));

    fake_daemon_stop (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_STARTING);
    g_paste_test_bus_wait_until (focus_moved_away, window, NULL);
    /* Taken off the window's list of what acts on a daemon for the test's
     * sake: what the user can reach meanwhile is sensitive. */
    gtk_widget_set_sensitive (elsewhere, TRUE);
    g_assert_true (gtk_widget_grab_focus (elsewhere));
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_test_bus_wait_until (list_shown, window, NULL);
    g_assert_true (gtk_root_get_focus (GTK_ROOT (window)) == elsewhere);

    gtk_window_destroy (GTK_WINDOW (window));
    drain ();
    fake_daemon_stop (&daemon);
}

static gboolean
shows_text (GtkWidget   *widget,
            const gchar *text)
{
    if (GTK_IS_INSCRIPTION (widget) && g_paste_str_equal (gtk_inscription_get_text (GTK_INSCRIPTION (widget)), text))
        return TRUE;
    if (GTK_IS_LABEL (widget) && g_paste_str_equal (gtk_label_get_text (GTK_LABEL (widget)), text))
        return TRUE;

    for (GtkWidget *child = gtk_widget_get_first_child (widget); child; child = gtk_widget_get_next_sibling (child))
    {
        if (shows_text (child, text))
            return TRUE;
    }

    return FALSE;
}

/* A row's upload is armed once it shows a text item, which is what its fetch
 * lands with. */
static gboolean
upload_armed (gconstpointer window,
              gconstpointer arg G_GNUC_UNUSED)
{
    GtkWidget *row = find_widget (GTK_WIDGET (((GPasteUiWindow *) window)->history), G_PASTE_TYPE_UI_ITEM);

    return row && shows_text (row, "value 0");
}

/* The toast saying how the upload went, which is all the window does with it. */
static gboolean
upload_toasted (gconstpointer window,
                gconstpointer arg G_GNUC_UNUSED)
{
    return shows_text (GTK_WIDGET (window), _("The item was uploaded, and its address copied"));
}

static gboolean
upload_row_unparented (gconstpointer row,
                       gconstpointer arg G_GNUC_UNUSED)
{
    return gtk_widget_get_root (GTK_WIDGET (row)) == NULL;
}

static gboolean
upload_held (gconstpointer daemon,
             gconstpointer arg G_GNUC_UNUSED)
{
    return ((const FakeDaemon *) daemon)->held_upload != NULL;
}

/* A history refresh may replace the row while its upload is still out: the
 * outcome is the window's to report all the same, the row off the list having
 * no window to toast in. */
static void
test_upload_after_row_removed (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteUiWindow) window = test_window ();
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    FakeDaemon daemon = { .size = 1, .hold_upload = TRUE };

    g_assert_no_error (error);
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_ui_window_setup (window, client);
    g_paste_test_bus_wait_until (upload_armed, window, NULL);

    g_autoptr (GPasteUiItem) row = g_object_ref (G_PASTE_UI_ITEM (find_widget (GTK_WIDGET (window->history), G_PASTE_TYPE_UI_ITEM)));

    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (row), "item.upload", NULL));
    g_paste_test_bus_wait_until (upload_held, &daemon, NULL);

    daemon.size = 0;
    g_paste_daemon3_emit_raw_update (daemon.skeleton, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, "", 0);
    g_paste_test_bus_wait_until (upload_row_unparented, row, NULL);
    g_assert_null (gtk_widget_get_root (GTK_WIDGET (row)));

    g_paste_daemon3_complete_upload_and_copy (daemon.skeleton, g_steal_pointer (&daemon.held_upload), UPLOADED_URL);
    g_paste_test_bus_wait_until (upload_toasted, window, NULL);
    g_assert_true (upload_toasted (window, NULL));

    gtk_window_destroy (GTK_WINDOW (window));
    drain ();
    fake_daemon_stop (&daemon);
}

/* An upload offered from a row goes through UploadAndCopy, the daemon keeping
 * the address, and the window says so. */
static void
test_upload (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteUiWindow) window = test_window ();
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    FakeDaemon daemon = { .size = 1 };

    g_assert_no_error (error);
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    g_paste_ui_window_setup (window, client);
    g_paste_test_bus_wait_until (upload_armed, window, NULL);
    g_assert_true (upload_armed (window, NULL));

    g_assert_true (gtk_widget_activate_action (find_widget (GTK_WIDGET (window->history), G_PASTE_TYPE_UI_ITEM), "item.upload", NULL));
    g_paste_test_bus_wait_for_count (&daemon.uploads, 1);
    g_paste_test_bus_wait_until (upload_toasted, window, NULL);
    g_assert_true (upload_toasted (window, NULL));

    gtk_window_destroy (GTK_WINDOW (window));
    drain ();
    fake_daemon_stop (&daemon);
}

static gboolean
showing_history (gconstpointer window,
                 gconstpointer arg G_GNUC_UNUSED)
{
    AdwNavigationSplitView *split_view = ((GPasteUiWindow *) window)->split_view;

    return adw_navigation_split_view_get_collapsed (split_view) && adw_navigation_split_view_get_show_content (split_view);
}

/* The widest window buttons g_paste_ui_window_init () sizes the minimum for:
 * the window's icon, then minimize, maximize and close (KDE Plasma's). The
 * icon only shows for a window that names one, as the application's does. */
#define WIDE_LAYOUT "icon:minimize,maximize,close"

static GPasteUiWindow *
wide_buttons_window (void)
{
    GPasteUiWindow *window = test_window ();

    gtk_window_set_icon_name (GTK_WINDOW (window), G_PASTE_ICON_NAME);

    return window;
}

static gboolean
collapsed_is (gconstpointer window,
              gconstpointer collapsed)
{
    return adw_navigation_split_view_get_collapsed (((GPasteUiWindow *) window)->split_view) == GPOINTER_TO_INT (collapsed);
}

static gboolean
first_frame_painted (gconstpointer window,
                     gconstpointer arg G_GNUC_UNUSED)
{
    GdkFrameClock *clock = gtk_widget_get_frame_clock (GTK_WIDGET (window));

    return clock && gdk_frame_clock_get_frame_counter (clock) >= 1;
}

/* Waits for the window's first frame to be painted: a size asked of it before
 * then is lost. Returns its frame, what the default size has over the width the
 * breakpoint measures -- client-side decorations, whose size is the theme's. */
static gint
settle (GPasteUiWindow *window)
{
    gint width;

    g_paste_test_bus_wait_until (first_frame_painted, window, NULL);
    g_assert_true (first_frame_painted (window, NULL));
    gtk_window_get_default_size (GTK_WINDOW (window), &width, NULL);

    return width - gtk_widget_get_width (GTK_WIDGET (window));
}

/* Resized to a content width of @content, the window being settle ()d with a
 * frame of @frame: on Xvfb, with no window manager, it takes the size asked of
 * it. Waited for until the split view says @collapsed, which is asserted; what
 * then has to fit is checked by the pump that follows, libadwaita warning --
 * fatally, in a test -- when it does not, and nothing announcing that it will
 * not. */
static void
resize_to_content (GPasteUiWindow *window,
                   gint            frame,
                   gint            content,
                   gboolean        collapsed)
{
    gtk_window_set_default_size (GTK_WINDOW (window), content + frame, 400);
    g_paste_test_bus_wait_until (collapsed_is, window, GINT_TO_POINTER (collapsed));
    g_assert_cmpint (adw_navigation_split_view_get_collapsed (window->split_view), ==, collapsed);
    g_paste_test_bus_pump (300);
}

/* The text scale, as GNOME's Large Text and Tweaks' Scaling Factor set it. */
static void
set_text_scale (gdouble scale)
{
    g_object_set (gtk_settings_get_default (), "gtk-xft-dpi", (gint) (96 * 1024 * scale), NULL);
}

/* The window at its minimum width (see g_paste_ui_window_init ()), with the
 * widest window buttons, showing the history page, which a window opened that
 * narrow collapses to: nothing in it asks for more width than that, in any of
 * its states, nor at the text scales up to the 1.5 the minimum is sized for --
 * which libadwaita warns about otherwise, a warning the test aborts on. What is
 * measured is only measured once shown, hence the history page asserted first,
 * and the fixed pumps after each state change: nothing announces the warning
 * not coming. */
static void
test_minimum_width (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteUiWindow) window = wide_buttons_window ();
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    FakeDaemon daemon = { .size = 3 };
    GtkSettings *settings = gtk_settings_get_default ();
    g_autofree gchar *layout = NULL;
    gint min_width, min_height, dpi;

    g_assert_no_error (error);
    gtk_widget_get_size_request (GTK_WIDGET (window), &min_width, &min_height);
    g_object_get (settings, "gtk-decoration-layout", &layout, "gtk-xft-dpi", &dpi, NULL);
    g_object_set (settings, "gtk-decoration-layout", WIDE_LAYOUT, NULL);
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    gtk_window_set_default_size (GTK_WINDOW (window), min_width, min_height);
    g_paste_ui_window_setup (window, client);
    g_paste_test_bus_wait_until (showing_history, window, NULL);
    g_assert_true (showing_history (window, NULL));
    g_assert_true (adw_header_bar_get_show_title (window->header));
    g_paste_test_bus_pump (300);

    /* Collapsed from the start, the list keeps the initial focus rather than
     * losing it to the history page's header. */
    g_paste_test_bus_wait_until (focus_returned, window, NULL);
    g_assert_true (history_has_focus (window));

    /* Large Text, and past it. */
    set_text_scale (1.25);
    g_paste_test_bus_pump (300);
    set_text_scale (1.5);
    g_paste_test_bus_pump (300);
    set_text_scale (1);

    /* The other states the window has at that width: picking items to merge,
     * the search bar up, and the banner offering a retry with no daemon. */
    on_enter_selection_mode (NULL, window);
    g_paste_test_bus_pump (300);
    on_cancel_selection_mode (NULL, window);
    gtk_search_bar_set_search_mode (window->search_bar, TRUE);
    g_paste_test_bus_pump (300);
    fake_daemon_stop (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_ABSENT);
    g_paste_test_bus_pump (300);
    g_assert_true (adw_banner_get_revealed (window->banner));

    gtk_window_destroy (GTK_WINDOW (window));
    drain ();
    g_object_set (settings, "gtk-decoration-layout", layout, "gtk-xft-dpi", dpi, NULL);
}

/* Both sides of G_PASTE_UI_WINDOW_COLLAPSE_WIDTH with the widest window
 * buttons: collapsed at it, side by side one pixel above, where the sidebar
 * and the history page's header have to fit together -- at a text scale below
 * 1, where the breakpoint is the one in px, and at Large Text's, where it is
 * the one in sp. */
static void
test_collapse_width (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteUiWindow) window = wide_buttons_window ();
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    FakeDaemon daemon = { .size = 3 };
    GtkSettings *settings = gtk_settings_get_default ();
    g_autofree gchar *layout = NULL;
    gint dpi;

    g_assert_no_error (error);
    g_object_get (settings, "gtk-decoration-layout", &layout, "gtk-xft-dpi", &dpi, NULL);
    g_object_set (settings, "gtk-decoration-layout", WIDE_LAYOUT, NULL);
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    gtk_window_set_default_size (GTK_WINDOW (window), 900, 400);
    g_paste_ui_window_setup (window, client);

    gint frame = settle (window);

    resize_to_content (window, frame, G_PASTE_UI_WINDOW_COLLAPSE_WIDTH, TRUE);
    resize_to_content (window, frame, G_PASTE_UI_WINDOW_COLLAPSE_WIDTH + 1, FALSE);

    set_text_scale (0.9);
    resize_to_content (window, frame, G_PASTE_UI_WINDOW_COLLAPSE_WIDTH, TRUE);
    resize_to_content (window, frame, G_PASTE_UI_WINDOW_COLLAPSE_WIDTH + 1, FALSE);

    set_text_scale (1.25);
    resize_to_content (window, frame, G_PASTE_UI_WINDOW_COLLAPSE_WIDTH * 5 / 4, TRUE);
    resize_to_content (window, frame, G_PASTE_UI_WINDOW_COLLAPSE_WIDTH * 5 / 4 + 1, FALSE);

    gtk_window_destroy (GTK_WINDOW (window));
    drain ();
    fake_daemon_stop (&daemon);
    g_object_set (settings, "gtk-decoration-layout", layout, "gtk-xft-dpi", dpi, NULL);
}

/* A window narrowed while the list has the focus keeps it there, the history
 * page shown; and one narrowed again after the user went back to the
 * histories shows the history once more (on_split_view_uncollapsed ()). */
static void
test_resize (void)
{
    if (!have_display)
    {
        g_test_skip ("A private Xvfb display is required");
        return;
    }

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteUiWindow) window = test_window ();
    g_autoptr (GPasteClient) client = g_paste_client_new_sync (&error);
    FakeDaemon daemon = { .size = 3 };

    g_assert_no_error (error);
    fake_daemon_start (&daemon);
    wait_for_presence (client, G_PASTE_DAEMON_PRESENCE_READY);
    gtk_window_set_default_size (GTK_WINDOW (window), 900, 400);
    g_paste_ui_window_setup (window, client);
    g_paste_test_bus_wait_until (list_shown, window, NULL);
    g_assert_true (gtk_widget_grab_focus (find_widget (GTK_WIDGET (window->history), GTK_TYPE_LIST_VIEW)));

    gint frame = settle (window);

    resize_to_content (window, frame, 550, TRUE);
    g_assert_true (showing_history (window, NULL));
    g_assert_true (history_has_focus (window));

    /* Back to the histories, wider, then narrow again. The transition back is
     * let finish first: uncollapsed halfway through it, libadwaita warns of a
     * page snapshotted with no allocation. */
    guint hidden = 0;
    AdwNavigationPage *content = adw_navigation_split_view_get_content (window->split_view);
    gulong handler = g_signal_connect_swapped (content, "hidden", G_CALLBACK (g_paste_test_bus_count_emission), &hidden);

    adw_navigation_split_view_set_show_content (window->split_view, FALSE);
    g_paste_test_bus_wait_for_count (&hidden, 1);
    g_assert_cmpuint (hidden, ==, 1);
    g_signal_handler_disconnect (content, handler);
    resize_to_content (window, frame, 890, FALSE);
    resize_to_content (window, frame, 550, TRUE);
    g_assert_true (showing_history (window, NULL));

    gtk_window_destroy (GTK_WINDOW (window));
    drain ();
    fake_daemon_stop (&daemon);
}

int
main (int argc, char **argv)
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DISPLAY);
    have_display = g_paste_test_env_has_display () && gtk_init_check ();
    g_test_init (&argc, &argv, NULL);

    if (have_display)
    {
        g_autoptr (GError) error = NULL;

        adw_init ();
        app = GTK_APPLICATION (adw_application_new ("org.gnome.GPaste.Ui.Test", G_APPLICATION_NON_UNIQUE));
        g_application_register (G_APPLICATION (app), NULL, &error);
        g_assert_no_error (error);
    }

    g_test_add_func ("/ui/daemon-presence/follow", test_follow);
    g_test_add_func ("/ui/daemon-presence/retried-client", test_retried_client);
    g_test_add_func ("/ui/daemon-presence/search-closed-away", test_search_closed_away);
    g_test_add_func ("/ui/daemon-presence/nothing-asked", test_nothing_asked);
    g_test_add_func ("/ui/daemon-presence/entry-focus-released", test_entry_focus_released);
    g_test_add_func ("/ui/daemon-presence/merge-entry-focus-released", test_merge_entry_focus_released);
    g_test_add_func ("/ui/daemon-presence/reply-after-close", test_reply_after_close);
    g_test_add_func ("/ui/daemon-presence/listing-after-switch", test_listing_after_switch);
    g_test_add_func ("/ui/daemon-presence/listing-overtaken", test_listing_overtaken);
    g_test_add_func ("/ui/daemon-presence/listing-abandoned", test_listing_abandoned);
    g_test_add_func ("/ui/daemon-presence/listing-after-close", test_listing_after_close);
    g_test_add_func ("/ui/daemon-presence/listing-pruned", test_listing_pruned);
    g_test_add_func ("/ui/daemon-presence/listing-handoff", test_listing_handoff);
    g_test_add_func ("/ui/daemon-presence/listing-owner-size-error", test_listing_owner_size_error);
    g_test_add_func ("/ui/daemon-presence/owner-learned", test_owner_learned);
    g_test_add_func ("/ui/daemon-presence/listing-daemon-gone", test_listing_daemon_gone);
    g_test_add_func ("/ui/daemon-presence/upload", test_upload);
    g_test_add_func ("/ui/daemon-presence/upload-after-row-removed", test_upload_after_row_removed);
    g_test_add_func ("/ui/daemon-presence/focus-restored", test_focus_restored);
    g_test_add_func ("/ui/daemon-presence/minimum-width", test_minimum_width);
    g_test_add_func ("/ui/daemon-presence/collapse-width", test_collapse_width);
    g_test_add_func ("/ui/daemon-presence/resize", test_resize);
    return g_paste_test_env_run ();
}
