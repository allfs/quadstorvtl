#!/bin/sh

set -x
checkerror() {
	if [ "$?" != "0" ]; then
		exit 1
	fi
}

sync
sleep 4
/quadstor/pgsql/etc/pgsql start
checkerror
sleep 8
/sbin/kldload /quadstor/quadstor/export/vtlcore.ko
checkerror

/sbin/kldload /quadstor/quadstor/export/ldev.ko
checkerror

/sbin/kldload /quadstor/quadstor/target-mode/iscsi/kernel/iscsit.ko
checkerror

sleep 4
cd /quadstor/quadstor/masterd && sh load.sh
sleep 4

/quadstor/quadstor/target-mode/iscsi/usr/ietd -d 3 -c /quadstor/conf/iscsi.conf
checkerror

cd /quadstor/quadstor/scctl
./scctl -l
