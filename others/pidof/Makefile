all: compile

CFLAGS = -O2 -pipe  -std=gnu99 -fstack-protector

compile: pidof.o
	$(CC) -o pidof pidof.o -lkvm

clean:
	rm -f *.o pidof

install:
	@set -e; \
	mkdir -p /quadstor/bin; \
	echo "cp -f pidof /quadstor/bin"; \
	install -m 755 pidof /quadstor/bin/pidof;
