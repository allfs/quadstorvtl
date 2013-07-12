#!/bin/bash
set -x
DESTDIR=$2
x86=$1
CURDIR=`pwd`
os=`uname`
rm -rf $DESTDIR/quadstorvtl/pgsql/bin/
mkdir -p $DESTDIR/quadstorvtl/pgsql/bin/
mkdir -p $DESTDIR/quadstorvtl/pgsql/lib/
cp bin/* $DESTDIR/quadstorvtl/pgsql/bin/
chmod +x $DESTDIR/quadstorvtl/pgsql/bin/*
cd $DESTDIR/quadstorvtl/pgsql/bin/ && ln -s postgres postmaster
cd $CURDIR

rm -rf $DESTDIR/quadstorvtl/pgsql/share
mkdir -p $DESTDIR/quadstorvtl/pgsql/share
#cp share/pg_hba.conf $DESTDIR/quadstorvtl/pgsql/share/pg_hba.conf.sample
#cp share/postgresql.conf $DESTDIR/quadstorvtl/pgsql/share/postgresql.conf.sample
cp share/postgres.bki $DESTDIR/quadstorvtl/pgsql/share/
cp share/postgres.shdescription $DESTDIR/quadstorvtl/pgsql/share/
cp share/*.sql $DESTDIR/quadstorvtl/pgsql/share/
cp -pr lib/* $DESTDIR/quadstorvtl/pgsql/lib/
cp sqls/*.sql $DESTDIR/quadstorvtl/pgsql/share/
cp share/*.sample $DESTDIR/quadstorvtl/pgsql/share/
cp share/sql_features.txt $DESTDIR/quadstorvtl/pgsql/share/
cp -r share/timezone $DESTDIR/quadstorvtl/pgsql/share/
cp -r share/timezonesets $DESTDIR/quadstorvtl/pgsql/share/
#cd $DESTDIR/quadstorvtl/pgsql/share/timezone
for i in `find $DESTDIR/quadstorvtl/pgsql/share/timezone -name CVS`;do
rm -rf $i
done
cd $CURDIR
cp share/postgres.description $DESTDIR/quadstorvtl/pgsql/share

rm -rf $DESTDIR/quadstorvtl/pgsql/scripts
mkdir $DESTDIR/quadstorvtl/pgsql/scripts/
cp scripts/*.sh $DESTDIR/quadstorvtl/pgsql/scripts/
chmod +x $DESTDIR/quadstorvtl/pgsql/scripts/*.sh
mkdir -p $DESTDIR/quadstorvtl/pgsql/data

rm -rf $DESTDIR/quadstorvtl/pgsql/etc
mkdir -p $DESTDIR/quadstorvtl/pgsql/etc
if [ "$os" = "FreeBSD" ]; then
	cp etc/pgsql $DESTDIR/quadstorvtl/pgsql/etc/pgsql
elif [ -f /etc/debian_version ];then
	cp etc/pgsql $DESTDIR/quadstorvtl/pgsql/etc/pgsql
elif [ -f /etc/SuSE-release ];then
	cp etc/pgsql $DESTDIR/quadstorvtl/pgsql/etc/pgsql
elif [ -f /etc/redhat-release ]; then
	cp etc/pgsql.linux $DESTDIR/quadstorvtl/pgsql/etc/pgsql
else
	cp etc/pgsql $DESTDIR/quadstorvtl/pgsql/etc/pgsql
fi
chmod +x $DESTDIR/quadstorvtl/pgsql/etc/pgsql
