#!/usr/bin/env bash

set -euo pipefail

run_ninja() {
    ninja -C build "${@}"
}

# What a release commit holds: the catalogs this script rewrites, and what the
# release itself edits beforehand -- the version, the notes, the libraries'
# soversions. Named file by file, as git pathspecs, so that a file modified for
# anything else stays out of it, the ones beside these in po/ and
# data/metainfo/ included: such a file stops the release before anything is
# committed, since the tarball would leave it out too, meson dist refusing a
# tree with changes it was not given.
release_paths=('po/*.po' po/GPaste.pot po/LINGUAS NEWS README.md meson.build src/libgpaste/meson.build
               'data/metainfo/*.xml.in')
# The two meson.build files above hold more than the version and the
# soversions: a line of theirs changed beyond those is an edit of its own.
release_lines="^[-+](  version: '[^']*',|[a-z0-9_]+_soversion = '[^']*')$"

main() {
    local version="${1}"
    local project_version

    # The tag, the commit and the tarball have to name one version: the one
    # meson.build was bumped to, read off meson.build itself -- a build
    # directory says what it was last configured from.
    project_version="$(meson introspect --projectinfo meson.build | python3 -c 'import json, sys; print (json.load (sys.stdin)["version"])')"
    if [[ "${version}" != "${project_version}" ]]; then
        echo "release.sh: asked to release ${version}, but meson.build is at ${project_version}" >&2
        exit 1
    fi

    local others
    others="$(git diff --name-only HEAD -- . "${release_paths[@]/#/:!}")"
    if [[ -n "${others}" ]]; then
        echo "release.sh: modified, and no part of a release:" >&2
        echo "${others}" >&2
        exit 1
    fi

    local edits
    edits="$(git diff -U0 HEAD -- meson.build src/libgpaste/meson.build | grep -E '^[-+]' | grep -vE '^(---|\+\+\+) ' | grep -vE "${release_lines}" || true)"
    if [[ -n "${edits}" ]]; then
        echo "release.sh: meson.build or src/libgpaste/meson.build changed beyond the version and the soversions:" >&2
        echo "${edits}" >&2
        exit 1
    fi

    appstreamcli validate data/metainfo/*.xml.in || exit 1
    run_ninja
    ls po/*.po | sed 's|po/||; s|\.po$||' | sort > po/LINGUAS
    run_ninja GPaste-pot
    run_ninja GPaste-update-po
    # A new language's catalog, and nothing else lying around in po/.
    git add -- po/LINGUAS po/*.po
    git commit -s -m "Release GPaste ${version}" -- "${release_paths[@]}"
    run_ninja dist
    git tag -m "Release GPaste ${version}" v${version}
}

main "${@}"
