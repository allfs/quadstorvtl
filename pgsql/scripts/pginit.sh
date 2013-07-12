#!/bin/sh

os=`uname`
if [ "$os" = "FreeBSD" ]; then
	sctmpfile=`mktemp -t XXXXXX`
else
	sctmpfile=`mktemp`
fi

echo quadstor > $sctmpfile
cd /quadstorvtl/pgsql/
bin/initdb -D /quadstorvtl/pgsql/data/ -U scdbuser --pwfile=$sctmpfile  > /tmp/qstorpgdb.log 2>&1
if [ "$?" != "0" ]; then
 echo "Failed to initialize postgresql database! Check /tmp/qstorpgdb.log for more information"
 rm -f $sctmpfile
 exit 1
fi

bin/pg_ctl -w -D /quadstorvtl/pgsql/data/ -l $sctmpfile start >> /tmp/qstorpgdb.log 2>&1

sleep 5

bin/createdb --owner=scdbuser qsdb >> /tmp/qstorpgdb.log 2>&1
if [ "$?" != "0" ]; then
 echo "Failed to create quadstor database! Check /tmp/qstorpgdb.log for more information"
 bin/pg_ctl -w -D /quadstorvtl/pgsql/data/ -l $sctmpfile stop >> /tmp/qstorpgdb.log 2>&1
 rm -f $sctmpfile
 exit 1
fi

bin/psql -f /quadstorvtl/pgsql/share/qsdb.sql qsdb >> /tmp/qstorpgdb.log 2>&1

bin/pg_ctl -w -D /quadstorvtl/pgsql/data/ -l $sctmpfile stop >> /tmp/qstorpgdb.log 2>&1
sleep 5

rm -f $sctmpfile
exit 0
