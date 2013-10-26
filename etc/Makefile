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
	mkdir -p $(QUADSTOR_ROOT)/etc; \
	cp $(ETC) $(QUADDSTOR_ROOT)/etc/quadstorvtl
