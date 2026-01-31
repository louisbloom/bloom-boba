dnl configure.ac - Autoconf configuration for bloom-boba
dnl Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([bloom-boba], [0.1.0], [thomasc1971@hotmail.com])
AC_CONFIG_SRCDIR([src/msg.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([foreign subdir-objects -Wall -Werror])

dnl Check for programs
AC_PROG_CC
AC_PROG_RANLIB
AM_PROG_AR

dnl Check for header files
AC_CHECK_HEADERS([stdlib.h string.h])

dnl Check for typedefs and structures
AC_TYPE_SIZE_T

dnl Output files
AC_CONFIG_FILES([
    Makefile
    src/Makefile
    include/Makefile
    tests/Makefile
    bloom-boba.pc
])

AC_OUTPUT
