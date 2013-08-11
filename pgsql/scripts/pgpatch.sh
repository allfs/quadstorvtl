#!/bin/sh
os=`uname`
/quadstorvtl/pgsql/etc/pgsql stop > /dev/null 2>&1
pkill -f /quadstorvtl/pgsql/bin/postmaster > /dev/null 2>&1
pkill -f /quadstorvtl/pgsql/bin/postgres > /dev/null 2>&1
rm -f /var/run/postmaster.9989.pid > /dev/null 2>&1
rm -f /quadstorvtl/pgsql/data/postmaster.pid > /dev/null 2>&1
rm -f /var/lock/subsys/pgsql > /dev/null 2>&1
rm -f /tmp/.s.PGSQL.9989* > /dev/null 2>&1

/quadstorvtl/pgsql/etc/pgsql start > /dev/null 2>&1

if [ -x /sbin/runuser ]
then
    SU=/sbin/runuser
else
    SU=su
fi

rm -f /tmp/qstorpgdbpatch.log
if [ "$os" = "FreeBSD" ]; then
	su -l vtdbuser -c '/quadstorvtl/pgsql/bin/psql -f /quadstorvtl/pgsql/share/qsdbpatch.sql qsdb > /tmp/qstorpgdbpatch.log 2>&1'
else
	$SU -l vtdbuser -c "/quadstorvtl/pgsql/bin/psql -f /quadstorvtl/pgsql/share/qsdbpatch.sql qsdb > /tmp/qstorpgdbpatch.log 2>&1"
fi

/quadstorvtl/pgsql/etc/pgsql stop > /dev/null 2>&1
