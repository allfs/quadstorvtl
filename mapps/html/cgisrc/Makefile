include ../../../build/rules.mk

CGILIST = vtsystem.cgi listvtl.cgi addvtl.cgi addvtlpost.cgi vtadddisk.cgi vtadddiskcomp.cgi vtadddiskpost.cgi indvvtl.cgi vtliststoragepool.cgi vtmodifystoragepool.cgi vtaddstoragepool.cgi vtaddstoragepoolpost.cgi vtdeletestoragepool.cgi vtviewstoragepool.cgi vtiscsiconfpost.cgi vtiscsiconf.cgi addvcartridge.cgi addvcartridgepost.cgi deletevcartridge.cgi deletevtl.cgi vtrescandisk.cgi addlvdrive.cgi listvdrive.cgi addvdrive.cgi addvdrivepost.cgi indvvdrive.cgi loadvdrive.cgi addvtlseldrive.cgi reloadexp.cgi vdrivestats.cgi resetvdrivestats.cgi

all: $(CGILIST) strip install

CGIMAIN_SRCS = cgimain.c

CGIMAIN_OBJS = cgimain.o

HTDOCS = /var/www/html/
CGIBIN = /var/www/cgi-bin/

CFLAGS += -Wall -I../cgihtml/ -I../../../includes/ -DCGIBIN=\"$(CGIBIN)\" -I$(QUADSTOR_ROOT)/pgsql/include/ -I ../../../library/common

LDFLAGS += -Wl,-rpath=/quadstor/lib:$(QUADSTOR_ROOT)/library/common -Wl,-rpath=/quadstor/lib:$(QUADSTOR_ROOT)/library/client -L../../../library/common/ -L../../../library/client -ltlclnt -ltlmsg


CGIHTML = ../cgihtml/cgihtml.a 

vtaddstoragepool.cgi: vtaddstoragepool.o cgimain.o
	$(CC) $(CFLAGS) -o vtaddstoragepool.cgi vtaddstoragepool.o cgimain.o $(CGIHTML) $(LDFLAGS)

vtaddstoragepoolpost.cgi: vtaddstoragepoolpost.o cgimain.o
	$(CC) $(CFLAGS) -o vtaddstoragepoolpost.cgi vtaddstoragepoolpost.o cgimain.o $(CGIHTML) $(LDFLAGS)

vtliststoragepool.cgi: vtliststoragepool.o cgimain.o
	$(CC) $(CFLAGS) -o vtliststoragepool.cgi vtliststoragepool.o cgimain.o $(CGIHTML) $(LDFLAGS)

vtmodifystoragepool.cgi: vtmodifystoragepool.o cgimain.o
	$(CC) $(CFLAGS) -o vtmodifystoragepool.cgi vtmodifystoragepool.o cgimain.o $(CGIHTML) $(LDFLAGS)

vtviewstoragepool.cgi: vtviewstoragepool.o cgimain.o
	$(CC) $(CFLAGS) -o vtviewstoragepool.cgi vtviewstoragepool.o cgimain.o $(CGIHTML) $(LDFLAGS)

vtdeletestoragepool.cgi: vtdeletestoragepool.o cgimain.o
	$(CC) $(CFLAGS) -o vtdeletestoragepool.cgi vtdeletestoragepool.o cgimain.o $(CGIHTML) $(LDFLAGS)


indvvtl.cgi: indvvtl.o cgimain.o
	$(CC) $(CFLAGS) -o indvvtl.cgi indvvtl.o cgimain.o $(CGIHTML) $(LDFLAGS)

addvcartridge.cgi: addvcartridge.o cgimain.o
	$(CC) $(CFLAGS) -o addvcartridge.cgi addvcartridge.o cgimain.o $(CGIHTML) $(LDFLAGS)

addvcartridgepost.cgi: addvcartridgepost.o cgimain.o
	$(CC) $(CFLAGS) -o addvcartridgepost.cgi addvcartridgepost.o cgimain.o $(CGIHTML) $(LDFLAGS)

listvtl.cgi: listvtl.o cgimain.o
	$(CC) $(CFLAGS) -o listvtl.cgi listvtl.o cgimain.o $(CGIHTML) $(LDFLAGS)

vtadddisk.cgi: vtadddisk.o cgimain.o
	$(CC) $(CFLAGS) -o vtadddisk.cgi vtadddisk.o cgimain.o $(CGIHTML) $(LDFLAGS)

