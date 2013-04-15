#!/bin/bash
set -x

cd ./usr 
#./ietadm --op delete >/dev/null 2>/dev/null
killall ietd 2> /dev/null

#pkill ietd
/sbin/rmmod iscsit.ko
