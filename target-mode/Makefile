QUADSTOR_ROOT=/quadstor/quadstor

include $(QUADSTOR_ROOT)/build/rules.mk

SUBDIRS = iscsi
CLOBBER_SUBDIRS = fc iscsi

ifeq "$(ENABLE_FC)" "1"
SUBDIRS += fc
endif

all: compile

compile:
	@set -e; \
	for i in $(SUBDIRS); do \
		$(MAKE) -C $$i; \
	done

clean:
	@set -e; \
	for i in $(SUBDIRS); do \
		$(MAKE) clean -C $$i; \
	done

clobber:
	@set -e; \
	for i in $(CLOBBER_SUBDIRS); do \
		$(MAKE) clean -C $$i; \
	done

