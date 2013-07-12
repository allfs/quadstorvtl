include ../build/rules.mk

CFLAGS += -Wall -I$(QUADSTOR_ROOT)/includes -I$(QUADSTOR_ROOT)/common -I$(QUADSTOR_ROOT)/core -Wl,-rpath=$(QUADSTOR_ROOT)/library/common -Wl,-rpath=$(QUADSTOR_ROOT)/library/client -Wl,-rpath=/quadstorvtl/lib -Wl,-rpath=$(QUADSTOR_ROOT)/library/server

LDFLAGS += -L../library/common/ -L../library/client -L../library/server -ltlclnt

ifeq ($(UNAME), FreeBSD)
LIBCAM = -lcam
CAM = cam
endif

all: scctl dbrecover $(CAM) vctl fcconfig

dbrecover: dbrecover.c
	$(CC) $(CFLAGS) -D_GNU_SOURCE -I../export/ -I../pgsql/include -I../library/server -o dbrecover dbrecover.c $(LDFLAGS) $(LIBGEOM) -ltlmsg -ltlsrv

cam: cam.c
	$(CC) $(CFLAGS) -o cam cam.c $(LDFLAGS) -lcam

install:
	@set -e;
	mkdir -p /quadstorvtl/bin
	cp -f scctl /quadstorvtl/bin
	cp -f dbrecover /quadstorvtl/bin
	if [ -f cam ]; then \
		cp -f cam /quadstorvtl/bin; \
	fi
	cp -f fcconfig /quadstorvtl/bin

clean:
	rm -f *.o scctl vctl cam dbrecover fcconfig

clobber: clean
