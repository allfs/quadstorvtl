all: compile

CFLAGS = -O2 -pipe  -std=gnu99 -fstack-protector

compile: pidof.o
	$(CC) -o pidof pidof.o -lkvm

clean:
	rm -f *.o pidof

install:
	@set -e; \
	mkdir -p /quadstorvtl/bin; \
	echo "cp -f pidof /quadstorvtl/bin"; \
	install -m 755 pidof /quadstorvtl/bin/pidof;
