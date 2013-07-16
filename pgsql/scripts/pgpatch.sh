#!/bin/sh
os=`uname`
/quadstorvtl/pgsql/etc/pgsql stop > /dev/null 2>&1

/quadstorvtl/pgsql/etc/pgsql start > /dev/null 2>&1
sleep 5

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
