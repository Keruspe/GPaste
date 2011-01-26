#!/bin/bash
[[ $# == 0 ]] && echo "Usage : $0 <locale>" && exit -1
pushd $(dirname $0) > /dev/null
intltool-update --pot
intltool-update $1
popd > /dev/null
exit 0

