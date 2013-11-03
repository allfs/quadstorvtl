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
	mkdir -p $(QUADSTOR_INSTALL_ROOT)/etc; \
	cp $(ETC) $(QUADSTOR_INSTALL_ROOT)/etc/quadstorvtl
