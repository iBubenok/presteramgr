sbin_PROGRAMS = presteramgr slwd

presteramgr_SOURCES =	\
	variant.h			\
	main.c				\
	init.c				\
	event.c				\
	port.c				\
	extsvc.c			\
	mgmt.c				\
	vlan.c				\
	debug.c				\
	presteramgr.h		\
	debug.h				\
	utils.h				\
	control.c			\
	data.c				\
	sysdeps.c			\
	mac.c				\
	qos.c				\
	pdsa.c				\
	wnct.c				\
	log.c				\
	zcontext.c			\
	mcg.c				\
	ip.c				\
	route.c				\
	nht.c				\
	ret.c				\
	qt2025-phy.c		\
	control-utils.c		\
	monitor.c			\
	env.c				\
	pcl.c				\
	fib.c				\
	arpc.c				\
	dgasp.c             \
	dev.c               \
	diag.c              \
	stack.c             \
	tipc.c				\
	trunk.c				\
	gif.c     \
	sec.c

nodist_presteramgr_SOURCES = control-proto.h rtbd.h arpd.h
BUILT_SOURCES = control-proto.h rtbd.h arpd.h
CLEANFILES = control-proto.h rtbd.h arpd.h
presteramgr_LDADD = @cpss@/compilation_root/cpssDrvLib.a \
	@cpss@/compilation_root/cpssLinuxBind.a

nodist_slwd_SOURCES = control-proto.h
slwd_SOURCES = slwd.c log.c debug.c

AM_CPPFLAGS = @linux_headers@ -I@pscp@ -I@cpss@/cpss/common/h \
	-I@cpss@/cpss/mainPpDrv/h -I@cpss@/cpss/cpssEnabler/mainOs/h \
	-I@cpss@/cpss/cpssEnabler/mainExtDrv/h -I @cpss@/compilation_root
AM_CFLAGS = -Wall -Werror

bin_PROGRAMS = presteractl fanctl sfpctl
presteractl_SOURCES = presteractl.c
fanctl_SOURCES = fanctl.c
sfpctl_SOURCES = sfpctl.c

sbin_SCRIPTS = presterarst
CLEANFILES += $(sbin_SCRIPTS)
EXTRA_DIST = presterarst.in

presterarst: Makefile presterarst.in
	sed 's,[@]sbindir[@],$(sbindir),g' $(srcdir)/presterarst.in > $@
	chmod +x $@

%.h: @pscp@/%.ecg
	ecgen -lc -o$@ $<

%.h: @rtbd@/%.ecg
	ecgen -lc -o$@ $<

%.h: @arpd@/%.ecg
	ecgen -lc -o$@ $<
