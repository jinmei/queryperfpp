#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT(queryperfpp, 0.1, jinmei@wide.ad.jp)
AC_CONFIG_SRCDIR(README)
AM_INIT_AUTOMAKE
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CXX
AC_PROG_LIBTOOL

AC_LANG([C++])

# Checks for libraries.

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.
if test "X$GXX" = "Xyes"; then
CXXFLAGS="$CXXFLAGS -Werror -Wall -Wextra -Wwrite-strings -Woverloaded-virtual -Wno-sign-compare"
fi

# Checks for library functions.

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 src/lib/Makefile])
AC_OUTPUT
