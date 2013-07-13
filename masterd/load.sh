#!/bin/bash
set -x
pkill vtmdaemon
rm -f /quadstorvtl/.vtmdaemon
ulimit -c  unlimited
#valgrind --leak-check=yes --track-fds=yes --log-file=mdval.txt ./vtmdaemon
PATH="/sbin:/usr/sbin:/bin:/usr/bin:$PATH"
export PATH=$PATH
./vtmdaemon
