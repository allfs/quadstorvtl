include ../build/rules.mk

CFLAGS += -Wall -I$(QUADSTOR_ROOT)/includes -I$(QUADSTOR_ROOT)/common -I$(QUADSTOR_ROOT)/core -Wl,-rpath=$(QUADSTOR_ROOT)/library/common -Wl,-rpath=$(QUADSTOR_ROOT)/library/client -Wl,-rpath=/quadstor/lib -Wl,-rpath=$(QUADSTOR_ROOT)/library/server

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
	mkdir -p /quadstor/bin
	cp -f scctl /quadstor/bin
	cp -f dbrecover /quadstor/bin
	if [ -f cam ]; then \
		cp -f cam /quadstor/bin; \
	fi
	cp -f fcconfig /quadstor/bin

clean:
	rm -f *.o scctl vctl cam dbrecover fcconfig

clobber: clean
