dnl This file is part of GPaste.
dnl
dnl Copyright 2011-2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
dnl
dnl GPaste is free software: you can redistribute it and/or modify
dnl it under the terms of the GNU General Public License as published by
dnl the Free Software Foundation, either version 3 of the License, or
dnl (at your option) any later version.
dnl
dnl GPaste is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU General Public License for more details.
dnl
dnl You should have received a copy of the GNU General Public License
dnl along with GPaste.  If not, see <http://www.gnu.org/licenses/>.

dnl G_PASTE_WITH([directory], [help string], [default value])
dnl Check if we override a directory and define it
AC_DEFUN([_G_PASTE_WITH], [
    AC_ARG_WITH([$1],
                AS_HELP_STRING([--with-$1=DIR], [$2]),
                [],
                [with_$4=$3])
    AC_SUBST([$1], [$with_$4])
])
AC_DEFUN([G_PASTE_WITH], [_G_PASTE_WITH([$1],[$2],[$3],AS_TR_SH([$1]))])

dnl G_PASTE_ENABLE([feature], [help string], [default value])
dnl Check if we enable a feature and define it
AC_DEFUN([_G_PASTE_ENABLE], [
    AC_ARG_ENABLE([$1],
                  AS_HELP_STRING([--$5-$1], [$2]),
                  [],
                  [enable_$4=$3])
    AM_CONDITIONAL(AS_TR_CPP(ENABLE_$1), [test x$enable_$4 = xyes])
])
AC_DEFUN([G_PASTE_ENABLE], [_G_PASTE_ENABLE([$1],[$2],[$3],AS_TR_SH([$1]),m4_if([$3],[no],[enable],[disable]))])

dnl G_PASTE_APPEND_CFLAGS([CFLAGS])
dnl Check if CFLAGS are supported by $CC and add them
AC_DEFUN([_G_PASTE_APPEND_CFLAG], [
    ac_save_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS $1"
    AC_COMPILE_IFELSE([AC_LANG_SOURCE([int foo;])], [], [CFLAGS="$ac_save_CFLAGS"])
])
AC_DEFUN([G_PASTE_APPEND_CFLAGS], [
  for cflag in $1; do
    _G_PASTE_APPEND_CFLAG([$cflag])
  done
])

dnl G_PASTE_APPEND_LDFLAGS([LDFLAGS])
dnl Check if LDFLAGS are supported by $CC and add them
AC_DEFUN([_G_PASTE_APPEND_LDFLAG], [
    ac_save_LDFLAGS="$LDFLAGS"
    LDFLAGS="$LDFLAGS $1"
    AC_LINK_IFELSE([AC_LANG_SOURCE([int main() { return 1; }])], [], [LDFLAGS="$ac_save_LDFLAGS"])
])
AC_DEFUN([G_PASTE_APPEND_LDFLAGS], [
  for ldflag in $1; do
    _G_PASTE_APPEND_LDFLAG([$ldflag])
  done
])
