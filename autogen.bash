#!/bin/bash
mkdir m4
autoreconf -i
intltoolize --force --automake
./configure $@
exit $?

