dnl @synopsis AX_ASIO
dnl
dnl Test for ASIO header files and dependency library
dnl
dnl This macro calls:
dnl
dnl   AC_DEFINE(HAVE_NONBOOST_ASIO, [1], [])
dnl   AC_SUBST(ASIO_CPPFLAGS)
dnl   AC_SUBST(ASIO_LDFLAGS)
dnl   AC_SUBST(ASIO_LIBS)
dnl

AC_DEFUN([AX_ASIO], [
AC_REQUIRE([AX_BOOST_INCLUDE])
AC_REQUIRE([AX_BOOST_LIB])
AC_LANG_SAVE
AC_LANG([C++])

#
# Configure ASIO header path
#

AC_ARG_WITH([asio-include],
  AC_HELP_STRING([--with-asio-include=PATH],
    [specify exact directory for ASIO headers]),
    [asio_include_path="$withval"])

CPPFLAGS_SAVED="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $BOOST_CPPFLAGS"
LDFLAGS_SAVED="$LDFLAGS"
LIBS_SAVED="$LIBS"

# If a non Boost header path is explicitly specified, try it first.
if test "${asio_include_path}" -a "${asio_include_path}" != "no"; then
   AC_MSG_CHECKING([for non-Boost ASIO headers])
   ax_asio_cppflags="-I${asio_include_path}"
   CPPFLAGS="$CPPFLAGS $ax_asio_cppflags"
   AC_TRY_LINK([
#include <asio.hpp>
],[
asio::io_service service;
],[
  AC_MSG_RESULT([yes])
  ASIO_CPPFLAGS=$ax_asio_cppflags
  AC_DEFINE(HAVE_NONBOOST_ASIO, [1],
  [Define to 1 if non-Boost version (header only) of ASIO is available])
], [
  AC_MSG_RESULT([no])])
fi

# If it failed, try Boost version (which requires a compiled binary lib)
if test "X$ASIO_CPPFLAGS" = "X"; then
  AC_MSG_CHECKING([for Boost ASIO header and dependency library])
  boost_ldflags="$BOOST_LDFLAGS $BOOST_RPATH"
  boost_system_lib=$BOOST_LIBS
  LDFLAGS="$LDFLAGS $boost_ldflags"
  LIBS="$LIBS $boost_system_lib"
   AC_TRY_LINK([
#include <boost/asio.hpp>
],[
boost::asio::io_service service;
],[
  AC_MSG_RESULT([yes])
  ASIO_CPPFLAGS=$BOOST_CPPFLAGS
  ASIO_LDFLAGS=$boost_ldflags
  ASIO_LIBS=$boost_system_lib
], [
  AC_MSG_RESULT([no])])
fi

CPPFLAGS="$CPPFLAGS_SAVED"
LDFLAGS="$LDFLAGS_SAVED"
LIBS="$LIBS_SAVED"

AC_SUBST(ASIO_CPPFLAGS)
AC_SUBST(ASIO_LDFLAGS)
AC_SUBST(ASIO_LIBS)

AC_LANG_RESTORE
])dnl AX_ASIO
