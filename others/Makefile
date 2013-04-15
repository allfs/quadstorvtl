UNAME := $(shell uname)
ifeq ($(UNAME), Linux)
SUBDIRS =
else
SUBDIRS = pidof
endif

all: compile

compile:
	@set -e; \
	for i in $(SUBDIRS); do \
		cd $$i; sh build.sh; cd ..;\
	done

clobber:
	@set -e; \
	for i in $(SUBDIRS); do \
		cd $$i; \
		if [ -f Makefile ]; then \
			make clean; \
		fi; cd ..;\
	done

install:
	@set -e; \
	for i in $(SUBDIRS); do \
		$(MAKE) install -C $$i; \
	done

clean: clobber

