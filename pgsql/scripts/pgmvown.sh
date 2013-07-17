#!/bin/sh

if [ -x /sbin/runuser ]
then
    SU=/sbin/runuser
else
    SU=su
fi

rm -f /tmp/qstorpgdbmove.sql
rm -f /tmp/qstorpgdbmove.log
echo "ALTER DATABASE qsdb OWNER TO vtdbuser;" >> /tmp/qstorpgdbmove.sql
echo "ALTER DATABASE postgres OWNER TO vtdbuser;" >> /tmp/qstorpgdbmove.sql
echo "ALTER DATABASE template0 OWNER TO vtdbuser;" >> /tmp/qstorpgdbmove.sql
echo "ALTER DATABASE template1 OWNER TO vtdbuser;" >> /tmp/qstorpgdbmove.sql
echo "ALTER TABLE fcconfig OWNER TO vtdbuser;" >> /tmp/qstorpgdbmove.sql
echo "ALTER TABLE iscsiconf OWNER TO vtdbuser;" >> /tmp/qstorpgdbmove.sql
echo "ALTER TABLE physstor OWNER TO vtdbuser;" >> /tmp/qstorpgdbmove.sql
echo "ALTER TABLE physstor_bid_seq OWNER TO vtdbuser;" >> /tmp/qstorpgdbmove.sql
echo "ALTER TABLE storagegroup OWNER TO vtdbuser;" >> /tmp/qstorpgdbmove.sql
echo "ALTER TABLE storagegroup_groupid_seq OWNER TO vtdbuser;" >> /tmp/qstorpgdbmove.sql
echo "ALTER TABLE sysinfo OWNER TO vtdbuser;" >> /tmp/qstorpgdbmove.sql
echo "ALTER TABLE vcartridge OWNER TO vtdbuser;" >> /tmp/qstorpgdbmove.sql
echo "ALTER TABLE vcartridge_tapeid_seq OWNER TO vtdbuser;" >> /tmp/qstorpgdbmove.sql
echo "ALTER TABLE vdrives OWNER TO vtdbuser;" >> /tmp/qstorpgdbmove.sql
echo "ALTER TABLE vtls OWNER TO vtdbuser;" >> /tmp/qstorpgdbmove.sql

chown -R scdbuser:scdbuser /quadstorvtl/pgsql/
if [ "$os" = "FreeBSD" ]; then
	su -l scdbuser -c '/quadstorvtl/pgsql/bin/pg_ctl start -w -D '/quadstorvtl/pgsql/data' -l /quadstorvtl/pgsql/pg.log'
	su -l scdbuser -c '/quadstorvtl/pgsql/bin/createuser -s -d -r -l vtdbuser >> /tmp/qstorpgdbmove.log 2>&1'
	su -l scdbuser -c '/quadstorvtl/pgsql/bin/psql -f /tmp/qstorpgdbmove.sql qsdb >> /tmp/qstorpgdbmove.log 2>&1'
else
	$SU -l scdbuser -c '/quadstorvtl/pgsql/bin/pg_ctl start -w -D '/quadstorvtl/pgsql/data' -l /quadstorvtl/pgsql/pg.log'
	$SU -l scdbuser -c '/quadstorvtl/pgsql/bin/createuser -s -d -r -l vtdbuser >> /tmp/qstorpgdbmove.log 2>&1'
	$SU -l scdbuser -c '/quadstorvtl/pgsql/bin/psql -f /tmp/qstorpgdbmove.sql qsdb >> /tmp/qstorpgdbmove.log 2>&1'
fi
/quadstorvtl/pgsql/etc/pgsql stop > /dev/null 2>&1
chown -R vtdbuser:vtdbuser /quadstorvtl/pgsql/
