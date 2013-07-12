#!/bin/sh
set -x

pkill ietd
rm -f /var/run/iet.sock
sleep 2

cd /quadstorvtl/quadstor/scctl/
./scctl -u
sleep 4

cd /quadstorvtl/quadstor/masterd
sh unload.sh
sleep 4

/sbin/kldunload iscsit
/sbin/kldunload ldev 
/sbin/kldunload vtlcore 

/quadstorvtl/pgsql/etc/pgsql stop
cd /quadstorvtl/quadstor
