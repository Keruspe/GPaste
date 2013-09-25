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

#include "config.h"

#include <gio/gio.h>
#include <glib/gi18n-lib.h>
#include <gpaste-client.h>
#include <stdio.h>
#include <stdlib.h>

static void
show_help (const gchar *caller)
{
    printf (_("Usage:\n"));
    printf (_("  %s [history]: print the history with indexes\n"), caller);
    printf (_("  %s backup-history <name>: backup current history\n"), caller);
    printf (_("  %s switch-history <name>: switch to another history\n"), caller);
    printf (_("  %s delete-history <name>: delete a history\n"), caller);
    printf (_("  %s list-histories: list available histories\n"), caller);
    printf (_("  %s raw-history: print the history without indexes\n"), caller);
    printf (_("  %s zero-history: print the history with NUL as separator\n"), caller);
    printf (_("  %s add <text>: set text to clipboard\n"), caller);
    printf (_("  %s get <number>: get the <number>th item from the history\n"), caller);
    printf (_("  %s select <number>: set the <number>th item from the history to the clipboard\n"), caller);
    printf (_("  %s delete <number>: delete <number>th item of the history\n"), caller);
    printf (_("  %s file <path>: put the content of the file at <path> into the clipboard\n"), caller);
    printf (_("  whatever | %s: set the output of whatever to clipboard\n"), caller);
    printf (_("  %s empty: empty the history\n"), caller);
    printf (_("  %s start: start tracking clipboard changes\n"), caller);
    printf (_("  %s stop: stop tracking clipboard changes\n"), caller);
    printf (_("  %s quit: alias for stop\n"), caller);
    printf (_("  %s daemon-reexec: reexecute the daemon (after upgrading...)\n"), caller);
    printf (_("  %s settings: launch the configuration tool\n"), caller);
#ifdef ENABLE_APPLET
    printf (_("  %s applet: launch the applet\n"), caller);
#endif
    printf (_("  %s version: display the version\n"), caller);
    printf (_("  %s help: display this help\n"), caller);
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
    gchar **history = g_paste_client_get_history (client, error);

    if (!*error)
    {
        unsigned int i = 0;

        for (gchar **h = history; *h; ++h)
        {
            if (!raw)
                printf ("%d: ", i++);
            printf ("%s%c", *h, (zero) ? '\0' : '\n');
        }

        g_strfreev (history);
    }
}

static gboolean
is_help (const gchar *option)
{
    return (g_strcmp0 (option, "help") == 0 ||
            g_strcmp0 (option, "-h") == 0 ||
            g_strcmp0 (option, "--help") == 0);
}

static gboolean
is_version (const gchar *option)
{
    return (g_strcmp0 (option, "v") == 0 ||
            g_strcmp0 (option, "version") == 0 ||
            g_strcmp0 (option, "-v") == 0 ||
            g_strcmp0 (option, "--version") == 0);
}

static void
failure_exit (GError *error)
{
    fprintf (stderr, "%s: %s\n", _("Couldn't connect to GPaste daemon"), error->message);
    g_error_free (error);
    exit (EXIT_FAILURE);
}

