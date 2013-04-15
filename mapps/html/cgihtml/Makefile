# Makefile for cgihtml.a
# $Id: Makefile,v 1.1.1.1 2011/11/21 23:22:24 quadstor Exp $

# macros and variables
CC= gcc
RANLIB= ranlib
CFLAGS= -g -Wall -DUNIX #-DUPLOADDIR='"/tmp"'
#CFLAGS = -g -Wall -DWINDOWS #-DUPLOADDIR='"/tmp"'

INSTALLDIR= /usr/local/etc/httpd/cgi-src

OBJS = string-lib.o cgi-llist.o cgi-lib.o html-lib.o
LIB = cgihtml.a

# targets
$(LIB): $(OBJS)
	ar cr $@ $(OBJS)
	$(RANLIB) $@

examples: $(LIB)
	cd examples; make all

all: examples

install: all
	cp -f *.h $(LIB) $(INSTALLDIR)
	cd examples; make install

clean:
	rm -f *.o $(LIB)

clobber:
	rm -f *.o $(LIB)
	cd examples; make clean

