#!/bin/bash
mkdir -p m4
autoreconf -i
intltoolize --force --automake
./configure $@
exit $?