int
main (int argc, char *argv[])
{
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

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

    int status = EXIT_SUCCESS;

    GError *error = NULL;
    GPasteClient *client = g_paste_client_new (&error);

    if (!client)
        failure_exit (error);

    if (!isatty (fileno (stdin)))
    {
        /* We are being piped */
        GString *data = g_string_new ("");
        gchar c;

        while ((c = fgetc (stdin)) != EOF)
            data = g_string_append_c (data, c);

        data->str[data->len - 1] = '\0';

        g_paste_client_add (client, data->str, &error);

        g_string_free (data, TRUE);
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
            if (g_strcmp0 (arg1, "start") == 0 ||
                g_strcmp0 (arg1, "d") == 0 ||
                g_strcmp0 (arg1, "daemon") == 0)
            {
                g_paste_client_track (client, TRUE, &error);
            }
            else if (g_strcmp0 (arg1, "stop") == 0 ||
                     g_strcmp0 (arg1, "q") == 0 ||
                     g_strcmp0 (arg1, "quit") == 0)
            {
                g_paste_client_track (client, FALSE, &error);
            }
            else if (g_strcmp0 (arg1, "e") == 0 ||
                     g_strcmp0 (arg1, "empty") == 0)
            {
                g_paste_client_empty (client, &error);
            }
#ifdef ENABLE_APPLET
            else if (g_strcmp0 (arg1, "applet") == 0)
            {
                g_spawn_command_line_async (PKGLIBEXECDIR "/gpaste-applet", &error);
                if (error)
                {
                    fprintf (stderr, _("Couldn't spawn gpaste-applet.\n"));
                    g_clear_error (&error);
                    status = EXIT_FAILURE;
                }
            }
#endif
            else if (g_strcmp0 (arg1, "s") == 0 ||
                     g_strcmp0 (arg1, "settings") == 0 ||
                     g_strcmp0 (arg1, "p") == 0 ||
                     g_strcmp0 (arg1, "preferences") == 0)
            {
                execl (PKGLIBEXECDIR "/gpaste-settings", "GPaste-Settings", NULL);
            }
            else if (g_strcmp0 (arg1, "dr") == 0 ||
                     g_strcmp0 (arg1, "daemon-reexec") == 0)
            {
                g_paste_client_reexecute (client, &error);
                if (error && error->code == G_DBUS_ERROR_NO_REPLY)
                {
                    printf (_("Successfully reexecuted the daemon\n"));
                }
            }
            else if (g_strcmp0 (arg1, "h") == 0 ||
                     g_strcmp0 (arg1, "history") == 0)
            {
                show_history (client, FALSE, FALSE, &error);
            }
            else if (g_strcmp0 (arg1, "rh") == 0 ||
                     g_strcmp0 (arg1, "raw-history") == 0)
            {
                show_history (client, TRUE, FALSE, &error);
            }
            else if (g_strcmp0 (arg1, "zh") == 0 ||
                     g_strcmp0 (arg1, "zero-history") == 0)
            {
                show_history (client, FALSE, TRUE, &error);
            }
            else if (g_strcmp0 (arg1, "lh") == 0 ||
                     g_strcmp0 (arg1, "list-histories") == 0)
            {
                gchar **histories = g_paste_client_list_histories (client, &error);
                if (!error)
                {
                    for (gchar **h = histories; *h; ++h)
                        printf ("%s\n", *h);
                    g_strfreev (histories);
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
            if (g_strcmp0 (arg1, "bh") == 0||
                g_strcmp0 (arg1, "backup-history") == 0)
            {
                g_paste_client_backup_history (client, arg2, &error);
            }
            else if (g_strcmp0 (arg1, "sh") == 0 ||
                     g_strcmp0 (arg1, "switch-history") == 0)
            {
                g_paste_client_switch_history (client, arg2, &error);
            }
            else if (g_strcmp0 (arg1, "dh") == 0 ||
                     g_strcmp0 (arg1, "delete-history") == 0)
            {
                g_paste_client_delete_history (client, arg2, &error);
            }
            else if (g_strcmp0 (arg1, "a") == 0 ||
                     g_strcmp0 (arg1, "add") == 0)
            {
                g_paste_client_add (client, arg2, &error);
            }
            else if (g_strcmp0 (arg1, "g") == 0||
                     g_strcmp0 (arg1, "get") == 0)
            {
                printf ("%s", g_paste_client_get_element (client, g_ascii_strtoull (arg2, NULL, 0), &error));
            }
            else if (g_strcmp0 (arg1, "s") == 0 ||
                     g_strcmp0 (arg1, "set") == 0 ||
                     g_strcmp0 (arg1, "select") == 0)
            {
                g_paste_client_select (client, g_ascii_strtoull (arg2, NULL, 0), &error);
            }
            else if (g_strcmp0 (arg1, "d") == 0 ||
                     g_strcmp0 (arg1, "delete") == 0)
            {
                g_paste_client_delete (client, g_ascii_strtoull (arg2, NULL, 0), &error);
            }
            else if (g_strcmp0 (arg1, "f") == 0 ||
                     g_strcmp0 (arg1, "file") == 0)
            {
                g_paste_client_add_file (client, arg2, &error);
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

    g_object_unref (client);

    return status;
}
