#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.67])
AC_INIT([presteramgr], [0.0.0], [obelozeorov@plgn.ru])
AM_INIT_AUTOMAKE([presteramgr], [0.0.0])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC

AC_ARG_WITH([cpss],
  [AS_HELP_STRING([--with-cpss=PATH],
                  [specify the CPSS location])],
  [cpss=`readlink -f $withval`],
  [AC_MSG_ERROR([you must specify the CPSS location])])
AC_SUBST([cpss])

# Checks for libraries.

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

AC_OUTPUT([Makefile src/Makefile])
