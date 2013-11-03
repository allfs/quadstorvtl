UNAME := $(shell uname)
ifeq ($(UNAME), Linux)
SUBDIRS := qla2xxx srpt
CMD := make
MOD := $(QUADSTOR_ROOT)/target-mode/fc/qla2xxx/qla2xxx.ko $(QUADSTOR_ROOT)/target-mode/fc/srpt/ib_srpt.ko
else
SUBDIRS = isp
CMD = make -f Makefile.isp
MOD := $(QUADSTOR_ROOT)/target-mode/fc/isp/ispmod.ko
endif

all: compile
compile:
	@set -e; \
	for i in $(SUBDIRS); do \
		cd $(QUADSTOR_ROOT)/target-mode/fc/$$i && $(CMD); \
	done

clean:
	@set -e; \
	for i in $(SUBDIRS); do \
		cd $(QUADSTOR_ROOT)/target-mode/fc/$$i && $(CMD) clean; \
	done

clobber: clean

install:
	@set -e; \
	mkdir -p $(QUADSTOR_INSTALL_ROOT)/lib/modules;
	for i in $(MOD); do \
		if [ -f $$i ]; then \
			cp -f $$i $(QUADSTOR_INSTALL_ROOT)/lib/modules; \
		fi; \
	done
