#!/usr/bin/env bash

set -euo pipefail

run_ninja() {
    ninja -C build "${@}"
}

# What a release commit holds: the catalogs this script rewrites, and what the
# release itself edits beforehand -- the version, the notes, the libraries'
# soversions. Named, so that a file modified for anything else stays out of it:
# such a file stops the release before anything is committed, since the tarball
# would leave it out too, meson dist refusing a tree with changes it was not
# given.
release_paths=(po NEWS README.md meson.build src/libgpaste/meson.build data/metainfo)

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

    appstreamcli validate data/metainfo/*.xml.in || exit 1
    run_ninja
    ls po/*.po | sed 's|po/||; s|\.po$||' | sort > po/LINGUAS
    run_ninja GPaste-pot
    run_ninja GPaste-update-po
    # msgmerge keeps what a release drops as #~ entries; nothing reads them back.
    # One command, so that set -e stops on a catalog it fails on.
    for po in po/*.po; do
        msgattrib --no-obsolete -o "${po}" "${po}"
    done
    # A new language's catalog, and nothing else lying around in po/.
    git add -- po/LINGUAS po/*.po
    git commit -m "Release GPaste ${version}" -- "${release_paths[@]}"
    run_ninja dist
    git tag -m "Release GPaste ${version}" v${version}
}

main "${@}"
