#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT(queryperfpp, 0.1, jinmei@wide.ad.jp)
AC_CONFIG_SRCDIR(README)
AM_INIT_AUTOMAKE
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])dnl be backward compatible
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CXX
AC_PROG_LIBTOOL

AC_LANG([C++])

# Checks for libraries.

# Check for BIND10 libdns++ headers
AC_ARG_WITH(bind10-include,
  AC_HELP_STRING([--with-bind10-include=PATH],
  [specify a path to BIND 10 header files
   (PATH, often needs to be in a BIND 10 source such as
   <somewhere>/bind10/src/lib)]),
    bind10_inc_path="$withval", bind10_inc_path="no")
# If not specified, try some common paths.
if test "$bind10_inc_path" = "no"; then
	bind10dirs="/usr/local /usr/pkg /opt /opt/local"
	for d in $bind10dirs
	do
		if test -f $d/dns/message.h; then
			bind10_inc_path=$d
			break
		fi
	done
fi
CPPFLAGS_SAVES="$CPPFLAGS"
if test "${bind10_inc_path}" ; then
	BIND10_INCLUDES="-I${bind10_inc_path}"
	CPPFLAGS="$CPPFLAGS $BIND10_INCLUDES"
fi
AC_CHECK_HEADERS([dns/message.h],,
  AC_MSG_ERROR([Missing required BIND 10 header files.]))
CPPFLAGS="$CPPFLAGS_SAVES"
AC_SUBST(BIND10_INCLUDES)

# Check for BIND10 lib libraries
AC_ARG_WITH(bind10-lib,
  AC_HELP_STRING([--with-bind10-lib=PATH],
  [specify a path to BIND 10 library files (PATH/lib)]),
    bind10_lib_path="$withval", bind10_lib_path="no")

if test bind10_lib_path != "no"; then
	BIND10_LDFLAGS="-L$bind10_lib_path/lib"
fi
BIND10_LDADD="-ldns++ -lutil -lexceptions"

CPPFLAGS_SAVED="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $BIND10_INCLUDES"
LDFLAGS_SAVED="$LDFLAGS"
LDFLAGS="$LDFLAGS $BIND10_LDFLAGS"
LIBS_SAVED=$LIBS
LIBS="$LIBS $BIND10_LDADD"

AC_MSG_CHECKING([BIND 10 libraries])
AC_TRY_LINK([
#include <dns/rrtype.h>
],[
isc::dns::RRType rrtype(1);
],
[ AC_MSG_RESULT(yes)],
[ AC_MSG_RESULT(no)
  AC_MSG_ERROR(unable to find required BIND 10 libraries)])

CPPFLAGS="$CPPFLAGS_SAVED"
LDFLAGS="$LDFLAGS_SAVES"
LIBS="$LIBS_SAVES"

AC_SUBST(BIND10_LDFLAGS)
AC_SUBST(BIND10_LDADD)

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
AC_SUBST(GTEST_INCLUDES)
AC_SUBST(GTEST_LDFLAGS)
AC_SUBST(GTEST_LDADD)

#
# Configure Boost header path
#
# If explicitly specified, use it.
AC_ARG_WITH([boost-include],
  AC_HELP_STRING([--with-boost-include=PATH],
    [specify exact directory for Boost headers]),
    [boost_include_path="$withval"])
# If not specified, try some common paths.
if test -z "$with_boost_include"; then
	boostdirs="/usr/local /usr/pkg /opt /opt/local"
	for d in $boostdirs
	do
		if test -f $d/include/boost/shared_ptr.hpp; then
			boost_include_path=$d/include
			break
		fi
	done
fi
CPPFLAGS_SAVES="$CPPFLAGS"
if test "${boost_include_path}" ; then
	BOOST_INCLUDES="-I${boost_include_path}"
	CPPFLAGS="$CPPFLAGS $BOOST_INCLUDES"
fi
AC_CHECK_HEADERS([boost/shared_ptr.hpp boost/foreach.hpp boost/interprocess/sync/interprocess_upgradable_mutex.hpp boost/date_time/posix_time/posix_time_types.hpp boost/bind.hpp boost/function.hpp],,
  AC_MSG_ERROR([Missing required header files.]))
CPPFLAGS="$CPPFLAGS_SAVES"
AC_SUBST(BOOST_INCLUDES)

#
# Configure ASIO header path
#
# If explicitly specified, use it.
AC_ARG_WITH([asio-include],
  AC_HELP_STRING([--with-asio-include=PATH],
    [specify exact directory for ASIO headers]),
    [asio_include_path="$withval"])
CPPFLAGS_SAVES="$CPPFLAGS"
if test "${asio_include_path}" ; then
	ASIO_INCLUDES="-I${asio_include_path}"
	CPPFLAGS="$CPPFLAGS $ASIO_INCLUDES"
fi
AC_CHECK_HEADERS([asio.hpp],,
  AC_MSG_ERROR([Missing required header files.]))
CPPFLAGS="$CPPFLAGS_SAVES"
AC_SUBST(ASIO_INCLUDES)

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.
if test "X$GXX" = "Xyes"; then
CXXFLAGS="$CXXFLAGS -Werror -Wall -Wextra -Wwrite-strings -Woverloaded-virtual -Wno-sign-compare"
fi

# Checks for library functions.

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 src/lib/Makefile
                 src/lib/tests/Makefile
                 src/bin/Makefile
                 src/bin/queryperfpp/Makefile])
AC_OUTPUT
