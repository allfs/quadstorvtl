#!/bin/sh
os=`uname`
/quadstor/pgsql/etc/pgsql stop > /dev/null 2>&1

/quadstor/pgsql/etc/pgsql start > /dev/null 2>&1
sleep 5

if [ -x /sbin/runuser ]
then
    SU=/sbin/runuser
else
    SU=su
fi

rm -f /tmp/qstorpgdbpatch.log
if [ "$os" = "FreeBSD" ]; then
	su -l scdbuser -c '/quadstor/pgsql/bin/psql -f /quadstor/pgsql/share/qsdbpatch.sql qsdb > /tmp/qstorpgdbpatch.log 2>&1'
else
	$SU -l scdbuser -c "/quadstor/pgsql/bin/psql -f /quadstor/pgsql/share/qsdbpatch.sql qsdb > /tmp/qstorpgdbpatch.log 2>&1"
fi

/quadstor/pgsql/etc/pgsql stop > /dev/null 2>&1
