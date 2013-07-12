#!/bin/sh

set -x
checkerror() {
	if [ "$?" != "0" ]; then
		exit 1
	fi
}

sync
sleep 4
/quadstorvtl/pgsql/etc/pgsql start
checkerror
sleep 8
/sbin/kldload /quadstorvtl/quadstor/export/vtlcore.ko
checkerror

/sbin/kldload /quadstorvtl/quadstor/export/ldev.ko
checkerror

/sbin/kldload /quadstorvtl/quadstor/target-mode/iscsi/kernel/iscsit.ko
checkerror

sleep 4
cd /quadstorvtl/quadstor/masterd && sh load.sh
sleep 4

/quadstorvtl/quadstor/target-mode/iscsi/usr/ietd -d 3 -c /quadstorvtl/conf/iscsi.conf
checkerror

cd /quadstorvtl/quadstor/scctl
./scctl -l
