#!/bin/bash
mkdir -p m4
autoreconf -i -Wall
intltoolize --force --automake
exit $?

