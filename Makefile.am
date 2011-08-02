AUTOMAKE_OPTIONS = foreign
SUBDIRS = src

initrd.cpio.gz: all
	make DESTDIR=@abs_top_srcdir@/initrd install
	cd @abs_top_srcdir@/initrd && find . | cpio -o -H newc | gzip > @abs_top_srcdir@/initrd.cpio.gz

uInitrd: initrd.cpio.gz
	mkimage -A arm -O linux -T ramdisk -C none -a 0 -e 0 -n initramfs -d initrd.cpio.gz uInitrd

clean-local:
	rm -f initrd.cpio.gz uInitrd
