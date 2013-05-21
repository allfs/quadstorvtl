#!/bin/sh

checkerror() {
	if [ "$?" != "0" ]; then
		echo "ERROR: Building kernel modules failed!"
		exit 1
	fi
}

if [ ! -f /quadstor/lib/modules/corelib.o ]; then
	echo "Cannot find core library. Check if quadstor-vtl-core package is installed"
	exit 1
fi

os=`uname`
cd /quadstor/src/export
make clean && make 
checkerror

kvers=`uname -r`
mkdir /quadstor/lib/modules/$kvers

cp -f vtlcore.ko /quadstor/lib/modules/$kvers/
cp -f ldev.ko /quadstor/lib/modules/$kvers/

cd /quadstor/src/target-mode/iscsi/kernel/
make clean && make 
checkerror

cp -f iscsit.ko /quadstor/lib/modules/$kvers/

cd /quadstor/src/target-mode/iscsi/usr/
make clean && make 
checkerror

mkdir -p /quadstor/bin
cp -f ietadm /quadstor/bin

mkdir -p /quadstor/sbin
cp -f ietd /quadstor/sbin

cd /quadstor/src/target-mode/fc/qla2xxx
make clean && make 
checkerror

cp -f qla2xxx.ko /quadstor/lib/modules/$kvers/

cd /quadstor/src/target-mode/fc/srpt
make clean && make 
checkerror

cp -f ib_srpt.ko /quadstor/lib/modules/$kvers/

#Install the newly build qla2xxx driver
/quadstor/bin/qlainst
