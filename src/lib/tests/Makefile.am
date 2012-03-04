AM_CPPFLAGS = -I$(builddir)/src/lib

TESTS =
if HAVE_GTEST
TESTS += run_unittests
run_unittests_SOURCES = run_unittests.cc
run_unittests_CPPFLAGS = $(AM_CPPFLAGS) $(GTEST_INCLUDES)
run_unittests_LDFLAGS = $(GTEST_LDFLAGS) $(AM_LDFLAGS)
run_unittests_LDADD = $(top_builddir)/src/lib/libqueryperf++.la
run_unittests_LDADD += $(GTEST_LDADD)
endif

noinst_PROGRAMS = $(TESTS)