vtadddiskcomp.cgi: vtadddiskcomp.o cgimain.o
	$(CC) $(CFLAGS) -o vtadddiskcomp.cgi vtadddiskcomp.o cgimain.o $(CGIHTML) $(LDFLAGS)

vtadddiskpost.cgi: vtadddiskpost.o cgimain.o
	$(CC) $(CFLAGS) -o vtadddiskpost.cgi vtadddiskpost.o cgimain.o $(CGIHTML) $(LDFLAGS)

addvtl.cgi: addvtl.o cgimain.o
	$(CC) $(CFLAGS) -o addvtl.cgi addvtl.o cgimain.o $(CGIHTML) $(LDFLAGS)

addvtlpost.cgi: addvtlpost.o cgimain.o
	$(CC) $(CFLAGS) -o addvtlpost.cgi addvtlpost.o cgimain.o $(CGIHTML) $(LDFLAGS)

deletevtl.cgi: deletevtl.o cgimain.o
	$(CC) $(CFLAGS) -o deletevtl.cgi deletevtl.o cgimain.o $(CGIHTML) $(LDFLAGS)

deletevcartridge.cgi: deletevcartridge.o cgimain.o
	$(CC) $(CFLAGS) -o deletevcartridge.cgi deletevcartridge.o cgimain.o $(CGIHTML) $(LDFLAGS)

vtsystem.cgi: vtsystem.o cgimain.o
	$(CC) $(CFLAGS) -o vtsystem.cgi vtsystem.o cgimain.o $(CGIHTML) $(LDFLAGS)

vtiscsiconf.cgi: vtiscsiconf.o cgimain.o
	$(CC) $(CFLAGS) -o vtiscsiconf.cgi vtiscsiconf.o cgimain.o $(CGIHTML) $(LDFLAGS)

vtiscsiconfpost.cgi: vtiscsiconfpost.o cgimain.o
	$(CC) $(CFLAGS) -o vtiscsiconfpost.cgi vtiscsiconfpost.o cgimain.o $(CGIHTML) $(LDFLAGS)

vtrescandisk.cgi: vtrescandisk.o cgimain.o
	$(CC) $(CFLAGS) -o vtrescandisk.cgi vtrescandisk.o cgimain.o $(CGIHTML) $(LDFLAGS)

addlvdrive.cgi: addlvdrive.o cgimain.o
	$(CC) $(CFLAGS) -o addlvdrive.cgi addlvdrive.o cgimain.o $(CGIHTML) $(LDFLAGS)

listvdrive.cgi: listvdrive.o cgimain.o
	$(CC) $(CFLAGS) -o listvdrive.cgi listvdrive.o cgimain.o $(CGIHTML) $(LDFLAGS)

addvdrive.cgi: addvdrive.o cgimain.o
	$(CC) $(CFLAGS) -o addvdrive.cgi addvdrive.o cgimain.o $(CGIHTML) $(LDFLAGS)

addvdrivepost.cgi: addvdrivepost.o cgimain.o
	$(CC) $(CFLAGS) -o addvdrivepost.cgi addvdrivepost.o cgimain.o $(CGIHTML) $(LDFLAGS)

indvvdrive.cgi: indvvdrive.o cgimain.o
	$(CC) $(CFLAGS) -o indvvdrive.cgi indvvdrive.o cgimain.o $(CGIHTML) $(LDFLAGS)

loadvdrive.cgi: loadvdrive.o cgimain.o
	$(CC) $(CFLAGS) -o loadvdrive.cgi loadvdrive.o cgimain.o $(CGIHTML) $(LDFLAGS)

addvtlseldrive.cgi: addvtlseldrive.o cgimain.o
	$(CC) $(CFLAGS) -o addvtlseldrive.cgi addvtlseldrive.o cgimain.o $(CGIHTML) $(LDFLAGS)

reloadexp.cgi: reloadexp.o cgimain.o
	$(CC) $(CFLAGS) -o reloadexp.cgi reloadexp.o cgimain.o $(CGIHTML) $(LDFLAGS)

vdrivestats.cgi: vdrivestats.o cgimain.o
	$(CC) $(CFLAGS) -o vdrivestats.cgi vdrivestats.o cgimain.o $(CGIHTML) $(LDFLAGS)

resetvdrivestats.cgi: resetvdrivestats.o cgimain.o
	$(CC) $(CFLAGS) -o resetvdrivestats.cgi resetvdrivestats.o cgimain.o $(CGIHTML) $(LDFLAGS)

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
