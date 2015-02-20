#!/bin/zsh

dissect() {
    mkdir -p "$(dirname "${3}")"
    touch "${3}.data"
    for include in $(grep '#include' "${1}" | awk '{print $NF}' | sed -e 's/<//' -e 's/>//' -e 's/"//g'); do
        [[ -z "${4}" ]] && echo "${include}" >> "${3}".includes
        sed -i '\|^'"${2}"'$|d' "${3}.data"
        echo "${2} => ${include}" >> "${3}.data"
        [[ "${include/gpaste/}" != "${include}" ]] && dissect **/"${include}" "${2} => ${include}"  "${3}" "rec"
    done
}

main() {
    local ROOTDIR="$(dirname ${1})/.."
    local TMPDIR="${ROOTDIR}/tmp"
    local RESULTFILE="${ROOTDIR}/result"
    local TMPFILE="${RESULTFILE}-tmp"

    pushd "${ROOTDIR}"

    rm -fr "${TMPDIR}" "${RESULTFILE}"

    for file in **/*.[ch]; do
        dissect "${file}" "${file}" "${TMPDIR}/${file}"
    done

    for result in "${TMPDIR}"/**/*.[ch].includes; do
        file="${result/.includes/}"
        file="${file/${TMPDIR}\//}"
        sort -u "${result}" | tee "${result}" > /dev/null
        rm -f "${TMPFILE}"
        for include in $(< "${result}"); do
            concurrent="$(grep "${include}" "${result/includes/data}" | grep -v "^${file} => ${include}" | head -n1)"
            [[ -z "${concurrent}" ]] || echo "${include}: ${concurrent}" >> "${TMPFILE}"
        done
        if [[ -s "${TMPFILE}" ]]; then
            echo "${file}\n================" >> "${RESULTFILE}"
            cat "${TMPFILE}" >> "${RESULTFILE}"
            echo >> "${RESULTFILE}"
        fi
    done

    rm -fr "${TMPDIR}"

    cat "${RESULTFILE}"

    popd
}

main "${0}"
