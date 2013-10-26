all: compile

CFLAGS = -O2 -pipe  -std=gnu99 -fstack-protector

compile: pidof.o
	$(CC) -o pidof pidof.o -lkvm

clean:
	rm -f *.o pidof

install:
	@set -e; \
	mkdir -p $(QUADSTOR_ROOT)/bin; \
	echo "cp -f pidof $(QUADSTOR_ROOT)/bin"; \
	install -m 755 pidof $(QUADSTOR_ROOT)/bin/pidof;
