#!/bin/bash
set -x
root=`pwd`

checkerror() {
	if [ "$?" != "0" ]; then
		exit 1
	fi
}

if [ -f /usr/bin/chcon ]; then
        /usr/bin/chcon -t textrel_shlib_t /quadstor/lib/libtl* > /dev/null 2>&1
        /usr/bin/chcon -v -R -t httpd_unconfined_script_exec_t /var/www/cgi-bin/*.cgi > /dev/null 2>&1
fi

#/sbin/modprobe netconsole netconsole=@10.0.13.101/eth0,6666@10.0.13.7/00:15:17:60:E7:B4
/sbin/modprobe netconsole netconsole=@10.0.13.7/br0,@10.0.13.4/00:15:17:26:70:D2

sync
/quadstor/pgsql/etc/pgsql start

/sbin/insmod /quadstor/quadstor/export/vtlcore.ko
checkerror

/sbin/insmod /quadstor/quadstor/export/ldev.ko
checkerror

/sbin/insmod /quadstor/quadstor/target-mode/iscsi/kernel/iscsit.ko

sleep 6

cd $root/masterd
sh load.sh

/quadstor/quadstor/target-mode/iscsi/usr/ietd  -d 3 -c /quadstor/conf/iscsi.conf

cd $root/scctl
./scctl -l
cd $root
