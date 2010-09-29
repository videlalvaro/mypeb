dnl $Id$
dnl config.m4 for extension peb

dnl Comments in this file start with the string 'dnl'.
dnl Remove where necessary. This file will not work
dnl without editing.

dnl If your extension references something external, use with:

dnl PHP_ARG_WITH(peb, for peb support,
dnl Make sure that the comment is aligned:
dnl [  --with-peb             Include peb support])

dnl Otherwise use enable:

PHP_ARG_ENABLE(peb, [whether to enable php-erlang bridge support],
    [AS_HELP_STRING([--enable-peb], [Enable php-erlang bridge support])])

if test "$PHP_PEB" = "yes"; then

  PHP_ARG_WITH([erlang],
      [for Erlang interface directory],
      [AS_HELP_STRING([--with-erlang],
          [Path to Erlang dev directory containing lib and include])], no, no)
  PHP_ARG_WITH([erlanglib],
      [for Erlang interface libs],
      [AS_HELP_STRING([--with-erlanglib=DIR],
          [Look for Erlang interface library in DIR])], no, no)
  PHP_ARG_WITH([erlanginc],
      [for Erlang interface includes],
      [AS_HELP_STRING([--with-erlanginc=DIR],
          [Look for Erlang interface headers in IDR])], no, no)

  if test "${PHP_ERLANG}" != "no"; then
    PHP_ADD_INCLUDE("${PHP_ERLANG}/include")
    erlang_libdir="${PHP_ERLANG}/lib"
  else
    if test "${PHP_ERLANGLIB}" != "no"; then
      erlang_libdir="${PHP_ERLANGLIB}"
    fi
    if test "$PHP_ERLANGINC" != "no"; then
      PHP_ADD_INCLUDE("${PHP_ERLANGINC}")
    fi
  fi

  if test -z "${erlang_libdir}"; then
    PHP_ADD_LIBRARY(ei)
    PHP_ADD_LIBRARY(erl_interface)
  else
    PHP_ADD_LIBRARY_WITH_PATH(ei, "${erlang_libdir}")
    PHP_ADD_LIBRARY_WITH_PATH(erl_interface, "${erlang_libdir}")
  fi

  CPPFLAGS="${CPPFLAGS} ${INCLUDES}"

  AC_CHECK_LIB([ei], [ei_connect_init], [],
      [AC_MSG_ERROR([Could not find libei])])
  AC_CHECK_LIB([erl_interface], [erl_connect], [],
      [AC_MSG_ERROR([Could not find liberl_interface])])
  AC_CHECK_HEADER([erl_interface.h], [],
      [AC_MSG_ERROR([Could not find header file erl_interface.h])])
  AC_CHECK_HEADER([ei.h], [],
      [AC_MSG_ERROR([Could not find header file ei.h])])

  PHP_ADD_LIBRARY(ei, 1, PEB_SHARED_LIBADD)
  PHP_NEW_EXTENSION(peb, peb.c , $ext_shared)
  PHP_SUBST(PEB_SHARED_LIBADD)

fi
