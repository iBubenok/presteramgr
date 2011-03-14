sbin_PROGRAMS = presteramgr
presteramgr_SOURCES = main.c init.c presteramgr.h debug.h
presteramgr_LDADD = @cpss@/compilation_root/cpssDrvLib.a \
	@cpss@/compilation_root/cpssLinuxBind.a
AM_CPPFLAGS = -I@cpss@/cpss/common/h \
	-I@cpss@/cpss/mainPpDrv/h -I@cpss@/cpss/cpssEnabler/mainOs/h \
	-I@cpss@/cpss/cpssEnabler/mainExtDrv/h -I @cpss@/compilation_root
