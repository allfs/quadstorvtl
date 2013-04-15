#/bin/sh

set -x
os=`uname`

if [ "$1" = "install" ]; then
	exit 0
fi

rm -f corelib.o
if [ "$os" = "FreeBSD" ]; then
	make -f Makefile.bsd $1
else
	make -f Makefile.ext $1
fi

if [ "$?" != "0" ]; then
  	exit 1
fi

if [ "$1" = "clean" ]; then
	exit 0
fi

rm -f core.ko corelib.o
if [ "$os" = "FreeBSD" ]; then
	ld  -d -warn-common -r -d -o corelib.o `ls *.o`
else
	ld -m elf_x86_64 -r -o corelib.o `ls *.o` `ls util/*.o`
fi

#objcopy --strip-debug corelib.o
#objcopy --strip-unneeded corelib.o
mkdir -p /quadstor/lib/modules/
cp -f corelib.o /quadstor/lib/modules/
