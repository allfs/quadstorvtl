#
# Makefile for the Linux kernel device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile.

EXTRA_CFLAGS += -I$(src)/../include 

ifeq ($(KDIR),)
	KDIR := /lib/modules/$(shell uname -r)/build
endif

ifeq ($(QUADSTOR_ROOT),)
	QUADSTOR_ROOT := /quadstorvtl/quadstor
endif

ifneq ($(KERNELRELEASE),)

KBUILD_EXTRA_SYMBOLS = $(QUADSTOR_ROOT)/export/Module.symvers

EXTRA_CFLAGS += -I$(src)/../include
EXTRA_CFLAGS += -I$(QUADSTOR_ROOT)/export -I$(KDIR)/drivers/scsi -DLINUX -DEXPORT_SYMTAB

obj-m		+= iscsit.o
iscsit-objs	:= tio.o iscsi.o nthread.o config.o digest.o \
			conn.o session.o target.o event.o param.o scdefs.o seq_list.o

else
default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
	strip --strip-debug *.ko
	strip --strip-unneeded *.ko
endif

clean:
	rm -f *.o *.ko iscsit.mod.c .*.o.cmd .*.ko.cmd Module.symvers .*unsigned* modules.order *.unsigned Module.markers
	rm -rf .tmp_versions
