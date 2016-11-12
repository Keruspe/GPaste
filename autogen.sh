#!/bin/bash

set -euo pipefail

autotools() {
    autoreconf -i -Wall
}

clean() {
    git clean -fdx
    autotools
}

scan_build_run() {
    scan-build --use-analyzer=/usr/bin/clang "${@}"
}

scan_build() {
    clean
    scan_build_run ./configure "${@}"
    scan_build_run make
}

static_analysis() {
    coverity-submit
    scan_build "${@}"
}

full() {
    static_analysis "${@}"
    clean
    ./configure "${@}"
    make
    make distcheck
}

run_action() {
    local action="${1}"
    shift

    local configure_args=(
        --prefix=/usr
        --sysconfdir=/etc
        --enable-appstream-util
        --enable-gnome-shell-extension
        --enable-introspection
        --enable-vala
        --enable-x-keybinder
    )

    case "${action}" in
        configure-full|cf)
            ./configure "${configure_args[@]}" "${@}"
            ;;
        coverity|cov)
            coverity-submit
            ;;
        scan-build|sb)
            scan_build "${configure_args[@]}" "${@}"
            ;;
        static-analysis|sa)
            static_analysis "${configure_args[@]}" "${@}"
            ;;
        full)
            full "${configure_args[@]}" "${@}"
            ;;
    esac
}

main() {
    autotools

    [[ "${#}" == 0 ]] || run_action "${@}"
}

main "${@}"
