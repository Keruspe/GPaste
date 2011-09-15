#!/bin/sh
mkdir -p m4
autoreconf -i -Wall
intltoolize --force --automake

