#!/bin/bash

run_action() {
    local action="${1}"
    shift

    case "${action}" in
        configure-full)
            ./configure --prefix=/usr --sysconfdir=/etc --enable-x-keybinder --enable-gnome-shell-extension --enable-applet --enable-appstream-util --enable-unity --enable-vala --enable-introspection "${@}"
            ;;
    esac
}

main() {
    autoreconf -i -Wall
    intltoolize --force --automake

    [[ "#{#}" != 0 ]] && run_action "${@}"
}

main "${@}"
