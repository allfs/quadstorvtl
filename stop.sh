#!/bin/sh
set -x

pkill ietd
rm -f /var/run/iet.sock
sleep 2

cd /quadstor/quadstor/scctl/
./scctl -u
sleep 4

cd /quadstor/quadstor/masterd
sh unload.sh
sleep 4

/sbin/kldunload iscsit
/sbin/kldunload ldev 
/sbin/kldunload vtlcore 

/quadstor/pgsql/etc/pgsql stop
cd /quadstor/quadstor
