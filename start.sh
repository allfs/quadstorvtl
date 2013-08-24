#!/bin/sh

set -x
checkerror() {
	if [ "$?" != "0" ]; then
		exit 1
	fi
}

mkdir -p /quadstorvtl/tmp
chmod 777 /quadstorvtl/tmp

sync
sleep 4
/quadstorvtl/pgsql/etc/pgsql start
checkerror
sleep 8
/sbin/kldload /quadstorvtl/quadstor/export/vtlcore.ko
checkerror

/sbin/kldload /quadstorvtl/quadstor/export/vtldev.ko
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
