#!/bin/bash
set -x

if [ "$QUADSTOR_ROOT" = "" ]; then
	QUADSTOR_ROOT=`cd .. && pwd`
fi

if [ "$QUADSTOR_INSTALL_ROOT" = "" ]; then
	QUADSTOR_INSTALL_ROOT="/quadstorvtl"
fi

tarfile="vtpgsql$1.tgz"
if [ "$1" = "" ]; then
	tarfile="vtpgsqlrhel6.tgz"
fi

oldpwd=`pwd`
cd /tmp
rm -rf /tmp/pgsql/
if [ "$?" != "0" ]; then
   echo "ERROR: unable to remove /tmp/pqsql. Check directory permissions"
   exit 1
fi
tar xvzf $QUADSTOR_ROOT/pgsql/tars/$tarfile
rm -rf $QUADSTOR_ROOT/pgsql/bin
rm -rf $QUADSTOR_ROOT/pgsql/lib
rm -rf $QUADSTOR_ROOT/pgsql/share
rm -rf $QUADSTOR_ROOT/pgsql/include
mv /tmp/pgsql/bin $QUADSTOR_ROOT/pgsql/
mv /tmp/pgsql/share $QUADSTOR_ROOT/pgsql/
mv /tmp/pgsql/include $QUADSTOR_ROOT/pgsql/
mv /tmp/pgsql/lib $QUADSTOR_ROOT/pgsql/
rm -rf /tmp/pgsql/
sed -i -e "s/#port =.*/port = 9989/" $QUADSTOR_ROOT/pgsql/share/postgresql.conf.sample
sed -i -e "s/#unix_socket_permissions = 0777/unix_socket_permissions = 0700/" $QUADSTOR_ROOT/pgsql/share/postgresql.conf.sample
sed -i -e "s/^host/#host/g" $QUADSTOR_ROOT/pgsql/share/pg_hba.conf.sample
rm -f $QUADSTOR_ROOT/pgsql/share/postgresql.conf.sample-e
rm -f $QUADSTOR_ROOT/pgsql/share/pg_hba.conf.sample-e

cd $QUADSTOR_ROOT/pgsql && gmake install

mkdir -p $QUADSTOR_INSTALL_ROOT/sbin
mkdir -p $QUADSTOR_INSTALL_ROOT/bin

rm -f $QUADSTOR_ROOT/target-mode/fc/qla2xxx

if [ "$1" = "rhel6" -o "$1" = "rhel6x86" ]; then
	cd $QUADSTOR_ROOT/target-mode/fc && ln -s qla2xxx.upstream qla2xxx
elif [ "$1" = "sles11sp2" ]; then
	cd $QUADSTOR_ROOT/target-mode/fc && ln -s qla2xxx.upstream qla2xxx
	sed -i -e "s/:= qla2xxx.*/:= qla2xxx/" $QUADSTOR_ROOT/target-mode/fc/Makefile
elif [ "$1" = "sles11" ]; then
	cd $QUADSTOR_ROOT/target-mode/fc && ln -s qla2xxx.slessp1 qla2xxx
	sed -i -e "s/:= qla2xxx.*/:= qla2xxx/" $QUADSTOR_ROOT/target-mode/fc/Makefile
elif [ "$1" = "debian7" ]; then
	cd $QUADSTOR_ROOT/target-mode/fc && ln -s qla2xxx.upstream qla2xxx
elif [ "$1" = "debian6" ]; then
	cd $QUADSTOR_ROOT/target-mode/fc && ln -s qla2xxx.deb64 qla2xxx
	sed -i -e "s/:= qla2xxx.*/:= qla2xxx/" $QUADSTOR_ROOT/target-mode/fc/Makefile
elif [ "$1" = "bsd9" ]; then
	cd $QUADSTOR_ROOT/target-mode/fc && ln -s isp9.0 isp
elif [ "$1" = "bsd" ]; then
	cd $QUADSTOR_ROOT/target-mode/fc && ln -s isp8.2 isp
elif [ "$1" = "rhel5" ]; then
	cd $QUADSTOR_ROOT/target-mode/fc && ln -s qla2xxx.58 qla2xxx
	sed -i -e "s/:= qla2xxx.*/:= qla2xxx/" $QUADSTOR_ROOT/target-mode/fc/Makefile
else
	cd $QUADSTOR_ROOT/target-mode/fc && ln -s qla2xxx.upstream qla2xxx
fi

if [ ! -d $QUADSTOR_ROOT/mapps/html/cgisrc/yui ]; then
	cd $QUADSTOR_ROOT/mapps/html/cgisrc && tar xvzf yui.tgz
fi

cd $oldpwd
