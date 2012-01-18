#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.67])
AC_INIT([presteramgr], [0.0.0], [obelozeorov@plgn.ru])
AM_INIT_AUTOMAKE([presteramgr], [0.0.0])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])

CPPFLAGS="-I`cross-config arm sysroot`/usr/include/libnl3 $CPPFLAGS"

AC_PROG_CC

AC_ARG_WITH([cpss],
  [AS_HELP_STRING([--with-cpss=PATH],
                  [specify the CPSS location])],
  [cpss=`readlink -f $withval`],
  [AC_MSG_ERROR([you must specify the CPSS location])])
AC_SUBST([cpss])

AC_ARG_ENABLE([msg-prio],
  [AS_HELP_STRING([--enable-msg-prio],
                  [print messages with priority higher or equal to argument by default])],
  [],
  [enable_msg_prio=MSG_DEBUG])
AC_DEFINE_UNQUOTED([MSG_MIN_PRIO], $enable_msg_prio,
                   [Default minimum message priority.])

AC_ARG_ENABLE([cpss-rprint],
  [AS_HELP_STRING([--enable-cpss-rprint],
                  [configure the CRP macro behavior:
                    0, no : do not print CPSS call results
                    1, yes: print non-OK CPSS call results (default)
                    2, all: print all CPSS call results])],
  [],
  [enable_cpss_rprint=1])
case $enable_cpss_rprint in
  0 | no)  AC_DEFINE([CPSS_RPRINT], [0], [CPSS result printing level.]);;
  1 | yes) AC_DEFINE([CPSS_RPRINT], [1], [CPSS result printing level.]);;
  2 | all) AC_DEFINE([CPSS_RPRINT], [2], [CPSS result printing level.]);;
  *)       AC_MSG_ERROR([invalid --enable-cpss-rprint argument]);;
esac

AC_CHECK_LIB([pthread], [pthread_create], , [AC_MSG_ERROR([no pthread_create() in -lpthread])])
AC_CHECK_LIB([rt], [clock_gettime], , [AC_MSG_ERROR([no clock_gettime() in -lrt])])
AC_CHECK_LIB([zmq], [zmq_init], , [AC_MSG_ERROR([no zmq_init() in -lzmq])])
AC_CHECK_LIB([czmq], [zctx_new], , [AC_MSG_ERROR([no zctx_new() in -lczmq])])
AC_CHECK_LIB([nl-3], [nl_send_sync], , [AC_MSG_ERROR([no nl_send_sync() in -lnl-3])])
AC_CHECK_LIB([nl-genl-3], [genl_connect], , [AC_MSG_ERROR([no genl_connect() in -lnl-genl-3])])

AC_CHECK_HEADER([zmq.h], , [AC_MSG_ERROR([zmq header not found])])
AC_CHECK_HEADER([czmq.h], , [AC_MSG_ERROR([czmq header not found])])
AC_CHECK_HEADER([netlink/netlink.h], , [AC_MSG_ERROR([netlink/netlink.h not found])])
AC_CHECK_HEADER([netlink/genl/genl.h], , [AC_MSG_ERROR([netlink/genl/genl.h not found])])
AC_CHECK_HEADER([netlink/genl/ctrl.h], , [AC_MSG_ERROR([netlink/genl/ctrl.h not found])])

AC_OUTPUT([Makefile src/Makefile])
