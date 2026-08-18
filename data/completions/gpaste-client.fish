# gpaste-client(1) completion
#
# SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
# SPDX-License-Identifier: BSD-2-Clause

# Is a daemon already there to answer us? Completion must never be what
# activates one: D-Bus would start it behind the user's back and block the shell
# until it finished loading the history.
function __gpaste_daemon_running
    gdbus call --session --dest org.freedesktop.DBus \
                         --object-path /org/freedesktop/DBus \
                         --method org.freedesktop.DBus.NameHasOwner org.gnome.GPaste 2>/dev/null \
        | string match -q '*true*'
end

# One file per history, named <history>.<extension>. Listing them off disk keeps
# history completion working (and instant) with no daemon running at all.
function __gpaste_histories
    set -l dir (test -n "$XDG_DATA_HOME"; and echo $XDG_DATA_HOME; or echo $HOME/.local/share)

    # The daemon stores under PACKAGE_NAME ("GPaste") when that directory
    # already exists, and under PACKAGE ("gpaste") otherwise.
    if test -d $dir/GPaste
        set dir $dir/GPaste
    else
        set dir $dir/gpaste
    end

    # De-duplicated in-shell rather than through a fork of sort(1): the same
    # history can have a file per flavour, but this is the one completion that
    # promises to be instant because it asks no daemon.
    set -l names

    for file in $dir/*.xml $dir/*.xmls $dir/*.db $dir/*.dbs
        test -f $file; or continue
        set -l name (string replace -r '\.[^.]*$' '' (basename $file))
        contains -- $name $names; or set -a names $name
    end

    printf '%s\n' $names
end

# "<uuid>\t<item>" so the item's text shows up as the completion's description.
# gpaste-client reads stdin whenever it is not a tty (that is how "foo |
# gpaste-client" works), so feed it /dev/null: completion is not always run with
# a terminal on stdin, and it would otherwise block waiting for EOF.
function __gpaste_items
    __gpaste_daemon_running; or return

    gpaste-client history --oneline $argv </dev/null 2>/dev/null \
        | string replace -r '^([^:]+): ' '$1\t'
end

# With --use-index the item arguments are indexes into the history, not uuids.
function __gpaste_uuids_or_indexes
    if __fish_contains_opt -s i use-index
        __gpaste_items --use-index
    else
        __gpaste_items
    end
end

complete -c gpaste-client -f

# Options. getopt_long permutes, so they are accepted anywhere on the line.
complete -c gpaste-client -s h -l help       -d 'Display the help'
complete -c gpaste-client -s v -l version    -d 'Display the version'
complete -c gpaste-client -s f -l favourites -d 'Only display the pinned items'
complete -c gpaste-client -s i -l use-index  -d 'Use the index of the item instead of its UUID'
complete -c gpaste-client -s o -l oneline    -d 'Display each item on one line'
complete -c gpaste-client -s r -l raw        -d 'Display the raw item, without its UUID'
complete -c gpaste-client -s e -l reverse    -d 'Display the items in reverse order'
complete -c gpaste-client -s z -l zero       -d 'Use a NUL character instead of a newline between each item'
complete -c gpaste-client -s d -l decoration -r -d 'Decoration to add around each item when merging'
complete -c gpaste-client -s s -l separator  -r -d 'Separator to add between each item when merging'

# Subcommands, the aliases sharing their verb's description.
function __gpaste_subcommand
    set -l description $argv[-1]

    for name in $argv[1..-2]
        complete -c gpaste-client -n __fish_use_subcommand -a $name -d $description
    end
end

__gpaste_subcommand about                              'Display the about dialog'
__gpaste_subcommand add a                              'Set text to clipboard'
__gpaste_subcommand add-password ap                    'Add a name / password pair to the clipboard'
__gpaste_subcommand backup-history bh                  'Back up the current history'
__gpaste_subcommand change-passphrase                  'Change the passphrase of the encrypted history'
__gpaste_subcommand daemon-reexec dr                   'Re-execute the daemon'
__gpaste_subcommand daemon-version dv                  'Display the daemon version'
__gpaste_subcommand delete del d remove rm             'Delete an element of the history'
__gpaste_subcommand delete-history dh                  'Delete a history'
__gpaste_subcommand delete-password dp                 'Delete a password'
__gpaste_subcommand empty e                            'Empty the history'
__gpaste_subcommand favourite fav                      'Pin an item so the history never drops it automatically'
__gpaste_subcommand file f                             'Put the content of a file into the clipboard'
__gpaste_subcommand get g                              'Display an element of the history'
__gpaste_subcommand get-history gh                     'Get the name of the current history'
__gpaste_subcommand help                               'Display the help'
__gpaste_subcommand history h                          'Display the history'
__gpaste_subcommand history-size hs                    'Display the size of the history'
__gpaste_subcommand list-histories lh                  'List available histories'
__gpaste_subcommand merge m                            'Merge various elements from the history'
__gpaste_subcommand migrate                            'Migrate the history to a different storage backend'
__gpaste_subcommand rename-password rp                 'Rename a password'
__gpaste_subcommand replace                            'Replace the contents of an item'
__gpaste_subcommand search                             'Search the history'
__gpaste_subcommand select set s                       'Select an element of the history'
__gpaste_subcommand set-password sp                    'Mark an item as being a password'
__gpaste_subcommand settings preferences p             'Launch the configuration tool'
__gpaste_subcommand show-history                       'Make the GNOME Shell extension display the history'
__gpaste_subcommand start daemon d                     'Start tracking clipboard changes'
__gpaste_subcommand stop quit q                        'Stop tracking clipboard changes'
__gpaste_subcommand switch-history sh                  'Switch to another history'
__gpaste_subcommand ui                                 'Launch the graphical tool'
__gpaste_subcommand unfavourite unfav                  'Unpin an item, letting the history drop it again'
__gpaste_subcommand upload u                           'Upload an item to a pastebin service'
__gpaste_subcommand version v                          'Display the version'

# Arguments. backup-history takes <history> <backup name>, the history being
# optional, so only its first argument names a history.
complete -c gpaste-client -n '__fish_seen_subcommand_from empty e history-size hs delete-history dh switch-history sh backup-history bh' \
    -a '(__gpaste_histories)' -d History

complete -c gpaste-client -n '__fish_seen_subcommand_from file f' -F

complete -c gpaste-client -n '__fish_seen_subcommand_from get g select set s delete del d remove rm upload u replace set-password sp merge m favourite fav unfavourite unfav' \
    -a '(__gpaste_uuids_or_indexes)'
