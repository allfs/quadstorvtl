
include ../../build/rules.mk

#CFLAGS += -g -O -Wall -I. -I../../includes -I../common/ -I$(QUADSTOR_ROOT)/pgsql/include/ -I../../io -fpic -Winit-self -Wuninitialized
CFLAGS += -Wall -pthread -I. -I../../includes -I../common/ -I$(QUADSTOR_ROOT)/pgsql/include/ -I../../io -fpic -Winit-self
#CFLAGS +=  -O1 -Wall -I../../includes -I../common/ -I$(QUADSTOR_ROOT)/pgsql/include/ -I../../io -fpic -Winit-self -Wuninitialized

#CFLAGS += -DENABLE_DEBUG
#CFLAGS += -DENABLE_PARSER_TEST
CFLAGS += -DENABLE_ASSERT

CFLAGS += -DENABLE_IMPEXP

CFLAGS += -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE

LDFLAGS += -Wl,-rpath=/quadstor/lib:$(QUADSTOR_ROOT)/library/common -L../common -L/quadstor/lib -ltlmsg

ifeq ($(UNAME), Linux)
LDFLAGS += -lpthread
else
LDFLAGS += -pthread
endif

ifeq ($(UNAME), FreeBSD)
LDFLAGS += -lcam -lgeom
endif

SRCS = serverapis.c

OBJS = serverapis.o ietadm.o vdevice.o sqlint.o diskinfo.o md5c.o physlib.o pgsql.o fcconfig.o

all: libtlsrv.so

libtlsrv.so: $(OBJS)
	@set -x; \
	$(CC) -Wall $(CFLAGS) -shared -o libtlsrv.so $(OBJS) $(QUADSTOR_ROOT)/pgsql/lib/libpq.a  $(LDFLAGS); \
	if [ "$(RELEASE_BUILD)" -eq "1" ]; then \
		strip --strip-unneeded *.so; \
	fi

clean:
	rm -f *.o *.so

