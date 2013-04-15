#!/bin/bash
set -x

#/sbin/insmod kernel/iscsi_trgt.ko debug_enable_flags=1
/sbin/insmod kernel/iscsit.ko

cd ./usr 
#./ietd  -d 3 -c /quadstor/conf/iscsi.conf
ulimit -c unlimited
./ietd  -d 3 -c /quadstor/conf/iscsi.conf
#./ietd  -c /quadstor/conf/iscsi.conf
