sbin_PROGRAMS = presteramgr presterarst
presteramgr_SOURCES = main.c init.c event.c port.c \
	extsvc.c mgmt.c vlan.c debug.c presteramgr.h debug.h utils.h \
	control.c data.c sysdeps.c mac.c qos.c pdsa.c wnct.c log.c \
	zcontext.c mcg.c ip.c route.c rtnl.c nht.c ret.c arp.c qt2025-phy.c \
	ift.c control-utils.c monitor.c
nodist_presteramgr_SOURCES = control-proto.h
BUILT_SOURCES = control-proto.h
CLEANFILES = control-proto.h
presteramgr_LDADD = @cpss@/compilation_root/cpssDrvLib.a \
	@cpss@/compilation_root/cpssLinuxBind.a

nodist_presterarst_SOURCES = control-proto.h
presterarst_SOURCES = presterarst.c log.c debug.c extsvc.c
presterarst_LDADD = @cpss@/compilation_root/cpssDrvLib.a \
	@cpss@/compilation_root/cpssLinuxBind.a

AM_CPPFLAGS = -I@cpss@/cpss/common/h \
	-I@cpss@/cpss/mainPpDrv/h -I@cpss@/cpss/cpssEnabler/mainOs/h \
	-I@cpss@/cpss/cpssEnabler/mainExtDrv/h -I @cpss@/compilation_root
AM_CFLAGS = -Wall

bin_PROGRAMS = presteractl fanctl sfpctl
presteractl_SOURCES = presteractl.c
fanctl_SOURCES = fanctl.c
sfpctl_SOURCES = sfpctl.c

%.h: @pscp@/%.ecg
	ecgen -lc -o$@ $<
