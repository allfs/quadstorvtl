#!/bin/bash
set -x
DESTDIR=$2
x86=$1
CURDIR=`pwd`
os=`uname`
rm -rf $DESTDIR/quadstor/pgsql/bin/
mkdir -p $DESTDIR/quadstor/pgsql/bin/
mkdir -p $DESTDIR/quadstor/pgsql/lib/
cp bin/* $DESTDIR/quadstor/pgsql/bin/
chmod +x $DESTDIR/quadstor/pgsql/bin/*
cd $DESTDIR/quadstor/pgsql/bin/ && ln -s postgres postmaster
cd $CURDIR

rm -rf $DESTDIR/quadstor/pgsql/share
mkdir -p $DESTDIR/quadstor/pgsql/share
#cp share/pg_hba.conf $DESTDIR/quadstor/pgsql/share/pg_hba.conf.sample
#cp share/postgresql.conf $DESTDIR/quadstor/pgsql/share/postgresql.conf.sample
cp share/postgres.bki $DESTDIR/quadstor/pgsql/share/
cp share/postgres.shdescription $DESTDIR/quadstor/pgsql/share/
cp share/*.sql $DESTDIR/quadstor/pgsql/share/
cp -pr lib/* $DESTDIR/quadstor/pgsql/lib/
cp sqls/*.sql $DESTDIR/quadstor/pgsql/share/
cp share/*.sample $DESTDIR/quadstor/pgsql/share/
cp share/sql_features.txt $DESTDIR/quadstor/pgsql/share/
cp -r share/timezone $DESTDIR/quadstor/pgsql/share/
cp -r share/timezonesets $DESTDIR/quadstor/pgsql/share/
#cd $DESTDIR/quadstor/pgsql/share/timezone
for i in `find $DESTDIR/quadstor/pgsql/share/timezone -name CVS`;do
rm -rf $i
done
cd $CURDIR
cp share/postgres.description $DESTDIR/quadstor/pgsql/share

rm -rf $DESTDIR/quadstor/pgsql/scripts
mkdir $DESTDIR/quadstor/pgsql/scripts/
cp scripts/*.sh $DESTDIR/quadstor/pgsql/scripts/
chmod +x $DESTDIR/quadstor/pgsql/scripts/*.sh
mkdir -p $DESTDIR/quadstor/pgsql/data

rm -rf $DESTDIR/quadstor/pgsql/etc
mkdir -p $DESTDIR/quadstor/pgsql/etc
if [ "$os" = "FreeBSD" ]; then
	cp etc/pgsql $DESTDIR/quadstor/pgsql/etc/pgsql
elif [ -f /etc/debian_version ];then
	cp etc/pgsql $DESTDIR/quadstor/pgsql/etc/pgsql
elif [ -f /etc/SuSE-release ];then
	cp etc/pgsql $DESTDIR/quadstor/pgsql/etc/pgsql
elif [ -f /etc/redhat-release ]; then
	cp etc/pgsql.linux $DESTDIR/quadstor/pgsql/etc/pgsql
else
	cp etc/pgsql $DESTDIR/quadstor/pgsql/etc/pgsql
fi
chmod +x $DESTDIR/quadstor/pgsql/etc/pgsql
