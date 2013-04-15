#!/bin/sh

checkerror() {
	if [ "$?" != "0" ]; then
		exit 1
	fi
}

os=`uname`
GMAKE="make"
if [ "$os" = "FreeBSD" ]; then
	GMAKE="gmake"
fi

cd /quadstor/src/target-mode/iscsi/kernel/
make clean && make 
checkerror

cp -f iscsit.ko /quadstor/lib/modules/

cd /quadstor/src/target-mode/iscsi/usr/
$GMAKE clean && $GMAKE 
checkerror

mkdir -p /quadstor/bin
cp -f ietadm /quadstor/bin

mkdir -p /quadstor/sbin
cp -f ietd /quadstor/sbin
