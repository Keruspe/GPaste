dnl This file is part of GPaste.
dnl
dnl Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>

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

dnl G_PASTE_APPEND_CFLAGS
dnl Check if CFLAGS are supported by $CC and add them
AC_DEFUN([_G_PASTE_APPEND_CFLAG], [
    ac_save_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS $1"
    AC_MSG_CHECKING([if $CC supports $1 cflag])
    AC_COMPILE_IFELSE([AC_LANG_SOURCE([int foo;])], [AC_MSG_RESULT([yes])], [AC_MSG_RESULT([no]);CFLAGS="$ac_save_CFLAGS"])
])
AC_DEFUN([_G_PASTE_APPEND_CFLAGS], [
  for cflag in $1; do
    _G_PASTE_APPEND_CFLAG([$cflag])
  done
])
AC_DEFUN([G_PASTE_APPEND_CFLAGS], [
    _G_PASTE_APPEND_CFLAGS([        \
        -pipe                       \
        -pedantic                   \
        -DANOTHER_BRICK_IN_THE      \
        -Wall                       \
        -W                          \
        -Wextra                     \
        -Wvla                       \
        -Wundef                     \
        -Wformat=2                  \
        -Wlogical-op                \
        -Wlogical-op-parentheses    \
        -Wsign-compare              \
        -Wformat-security           \
        -Wmissing-include-dirs      \
        -Wformat-nonliteral         \
        -Wold-style-definition      \
        -Wpointer-arith             \
        -Winit-self                 \
        -Wfloat-equal               \
        -Wmissing-prototypes        \
        -Wstrict-prototypes         \
        -Wredundant-decls           \
        -Wmissing-declarations      \
        -Wmissing-noreturn          \
        -Wshadow                    \
        -Wendif-labels              \
        -Wcast-align                \
        -Wstrict-aliasing=2         \
        -Wwrite-strings             \
        -Wno-unknown-warning-option \
        -Wno-float-equal            \
        -Werror=overflow            \
        -Wp,-D_FORTIFY_SOURCE=2     \
        -ffast-math                 \
        -fno-common                 \
        -fdiagnostics-show-option   \
        -fno-strict-aliasing        \
        -fvisibility=hidden         \
        -ffunction-sections         \
        -fdata-sections             \
    ])
])


dnl G_PASTE_APPEND_LDFLAGS
dnl Check if LDFLAGS are supported by $CC and add them
AC_DEFUN([_G_PASTE_APPEND_LDFLAG], [
    ac_save_LDFLAGS="$LDFLAGS"
    LDFLAGS="$LDFLAGS $1"
    AC_MSG_CHECKING([if $CC supports $1 ldflag])
    AC_LINK_IFELSE([AC_LANG_SOURCE([int main() { return 1; }])], [AC_MSG_RESULT([yes])], [AC_MSG_RESULT([no]);LDFLAGS="$ac_save_LDFLAGS"])
])
AC_DEFUN([_G_PASTE_APPEND_LDFLAGS], [
  for ldflag in $1; do
    _G_PASTE_APPEND_LDFLAG([$ldflag])
  done
])
AC_DEFUN([G_PASTE_APPEND_LDFLAGS], [
    _G_PASTE_APPEND_LDFLAGS([ \
        -Wl,--as-needed       \
        -Wl,--gc-sections     \
    ])
])
