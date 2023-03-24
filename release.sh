#!/usr/bin/env bash

set -euo pipefail

run_ninja() {
    ninja -C build "${@}"
}

main() {
    local version="${1}"
    appstreamcli validate data/appstream/*.xml.in || exit 1
    run_ninja
    run_ninja GPaste-pot
    run_ninja GPaste-update-po
    git commit -asm "Release GPaste ${version}"
    run_ninja dist
    git tag -sm "Release GPaste ${version}" v${version}
}

main "${@}"
