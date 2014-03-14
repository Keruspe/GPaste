/*
 *      This file is part of GPaste.
 *
 *      Copyright 2012-2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include <gpaste-client.h>

#include <gio/gio.h>
#include <glib/gi18n-lib.h>

#include <stdio.h>
#include <stdlib.h>

static void
show_help (const gchar *caller)
{
    printf (_("Usage:\n"));
    /* Translators: help for gpaste history */
    printf ("  %s [history]: %s\n", caller, _("print the history with indexes"));
    /* Translators: help for gpaste backup-history <name> */
    printf ("  %s backup-history <%s>: %s\n", caller, _("name"), _("backup current history"));
    /* Translators: help for gpaste switch-history <name> */
    printf ("  %s switch-history <%s>: %s\n", caller, _("name"), _("switch to another history"));
    /* Translators: help for gpaste delete-history <name> */
    printf ("  %s delete-history <%s>: %s\n", caller, _("name"),  _("delete a history"));
    /* Translators: help for gpaste list-histories */
    printf ("  %s list-histories: %s\n", caller, _("list available histories"));
    /* Translators: help for gpaste raw-history */
    printf ("  %s raw-history: %s\n", caller, _("print the history without indexes"));
    /* Translators: help for gpaste zero-history */
    printf ("  %s zero-history: %s\n", caller, _("print the history with NUL as separator"));
    /* Translators: help for gpaste add <text> */
    printf ("  %s add <%s>: %s\n", caller, _("text"), _("set text to clipboard"));
    /* Translators: help for gpaste get <number> */
    printf ("  %s get <%s>: %s\n", caller, _("number"), _("get the <number>th item from the history"));
    /* Translators: help for gpaste select <number> */
    printf ("  %s select <%s>: %s\n", caller, _("number"), _("set the <number>th item from the history to the clipboard"));
    /* Translators: help for gpaste delete <number> */
    printf ("  %s delete <%s>: %s\n", caller, _("number"), _("delete <number>th item of the history"));
    /* Translators: help for gpaste file <path> */
    printf ("  %s file <%s>: %s\n", caller, _("path"), _("put the content of the file at <path> into the clipboard"));
    /* Translators: help for whatever | gpaste */
    printf ("  %s | %s: %s\n", _("whatever"), caller, _("set the output of whatever to clipboard"));
    /* Translators: help for gpaste empty */
    printf ("  %s empty: %s\n", caller, _("empty the history"));
    /* Translators: help for gpaste start */
    printf ("  %s start: %s\n", caller, _("start tracking clipboard changes"));
    /* Translators: help for gpaste stop */
    printf ("  %s stop: %s\n", caller, _("stop tracking clipboard changes"));
    /* Translators: help for gpaste quit */
    printf ("  %s quit: %s\n", caller, _("alias for stop"));
    /* Translators: help for gpaste daemon-reexec */
    printf ("  %s daemon-reexec: %s\n", caller, _("reexecute the daemon (after upgrading...)"));
    /* Translators: help for gpaste settings */
    printf ("  %s settings: %s\n", caller, _("launch the configuration tool"));
#ifdef ENABLE_APPLET
    /* Translators: help for gpaste applet */
    printf ("  %s applet: %s\n", caller, _("launch the applet"));
#endif
    /* Translators: help for gpaste version */
    printf ("  %s version: %s\n", caller, _("display the version"));
    /* Translators: help for gpaste help */
    printf ("  %s help: %s\n", caller, _("display this help"));
}

static void
show_version (void)
{
    printf ("%s\n", PACKAGE_STRING);
}

static void
show_history (GPasteClient *client,
              gboolean      raw,
              gboolean      zero,
              GError      **error)
{
    G_PASTE_CLEANUP_STRFREEV GStrv history = g_paste_client_get_history_sync (client, error);

    if (!*error)
    {
        unsigned int i = 0;

        for (GStrv h = history; *h; ++h)
        {
            if (!raw)
                printf ("%d: ", i++);
            printf ("%s%c", *h, (zero) ? '\0' : '\n');
        }
    }
}

static gboolean
is_help (const gchar *option)
{
    return (!g_strcmp0 (option, "help") ||
            !g_strcmp0 (option, "-h") ||
            !g_strcmp0 (option, "--help"));
}

static gboolean
is_version (const gchar *option)
{
    return (!g_strcmp0 (option, "v") ||
            !g_strcmp0 (option, "version") ||
            !g_strcmp0 (option, "-v") ||
            !g_strcmp0 (option, "--version"));
}

G_PASTE_NORETURN static void
failure_exit (GError *error)
{
    g_critical ("%s: %s\n", _("Couldn't connect to GPaste daemon"), (error) ? error->message: "unknwown error");
    exit (EXIT_FAILURE);
}

