#/bin/sh

set -x
os=`uname`

if [ "$QUADSTOR_ROOT" = "" ]; then
	QUADSTOR_ROOT=`cd .. && pwd`
fi

if [ "$1" = "install" ]; then
	exit 0
fi

rm -f corelib.o
if [ "$os" = "FreeBSD" ]; then
	make -f Makefile.bsd $1 QUADSTOR_ROOT=$QUADSTOR_ROOT
else
	if [ "$1" != "x86" ]; then
		make -f Makefile.ext $1 QUADSTOR_ROOT=$QUADSTOR_ROOT
	else
		rm -f corelib.c
		for i in `ls -1 *.c util/*.c | grep -v bsd`; do
			echo "#include \"$i\"" >> corelib.c
		done
		make -f Makefile.ext.x86 QUADSTOR_ROOT=$QUADSTOR_ROOT
	fi
fi

if [ "$?" != "0" ]; then
  	exit 1
fi

if [ "$1" = "clean" ]; then
	exit 0
fi

if [ "$os" = "FreeBSD" ]; then
	rm -f core.ko corelib.o
	ld  -d -warn-common -r -d -o corelib.o `ls *.o`
else
	if [ "$1" != "x86" ]; then
		rm -f core.ko corelib.o
		ld -m elf_x86_64 -r -o corelib.o `ls *.o` `ls util/*.o`
	fi
fi

#objcopy --strip-debug corelib.o
#objcopy --strip-unneeded corelib.o
mkdir -p $QUADSTOR_ROOT/lib/modules/
cp -f corelib.o $QUADSTOR_ROOT/lib/modules/
