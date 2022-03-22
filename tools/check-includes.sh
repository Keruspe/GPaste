#!/usr/bin/env zsh

typeset -A includes

get_file_name() {
    local file="${1}"

    if [[ "${file}" == *.h ]]; then
       echo "$(basename ${file})"
   else 
       echo "${file}"
   fi
}

parse_includes() {
    local file name

    for file in **/*.[ch]; do
        name=$(get_file_name "${file}")

        includes[${name}]="$(grep '#include' "${file}" | awk '{print $NF}' | sed -e 's/<//' -e 's/>//' -e 's/"//g')"
    done
}

get_includes() {
    local file="${1}"
    local name

    name=$(get_file_name "${file}")

    echo ${includes[${name}]}
}

has_include() {
    local file="${1}"
    local include="${2}"
    local output="${3}"
    local i

    for i in $(get_includes "${file}"); do
        [[ "${i}" == "${include}" ]] && echo "${include}: ${output} => ${include}" && return 0
        has_include "${i}" "${include}" "${output} => ${i}" && return 0
    done

    return 1
}

check_include() {
    local file="${1}"
    local include="${2}"
    local i

    for i in $(get_includes "${file}"); do
        [[ "${include}" == "${i}" ]] && continue
        has_include "${i}" "${include}" "${file} => ${i}"
    done
}

check_includes() {
    local file i

    for file in **/*.[ch]; do
        [[ "${file}" == "src/libgpaste/gpaste.h" ]] && continue
        [[ "${file}" == "src/libgpaste/gpaste-gtk3.h" ]] && continue
        [[ "${file}" == "src/libgpaste/gpaste-gtk4.h" ]] && continue
        for i in $(get_includes "${file}"); do
            check_include "${file}" "${i}"
        done
    done
}

main() {
    pushd "$(dirname "${1}")/.." &>/dev/null
    parse_includes
    check_includes
    popd &>/dev/null
}

main "${0}"
