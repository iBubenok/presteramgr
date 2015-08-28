#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.67])
AC_INIT([presteramgr], [0.0.0], [obelozeorov@plgn.ru])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE

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

AC_ARG_WITH([pscp],
  [AS_HELP_STRING([--with-pscp=PATH],
                  [specify the PSCP templates location])],
  [pscp=`readlink -f $withval`],
  [pscp=/usr/include/pscp])
AC_SUBST([pscp])

AC_ARG_WITH([rtbd],
  [AS_HELP_STRING([--with-rtbd=PATH],
                  [specify the RTBD templates location])],
  [rtbd=`readlink -f $withval`],
  [rtbd=/usr/include/rtbd])
AC_SUBST([rtbd])

AC_ARG_WITH([arpd],
  [AS_HELP_STRING([--with-arpd=PATH],
                  [specify the ARPD templates location])],
  [arpd=`readlink -f $withval`],
  [arpd=/usr/include/arpd])
AC_SUBST([arpd])

AC_ARG_WITH([linux-headers],
  [AS_HELP_STRING([--with-linux-headers=PATH],
                  [specify the Linux header files location])],
  [linux_headers="-I`readlink -f $withval`"],
  [linux_headers=""])
AC_SUBST([linux_headers])

AC_ARG_ENABLE([variant],
  [AS_HELP_STRING([--enable-variant],
                  [build variant (required)])],
  [variant="$enableval"],
  [variant="arlan-3424fe"])
case "$variant" in
     arlan-3424fe)  AC_DEFINE([VARIANT_ARLAN_3424FE], [1], [Arlan-3424FE variant]) ;;
     tamotator)     AC_DEFINE([VARIANT_ARLAN_3424FE], [1], [Arlan-3424FE variant]) ;;
     arlan-3424ge)  AC_DEFINE([VARIANT_ARLAN_3424GE], [1], [Arlan-3424GE variant]) ;;
     sm-12f)        AC_DEFINE([VARIANT_SM_12F], [1], [Switch Module 12F variant]) ;;
     arlan-3424pfe) AC_DEFINE([VARIANT_ARLAN_3424PFE], [1], [Arlan-3424PFE variant]) ;;
     arlan-3448pge) AC_DEFINE([VARIANT_ARLAN_3448PGE], [1], [Arlan-3448PGE variant]) ;;
     arlan-3448ge)  AC_DEFINE([VARIANT_ARLAN_3448GE], [1], [Arlan-3448GE variant]) ;;
     arlan-3050pge) AC_DEFINE([VARIANT_ARLAN_3050PGE], [1], [Arlan-3050PGE variant]) ;;
     *)             AC_MSG_ERROR([unsupported variant]) ;;
esac

AC_DEFINE([_GNU_SOURCE], [1], [for TEMP_FAILURE_RETRY() etc.])

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
