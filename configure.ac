#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.59])
AC_INIT(queryperfpp, 0.9.0, jinmei@wide.ad.jp)
AC_CONFIG_SRCDIR(README)
AM_INIT_AUTOMAKE
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CXX
AC_PROG_LIBTOOL

AC_LANG([C++])

# Checks for BIND 10 headers and libraries
AX_ISC_BIND10

# We use -R, -rpath etc so the resulting program will be more likekly to
# "just work" whenever they are available.
if test "x$BIND10_RPATH" != "x"; then
   LDFLAGS="$LDFLAGS $BIND10_RPATH"
fi

# Check for the BIND 10 DNS library.
if test "x$BIND10_DNS_LIB" = "x"; then
   AC_MSG_ERROR([unable to find BIND 10 DNS library])
fi

# Check ASIO header path and its dependency if necessary
AX_ASIO
if test "X$ASIO_CPPFLAGS" = "X"; then
   AC_MSG_ERROR([unable to find workable ASIO])
fi

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.

werror_ok=0
# Certain versions of gcc (g++) have a bug that incorrectly warns about
# the use of anonymous name spaces even if they're closed in a single
# translation unit.  For these versions we have to disable -Werror.
CXXFLAGS_SAVED="$CXXFLAGS"
CXXFLAGS="$CXXFLAGS -Werror"
AC_MSG_CHECKING(for in-TU anonymous namespace breakage)
AC_TRY_COMPILE([namespace { class Foo {}; }
namespace isc {class Bar {Foo foo_;};} ],,
	[AC_MSG_RESULT(no)
	 werror_ok=1
	 CXX_WERROR_FLAGS="-Werror"],
	[AC_MSG_RESULT(yes)])
CXXFLAGS="$CXXFLAGS_SAVED $CXX_WERROR_FLAGS"

if test "X$GXX" = "Xyes"; then
CXXFLAGS="$CXXFLAGS -Wall -Wextra -Wwrite-strings -Woverloaded-virtual -Wno-sign-compare"
fi

# Checks for library functions.

# See if we need to be explicit for thread support
PTHREAD_LDFLAGS=
AC_CHECK_LIB(pthread, pthread_create,[ PTHREAD_LDFLAGS=-lpthread ], [])
LDFLAGS="$LDFLAGS $PTHREAD_LDFLAGS"

# Check for availability of (optional) gootletest.
AC_ARG_WITH(gtest,
  AC_HELP_STRING([--with-gtest=PATH],
  [specify a path to gtest header files (PATH/include) and library (PATH/lib)]),
    gtest_path="$withval", gtest_path="no")
if test "$gtest_path" != "no"
then
	DISTCHECK_GTEST_CONFIGURE_FLAG="--with-gtest=\"$gtest_path\""
	if test "$gtest_path" != "yes"; then
		GTEST_PATHS=$gtest_path
		if test -x "${gtest_path}/bin/gtest-config" ; then
			GTEST_CONFIG="${gtest_path}/bin/gtest-config"
		fi
	else
		AC_PATH_PROG([GTEST_CONFIG], [gtest-config])
	fi
	if test -x "${GTEST_CONFIG}" ; then :
		# using cppflags instead of cxxflags
		GTEST_INCLUDES=`${GTEST_CONFIG} --cppflags`
		GTEST_LDFLAGS=`${GTEST_CONFIG} --ldflags`
		GTEST_LDADD=`${GTEST_CONFIG} --libs`
		GTEST_FOUND="true"
	else
		AC_MSG_WARN([Unable to locate Google Test gtest-config.])
		if test -z "${GTEST_PATHS}" ; then
			GTEST_PATHS="/usr /usr/local"
		fi
		GTEST_FOUND="false"
	fi
	if test "${GTEST_FOUND}" != "true"; then
		GTEST_FOUND="false"
		for dir in $GTEST_PATHS; do
			if test -f "$dir/include/gtest/gtest.h"; then
				GTEST_INCLUDES="-I$dir/include"
				GTEST_LDFLAGS="-L$dir/lib"
				GTEST_LDADD="-lgtest"
				GTEST_FOUND="true"
				# There is no gtest-config script on this
				# system, which is supposed to inform us
				# whether we need pthreads as well (a
				# gtest compile-time option). So we still
				# need to test that manually.
				CPPFLAGS_SAVED="$CPPFLAGS"
				CPPFLAGS="$CPPFLAGS $GTEST_INCLUDES"
				LDFLAGS_SAVED="$LDFLAGS"
				LDFLAGS="$LDFLAGS $GTEST_LDFLAGS"
				LIBS_SAVED=$LIBS
				LIBS="$LIBS $GTEST_LDADD"
				AC_MSG_CHECKING([Checking whether gtest tests need pthreads])
				# First try to compile without pthreads
				AC_TRY_LINK([
					#include <gtest/gtest.h>
					],[
						int i = 0;
						char* c = NULL;
						::testing::InitGoogleTest(&i, &c);
						return (0);
					],
					[ AC_MSG_RESULT(no) ],
					[
						LIBS="$SAVED_LIBS $GTEST_LDADD $PTHREAD_LDFLAGS"
						# Now try to compile with pthreads
						AC_TRY_LINK([
							#include <gtest/gtest.h>
							],[
								int i = 0;
								char* c = NULL;
								::testing::InitGoogleTest(&i, &c);
								return (0);
							],
							[ AC_MSG_RESULT(yes)
							  GTEST_LDADD="$GTEST_LDADD $PTHREAD_LDFLAGS"
							],
							# Apparently we can't compile it at all
							[ AC_MSG_ERROR(unable to compile with gtest) ])
				])
				CPPFLAGS=$CPPFLAGS_SAVED
				LDFLAGS=$LDFLAGS_SAVED
				LIBS=$LIBS_SAVED
				break
			fi
		done
	fi
	if test "${GTEST_FOUND}" != "true"; then
		AC_MSG_ERROR([Cannot find gtest in: $GTEST_PATHS])
	fi
else
	GTEST_INCLUDES=
	GTEST_LDFLAGS=
	GTEST_LDADD=
	DISTCHECK_GTEST_CONFIGURE_FLAG=
fi
AM_CONDITIONAL(HAVE_GTEST, test $gtest_path != "no")
AC_SUBST(DISTCHECK_GTEST_CONFIGURE_FLAG)
AC_SUBST(GTEST_CPPFLAGS)
AC_SUBST(GTEST_LDFLAGS)
AC_SUBST(GTEST_LDADD)

# Man pages (optional)
AC_ARG_ENABLE(man, [AC_HELP_STRING([--enable-man],
  [regenerate man pages [default=no]])], enable_man=$enableval, enable_man=no)

AM_CONDITIONAL(ENABLE_MAN, test x$enable_man != xno)

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 src/lib/Makefile
                 src/lib/tests/Makefile
                 src/bin/Makefile
                 src/bin/queryperfpp/Makefile])
AC_OUTPUT
