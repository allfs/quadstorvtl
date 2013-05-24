#!/bin/bash
set -x

tarfile="pgsql$1.tgz"
if [ "$1" = "sles11sp2" ]; then
	tarfile="pgsqlsles11.tgz"
fi

oldpwd=`pwd`
cd /tmp
rm -rf /tmp/pgsql/
if [ "$?" != "0" ]; then
   echo "ERROR: unable to remove /tmp/pqsql. Check directory permissions"
   exit 1
fi
tar xvzf /quadstor/quadstor/pgsql/tars/$tarfile
rm -rf /quadstor/quadstor/pgsql/bin
rm -rf /quadstor/quadstor/pgsql/lib
rm -rf /quadstor/quadstor/pgsql/share
rm -rf /quadstor/quadstor/pgsql/include
mv /tmp/pgsql/bin /quadstor/quadstor/pgsql/
mv /tmp/pgsql/share /quadstor/quadstor/pgsql/
mv /tmp/pgsql/include /quadstor/quadstor/pgsql/
mv /tmp/pgsql/lib /quadstor/quadstor/pgsql/
rm -rf /tmp/pgsql/
sed -i -e "s/#unix_socket_permissions = 0777/unix_socket_permissions = 0700/" /quadstor/quadstor/pgsql/share/postgresql.conf.sample
sed -i -e "s/^host/#host/g" /quadstor/quadstor/pgsql/share/pg_hba.conf.sample
rm -f /quadstor/quadstor/pgsql/share/postgresql.conf.sample-e
rm -f /quadstor/quadstor/pgsql/share/pg_hba.conf.sample-e

cd /quadstor/quadstor/pgsql && gmake install

mkdir -p /quadstor/sbin
mkdir -p /quadstor/bin

rm -f /quadstor/quadstor/target-mode/fc/qla2xxx

if [ "$1" = "rhel6" ]; then
	cd /quadstor/quadstor/target-mode/fc && ln -s qla2xxx.upstream qla2xxx
elif [ "$1" = "sles11sp2" ]; then
	cd /quadstor/quadstor/target-mode/fc && ln -s qla2xxx.upstream qla2xxx
elif [ "$1" = "sles11" ]; then
	cd /quadstor/quadstor/target-mode/fc && ln -s qla2xxx.slessp1 qla2xxx
elif [ "$1" = "ubuntu11" ]; then
	cd /quadstor/quadstor/target-mode/fc && ln -s qla2xxx.ubuntu11.10 qla2xxx
elif [ "$1" = "debian6" ]; then
	cd /quadstor/quadstor/target-mode/fc && ln -s qla2xxx.deb64 qla2xxx
elif [ "$1" = "bsd9" ]; then
	cd /quadstor/quadstor/target-mode/fc && ln -s isp9.0 isp
elif [ "$1" = "bsd" ]; then
	cd /quadstor/quadstor/target-mode/fc && ln -s isp8.2 isp
else
	cd /quadstor/quadstor/target-mode/fc && ln -s qla2xxx.58 qla2xxx
	sed -i -e "s/:= qla2xxx.*/:= qla2xxx/" /quadstor/quadstor/target-mode/fc/Makefile
fi

if [ ! -d /quadstor/quadstor/mapps/html/cgisrc/yui ]; then
	cd /quadstor/quadstor/mapps/html/cgisrc && tar xvzf yui.tgz
fi

cd $oldpwd
