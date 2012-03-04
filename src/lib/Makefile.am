SUBDIRS = . tests

AM_CPPFLAGS = $(BOOST_INCLUDES) $(BIND10_INCLUDES)

lib_LTLIBRARIES = libqueryperf++.la

libqueryperf___la_SOURCES = query_repository.h query_repository.cc

libqueryperf___la_LDFLAGS = ${BIND10_LDFLAGS}
libqueryperf___la_LIBADD = ${BIND10_LIBS}
