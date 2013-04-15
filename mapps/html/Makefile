SUBDIRS = cgihtml cgisrc
INSTALL_SUBDIRS = cgisrc

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

install:
	@set -e; \
	for i in $(INSTALL_SUBDIRS); do \
		$(MAKE) install -C $$i; \
	done


