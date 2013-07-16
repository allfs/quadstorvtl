#!/bin/sh

os=`uname`

pkill -f /quadstorvtl/pgsql/bin/postmaster > /dev/null 2>&1
pkill -f /quadstorvtl/pgsql/bin/postgres > /dev/null 2>&1
rm -f /var/run/postmaster.9989.pid > /dev/null 2>&1
rm -f /quadstorvtl/pgsql/data/postmaster.pid > /dev/null 2>&1
rm -f /var/lock/subsys/pgsql > /dev/null 2>&1
rm -f /tmp/.s.PGSQL.9989* > /dev/null 2>&1

if [ "$os" = "FreeBSD" ]; then
	echo "vtdbuser" | /usr/sbin/pw add user vtdbuser -d /quadstorvtl/pgsql -h 0 
else 
	/usr/sbin/groupadd vtdbuser > /dev/null 2>&1
	/usr/sbin/useradd -d /quadstorvtl/pgsql -g vtdbuser vtdbuser > /dev/null 2>&1
fi

chown -R vtdbuser:vtdbuser /quadstorvtl/pgsql/
if [ -x /sbin/runuser ]
then
    SU=/sbin/runuser
else
    SU=su
fi

rm -f /tmp/qstorpgdb.log

if [ "$os" = "FreeBSD" ]; then
	su -l vtdbuser /quadstorvtl/pgsql/scripts/pginit.sh
else
	$SU -l vtdbuser -c "/quadstorvtl/pgsql/scripts/pginit.sh"
fi
