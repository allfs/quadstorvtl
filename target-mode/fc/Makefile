UNAME := $(shell uname)
ifeq ($(UNAME), Linux)
SUBDIRS := qla2xxx srpt
CMD := make
MOD := /quadstorvtl/quadstor/target-mode/fc/qla2xxx/qla2xxx.ko /quadstorvtl/quadstor/target-mode/fc/srpt/ib_srpt.ko
else
SUBDIRS = isp
CMD = make -f Makefile.isp
MOD := /quadstorvtl/quadstor/target-mode/fc/isp/ispmod.ko
endif

all: compile
compile:
	@set -e; \
	for i in $(SUBDIRS); do \
		cd /quadstorvtl/quadstor/target-mode/fc/$$i && $(CMD); \
	done

clean:
	@set -e; \
	for i in $(SUBDIRS); do \
		cd /quadstorvtl/quadstor/target-mode/fc/$$i && $(CMD) clean; \
	done

clobber: clean

install:
	@set -e; \
	mkdir -p /quadstorvtl/lib/modules;
	cp -f $(MOD) /quadstorvtl/lib/modules

