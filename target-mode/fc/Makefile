UNAME := $(shell uname)
ifeq ($(UNAME), Linux)
SUBDIRS := qla2xxx
CMD := make
MOD := qla2xxx.ko 
else
SUBDIRS = isp
CMD = make -f Makefile.isp
MOD := ispmod.ko
endif

all: compile
compile:
	@set -e; \
	for i in $(SUBDIRS); do \
		cd $$i && $(CMD); \
	done

clean:
	@set -e; \
	for i in $(SUBDIRS); do \
		cd $$i && $(CMD) clean; \
	done

clobber: clean

install:
	@set -e; \
	mkdir -p /quadstor/lib/modules;
	cd $(SUBDIRS) && cp -f $(MOD) /quadstor/lib/modules;

