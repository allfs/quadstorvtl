#!/bin/sh

os=`uname`

pkill -f /quadstor/pgsql/bin/postmaster > /dev/null 2>&1
pkill -f /quadstor/pgsql/bin/postgres > /dev/null 2>&1
rm -f /var/run/postmaster.9988.pid > /dev/null 2>&1
rm -f /quadstor/pgsql/data/postmaster.pid > /dev/null 2>&1
rm -f /var/lock/subsys/pgsql > /dev/null 2>&1
rm -f /tmp/.s.PGSQL.9988* > /dev/null 2>&1

if [ "$os" = "FreeBSD" ]; then
	echo "scdbuser" | /usr/sbin/pw add user scdbuser -d /quadstor/pgsql -h 0 
else 
	/usr/sbin/groupadd scdbuser > /dev/null 2>&1
	/usr/sbin/useradd -d /quadstor/pgsql -g scdbuser scdbuser > /dev/null 2>&1
fi

chown -R scdbuser:scdbuser /quadstor/pgsql/
if [ -x /sbin/runuser ]
then
    SU=/sbin/runuser
else
    SU=su
fi

rm -f /tmp/qstorpgdb.log

if [ "$os" = "FreeBSD" ]; then
	su -l scdbuser /quadstor/pgsql/scripts/pginit.sh
else
	$SU -l scdbuser -c "/quadstor/pgsql/scripts/pginit.sh"
fi
