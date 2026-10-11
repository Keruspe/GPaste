// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-error.h>
#include <gpaste-3/gpaste-gdbus-defines.h>
#include <gpaste-3/gpaste-gsettings-keys.h>
#include <gpaste-3/gpaste-settings.h>
#include <gpaste-3/gpaste-util.h>

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    GPasteClient *client;
    gint          argc;
    const gchar **args;
    gchar        *pipe_data;
    gboolean      help;
    gboolean      version;
    gboolean      favourites;
    gboolean      oneline;
    gboolean      raw;
    gboolean      reverse;
    gboolean      use_index;
    gboolean      zero;
    guint         timeout;
    gboolean      timeout_given;
    const gchar  *decoration;
    const gchar  *separator;
} Context;

/*
 * Utility functions
 */

/* Whether @arg is a whole unsigned decimal number no greater than @max, in
 * @value: digits alone, as an index or a timeout is written.
 * g_ascii_string_to_unsigned () refuses a sign or a space in front, but tells
 * a space by g_ascii_isspace (), which a vertical tab is not, while the
 * strtoull () beneath it skips one: the digit asked of the first character is
 * what keeps "\v-1" from reading as G_MAXUINT64. */
static gboolean
parse_unsigned (const gchar *arg,
                guint64      max,
                guint64     *value)
{
    return g_ascii_isdigit (*arg) && g_ascii_string_to_unsigned (arg, 10, 0, max, value, NULL);
}

static gboolean
parse_cmdline (int     *argc,
               char   **argv[],
               Context *ctx,
               GError **error)
{
    struct option long_options[] = {
        { "decoration", required_argument, NULL,  'd'  },
        { "favourites", no_argument,       NULL,  'f'  },
        { "help",       no_argument,       NULL,  'h'  },
        { "oneline",    no_argument,       NULL,  'o'  },
        { "raw",        no_argument,       NULL,  'r'  },
        { "reverse",    no_argument,       NULL,  'e'  },
        { "separator",  required_argument, NULL,  's'  },
        { "timeout",    required_argument, NULL,  't'  },
        { "use-index",  no_argument,       NULL,  'i'  },
        { "version",    no_argument,       NULL,  'v'  },
        { "zero",       no_argument,       NULL,  'z'  },
        { NULL,         no_argument,       NULL,  '\0' }
    };
    gint64 c;

    while ((c = getopt_long (*argc, *argv, "d:fhores:t:ivz", long_options, NULL)) != -1)
    {
        switch (c)
        {
        case 'd':
            ctx->decoration = optarg;
            break;
        case 'f':
            ctx->favourites = TRUE;
            break;
        case 'h':
            ctx->help = TRUE;
            break;
        case 'o':
            ctx->oneline = TRUE;
            break;
        case 'r':
            ctx->raw = TRUE;
            break;
        case 'e':
            ctx->reverse = TRUE;
            break;
        case 's':
            ctx->separator = optarg;
            break;
        case 't':
        {
            guint64 timeout = 0;

            /* 0 is a timeout of its own -- a password that is never taken off
             * the clipboard -- so a malformed value has to be refused rather
             * than fall to it: "-0" or " 0" included. Past the range is refused
             * for the same reason: the item caps what it is given, and a number
             * silently becoming a smaller one is a password expiring at a time
             * nobody asked for. */
            if (!parse_unsigned (optarg, G_PASTE_PASSWORD_TIMEOUT_MAX, &timeout))
            {
                g_set_error (error, G_PASTE_ERROR, G_PASTE_ERROR_INVALID_ARGUMENT, _("Invalid timeout: %s"), optarg);
                return FALSE;
            }

            ctx->timeout = (guint) timeout;
            ctx->timeout_given = TRUE;
            break;
        }
        case 'i':
            ctx->use_index = TRUE;
            break;
        case 'v':
            ctx->version = TRUE;
            break;
        case 'z':
            ctx->zero = TRUE;
            break;
        default:
            ctx->help = TRUE;
            return FALSE;
        }
    }

    *argc -= optind;
    *argv += optind;

    /* argv[0] is the verb and ctx->args the arguments after it. With no verb at
     * all (a plain "gpaste-client", or one only fed through a pipe) there is
     * nothing past it: stay on argv's own NULL terminator rather than stepping
     * over it, which ctx->args[0] would then read out of bounds. */
    ctx->argc = (*argc > 0) ? *argc - 1 : 0;
    ctx->args = (const gchar **) *argv + ((*argc > 0) ? 1 : 0);

    return TRUE;
}

/* What is piped in, whole, in @data -- %NULL for nothing piped. A read that
 * fails, or input carrying a NUL byte, which no D-Bus string can carry, is
 * refused rather than cut short: a prefix of the input would otherwise go for
 * all of it, and an empty one for nothing piped -- the listing, then, on a
 * line with no verb. */
static gboolean
extract_pipe_data (gchar  **data,
                   GError **error)
{
    *data = NULL;

    if (isatty (STDIN_FILENO))
        return TRUE;

    g_autoptr (GString) input = g_string_new (NULL);
    gchar buffer[8192];
    gsize length;

    while ((length = fread (buffer, 1, sizeof (buffer), stdin)) > 0)
        g_string_append_len (input, buffer, (gssize) length);

    if (ferror (stdin))
    {
        gint saved_errno = errno;

        g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved_errno), _("Could not read the input: %s"), g_strerror (saved_errno));
        return FALSE;
    }

    if (memchr (input->str, '\0', input->len))
    {
        g_set_error_literal (error, G_PASTE_ERROR, G_PASTE_ERROR_INVALID_ARGUMENT, _("Cannot use input carrying a NUL byte."));
        return FALSE;
    }

    if (input->len)
        *data = g_string_free_and_steal (g_steal_pointer (&input));

    return TRUE;
}

/* @text as it goes out: on a terminal, a control character an application put
 * on the clipboard -- an escape sequence retitling the window, clearing it or
 * writing the terminal's own clipboard -- is shown as U+FFFD rather than acted
 * on, line breaks and tabs aside: a line feed, a tab, and the carriage return
 * of a CRLF line ending, which takes the cursor nowhere the line feed after it
 * does not -- a lone one, writing over its line, is shown. Anywhere else it is
 * written as it is, a script reading the bytes the item holds.
 *
 * Whether stdout is a terminal is asked once, a listing printing many items,
 * and what lies between two such characters is written in one go. */
