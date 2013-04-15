
SUBDIRS = common server client

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

clobber: clean

install:
