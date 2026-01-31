# tests/Makefile.am - Build and run bloom-boba unit tests

check_PROGRAMS = test_textinput test_viewport

TESTS = $(check_PROGRAMS)

AM_CPPFLAGS = -I$(top_srcdir)/include
LDADD = $(top_builddir)/src/libbloom-boba.a

test_textinput_SOURCES = test_textinput.c
test_viewport_SOURCES = test_viewport.c
