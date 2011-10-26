sbin_PROGRAMS = presteramgr
presteramgr_SOURCES = main.c init.c event.c port.c \
	extsvc.c mgmt.c vlan.c debug.c presteramgr.h debug.h utils.h \
	control.c
presteramgr_LDADD = @cpss@/compilation_root/cpssDrvLib.a \
	@cpss@/compilation_root/cpssLinuxBind.a
AM_CPPFLAGS = -I@cpss@/cpss/common/h \
	-I@cpss@/cpss/mainPpDrv/h -I@cpss@/cpss/cpssEnabler/mainOs/h \
	-I@cpss@/cpss/cpssEnabler/mainExtDrv/h -I @cpss@/compilation_root
AM_CFLAGS = -Wall

bin_PROGRAMS = pmntest
pmntest_SOURCES = pmntest.c
