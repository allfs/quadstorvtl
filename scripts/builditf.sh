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

cd /quadstorvtl/src/target-mode/iscsi/kernel/
make clean && make 
checkerror

cp -f iscsit.ko /quadstorvtl/lib/modules/

cd /quadstorvtl/src/target-mode/iscsi/usr/
$GMAKE clean && $GMAKE 
checkerror

mkdir -p /quadstorvtl/bin
cp -f ietadm /quadstorvtl/bin

mkdir -p /quadstorvtl/sbin
cp -f ietd /quadstorvtl/sbin
