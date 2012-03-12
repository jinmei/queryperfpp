bin_PROGRAMS = queryperf++

AM_CPPFLAGS = $(BOOST_INCLUDES)
AM_CPPFLAGS += -I$(top_srcdir)/src/lib
# Ideally we should avoid the need for this:
AM_CPPFLAGS += $(BIND10_INCLUDES)

queryperf___SOURCES = queryperfpp.cc
queryperf___LDADD = $(top_builddir)/src/lib/libqueryperf++.la