gint
main (gint argc, gchar *argv[])
{
    G_PASTE_INIT_GETTEXT ();

    if (argc > 1)
    {
        if (is_help (argv[1]))
        {
            show_help (argv[0]);
            return EXIT_SUCCESS;
        }
        else if (is_version (argv[1]))
        {
            show_version ();
            return EXIT_SUCCESS;
        }
    }

    int status = EXIT_SUCCESS;

    G_PASTE_CLEANUP_ERROR_FREE GError *error = NULL;
    G_PASTE_CLEANUP_UNREF GPasteClient *client = g_paste_client_new_sync (&error);

    if (!client)
        failure_exit (error);

    if (!isatty (fileno (stdin)))
    {
        /* We are being piped */
        G_PASTE_CLEANUP_STRING_FREE GString *data = g_string_new ("");
        gchar c;

        while ((c = fgetc (stdin)) != EOF)
            data = g_string_append_c (data, c);

        data->str[data->len - 1] = '\0';

        g_paste_client_add_sync (client, data->str, &error);
    }
    else
    {
        const gchar *arg1, *arg2;
        switch (argc)
        {
        case 1:
            show_history (client, FALSE, FALSE, &error);
            break;
        case 2:
            arg1 = argv[1];
            if (!g_strcmp0 (arg1, "start") ||
                !g_strcmp0 (arg1, "d") ||
                !g_strcmp0 (arg1, "daemon"))
            {
                g_paste_client_track_sync (client, TRUE, &error);
            }
            else if (!g_strcmp0 (arg1, "stop") ||
                     !g_strcmp0 (arg1, "q") ||
                     !g_strcmp0 (arg1, "quit"))
            {
                g_paste_client_track_sync (client, FALSE, &error);
            }
            else if (!g_strcmp0 (arg1, "e") ||
                     !g_strcmp0 (arg1, "empty"))
            {
                g_paste_client_empty_sync (client, &error);
            }
#ifdef ENABLE_APPLET
            else if (!g_strcmp0 (arg1, "applet"))
            {
                g_spawn_command_line_async (PKGLIBEXECDIR "/gpaste-applet", &error);
                if (error)
                {
                    g_critical ("%s: %s", _("Couldn't spawn gpaste-applet.\n"), error->message);
                    g_clear_error (&error);
                    status = EXIT_FAILURE;
                }
            }
#endif
            else if (!g_strcmp0 (arg1, "s") ||
                     !g_strcmp0 (arg1, "settings") ||
                     !g_strcmp0 (arg1, "p") ||
                     !g_strcmp0 (arg1, "preferences"))
            {
                execl (PKGLIBEXECDIR "/gpaste-settings", "GPaste-Settings", NULL);
            }
            else if (!g_strcmp0 (arg1, "dr") ||
                     !g_strcmp0 (arg1, "daemon-reexec"))
            {
                g_paste_client_reexecute_sync (client, &error);
                if (error && error->code == G_DBUS_ERROR_NO_REPLY)
                {
                    printf (_("Successfully reexecuted the daemon\n"));
                    return EXIT_SUCCESS;
                }
            }
            else if (!g_strcmp0 (arg1, "h") ||
                     !g_strcmp0 (arg1, "history"))
            {
                show_history (client, FALSE, FALSE, &error);
            }
            else if (!g_strcmp0 (arg1, "rh") ||
                     !g_strcmp0 (arg1, "raw-history"))
            {
                show_history (client, TRUE, FALSE, &error);
            }
            else if (!g_strcmp0 (arg1, "zh") ||
                     !g_strcmp0 (arg1, "zero-history"))
            {
                show_history (client, FALSE, TRUE, &error);
            }
            else if (!g_strcmp0 (arg1, "lh") ||
                     !g_strcmp0 (arg1, "list-histories"))
            {
                G_PASTE_CLEANUP_STRFREEV GStrv histories = g_paste_client_list_histories_sync (client, &error);
                if (!error)
                {
                    for (GStrv h = histories; *h; ++h)
                        printf ("%s\n", *h);
                }
            }
            else
            {
                show_help (argv[0]);
                status = EXIT_FAILURE;
            }
            break;
        case 3:
            arg1 = argv[1];
            arg2 = argv[2];
            if (!g_strcmp0 (arg1, "bh")||
                !g_strcmp0 (arg1, "backup-history"))
            {
                g_paste_client_backup_history_sync (client, arg2, &error);
            }
            else if (!g_strcmp0 (arg1, "sh") ||
                     !g_strcmp0 (arg1, "switch-history"))
            {
                g_paste_client_switch_history_sync (client, arg2, &error);
            }
            else if (!g_strcmp0 (arg1, "dh") ||
                     !g_strcmp0 (arg1, "delete-history"))
            {
                g_paste_client_delete_history_sync (client, arg2, &error);
            }
            else if (!g_strcmp0 (arg1, "a") ||
                     !g_strcmp0 (arg1, "add"))
            {
                g_paste_client_add_sync (client, arg2, &error);
            }
            else if (!g_strcmp0 (arg1, "g")||
                     !g_strcmp0 (arg1, "get"))
            {
                printf ("%s", g_paste_client_get_element_sync (client, g_ascii_strtoull (arg2, NULL, 0), &error));
            }
            else if (!g_strcmp0 (arg1, "s") ||
                     !g_strcmp0 (arg1, "set") ||
                     !g_strcmp0 (arg1, "select"))
            {
                g_paste_client_select_sync (client, g_ascii_strtoull (arg2, NULL, 0), &error);
            }
            else if (!g_strcmp0 (arg1, "d") ||
                     !g_strcmp0 (arg1, "delete"))
            {
                g_paste_client_delete_sync (client, g_ascii_strtoull (arg2, NULL, 0), &error);
            }
            else if (!g_strcmp0 (arg1, "f") ||
                     !g_strcmp0 (arg1, "file"))
            {
                g_paste_client_add_file_sync (client, arg2, &error);
            }
            else
            {
                show_help (argv[0]);
                status = EXIT_FAILURE;
            }
            break;
        default:
            show_help (argv[0]);
            status = EXIT_FAILURE;
            break;
        }
    }

    if (error)
        failure_exit (error);

    return status;
}
