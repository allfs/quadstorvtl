#!/bin/sh
set -x
curdir=`pwd`

cd /quadstor/quadstor/ && sh build.sh clean
cd $curdir
DEBIAN_ROOT=$curdir/debian
rm -rf $DEBIAN_ROOT
mkdir $DEBIAN_ROOT
mkdir $DEBIAN_ROOT/DEBIAN
cp debian-itf-control $DEBIAN_ROOT/DEBIAN/control
install -m 755 debian-itf-prerm $DEBIAN_ROOT/DEBIAN/prerm
install -m 755 debian-itf-postrm $DEBIAN_ROOT/DEBIAN/postrm
install -m 755 debian-itf-postinst $DEBIAN_ROOT/DEBIAN/postinst

install -m 755 -d $DEBIAN_ROOT/etc/udev/rules.d
install -m 755 -d $DEBIAN_ROOT/quadstor/src
install -m 755 -d $DEBIAN_ROOT/quadstor/bin
install -m 755 -d $DEBIAN_ROOT/quadstor/src/export
install -m 644 /quadstor/quadstor/export/devq.[ch] $DEBIAN_ROOT/quadstor/src/export/
install -m 644 /quadstor/quadstor/export/linuxdefs.h $DEBIAN_ROOT/quadstor/src/export/
install -m 644 /quadstor/quadstor/export/exportdefs.h $DEBIAN_ROOT/quadstor/src/export/
install -m 644 /quadstor/quadstor/export/missingdefs.h $DEBIAN_ROOT/quadstor/src/export/
install -m 644 /quadstor/quadstor/export/qsio_ccb.h $DEBIAN_ROOT/quadstor/src/export/
install -m 644 /quadstor/quadstor/export/core_linux.c $DEBIAN_ROOT/quadstor/src/export/
install -m 644 /quadstor/quadstor/export/ldev_linux.[ch] $DEBIAN_ROOT/quadstor/src/export/
install -m 644 /quadstor/quadstor/export/Makefile.dist $DEBIAN_ROOT/quadstor/src/export/Makefile
install -m 644 /quadstor/quadstor/export/queue.h $DEBIAN_ROOT/quadstor/src/export/

install -m 755 -d $DEBIAN_ROOT/quadstor/src/common
install -m 644 /quadstor/quadstor/common/ioctldefs.h $DEBIAN_ROOT/quadstor/src/common/
install -m 644 /quadstor/quadstor/common/commondefs.h $DEBIAN_ROOT/quadstor/src/common/

install -m 755 -d $DEBIAN_ROOT/quadstor/src/target-mode/iscsi
install -m 755 -d $DEBIAN_ROOT/quadstor/src/target-mode/iscsi/include
install -m 755 -d $DEBIAN_ROOT/quadstor/src/target-mode/iscsi/kernel
install -m 755 -d $DEBIAN_ROOT/quadstor/src/target-mode/iscsi/usr
install -m 644 /quadstor/quadstor/target-mode/iscsi/include/*.h $DEBIAN_ROOT/quadstor/src/target-mode/iscsi/include/
install -m 644 /quadstor/quadstor/target-mode/iscsi/kernel/*.[ch] $DEBIAN_ROOT/quadstor/src/target-mode/iscsi/kernel/
install -m 644 /quadstor/quadstor/target-mode/iscsi/kernel/Makefile.dist $DEBIAN_ROOT/quadstor/src/target-mode/iscsi/kernel/Makefile
install -m 644 /quadstor/quadstor/target-mode/iscsi/usr/*.[ch] $DEBIAN_ROOT/quadstor/src/target-mode/iscsi/usr/
install -m 644 /quadstor/quadstor/target-mode/iscsi/usr/Makefile.dist $DEBIAN_ROOT/quadstor/src/target-mode/iscsi/usr/Makefile

install -m 755 -d $DEBIAN_ROOT/quadstor/etc/iet/
install -m 644 /quadstor/quadstor/target-mode/iscsi/etc/targets.allow $DEBIAN_ROOT/quadstor/etc/iet/targets.allow.sample
install -m 644 /quadstor/quadstor/target-mode/iscsi/etc/initiators.allow $DEBIAN_ROOT/quadstor/etc/iet/initiators.allow.sample
install -m 644 /quadstor/quadstor/target-mode/iscsi/etc/ietd.conf $DEBIAN_ROOT/quadstor/etc/iet/ietd.conf.sample
install -m 755 /quadstor/quadstor/target-mode/iscsi/etc/initd/initd.redhat $DEBIAN_ROOT/quadstor/etc/initd.iscsi

install -m 755 -d $DEBIAN_ROOT/quadstor/src/target-mode/fc/common
install -m 644 /quadstor/quadstor/target-mode/fc/common/fccommon.c $DEBIAN_ROOT/quadstor/src/target-mode/fc/common/
install -m 755 -d $DEBIAN_ROOT/quadstor/src/target-mode/fc/qla2xxx
install -m 644 /quadstor/quadstor/target-mode/fc/qla2xxx/*.[ch] $DEBIAN_ROOT/quadstor/src/target-mode/fc/qla2xxx
install -m 644 /quadstor/quadstor/target-mode/fc/qla2xxx/Makefile.dist $DEBIAN_ROOT/quadstor/src/target-mode/fc/qla2xxx/Makefile

install -m 755 /quadstor/quadstor/scripts/builditf.linux.sh $DEBIAN_ROOT/quadstor/bin/builditf
install -m 755 /quadstor/quadstor/scripts/qlainst $DEBIAN_ROOT/quadstor/bin/qlainst
install -m 755 /quadstor/quadstor/scripts/qlauninst $DEBIAN_ROOT/quadstor/bin/qlauninst

rm -f debian.deb
fakeroot dpkg-deb --build $DEBIAN_ROOT 
