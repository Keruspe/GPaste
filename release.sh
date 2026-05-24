#!/usr/bin/env bash

set -euo pipefail

run_ninja() {
    ninja -C build "${@}"
}

main() {
    local version="${1}"
    appstreamcli validate data/appstream/*.xml.in || exit 1
    run_ninja
    ls po/*.po | sed 's|po/||; s|\.po$||' | sort > po/LINGUAS
    run_ninja GPaste-pot
    run_ninja GPaste-update-po
    git commit -asm "Release GPaste ${version}"
    run_ninja dist
    git tag -m "Release GPaste ${version}" v${version}
}

main "${@}"
