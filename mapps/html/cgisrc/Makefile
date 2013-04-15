include ../../../build/rules.mk

CGILIST = system.cgi listvtl.cgi addvtl.cgi addvtlpost.cgi adddisk.cgi adddiskcomp.cgi adddiskpost.cgi indvvtl.cgi liststoragepool.cgi modifystoragepool.cgi addstoragepool.cgi addstoragepoolpost.cgi deletestoragepool.cgi viewstoragepool.cgi iscsiconfpost.cgi iscsiconf.cgi addvcartridge.cgi addvcartridgepost.cgi deletevcartridge.cgi deletevtl.cgi

all: $(CGILIST) strip install

CGIMAIN_SRCS = cgimain.c

CGIMAIN_OBJS = cgimain.o

HTDOCS = /var/www/html/
CGIBIN = /var/www/cgi-bin/

CFLAGS += -Wall -I../cgihtml/ -I../../../includes/ -DCGIBIN=\"$(CGIBIN)\" -I$(QUADSTOR_ROOT)/pgsql/include/ -I ../../../library/common

LDFLAGS += -Wl,-rpath=/quadstor/lib:$(QUADSTOR_ROOT)/library/common -Wl,-rpath=/quadstor/lib:$(QUADSTOR_ROOT)/library/client -L../../../library/common/ -L../../../library/client -ltlclnt -ltlmsg


CGIHTML = ../cgihtml/cgihtml.a 

addstoragepool.cgi: addstoragepool.o cgimain.o
	$(CC) $(CFLAGS) -o addstoragepool.cgi addstoragepool.o cgimain.o $(CGIHTML) $(LDFLAGS)

addstoragepoolpost.cgi: addstoragepoolpost.o cgimain.o
	$(CC) $(CFLAGS) -o addstoragepoolpost.cgi addstoragepoolpost.o cgimain.o $(CGIHTML) $(LDFLAGS)

liststoragepool.cgi: liststoragepool.o cgimain.o
	$(CC) $(CFLAGS) -o liststoragepool.cgi liststoragepool.o cgimain.o $(CGIHTML) $(LDFLAGS)

modifystoragepool.cgi: modifystoragepool.o cgimain.o
	$(CC) $(CFLAGS) -o modifystoragepool.cgi modifystoragepool.o cgimain.o $(CGIHTML) $(LDFLAGS)

viewstoragepool.cgi: viewstoragepool.o cgimain.o
	$(CC) $(CFLAGS) -o viewstoragepool.cgi viewstoragepool.o cgimain.o $(CGIHTML) $(LDFLAGS)

deletestoragepool.cgi: deletestoragepool.o cgimain.o
	$(CC) $(CFLAGS) -o deletestoragepool.cgi deletestoragepool.o cgimain.o $(CGIHTML) $(LDFLAGS)


indvvtl.cgi: indvvtl.o cgimain.o
	$(CC) $(CFLAGS) -o indvvtl.cgi indvvtl.o cgimain.o $(CGIHTML) $(LDFLAGS)

addvcartridge.cgi: addvcartridge.o cgimain.o
	$(CC) $(CFLAGS) -o addvcartridge.cgi addvcartridge.o cgimain.o $(CGIHTML) $(LDFLAGS)

addvcartridgepost.cgi: addvcartridgepost.o cgimain.o
	$(CC) $(CFLAGS) -o addvcartridgepost.cgi addvcartridgepost.o cgimain.o $(CGIHTML) $(LDFLAGS)

listvtl.cgi: listvtl.o cgimain.o
	$(CC) $(CFLAGS) -o listvtl.cgi listvtl.o cgimain.o $(CGIHTML) $(LDFLAGS)

adddisk.cgi: adddisk.o cgimain.o
	$(CC) $(CFLAGS) -o adddisk.cgi adddisk.o cgimain.o $(CGIHTML) $(LDFLAGS)

adddiskcomp.cgi: adddiskcomp.o cgimain.o
	$(CC) $(CFLAGS) -o adddiskcomp.cgi adddiskcomp.o cgimain.o $(CGIHTML) $(LDFLAGS)

adddiskpost.cgi: adddiskpost.o cgimain.o
	$(CC) $(CFLAGS) -o adddiskpost.cgi adddiskpost.o cgimain.o $(CGIHTML) $(LDFLAGS)

addvtl.cgi: addvtl.o cgimain.o
	$(CC) $(CFLAGS) -o addvtl.cgi addvtl.o cgimain.o $(CGIHTML) $(LDFLAGS)

addvtlpost.cgi: addvtlpost.o cgimain.o
	$(CC) $(CFLAGS) -o addvtlpost.cgi addvtlpost.o cgimain.o $(CGIHTML) $(LDFLAGS)

deletevtl.cgi: deletevtl.o cgimain.o
	$(CC) $(CFLAGS) -o deletevtl.cgi deletevtl.o cgimain.o $(CGIHTML) $(LDFLAGS)

deletevcartridge.cgi: deletevcartridge.o cgimain.o
	$(CC) $(CFLAGS) -o deletevcartridge.cgi deletevcartridge.o cgimain.o $(CGIHTML) $(LDFLAGS)

system.cgi: system.o cgimain.o
	$(CC) $(CFLAGS) -o system.cgi system.o cgimain.o $(CGIHTML) $(LDFLAGS)

iscsiconf.cgi: iscsiconf.o cgimain.o
	$(CC) $(CFLAGS) -o iscsiconf.cgi iscsiconf.o cgimain.o $(CGIHTML) $(LDFLAGS)

iscsiconfpost.cgi: iscsiconfpost.o cgimain.o
	$(CC) $(CFLAGS) -o iscsiconfpost.cgi iscsiconfpost.o cgimain.o $(CGIHTML) $(LDFLAGS)

clean:
	rm -f *.o *.cgi


install: $(CGILIST)
	@set -x; \
	export LD_LIBRARY_PATH=.; \
	sudo sh ./install.sh
	
strip:
	@set -x; \
	if [ "$(RELEASE_BUILD)" -eq "1" ]; then \
		strip --strip-debug *.cgi; \
	fi
