#!/usr/bin/env bash

set -euo pipefail

run_ninja() {
    ninja -C build "${@}"
}

main() {
    local version="${1}"
    run_ninja
    run_ninja GPaste-pot
    run_ninja GPaste-update-po
    run_ninja dist
    git commit -asm "Release GPaste ${version}"
    git tag -sm "Release GPaste ${version}" v${version}
}

main "${@}"
