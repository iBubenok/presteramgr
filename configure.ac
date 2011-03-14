#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.67])
AC_INIT([presteramgr], [0.0.0], [obelozeorov@plgn.ru])
AM_INIT_AUTOMAKE([presteramgr], [0.0.0])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CC

AC_ARG_WITH([cpss],
  [AS_HELP_STRING([--with-cpss=PATH],
                  [specify the CPSS location])],
  [cpss=`readlink -f $withval`],
  [AC_MSG_ERROR([you must specify the CPSS location])])
AC_SUBST([cpss])

AC_CHECK_LIB([pthread], [pthread_create], , [AC_MSG_ERROR([no pthread_create() in -lpthread])])
AC_CHECK_LIB([rt], [clock_gettime], , [AC_MSG_ERROR([no clock_gettime() in -lrt])])

AC_OUTPUT([Makefile src/Makefile])
