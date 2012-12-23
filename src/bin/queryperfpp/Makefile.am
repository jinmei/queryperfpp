bin_PROGRAMS = queryperf++

AM_CPPFLAGS = $(BOOST_CPPFLAGS)
AM_CPPFLAGS += -I$(top_srcdir)/src/lib

queryperf___SOURCES = queryperfpp.cc
queryperf___LDADD = $(top_builddir)/src/lib/libqueryperf++.la

if ENABLE_MAN
man_MANS = queryperf++.1

queryperf++.1: queryperf++.xml
	xsltproc --novalid --xinclude --nonet -o $@ http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl queryperf++.xml
endif

EXTRA_DIST = $(man_MANS) queryperf++.xml
