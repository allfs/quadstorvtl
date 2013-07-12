UNAME := $(shell uname)
ETC := quadstor
ifeq ($(UNAME), Linux)
ETC := quadstor.linux
endif

compile:

clean:

clobber:

install:
	@set -x; \
	mkdir -p /quadstorvtl/etc; \
	cp $(ETC) /quadstorvtl/etc/
