#!/bin/bash
set -x

tarfile="vtpgsql$1.tgz"
#if [ "$1" = "sles11sp2" ]; then
#	tarfile="pgsqlsles11.tgz"
#fi

oldpwd=`pwd`
cd /tmp
rm -rf /tmp/pgsql/
if [ "$?" != "0" ]; then
   echo "ERROR: unable to remove /tmp/pqsql. Check directory permissions"
   exit 1
fi
tar xvzf /quadstorvtl/quadstor/pgsql/tars/$tarfile
rm -rf /quadstorvtl/quadstor/pgsql/bin
rm -rf /quadstorvtl/quadstor/pgsql/lib
rm -rf /quadstorvtl/quadstor/pgsql/share
rm -rf /quadstorvtl/quadstor/pgsql/include
mv /tmp/pgsql/bin /quadstorvtl/quadstor/pgsql/
mv /tmp/pgsql/share /quadstorvtl/quadstor/pgsql/
mv /tmp/pgsql/include /quadstorvtl/quadstor/pgsql/
mv /tmp/pgsql/lib /quadstorvtl/quadstor/pgsql/
rm -rf /tmp/pgsql/
sed -i -e "s/#port =.*/port = 9989/" /quadstorvtl/quadstor/pgsql/share/postgresql.conf.sample
sed -i -e "s/#unix_socket_permissions = 0777/unix_socket_permissions = 0700/" /quadstorvtl/quadstor/pgsql/share/postgresql.conf.sample
sed -i -e "s/^host/#host/g" /quadstorvtl/quadstor/pgsql/share/pg_hba.conf.sample
rm -f /quadstorvtl/quadstor/pgsql/share/postgresql.conf.sample-e
rm -f /quadstorvtl/quadstor/pgsql/share/pg_hba.conf.sample-e

cd /quadstorvtl/quadstor/pgsql && gmake install

mkdir -p /quadstorvtl/sbin
mkdir -p /quadstorvtl/bin

rm -f /quadstorvtl/quadstor/target-mode/fc/qla2xxx

if [ "$1" = "rhel6" ]; then
	cd /quadstorvtl/quadstor/target-mode/fc && ln -s qla2xxx.upstream qla2xxx
elif [ "$1" = "sles11sp2" ]; then
	cd /quadstorvtl/quadstor/target-mode/fc && ln -s qla2xxx.upstream qla2xxx
elif [ "$1" = "sles11" ]; then
	cd /quadstorvtl/quadstor/target-mode/fc && ln -s qla2xxx.slessp1 qla2xxx
	sed -i -e "s/:= qla2xxx.*/:= qla2xxx/" /quadstorvtl/quadstor/target-mode/fc/Makefile
elif [ "$1" = "debian7" ]; then
	cd /quadstorvtl/quadstor/target-mode/fc && ln -s qla2xxx.upstream qla2xxx
elif [ "$1" = "debian6" ]; then
	cd /quadstorvtl/quadstor/target-mode/fc && ln -s qla2xxx.deb64 qla2xxx
	sed -i -e "s/:= qla2xxx.*/:= qla2xxx/" /quadstorvtl/quadstor/target-mode/fc/Makefile
elif [ "$1" = "bsd9" ]; then
	cd /quadstorvtl/quadstor/target-mode/fc && ln -s isp9.0 isp
elif [ "$1" = "bsd" ]; then
	cd /quadstorvtl/quadstor/target-mode/fc && ln -s isp8.2 isp
else
	cd /quadstorvtl/quadstor/target-mode/fc && ln -s qla2xxx.58 qla2xxx
	sed -i -e "s/:= qla2xxx.*/:= qla2xxx/" /quadstorvtl/quadstor/target-mode/fc/Makefile
fi

if [ ! -d /quadstorvtl/quadstor/mapps/html/cgisrc/yui ]; then
	cd /quadstorvtl/quadstor/mapps/html/cgisrc && tar xvzf yui.tgz
fi

cd $oldpwd