static void
print_text (const gchar *text)
{
    static gint terminal = -1;

    if (terminal < 0)
        terminal = isatty (STDOUT_FILENO);

    if (!terminal)
    {
        fputs (text, stdout);
        return;
    }

    const gchar *run = text;

    for (const gchar *c = text; *c; c = g_utf8_next_char (c))
    {
        gunichar u = g_utf8_get_char (c);

        if (u == '\n' || u == '\t' || (u == '\r' && c[1] == '\n') || !g_unichar_iscntrl (u))
            continue;

        fwrite (run, 1, (gsize) (c - run), stdout);
        fputs ("\xef\xbf\xbd", stdout);
        run = g_utf8_next_char (c);
    }

    fputs (run, stdout);
}

static void
print_history_line (const gchar *line,
                    guint        index,
                    const gchar *uuid,
                    Context     *ctx)
{
    if (!ctx->raw)
    {
        if (ctx->use_index)
            printf ("%u: ", index);
        else
            printf ("%s: ", uuid);
    }
    g_autofree gchar *oneline = (ctx->oneline) ? g_paste_util_one_line (line) : NULL;

    print_text ((oneline) ?: line);
    putchar ((ctx->zero) ? '\0' : '\n');
}

static gint
spawn (const gchar *app)
{
    g_autoptr (GError) error = NULL;

    if (!g_paste_util_spawn_sync (app, &error))
    {
        /* Translators: %s is the program GPaste tried to start, then the reason it could not. */
        g_printerr (_("Couldn’t spawn %s: %s"), app, error->message);
        g_printerr ("\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static void show_help (FILE *out);

static void
show_version (void)
{
    printf ("%s\n", PACKAGE_STRING);
}

/*
 * GPaste commands
 */

static gint
g_paste_help (Context *ctx   G_GNUC_UNUSED,
              GError **error G_GNUC_UNUSED)
{
    show_help (stdout);

    return EXIT_SUCCESS;
}

static gint
g_paste_version (Context *ctx   G_GNUC_UNUSED,
                 GError **error G_GNUC_UNUSED)
{
    show_version ();

    return EXIT_SUCCESS;
}

static gint
g_paste_flag_action (Context *ctx,
                     GError **error)
{
    if (ctx->help)
        return g_paste_help (ctx, error);
    if (ctx->version)
        return g_paste_version (ctx, error);
    return -1;
}

/* Whether a flag was given that, on a line with no verb, only the history
 * listing reads: g_paste_history () and print_history_line () read every one of
 * them, and the add that a verb-less line can also be reads none. A flag either
 * of those two comes to read belongs here, and in the client test's list and
 * the man page's DESCRIPTION with it (see dispatch_reads_stdin ()). */
static gboolean
listing_flags_given (const Context *ctx)
{
    return ctx->favourites || ctx->oneline || ctx->raw || ctx->reverse || ctx->use_index || ctx->zero;
}

static gint
g_paste_history (Context *ctx,
                 GError **error)
{
    /* The whole history even under --favourites, where GetFavourites would
     * answer the pinned items alone: what this prints beside each row is where
     * it sits in the history, which only a listing of the history knows. The
     * graphical front ends have no such number to print and do ask for the
     * short listing. */
    g_autolist (GPasteClientItem) history = g_paste_client_get_history_sync (ctx->client, error);

    if (*error)
        return EXIT_FAILURE;

    guint length = g_list_length (history);
    guint index = 0;

    for (const GList *i = (ctx->reverse ? g_list_last (history) : history); i; i = ctx->reverse ? i->prev : i->next)
    {
        GPasteClientItem *item = i->data;
        /* Where the item sits in the history, which is what --use-index takes
         * back: counted over every item, printed or not (a filtered listing
         * that renumbered its rows would name a different item on the way in),
         * and from the far end when the walk runs backwards. */
        guint position = (ctx->reverse) ? length - 1 - index : index;

        ++index;

        if (ctx->favourites && !g_paste_client_item_is_favourite (item))
            continue;

        print_history_line (g_paste_client_item_get_value (item), position, g_paste_client_item_get_uuid (item), ctx);
    }

    return EXIT_SUCCESS;
}

/* Activating the UI app's own action, not a round trip through the daemon: the
 * daemon did nothing else with this than make the same call, and a client that
 * only wants the about dialog has no reason to need a daemon at all. */
static gint
g_paste_about (Context *ctx G_GNUC_UNUSED,
               GError **error)
{
    /* Synchronously: there is no main loop here to finish an asynchronous call
     * from, so the process would exit before anything reached the bus. */
    if (!g_paste_util_activate_ui_sync ("about", NULL, error))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* A daemon that honours the re-exec tears its D-Bus connection down before
 * replying, so @triggered is already TRUE for the expected missing reply; this
 * only covers an unsupported method by signalling the recorded daemon pid. */
static gboolean
reexec_fallback (GPasteClient *client,
                 gboolean      triggered,
                 GError      **error)
{
    /* Only an unsupported method warrants the legacy signal path, which
     * g_paste_util_reexecute_legacy_daemon () takes only for a daemon that
     * predates this interface. A daemon's refusal (including an expiry
     * deadline) must reach the caller unchanged, and so does the unsupported
     * method when the signal cannot be sent. */
    if (!triggered && error && g_error_matches (*error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD) &&
        g_paste_util_reexecute_legacy_daemon (client, NULL))
    {
        g_clear_error (error);
        triggered = TRUE;
    }

    /* One of this version owning the name before it exports anything -- still
     * settling its storage, a prompt up for that maybe -- answers the same
     * unknown method, which GDBus words as an object missing at a path: what
     * is the matter is said instead. */
    if (!triggered && error && g_error_matches (*error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD) &&
        g_paste_client_get_daemon_presence (client) == G_PASTE_DAEMON_PRESENCE_STARTING)
    {
        g_clear_error (error);
        g_set_error_literal (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED,
                             _("The daemon is still starting, settling its storage: try again once it serves"));
    }

    return triggered;
}

static gint
g_paste_daemon_reexec (Context *ctx,
                       GError **error)
{
    if (!reexec_fallback (ctx->client, g_paste_util_reexecute_daemon (ctx->client, error), error))
        return EXIT_FAILURE;

    printf ("%s\n", _("Successfully re-executed the daemon"));

    return EXIT_SUCCESS;
}

static gint
g_paste_migrate (Context *ctx,
                 GError **error)
{
    /* Through the daemon, and through the very same re-exec as above: the gate
     * is opened here (reset the revision, sync) so that its next start flushes,
     * re-runs the migration and reloads the chosen backend, rather than another
     * process racing the running daemon.
     *
     * Closed again only once the fallback has failed too, and not on the failure
     * the method call reports: the signal reexec_fallback () sends is that same
     * re-exec happening by another route, and a gate closed before it would hand
     * the successor nothing to migrate while this printed a success. */
    guint64 revision = g_paste_util_prepare_storage_migration ();

    if (!reexec_fallback (ctx->client, g_paste_util_reexecute_daemon (ctx->client, error), error))
    {
        g_paste_util_cancel_storage_migration (revision);
        return EXIT_FAILURE;
    }

    printf ("%s\n", _("Successfully triggered the storage migration"));

    return EXIT_SUCCESS;
}

static gint
g_paste_change_passphrase (Context *ctx,
                           GError **error)
{
    /* The daemon owns the prompts (it is the one that can show them, through its
     * own GPastePrompt backend), so this only asks it to. The passphrases are
     * never sent over the bus. */
    g_paste_client_change_passphrase_sync (ctx->client, error);

    if (*error)
        return EXIT_FAILURE;

    printf ("%s\n", _("Successfully triggered the passphrase change"));

    return EXIT_SUCCESS;
}

static gint
g_paste_daemon_version (Context *ctx,
                        GError **error)
{
    /* As in current_history (): the cached property is empty until the daemon
     * has answered for it, and again once it leaves the bus. */
    g_autofree gchar *v = g_paste_client_get_version (ctx->client);

    if (!v)
    {
        g_set_error_literal (error, G_PASTE_ERROR, G_PASTE_ERROR_NOT_FOUND, _("Couldn’t get the version of the daemon."));
        return EXIT_FAILURE;
    }

    printf ("%s\n", v);

    return EXIT_SUCCESS;
}

/* The history a command acts on when none was named on the command line. It
 * comes off the proxy's cached property, which is empty until the daemon has
 * answered for it and again once it leaves the bus -- and a %NULL name must not
 * travel on to a call that expects one, where it would be built as
 * g_variant_new ("(s)", NULL). */
static gchar *
current_history (Context *ctx,
                 GError **error)
{
    gchar *name = g_paste_client_get_history_name (ctx->client);

    if (!name)
        g_set_error_literal (error, G_PASTE_ERROR, G_PASTE_ERROR_NOT_FOUND, _("Couldn’t get the name of the current history."));

    return name;
}

static gint
g_paste_empty (Context *ctx,
               GError **error)
{
    g_autofree gchar *name = (ctx->argc) ? g_strdup (ctx->args[0]) : current_history (ctx, error);

    if (*error)
        return EXIT_FAILURE;

    g_paste_client_empty_history_sync (ctx->client, name, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_get_history (Context *ctx,
                     GError **error)
{
    g_autofree gchar *name = current_history (ctx, error);

    if (*error)
        return EXIT_FAILURE;

    printf ("%s\n", name);

    return EXIT_SUCCESS;
}

static gint
g_paste_history_size (Context *ctx,
                      GError **error)
{
    guint64 size = g_paste_client_get_history_size_sync (ctx->client, error);

    if (*error)
        return EXIT_FAILURE;

    printf ("%" G_GUINT64_FORMAT "\n", size);

    return EXIT_SUCCESS;
}

static gint
g_paste_list_histories (Context *ctx,
                        GError **error)
{
    g_autolist (GPasteClientHistory) histories = g_paste_client_list_histories_sync (ctx->client, error);

    if (*error)
        return EXIT_FAILURE;

    /* The names alone: this is what a script enumerating histories reads, the
     * sizes the listing also carries having gpaste-client history-size and the
     * graphical panel for a home. Ended as the items are, --zero included: a
     * history's name may hold a line break of its own. */
    for (const GList *h = histories; h; h = h->next)
        printf ("%s%c", g_paste_client_history_get_name (h->data), (ctx->zero) ? '\0' : '\n');

    return EXIT_SUCCESS;
}

static gint
g_paste_delete_history (Context *ctx,
                        GError **error)
{
    g_autofree gchar *name = (ctx->argc) ? g_strdup (ctx->args[0]) : current_history (ctx, error);

    if (*error)
        return EXIT_FAILURE;

    g_paste_client_delete_history_sync (ctx->client, name, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_preferences (Context *ctx   G_GNUC_UNUSED,
                     GError **error G_GNUC_UNUSED)
{
    return spawn ("Preferences");
}

static gint
g_paste_show_history (Context *ctx,
                      GError **error)
{
    g_paste_client_show_history_sync (ctx->client, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_start (Context *ctx,
               GError **error)
{
    g_paste_client_set_active_sync (ctx->client, TRUE, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_stop (Context *ctx,
              GError **error)
{
    g_paste_client_set_active_sync (ctx->client, FALSE, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_ui (Context *ctx   G_GNUC_UNUSED,
            GError **error G_GNUC_UNUSED)
{
    return spawn ("Ui");
}

static gint
g_paste_add (Context *ctx,
             GError **error)
{
    const gchar *data = (ctx->argc > 0) ? ctx->args[0] : ctx->pipe_data;
    g_autofree gchar *uuid = g_paste_client_add_text_sync (ctx->client, data, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

/* 0 is a timeout of its own -- a password that is never taken back off -- so
 * without --timeout, AddPassword is sent the one the settings carry. Not asked
 * for the default (G_PASTE_PASSWORD_TIMEOUT_UNCHANGED): a GPaste 51.0 daemon,
 * still running after the update that brought this client, takes that for a
 * duration, eighteen hours of it. */
static guint
password_timeout (Context *ctx)
{
    if (ctx->timeout_given)
        return ctx->timeout;

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    return (guint) g_paste_settings_get_password_timeout (settings);
}

static gint
g_paste_add_password (Context *ctx,
                      GError **error)
{
    const gchar *data = (ctx->argc > 1) ? ctx->args[1] : ctx->pipe_data;

    /* A piped password stands in for the argument, which has no newline at its
     * end: the one ending the line the command printing it wrote is not part of
     * it, as `pass -c` leaves it off the first line it copies. One alone, and
     * nothing else cut: the password itself could end in a line break, which
     * nobody types as an argument either, and what else a store prints is the
     * caller's to leave out (the man page). */
    g_autofree gchar *line = (data == ctx->pipe_data && g_str_has_suffix (data, "\n")) ? g_strndup (data, strlen (data) - 1) : NULL;
    g_autofree gchar *uuid = g_paste_client_add_password_sync (ctx->client, ctx->args[0], (line) ? line : data, password_timeout (ctx), error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_backup_history (Context *ctx,
                        GError **error)
{
    guint64 index = 0;
    g_autofree gchar *name = (ctx->argc > 1) ? g_strdup (ctx->args[index++]) : current_history (ctx, error);

    if (*error)
        return EXIT_FAILURE;

    g_paste_client_backup_history_sync (ctx->client, name, ctx->args[index], error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_delete (Context *ctx,
                GError **error)
{
    g_paste_client_delete_item_sync (ctx->client, ctx->args[0], error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_favourite (Context *ctx,
                   GError **error)
{
    g_paste_client_set_favourite_sync (ctx->client, ctx->args[0], TRUE, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_unfavourite (Context *ctx,
                     GError **error)
{
    g_paste_client_set_favourite_sync (ctx->client, ctx->args[0], FALSE, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_delete_password (Context *ctx,
                         GError **error)
{
    g_paste_client_delete_password_sync (ctx->client, ctx->args[0], error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_file (Context *ctx,
              GError **error)
{
    g_autofree gchar *uuid = g_paste_client_add_file_sync (ctx->client, ctx->args[0], error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_get (Context *ctx,
             GError **error)
{
    g_autoptr (GPasteClientItem) item = g_paste_client_get_item_sync (ctx->client, ctx->args[0], error);

    if (*error)
        return EXIT_FAILURE;

    print_text (g_paste_client_item_get_value (item));

    return EXIT_SUCCESS;
}

static gint
g_paste_replace (Context *ctx,
                 GError **error)
{
    const gchar *data = (ctx->argc > 1) ? ctx->args[1] : ctx->pipe_data;
    g_autofree gchar *uuid = g_paste_client_replace_sync (ctx->client, ctx->args[0], data, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_strip_rich_text (Context *ctx,
                         GError **error)
{
    g_paste_client_strip_rich_text_sync (ctx->client, ctx->args[0], error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_search (Context *ctx,
                GError **error)
{
    g_autolist (GPasteClientItem) items = g_paste_client_search_sync (ctx->client, ctx->args[0], error);

    if (*error)
        return EXIT_FAILURE;

    /* Under --use-index, the number beside each match is where it sits in the
     * history, as the listing prints it: the number --use-index reads back.
     * Search answers the matching items alone, so their places come off a
     * listing of the history, and a match gone from it by then is gone. That
     * listing brings every item whole for its uuid alone, as every item
     * crosses the bus (the item's doc in org.gnome.GPaste3.xml): a method
     * answering where a uuid sits would be a change of the interface for this
     * one option. */
    g_autoptr (GHashTable) positions = NULL;

    if (items && ctx->use_index && !ctx->raw)
    {
        g_autolist (GPasteClientItem) history = g_paste_client_get_history_sync (ctx->client, error);

        if (*error)
            return EXIT_FAILURE;

        positions = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

        guint position = 0;

        for (const GList *h = history; h; h = h->next)
            g_hash_table_insert (positions, g_strdup (g_paste_client_item_get_uuid (h->data)), GUINT_TO_POINTER (position++));
    }

    for (const GList *i = (ctx->reverse ? g_list_last (items) : items); i; i = ctx->reverse ? i->prev : i->next)
    {
        GPasteClientItem *item = i->data;
        const gchar *uuid = g_paste_client_item_get_uuid (item);
        gpointer position = NULL;

        if (ctx->favourites && !g_paste_client_item_is_favourite (item))
            continue;
        if (positions && !g_hash_table_lookup_extended (positions, uuid, NULL, &position))
            continue;

        print_history_line (g_paste_client_item_get_value (item), GPOINTER_TO_UINT (position), uuid, ctx);
    }

    return EXIT_SUCCESS;
}

static gint
g_paste_select (Context *ctx,
                GError **error)
{
    g_paste_client_select_sync (ctx->client, ctx->args[0], error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_switch_history (Context *ctx,
                        GError **error)
{
    g_paste_client_switch_history_sync (ctx->client, ctx->args[0], error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_upload (Context *ctx,
                GError **error)
{
    g_autofree gchar *url = g_paste_client_upload_sync (ctx->client, ctx->args[0], error);

    if (*error)
        return EXIT_FAILURE;

    printf ("%s\n", url);

    return EXIT_SUCCESS;
}

/* What the keyboard shortcut does, and prints the url as upload does: see
 * UploadAndCopy in the D-Bus XML. */
static gint
g_paste_upload_and_copy (Context *ctx,
                         GError **error)
{
    g_autofree gchar *url = g_paste_client_upload_and_copy_sync (ctx->client, ctx->args[0], error);

    if (*error)
        return EXIT_FAILURE;

    printf ("%s\n", url);

    return EXIT_SUCCESS;
}

static gint
g_paste_make_password (Context *ctx,
                       GError **error)
{
    /* Without --timeout there is nothing to write: an item that is already a
     * password keeps the countdown it carries -- the settings' default would
     * cancel a running one -- and a text item becoming one takes that default
     * anyway, which is what the sentinel asks the daemon for. */
    guint timeout = (ctx->timeout_given) ? ctx->timeout : G_PASTE_PASSWORD_TIMEOUT_UNCHANGED;
    g_autofree gchar *uuid = g_paste_client_make_password_sync (ctx->client, ctx->args[0], ctx->args[1], timeout, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

static gint
g_paste_merge (Context *ctx,
               GError **error)
{
    g_autofree gchar *uuid = g_paste_client_merge_sync (ctx->client, ctx->decoration, ctx->separator, ctx->args, error);

    return (*error) ? EXIT_FAILURE : EXIT_SUCCESS;
}

/*
 * Main
 */

/* What a verb needs of the daemon: nothing; a client, for one that copes with
 * a daemon not serving (yet, or ever: one older than this interface); or a
 * daemon serving, waited for. */
typedef enum
{
    CLIENT_NONE,
    CLIENT_ANY,
    CLIENT_READY,
} ClientNeed;

typedef struct
{
    gint         argc;         /* the least argv count, verb included */
    const gchar *verb;         /* canonical name, NULL for the verb-less forms */
    const gchar *aliases;      /* space separated, or NULL */
    gint         extra_args;
    ClientNeed   needs_client;
    gboolean     reads_pipe;   /* takes its last argument from stdin when it is absent */
    gint         item_args;    /* leading item arguments, G_MAXINT for all */
    const gchar *args;         /* what --help shows after the verb */
    const gchar *doc;          /* untranslated */
    gint       (*handler) (Context *ctx,
                           GError **error);
} Command;

/* Every verb GPaste answers to, in the order --help lists them. Carrying the
 * usage and the description here rather than in a second list is what stops a
 * verb from being added without them; tests/completions checks the shell
 * completions and the man page against it. */
static const Command commands[] = {
        /* The verb-less forms, first: a flag with no verb at all, then a piped-in
         * item or, with nothing at all, the history. */
        { 0, NULL, NULL, G_MAXINT, CLIENT_NONE,   FALSE, 0,        NULL, NULL, g_paste_flag_action },
        { 0, NULL, NULL, 0,        CLIENT_READY,  TRUE,  0,        NULL, NULL, g_paste_add         },
        { 0, NULL, NULL, 0,        CLIENT_READY,  FALSE, 0,        NULL, NULL, g_paste_history     },

        { 1, "history",           "h",               0,        CLIENT_READY,  FALSE, 0,        NULL,                         N_ ("print the history with UUIDs"),                                                            g_paste_history },
        { 1, "history-size",      "hs",              0,        CLIENT_READY,  FALSE, 0,        NULL,                         N_ ("print the size of the current history"),                                                   g_paste_history_size },
        { 2, "search",            NULL,              0,        CLIENT_READY,  FALSE, 0,        N_ ("<pattern>"),             N_ ("print the items of the history matching <pattern>"),                                       g_paste_search },
        { 1, "get-history",       "gh",              0,        CLIENT_READY,  FALSE, 0,        NULL,                         N_ ("get the name of the current history"),                                                     g_paste_get_history },
        { 2, "backup-history",    "bh",              1,        CLIENT_READY,  FALSE, 0,        N_ ("[<history>] <backup>"),  N_ ("back up <history>, the current one if not given, as <backup>"),                            g_paste_backup_history },
        { 2, "switch-history",    "sh",              0,        CLIENT_READY,  FALSE, 0,        N_ ("<name>"),                N_ ("switch to another history"),                                                               g_paste_switch_history },
        { 1, "delete-history",    "dh",              1,        CLIENT_READY,  FALSE, 0,        N_ ("[<name>]"),              N_ ("delete the history <name>, the current one if not given"),                                 g_paste_delete_history },
        { 1, "list-histories",    "lh",              0,        CLIENT_READY,  FALSE, 0,        NULL,                         N_ ("list available histories"),                                                                g_paste_list_histories },
        { 1, "add",               "a",               1,        CLIENT_READY,  TRUE,  0,        N_ ("<text>"),                N_ ("set text to clipboard"),                                                                   g_paste_add },
        { 2, "add-password",      "ap",              1,        CLIENT_READY,  TRUE,  0,        N_ ("<name> [<password>]"),   N_ ("add the <name> / <password> pair to the clipboard, <password> read from stdin if absent"), g_paste_add_password },
        { 2, "get",               "g",               0,        CLIENT_READY,  FALSE, 1,        N_ ("<uuid>"),                N_ ("get the item <uuid> from the history"),                                                    g_paste_get },
        { 2, "select",            "s set",           0,        CLIENT_READY,  FALSE, 1,        N_ ("<uuid>"),                N_ ("set the item <uuid> from the history to the clipboard"),                                   g_paste_select },
        { 2, "replace",           NULL,              1,        CLIENT_READY,  TRUE,  1,        N_ ("<uuid> <contents>"),     N_ ("replace the contents of the item <uuid> from the history with the provided one"),          g_paste_replace },
        { 2, "strip-rich-text",   "srt",             0,        CLIENT_READY,  FALSE, 1,        N_ ("<uuid>"),                N_ ("drop the rich text flavours of the item <uuid>, keeping the plain text it shows"),          g_paste_strip_rich_text },
        { 3, "merge",             "m",               G_MAXINT, CLIENT_READY,  FALSE, G_MAXINT, N_ ("<uuid> … <uuid>"),       N_ ("merge the items matching the UUIDs from the history and put the result in the clipboard"), g_paste_merge },
        { 3, "make-password",     "mp",              0,        CLIENT_READY,  FALSE, 1,        N_ ("<uuid> <name>"),         N_ ("make the item <uuid> from the history a password named <name>, or update one that is"),    g_paste_make_password },
        { 2, "delete",            "d del rm remove", 0,        CLIENT_READY,  FALSE, 1,        N_ ("<uuid>"),                N_ ("delete item <uuid> from the history"),                                                     g_paste_delete },
        { 2, "favourite",         "fav",             0,        CLIENT_READY,  FALSE, 1,        N_ ("<uuid>"),                N_ ("pin item <uuid> so the history never drops it automatically"),                             g_paste_favourite },
        { 2, "unfavourite",       "unfav",           0,        CLIENT_READY,  FALSE, 1,        N_ ("<uuid>"),                N_ ("unpin item <uuid>, letting the history drop it again"),                                    g_paste_unfavourite },
        { 2, "delete-password",   "dp",              0,        CLIENT_READY,  FALSE, 0,        N_ ("<name>"),                N_ ("delete the password <name> from the history"),                                             g_paste_delete_password },
        { 2, "file",              "f",               0,        CLIENT_READY,  FALSE, 0,        N_ ("<path>"),                N_ ("put the content of the file at <path> into the clipboard"),                                g_paste_file },
        { 1, "empty",             "e",               1,        CLIENT_READY,  FALSE, 0,        N_ ("[<name>]"),              N_ ("empty the history <name>, the current one if not given"),                                  g_paste_empty },
        { 1, "start",             "d daemon",        0,        CLIENT_READY,  FALSE, 0,        NULL,                         N_ ("start tracking clipboard changes"),                                                        g_paste_start },
        { 1, "stop",              "q quit",          0,        CLIENT_READY,  FALSE, 0,        NULL,                         N_ ("stop tracking clipboard changes"),                                                         g_paste_stop },
        { 1, "daemon-reexec",     "dr",              0,        CLIENT_ANY,    FALSE, 0,        NULL,                         N_ ("re-execute the daemon (after upgrading it, for instance)"),                                g_paste_daemon_reexec },
        { 1, "migrate",           NULL,              0,        CLIENT_ANY,    FALSE, 0,        NULL,                         N_ ("migrate the history to a different storage backend"),                                      g_paste_migrate },
        { 1, "change-passphrase", NULL,              0,        CLIENT_READY,  FALSE, 0,        NULL,                         N_ ("change the passphrase of the encrypted history"),                                          g_paste_change_passphrase },
        { 1, "preferences",       "p settings",      0,        CLIENT_NONE,   FALSE, 0,        NULL,                         N_ ("launch the configuration tool"),                                                           g_paste_preferences },
        { 1, "ui",                NULL,              0,        CLIENT_NONE,   FALSE, 0,        NULL,                         N_ ("launch the graphical tool"),                                                               g_paste_ui },
        { 1, "show-history",      NULL,              0,        CLIENT_READY,  FALSE, 0,        NULL,                         N_ ("make the GNOME Shell extension display the history"),                                      g_paste_show_history },
        { 2, "upload",            "u",               0,        CLIENT_READY,  FALSE, 1,        N_ ("<uuid>"),                N_ ("upload the item <uuid> to a pastebin service"),                                            g_paste_upload },
        { 2, "upload-and-copy",   "uc",              0,        CLIENT_READY,  FALSE, 1,        N_ ("<uuid>"),                N_ ("upload the item <uuid> to a pastebin service, and copy its address"),                      g_paste_upload_and_copy },
        { 1, "version",           "v",               0,        CLIENT_NONE,   FALSE, 0,        NULL,                         N_ ("display the version"),                                                                     g_paste_version },
        { 1, "daemon-version",    "dv",              0,        CLIENT_READY,  FALSE, 0,        NULL,                         N_ ("display the daemon version"),                                                              g_paste_daemon_version },
        { 1, "help",              NULL,              0,        CLIENT_NONE,   FALSE, 0,        NULL,                         N_ ("display this help"),                                                                       g_paste_help },
        { 1, "about",             NULL,              0,        CLIENT_NONE,   FALSE, 0,        NULL,                         N_ ("display the about dialog"),                                                                g_paste_about },
};

/* Whether a command line takes its content from a pipe. Three verbs fall back
 * to it for a missing trailing argument, and the verb-less form is nothing but
 * a pipe -- they are the ones reading ctx->pipe_data, and the only reason to go
 * near stdin at all. Reading it for anything else would hang behind a pipe
 * nobody is going to write to or close, which is what a verb run from a script
 * with its stdin left open is.
 *
 * Which verbs those are is @reads_pipe on the row itself: a second list of them
 * here would be one more place to remember when a verb is added, and commands[]
 * is where a verb is described.
 *
 * The pipe stands in for an argument, so it is only wanted when that argument
 * is absent -- which is @command matching at its shortest, every one of these
 * taking the content last. */
static gboolean
command_reads_stdin (const Command *command,
                     gint           argc)
{
    return command->reads_pipe && argc == command->argc;
}

/* @verb is the canonical name or any of the aliases. */
static gboolean
command_matches (const Command *command,
                 const gchar   *verb)
{
    if (g_paste_str_equal (verb, command->verb))
        return TRUE;

    if (!command->aliases)
        return FALSE;

    for (const gchar *a = command->aliases; *a; )
    {
        const gchar *end = strchr (a, ' ');
        gsize len = (end) ? (gsize) (end - a) : strlen (a);

        if (!strncmp (verb, a, len) && !verb[len])
            return TRUE;

        a = (end) ? end + 1 : a + len;
    }

    return FALSE;
}

/* Whether @command is the row a command line of @argc arguments starting with
 * @verb dispatches to: the arity it takes, plus the verb when there is one.
 * Asked twice -- once before the dispatch, to know whether stdin is wanted, and
 * once by the dispatch itself -- and written once, so the two cannot answer
 * differently about the same command line. */
static gboolean
command_matches_line (const Command *command,
                      gint           argc,
                      const gchar   *verb)
{
    /* @argc is the least a row takes, @extra_args how many more: G_MAXINT for
     * as many as are given, which still leaves @argc the least. */
    if (argc < command->argc || (command->extra_args != G_MAXINT && argc > command->argc + command->extra_args))
        return FALSE;

    return !(argc > 0 && command->verb) || command_matches (command, verb);
}

/* On @out: stdout when asked for, stderr when the command line was wrong, so
 * that a command substitution never takes the help for the command's output.
 * A translation goes in through %s, never as the format: one carrying a % sign
 * would be read as a directive, which nothing checks a string without one for. */
static void
show_help (FILE *out)
{
    const char *progname = g_get_prgname ();

    fprintf (out, "%s\n", _("Usage:"));

    for (guint64 i = 0; i < G_N_ELEMENTS (commands); ++i)
    {
        const Command *c = &commands[i];

        if (!c->verb)
            continue;

        /* The history is what running with no verb at all does, so it is shown
         * the way it is actually reached. */
        if (g_paste_str_equal (c->verb, "history"))
            fprintf (out, "  %s [%s]: %s\n", progname, c->verb, _(c->doc));
        else if (c->args)
            fprintf (out, "  %s %s %s: %s\n", progname, c->verb, _(c->args), _(c->doc));
        else
            fprintf (out, "  %s %s: %s\n", progname, c->verb, _(c->doc));
    }

    /* Not a verb: the item comes in on stdin. */
    /* Translators: the metavariable standing for any command the user pipes into GPaste. */
    fprintf (out, "  %s | %s: %s\n", _("<command>"), progname, _("copy the output of <command> to the clipboard"));

    fprintf (out, "\n");
    fprintf (out, "%s\n", _("General options:"));
    /* The verbs' own words: each option does what its verb does. */
    fprintf (out, "  -h, --help: %s\n", _("display this help"));
    fprintf (out, "  -v, --version: %s\n", _("display the version"));

    fprintf (out, "\n");
    fprintf (out, "%s\n", _("Convenience options:"));
    /* Translators: help for --favourites */
    fprintf (out, "  -f, --favourites: %s\n", _("only display the pinned items"));
    /* Translators: help for --timeout */
    fprintf (out, "  -t, --timeout: %s\n", _("how long a password stays on the clipboard, in seconds; 0 for as long as anything else"));
    /* Translators: help for --use-index */
    fprintf (out, "  -i, --use-index: %s\n", _("use the index of the item instead of its UUID"));

    fprintf (out, "\n");
    fprintf (out, "%s\n", _("Display options:"));
    /* Translators: help for --oneline */
    fprintf (out, "  -o, --oneline: %s\n", _("display each item on only one line"));
    /* Translators: help for --raw */
    fprintf (out, "  -r, --raw: %s\n", _("display each item raw (without line numbers)"));
    /* Translators: help for --reverse */
    fprintf (out, "  -e, --reverse: %s\n", _("display the items in reverse order"));
    /* Translators: help for --zero */
    fprintf (out, "  -z, --zero: %s\n", _("use a NUL character instead of a new line between each item"));

    fprintf (out, "\n");
    fprintf (out, "%s\n", _("Merge options:"));
    /* Translators: help for --decoration <string> */
    fprintf (out, "  -d, --decoration <%s>: %s\n", _("string"), _("add the given decoration to the beginning and the end of each item before merging"));
    /* Translators: help for --separator <string> */
    fprintf (out, "  -s, --separator <%s>: %s\n", _("string"), _("add the given separator between each item when merging"));
}

/* Whether this command line can reach a command that reads a pipe, tried with
 * the same match g_paste_dispatch () runs: the pipe has to be read before the
 * dispatch, since with no verb at all it is the presence of piped data that
 * decides between adding it and printing the history.
 *
 * The three handlers reading stdin themselves when their argument is missing
 * would behave the same -- the verb-less add already declines with nothing
 * piped, handing the line on to the history -- and would need neither this
 * pass nor @reads_pipe. It is kept on purpose: @reads_pipe makes which command
 * lines touch stdin a fact commands[] states, where every other property of a
 * verb lives (see AGENTS.md on single sources of truth), and a fact the client
 * test can check row by row without running a handler, a daemon or a pipe. A
 * handler reading on demand would make it something only running each verb can
 * tell. */
static gboolean
dispatch_reads_stdin (gint           argc,
                      const gchar   *verb,
                      const Context *ctx)
{
    /* A flag that is an action of its own answers with no verb at all, which is
     * the shape the verb-less add has too -- and its row comes first, so --help
     * and --version are answered before anything asks for a pipe. That order is
     * the half the loop below cannot express, matching rows rather than running
     * them. */
    if (ctx->help || ctx->version)
        return FALSE;

    /* A verb-less line carrying one of the listing's own flags is that listing,
     * answered without a look at stdin. A plain "gpaste-client" still reads it,
     * having nothing but the pipe to tell an add from a listing by. */
    if (argc == 0 && listing_flags_given (ctx))
        return FALSE;

    for (guint64 i = 0; i < G_N_ELEMENTS (commands); ++i)
    {
        const Command *c = &commands[i];

        if (command_matches_line (c, argc, verb) && command_reads_stdin (c, argc))
            return TRUE;
    }

    return FALSE;
}

/* Whether every string a command line carries -- its arguments, what is
 * piped in for one, merge's decoration and separator -- is UTF-8, as a D-Bus
 * string has to be. GLib would otherwise send "[Invalid UTF-8]" in its place,
 * and the daemon act on that: switch to a history of that name, or store a
 * password under it. A path is no exception: AddFile takes it as a string and
 * opens the file the string names, so one that is not UTF-8 cannot reach the
 * daemon at all, and converting it would name a file that is not there. */
static gboolean
check_utf8 (const Context *ctx,
            GError       **error)
{
    const gchar *options[] = { ctx->pipe_data, ctx->decoration, ctx->separator };
    gboolean valid = TRUE;

    for (gint i = 0; valid && i < ctx->argc; ++i)
        valid = g_utf8_validate (ctx->args[i], -1, NULL);
    for (guint i = 0; valid && i < G_N_ELEMENTS (options); ++i)
        valid = !options[i] || g_utf8_validate (options[i], -1, NULL);

    if (!valid)
        g_set_error_literal (error, G_PASTE_ERROR, G_PASTE_ERROR_INVALID_ARGUMENT, _("Cannot use arguments or input that are not valid UTF-8."));

    return valid;
}

/* The positions @command's leading item arguments name under --use-index,
 * every one of them checked before any is looked up.
 *
 * An index is a complete unsigned decimal number (parse_unsigned ()):
 * g_ascii_strtoull () alone would read "abc" as 0 and "1x" as 1, and act on
 * items nobody named. */
static GArray *
item_indexes (const Command *command,
              const Context *ctx,
              GError       **error)
{
    gint count = MIN (ctx->argc, command->item_args);
    g_autoptr (GArray) indexes = g_array_sized_new (FALSE, FALSE, sizeof (guint64), (guint) count);

    for (gint i = 0; i < count; ++i)
    {
        const gchar *arg = ctx->args[i];
        guint64 index = 0;

        if (!parse_unsigned (arg, G_MAXUINT64, &index))
        {
            g_set_error (error, G_PASTE_ERROR, G_PASTE_ERROR_INVALID_ARGUMENT, _("Invalid item index: %s"), arg);
            return NULL;
        }

        g_array_append_val (indexes, index);
    }

    return g_steal_pointer (&indexes);
}

/* The arguments a command runs with under --use-index: the leading ones
 * @indexes came from swapped for the uuids of the items at those positions,
 * and the others -- text, a pattern, a name, a path -- going through as they
 * are.
 *
 * Several indexes are all read off one listing of the history: looked up one
 * call at a time, a copy landing between two calls would shift every later
 * index by one, and a merge would take an item twice and skip another. A
 * single index cannot be caught that way, and its own lookup spares reading
 * the listing, which carries the text of every item. */
static GStrv
resolve_item_args (const Context *ctx,
                   GArray        *indexes,
                   GError       **error)
{
    g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();

    if (indexes->len == 1)
    {
        g_autoptr (GPasteClientItem) item = g_paste_client_get_item_at_index_sync (ctx->client, g_array_index (indexes, guint64, 0), error);

        if (!item)
            return NULL;

        g_strv_builder_add (builder, g_paste_client_item_get_uuid (item));
    }
    else
    {
        g_autolist (GPasteClientItem) history = g_paste_client_get_history_sync (ctx->client, error);

        if (*error)
            return NULL;

        guint length = g_list_length (history);

        for (guint i = 0; i < indexes->len; ++i)
        {
            guint64 index = g_array_index (indexes, guint64, i);

            /* The error GetItemAtIndex answers past the end, so that one index
             * or several report it alike. */
            if (index >= length)
            {
                g_set_error (error, G_PASTE_ERROR, G_PASTE_ERROR_INVALID_INDEX, "No item at index %" G_GUINT64_FORMAT, index);
                return NULL;
            }

            g_strv_builder_add (builder, g_paste_client_item_get_uuid (g_list_nth_data (history, index)));
        }
    }

    for (gint i = (gint) indexes->len; i < ctx->argc; ++i)
        g_strv_builder_add (builder, ctx->args[i]);

    return g_strv_builder_end (builder);
}

typedef struct
{
    gboolean done;
    gboolean serving;
    GError  *error;
} DaemonWaited;

static void
on_daemon_waited (GObject      *source_object G_GNUC_UNUSED,
                  GAsyncResult *res,
                  gpointer      user_data)
{
    DaemonWaited *waited = user_data;

    waited->serving = g_paste_util_wait_for_daemon_finish (res, &waited->error);
    waited->done = TRUE;
}

/* Whether a daemon serves @client, waited for: see g_paste_util_wait_for_daemon
 * (), which this runs the main context for. */
static gboolean
wait_for_daemon (GPasteClient *client,
                 GError      **error)
{
    DaemonWaited waited = { 0 };

    g_paste_util_wait_for_daemon (client, NULL, on_daemon_waited, &waited);
    while (!waited.done)
        g_main_context_iteration (NULL, TRUE);

    if (waited.error)
        g_propagate_error (error, waited.error);

    return waited.serving;
}

static gint
g_paste_dispatch (gint         argc,
                  const gchar *verb,
                  Context     *ctx,
                  GError     **error)
{
    for (guint64 i = 0; i < G_N_ELEMENTS (commands); ++i)
    {
        const Command *c = &commands[i];

        if (command_matches_line (c, argc, verb))
        {
            g_autoptr (GArray) indexes = NULL;

            /* A command reading its last argument from a pipe that brought
             * nothing has nothing to send: not this row, then -- the listing
             * for a line with no verb, the help for any other -- and decided
             * before a client is built, which would have D-Bus start a daemon
             * for nothing. */
            if (command_reads_stdin (c, argc) && !ctx->pipe_data)
                continue;

            /* Only a command that talks to the daemon has its strings checked:
             * the launchers and the flag actions send nothing, and --help with
             * something stray after it still answers. */
            if (c->needs_client && !check_utf8 (ctx, error))
                return EXIT_FAILURE;

            if (ctx->use_index && c->item_args && ctx->argc)
            {
                indexes = item_indexes (c, ctx, error);
                if (!indexes)
                    return EXIT_FAILURE;
            }

            /* Building the client is what has D-Bus start the daemon, so it is
             * built for the commands that talk to it alone -- not for help,
             * version or a launcher -- and only once what they would send is
             * known to be well formed: refusing a malformed index or a string
             * that is not UTF-8 needs no daemon. */
            if (c->needs_client && !ctx->client)
            {
                ctx->client = g_paste_client_new_sync (error);
                if (!ctx->client)
                    return EXIT_FAILURE;
            }

            if (c->needs_client == CLIENT_READY && !wait_for_daemon (ctx->client, error))
                return EXIT_FAILURE;

            Context command_ctx = *ctx;
            g_auto (GStrv) resolved = NULL;

            if (indexes)
            {
                resolved = resolve_item_args (ctx, indexes, error);
                if (!resolved)
                    return EXIT_FAILURE;

                command_ctx.args = (const gchar **) resolved;
            }

            gint ret = c->handler (&command_ctx, error);
            if (ret >= 0)
                return ret;
        }
    }

    return -1;
}

gint
main (gint argc, gchar *argv[])
{
    G_PASTE_INIT_GETTEXT ();
    g_set_prgname (argv[0]);

    g_autoptr (GError) error = NULL;
    /* Every field starts empty, and says so once: a positional list has to be
     * recounted against the struct each time a member is added, and miscounting
     * it seeds a neighbouring flag instead — every field being a gboolean or a
     * pointer, nothing would say so. */
    Context ctx = { 0 };
    gint status = EXIT_SUCCESS;

    if (parse_cmdline (&argc, &argv, &ctx, &error))
    {
        g_autofree gchar *pipe_data = NULL;

        /* Only where a pipe is what the command line is for: extract_pipe_data ()
         * reads stdin to its end, and an end is not something every caller is
         * going to provide (see command_reads_stdin ()). */
        if (dispatch_reads_stdin (argc, (argc > 0) ? argv[0] : NULL, &ctx) && !extract_pipe_data (&pipe_data, &error))
            status = EXIT_FAILURE;
        else
        {
            ctx.pipe_data = pipe_data;
            status = g_paste_dispatch (argc, (argc > 0) ? argv[0] : NULL, &ctx, &error);
        }

        g_autoptr (GPasteClient) client = ctx.client;

        /* Nothing matched the command line at all: a verb we do not know, or one
         * we do with the wrong number of arguments after it. Say so the way a
         * bad option already does. Left alone, g_paste_dispatch ()'s "not mine"
         * sentinel travels out of main () as a status of its own -- 255 to a
         * shell, and not a word printed. */
        if (status < 0)
        {
            show_help (stderr);
            status = EXIT_FAILURE;
        }

        /* A refusal, a missing item, a bad argument: what the user is told
         * about, with the status saying it failed. Not a critical, which is
         * for a bug, and which G_DEBUG=fatal-criticals turns into a crash. */
        if (error)
        {
            g_printerr ("%s\n", error->message);
            status = EXIT_FAILURE;
        }
    }
    /* An option getopt () does not know it has named already, and the help
     * says which there are; one whose value is wrong is named here. */
    else if (error)
    {
        g_printerr ("%s\n", error->message);
        status = EXIT_FAILURE;
    }
    else
    {
        show_help (stderr);
        status = EXIT_FAILURE;
    }

    /* What a command prints is what it is run for -- a script saving an item,
     * a backup reading the history -- so output that could not be written is
     * a failure: a full disk, a closed pipe. */
    if (fflush (stdout) || ferror (stdout))
    {
        gint saved_errno = errno;

        g_printerr ("%s: %s\n", _("Could not write the output"), g_strerror (saved_errno));
        status = EXIT_FAILURE;
    }

    return status;
}
