#!/bin/sh
# autogen.sh - Bootstrap the autotools build system

set -e

echo "Generating build system files..."
echo

# Generate aclocal.m4 from m4/*.m4 macros
aclocal -I m4

# Generate configure from configure.ac
autoconf

# Generate config.h.in from configure.ac
autoheader

# Generate Makefile.in files from Makefile.am files
automake --add-missing --copy --foreign

echo
echo "Now run:"
echo "  ./configure"
echo "  make"
