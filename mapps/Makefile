CLEAN_SUBDIRS = html
SUBDIRS = html

all: compile

compile:
	@set -e; \
	for i in $(SUBDIRS); do \
		$(MAKE) -C $$i; \
	done

clobber:
	@set -e; \
	for i in $(SUBDIRS); do \
		$(MAKE) clean -C $$i; \
	done

clean:
	@set -e; \
	for i in $(CLEAN_SUBDIRS); do \
		$(MAKE) clean -C $$i; \
	done


