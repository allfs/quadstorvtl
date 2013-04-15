
include ../../build/rules.mk

CFLAGS += -Wall -pthread -I../../includes -fpic
#CFLAGS += -O1 -Wall -I../../includes -fpic  -Winit-self -Wuninitialized

LDFLAGS += -Wl,-rpath=/quadstor/lib:$(QUADSTOR_ROOT)/library/common -L../common -ltlmsg

SRCS = clientapis.c

OBJS = clientapis.o

all: libtlclnt.so

libtlclnt.so: $(OBJS)
	@set -x; \
	$(CC) $(CFLAGS) -shared -o libtlclnt.so $(OBJS) -Wl,-rpath=/quadstor/lib:$(QUADSTOR_ROOT)/library/common -L../common -ltlmsg; \
	if [ "$(RELEASE_BUILD)" -eq "1" ]; then \
		strip --strip-debug *.so; \
	fi

clean:
	rm -f *.o *.so
