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

PHP_ARG_ENABLE(peb, whether to enable php-erlang bridge support,
[  --enable-peb           Enable php-erlang bridge support])

if test "$PHP_PEB" = "yes"; then
  dnl Write more examples of tests here...

  dnl # --with-peb -> check with-path
  dnl SEARCH_PATH="/usr/local /usr"     # you might want to change this
  dnl SEARCH_FOR="/include/peb.h"  # you most likely want to change this
  dnl if test -r $PHP_PEB/$SEARCH_FOR; then # path given as parameter
  dnl   PEB_DIR=$PHP_PEB
  dnl else # search default path list
  dnl   AC_MSG_CHECKING([for peb files in default path])
  dnl   for i in $SEARCH_PATH ; do
  dnl     if test -r $i/$SEARCH_FOR; then
  dnl       PEB_DIR=$i
  dnl       AC_MSG_RESULT(found in $i)
  dnl     fi
  dnl   done
  dnl fi
  dnl
  dnl if test -z "$PEB_DIR"; then
  dnl   AC_MSG_RESULT([not found])
  dnl   AC_MSG_ERROR([Please reinstall the peb distribution])
  dnl fi

  dnl # --with-peb -> add include path
  dnl PHP_ADD_INCLUDE($PEB_DIR/include)

  dnl # --with-peb -> check for lib and symbol presence
  dnl LIBNAME=peb # you may want to change this
  dnl LIBSYMBOL=peb # you most likely want to change this

  dnl PHP_CHECK_LIBRARY($LIBNAME,$LIBSYMBOL,
  dnl [
  dnl   PHP_ADD_LIBRARY_WITH_PATH($LIBNAME, $PEB_DIR/lib, PEB_SHARED_LIBADD)
  dnl   AC_DEFINE(HAVE_PEBLIB,1,[ ])
  dnl ],[
  dnl   AC_MSG_ERROR([wrong peb lib version or lib not found])
  dnl ],[
  dnl   -L$PEB_DIR/lib -lm -ldl
  dnl ])
  dnl

  PHP_ADD_LIBRARY(ei, 1, PEB_SHARED_LIBADD)
  PHP_NEW_EXTENSION(peb, peb.c , $ext_shared)
  PHP_SUBST(PEB_SHARED_LIBADD)

fi
