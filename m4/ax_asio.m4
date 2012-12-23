dnl @synopsis AX_ASIO
dnl
dnl Test for ASIO header files and dependency library
dnl
dnl This macro calls:
dnl
dnl   AC_SUBST(ASIO_CPPFLAGS)
dnl   AC_SUBST(ASIO_LDFLAGS)
dnl   AC_SUBST(ASIO_LIBS)
dnl

AC_DEFUN([AX_ASIO], [
AC_REQUIRE([AX_BOOST_INCLUDE])
AC_LANG_SAVE
AC_LANG([C++])

#
# Configure ASIO header path
#
# If explicitly specified, use it.
AC_ARG_WITH([asio-include],
  AC_HELP_STRING([--with-asio-include=PATH],
    [specify exact directory for ASIO headers]),
    [asio_include_path="$withval"])
CPPFLAGS_SAVES="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $BOOST_CPPFLAGS"
if test "${asio_include_path}" ; then
	ASIO_CPPFLAGS="-I${asio_include_path}"
	CPPFLAGS="$CPPFLAGS $ASIO_CPPFLAGS"
fi
AC_CHECK_HEADERS([asio.hpp],,
  AC_MSG_ERROR([Missing required header files.]))
CPPFLAGS="$CPPFLAGS_SAVES"
AC_SUBST(ASIO_CPPFLAGS)
AC_SUBST(ASIO_LDFLAGS)
AC_SUBST(ASIO_LIBS)


AC_LANG_RESTORE
])dnl AX_ASIO
