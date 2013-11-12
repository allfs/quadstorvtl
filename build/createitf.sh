#!/bin/sh
libvers="2.2.15-FreeBSD8.2-x86_64"
rm -rf /quadstorvtl/bin
rm -rf /quadstorvtl/lib
rm -rf /quadstorvtl/sbin
rm -rf /quadstorvtl/etc

mkdir -p /quadstorvtl/etc
mkdir -p /quadstorvtl/etc/iet
mkdir -p /quadstorvtl/bin
mkdir -p /quadstorvtl/sbin
mkdir -p /quadstorvtl/lib/modules

cd /quadstorvtl/quadstor/
sh build.sh clean
sh build.sh 
sh build.sh install

install -m 755 /quadstorvtl/quadstor/target-mode/iscsi/etc/targets.allow /quadstorvtl/etc/iet/targets.allow.sample
install -m 755 /quadstorvtl/quadstor/target-mode/iscsi/etc/initiators.allow /quadstorvtl/etc/iet/initiators.allow.sample
install -m 755 /quadstorvtl/quadstor/target-mode/iscsi/etc/ietd.conf /quadstorvtl/etc/iet/ietd.conf.sample

install -m 755 /quadstorvtl/quadstor/target-mode/iscsi/kernel/iscsit.ko /quadstorvtl/lib/modules/
install -m 755 /quadstorvtl/quadstor/target-mode/iscsi/usr/ietd /quadstorvtl/sbin/
install -m 755 /quadstorvtl/quadstor/target-mode/iscsi/usr/ietadm /quadstorvtl/bin/

#install source
rm -rf /quadstorvtl/src/
cd /quadstorvtl/quadstor/target-mode/iscsi/kernel && make -f Makefile.iet clean
cd /quadstorvtl/quadstor/target-mode/iscsi/usr && gmake clean

mkdir -p /quadstorvtl/src/target-mode/iscsi/kernel
mkdir -p /quadstorvtl/src/target-mode/iscsi/usr
mkdir -p /quadstorvtl/src/target-mode/iscsi/include
cp /quadstorvtl/quadstor/target-mode/iscsi/include/*.h /quadstorvtl/src/target-mode/iscsi/include/
cp /quadstorvtl/quadstor/target-mode/iscsi/kernel/*.[ch] /quadstorvtl/src/target-mode/iscsi/kernel/
cp /quadstorvtl/quadstor/target-mode/iscsi/kernel/Makefile.iet.dist /quadstorvtl/src/target-mode/iscsi/kernel/Makefile
cp /quadstorvtl/quadstor/target-mode/iscsi/usr/*.[ch] /quadstorvtl/src/target-mode/iscsi/usr/
cp /quadstorvtl/quadstor/target-mode/iscsi/usr/Makefile.dist /quadstorvtl/src/target-mode/iscsi/usr/Makefile

cd /quadstorvtl/quadstor/build/
rm /quadstorvtl/quadstor/build/pkg-plist
echo "quadstorvtl/bin/ietadm" >> /quadstorvtl/quadstor/build/pkg-plist
echo "quadstorvtl/sbin/ietd" >> /quadstorvtl/quadstor/build/pkg-plist
echo "quadstorvtl/lib/modules/iscsit.ko" >> /quadstorvtl/quadstor/build/pkg-plist
cd / && find quadstorvtl/etc/iet/* >> /quadstorvtl/quadstor/build/pkg-plist

for i in `cd / && find quadstorvtl/src/`;do
	if [ -d $i ]; then
		continue
	fi
	echo $i >> /quadstorvtl/quadstor/build/pkg-plist
done

cd /quadstorvtl/quadstor/build/
rm quadstor-vtl-itf-*.tbz
pkg_create -c pkg-comment -d pkg-info-itf -f pkg-plist -p / -I pkg-post-itf.sh -k pkg-preun.sh -K pkg-postun-itf.sh quadstor-vtl-itf-$libvers
