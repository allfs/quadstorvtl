#Requires gmake for building on FreeBSD systems. To complile in this directory type gmake

UNAME := $(shell uname)

ifeq ($(UNAME), Linux)
CFLAGS += -DLINUX
CXXFLAGS += -DLINUX
endif

ifeq ($(UNAME), FreeBSD)
CFLAGS += -DFREEBSD
CXXFLAGS += -DFREEBSD
endif


CFLAGS += -O2 -fno-inline -Wall -Wstrict-prototypes -g -I../include -I.
CFLAGS += -D_GNU_SOURCE # required for glibc >= 2.8
#CFLAGS += -DENABLE_DEBUG

PROGRAMS = ietd ietadm
LIBS = 

all: $(PROGRAMS) install

ietd: ietd.o iscsid.o conn.o session.o target.o message.o ctldev.o log.o chap.o event.o param.o plain.o isns.o md5.o sha1.o

	$(CC) $^ -o $@ $(LIBS)

ietadm: ietadm.o param.o
	$(CC) $^ -o $@

install:
	@set -e; \
	mkdir -p $(QUADSTOR_INSTALL_ROOT)/bin; \
	mkdir -p $(QUADSTOR_INSTALL_ROOT)/sbin; \
	echo "cp -f ietadm $(QUADSTOR_INSTALL_ROOT)/bin"; \
	cp -f ietadm $(QUADSTOR_INSTALL_ROOT)/bin; \
	echo "cp -f ietd $(QUADSTOR_INSTALL_ROOT)/sbin"; \
	cp -f ietd $(QUADSTOR_INSTALL_ROOT)/sbin;

clean:
	rm -f *.o $(PROGRAMS)
