include ../build/rules.mk

CFLAGS += -Wall -O2 -I$(QUADSTOR_ROOT)/includes -I$(QUADSTOR_ROOT)/common
#CFLAGS += -DENABLE_DEBUG

LDFLAGS += -L../library/common/ -L../library/server -ltlsrv -Wl,-rpath=/quadstorvtl/lib:/quadstorvtl/pgsql/lib:$(QUADSTOR_ROOT)/library/common:$(QUADSTOR_ROOT)/library/server -pthread

ifeq ($(UNAME), Linux)
LDFLAGS += -lpthread
else
LDFLAGS += -pthread
endif


all: mdaemon

SRCS = mdaemon.c

OBJS = mdaemon.o

mdaemon: $(SRCS)
	@set -x; \
	$(CC) $(CFLAGS) -o mdaemon $(SRCS) $(LDFLAGS); \
	if [ "$(RELEASE_BUILD)" -eq "1" ]; then \
		strip --strip-unneeded mdaemon; \
	fi

clean:
	rm -f *.o mdaemon 

clobber: clean

install:
	@set -e; \
	mkdir -p /quadstorvtl/sbin; \
	echo "cp -f mdaemon /quadstorvtl/sbin"; \
	cp -f mdaemon /quadstorvtl/sbin;
