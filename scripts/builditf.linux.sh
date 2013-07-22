#!/bin/sh

checkerror() {
	if [ "$?" != "0" ]; then
		echo "ERROR: Building kernel modules failed!"
		exit 1
	fi
}

if [ ! -f /quadstorvtl/lib/modules/corelib.o ]; then
	echo "Cannot find core library. Check if quadstor-vtl-core package is installed"
	exit 1
fi

os=`uname`
cd /quadstorvtl/src/export
make clean && make 
checkerror

kvers=`uname -r`
mkdir /quadstorvtl/lib/modules/$kvers

cp -f vtlcore.ko /quadstorvtl/lib/modules/$kvers/
cp -f vtldev.ko /quadstorvtl/lib/modules/$kvers/

cd /quadstorvtl/src/target-mode/iscsi/kernel/
make clean && make 
checkerror

cp -f iscsit.ko /quadstorvtl/lib/modules/$kvers/

cd /quadstorvtl/src/target-mode/iscsi/usr/
make clean && make 
checkerror

mkdir -p /quadstorvtl/bin
cp -f ietadm /quadstorvtl/bin

mkdir -p /quadstorvtl/sbin
cp -f ietd /quadstorvtl/sbin

cd /quadstorvtl/src/target-mode/fc/qla2xxx
make clean && make 
checkerror

cp -f qla2xxx.ko /quadstorvtl/lib/modules/$kvers/

cd /quadstorvtl/src/target-mode/fc/srpt
make clean > /dev/null 2>&1
make  > /dev/null 2>&1

if [ -f ib_srpt.ko ]; then
	cp -f ib_srpt.ko /quadstorvtl/lib/modules/$kvers/
fi

#Install the newly build qla2xxx driver
/quadstorvtl/bin/qlainst
