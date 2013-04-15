
include ../../build/rules.mk

CFLAGS += -Wall -pthread -I$(QUADSTOR_ROOT)/includes -fpic
#CFLAGS += -O1 -Wall -I$(QUADSTOR_ROOT)/includes -I$(QUADSTOR_ROOT)/sgutils/ -I$(QUADSTOR_ROOT)/io/ -I$(QUADSTOR_ROOT)/pgsql/include/ -fpic  -Winit-self -Wuninitialized

#CFLAGS += -DENABLE_DEBUG
CFLAGS += -DENABLE_ASSERT
#CFLAGS += -DENABLE_MEMCHECK

ifeq ($(UNAME), Linux)
LDFLAGS += -lpthread
else
LDFLAGS += -pthread
endif


ifeq ($(UNAME), FreeBSD)
LDFLAGS += -lcam
endif

OBJS = messages.o commonapis.o physdisk.o

all: libtlmsg.so

libtlmsg.so: $(OBJS)
	@set -x; \
	$(CC) $(CFLAGS) -shared -o libtlmsg.so $(OBJS)  $(LDFLAGS); \
	if [ "$(RELEASE_BUILD)" -eq "1" ]; then \
		strip --strip-debug *.so; \
	fi

clean:
	rm -f *.o *.so
