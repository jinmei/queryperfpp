AM_CPPFLAGS = -I$(top_srcdir)/src/lib

TESTS =
if HAVE_GTEST
TESTS += run_unittests
run_unittests_SOURCES = run_unittests.cc
run_unittests_SOURCES += query_repository_test.cc
run_unittests_SOURCES += query_context_test.cc
run_unittests_SOURCES += dispatcher_test.cc
run_unittests_CPPFLAGS = $(AM_CPPFLAGS) $(GTEST_INCLUDES) $(BIND10_INCLUDES)
run_unittests_LDFLAGS = $(GTEST_LDFLAGS) $(BIND10_LDFLAGS) $(AM_LDFLAGS)
run_unittests_LDADD = $(top_builddir)/src/lib/libqueryperf++.la
run_unittests_LDADD += $(GTEST_LDADD) $(BIND10_LDADD)
endif

noinst_PROGRAMS = $(TESTS)
