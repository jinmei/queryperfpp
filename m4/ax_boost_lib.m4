dnl @synopsis AX_BOOST_LIB
dnl
dnl Test for the Boost C++ header files (but right now it only checks for
dnl boost::system library for simplicity)
dnl
dnl If no path to the boost library files is given via the
dnl --with-boost-lib option,  the macro searchs under
dnl /usr/local/lib /usr/pkg/lib /opt/lib /opt/local/lib directories.
dnl
dnl This macro calls:
dnl
dnl   AC_SUBST(BOOST_LDFLAGS)
dnl   AC_SUBST(BOOST_LIBS)
dnl
dnl And sets:
dnl   BOOST_RPATH
dnl

AC_DEFUN([AX_BOOST_LIB], [
AC_REQUIRE([AX_ISC_RPATH])
AC_REQUIRE([AX_BOOST_INCLUDE])
AC_LANG_SAVE
AC_LANG([C++])

#
# Configure Boost library path and compiler/linker options
#
CPPFLAGS_SAVED="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $BOOST_CPPFLAGS"

# If explicitly specified, use it; otherwise try usual suspects.
AC_ARG_WITH([boost-lib],
  AS_HELP_STRING([--with-boost-lib=PATH],
    [specify exact directory for Boost libraries (needed if you use Boost ASIO]),
    [boostdirs="$withval"],
    [boostdirs="/usr/local/lib /usr/pkg/lib /opt/lib /opt/local/loib"])

# Search for available library; use the one found first.
for d in $boostdirs; do
	boost_ldflags=-L$d
	if test "x$ISC_RPATH_FLAG" != "x"; then
		boost_rpath="${ISC_RPATH_FLAG}${d}"
	fi
	LDFLAGS="$LDFLAGS ${boost_ldflags}"
	for l in boost_system boost_system-mt; do
		LIBS=-l$l
		AC_MSG_CHECKING([for Boost System library in $d with $l])
		AC_TRY_LINK([
#include <boost/system/error_code.hpp>
],[
boost::system::error_code ec;
],
		[AC_MSG_RESULT([yes])
		 boost_libs=-l$l],
		[AC_MSG_RESULT([no])])
		LDFLAGS="$LDFLAGS_SAVED"
		LIBS="$LIBS_SAVED"
		if test "${boost_libs}"; then
			break
		fi
	done
	if test "${boost_libs}"; then
		break
	fi
done

if test "${boost_libs}" ; then
	BOOST_LDFLAGS="${boost_ldflags}"
	if test $boost_rpath; then
		BOOST_RPATH=$boost_rpath
	fi
	BOOST_LIBS="${boost_libs}"
fi

AC_SUBST(BOOST_LDFLAGS)
AC_SUBST(BOOST_LIBS)

CPPFLAGS="$CPPFLAGS_SAVED"
LDFLAGS="$LDFLAGS_SAVED"
LIBS="$LIBS_SAVED"

AC_LANG_RESTORE
])dnl AX_BOOST_LIB
