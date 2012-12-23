SUBDIRS = . tests

AM_CPPFLAGS = $(BOOST_CPPFLAGS) $(ASIO_CPPFLAGS) $(BIND10_CPPFLAGS)

lib_LTLIBRARIES = libqueryperf++.la

libqueryperf___la_SOURCES = query_repository.h query_repository.cc
libqueryperf___la_SOURCES += query_context.h query_context.cc
libqueryperf___la_SOURCES += dispatcher.h dispatcher.cc
libqueryperf___la_SOURCES += message_manager.h
libqueryperf___la_SOURCES += asio_message_manager.h asio_message_manager.cc
libqueryperf___la_SOURCES += libqueryperfpp_fwd.h

libqueryperf___la_LDFLAGS = ${BIND10_LDFLAGS} ${ASIO_LDFLAGS}
libqueryperf___la_LIBADD = ${BIND10_DNS_LIB} ${ASIO_LIBS}
