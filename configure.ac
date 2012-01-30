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

AC_ARG_WITH([libnl3-includes],
  [AS_HELP_STRING([--with-libnl3-includes=PATH],
                  [specify the libnl3 headers location])],
  [libnl3_includes=`readlink -f $withval`],
  [libnl3_includes=/usr/include/libnl3])
CPPFLAGS="-I$libnl3_includes $CPPFLAGS"

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
