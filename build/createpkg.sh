#!/bin/sh
libvers="2.2.14-FreeBSD8.2-x86_64"
rm -rf /quadstorvtl/lib
rm -rf /quadstorvtl/bin
rm -rf /quadstorvtl/sbin
rm -rf /quadstorvtl/etc
rm -rf /quadstorvtl/share

cd /quadstorvtl/quadstor/
sh build.sh clean
sh build.sh 
sh build.sh install

mkdir -p /quadstorvtl/lib
mkdir -p /quadstorvtl/lib/modules
mkdir -p /quadstorvtl/bin
mkdir -p /quadstorvtl/sbin
mkdir -p /quadstorvtl/lib
mkdir -p /quadstorvtl/lib/modules
mkdir -p /quadstorvtl/etc
rm -f /quadstorvtl/lib/modules/corelib.o
install -m 755  /quadstorvtl/quadstor/export/vtlcore.ko /quadstorvtl/lib/modules
install -m 755  /quadstorvtl/quadstor/export/vtldev.ko /quadstorvtl/lib/modules
install -m 755  /quadstorvtl/quadstor/library/client/libtlclnt.so /quadstorvtl/lib/libtlclnt.so.$libvers
install -m 755 /quadstorvtl/quadstor/library/server/libtlsrv.so /quadstorvtl/lib/libtlsrv.so.$libvers
install -m 755 /quadstorvtl/quadstor/library/common/libtlmsg.so /quadstorvtl/lib/libtlmsg.so.$libvers
install -m 755 /quadstorvtl/quadstor/scripts/free /quadstorvtl/bin/free

cp /quadstorvtl/quadstor/build/LICENSE /quadstorvtl/
cp /quadstorvtl/quadstor/build/GPLv2 /quadstorvtl/
cd /quadstorvtl/lib && ln -fs libtlclnt.so.$libvers libtlclnt.so.1
cd /quadstorvtl/lib && ln -fs libtlclnt.so.$libvers libtlclnt.so
cd /quadstorvtl/lib && ln -fs libtlsrv.so.$libvers libtlsrv.so.1
cd /quadstorvtl/lib && ln -fs libtlsrv.so.$libvers libtlsrv.so
cd /quadstorvtl/lib && ln -fs libtlmsg.so.$libvers libtlmsg.so.1
cd /quadstorvtl/lib && ln -fs libtlmsg.so.$libvers libtlmsg.so

#install source
rm -rf /quadstorvtl/src/
mkdir -p /quadstorvtl/src/export

cp /quadstorvtl/quadstor/export/bsddefs.h /quadstorvtl/src/export/
cp /quadstorvtl/quadstor/export/exportdefs.h /quadstorvtl/src/export/
cp /quadstorvtl/quadstor/export/missingdefs.h /quadstorvtl/src/export/
cp /quadstorvtl/quadstor/export/qsio_ccb.h /quadstorvtl/src/export/

mkdir -p /quadstorvtl/src/others
cp /quadstorvtl/quadstor/core/lzf*.[ch] /quadstorvtl/src/others/
cp /quadstorvtl/quadstor/core/lz4*.[ch] /quadstorvtl/src/others/
cp /quadstorvtl/quadstor/library/server/md5*.[ch] /quadstorvtl/src/others/

cd /quadstorvtl/quadstor/build/
rm /quadstorvtl/quadstor/build/pkg-plist

#bin dir
echo "quadstorvtl/bin/cam" >> /quadstorvtl/quadstor/build/pkg-plist
echo "quadstorvtl/bin/free" >> /quadstorvtl/quadstor/build/pkg-plist
echo "quadstorvtl/bin/pidof" >> /quadstorvtl/quadstor/build/pkg-plist
echo "quadstorvtl/bin/scctl" >> /quadstorvtl/quadstor/build/pkg-plist
echo "quadstorvtl/bin/fcconfig" >> /quadstorvtl/quadstor/build/pkg-plist
echo "quadstorvtl/bin/dbrecover" >> /quadstorvtl/quadstor/build/pkg-plist

echo "quadstorvtl/sbin/vtmdaemon" >> /quadstorvtl/quadstor/build/pkg-plist

echo "quadstorvtl/etc/quadstorvtl" >> /quadstorvtl/quadstor/build/pkg-plist

cd / && find quadstorvtl/lib/lib* >> /quadstorvtl/quadstor/build/pkg-plist
#modules dir
echo "quadstorvtl/lib/modules/vtlcore.ko" >> /quadstorvtl/quadstor/build/pkg-plist
echo "quadstorvtl/lib/modules/vtldev.ko" >> /quadstorvtl/quadstor/build/pkg-plist
echo "quadstorvtl/lib/modules/ispmod.ko" >> /quadstorvtl/quadstor/build/pkg-plist

cd / && find quadstorvtl/httpd/www/*.html >> /quadstorvtl/quadstor/build/pkg-plist
cd / && find quadstorvtl/httpd/cgi-bin/* >> /quadstorvtl/quadstor/build/pkg-plist
echo "quadstorvtl/LICENSE" >> /quadstorvtl/quadstor/build/pkg-plist
echo "quadstorvtl/GPLv2" >> /quadstorvtl/quadstor/build/pkg-plist

for i in `cd / && find quadstorvtl/pgsql/`;do
	if [ -d $i ]; then
		continue
	fi
	echo $i >> /quadstorvtl/quadstor/build/pkg-plist
done

for i in `cd / && find quadstorvtl/src/`;do
	if [ -d $i ]; then
		continue
	fi
	echo $i >> /quadstorvtl/quadstor/build/pkg-plist
done

for i in `cd / && find quadstorvtl/httpd/www/quadstorvtl/`;do
	if [ -d $i ]; then
		continue
	fi
	echo $i >> /quadstorvtl/quadstor/build/pkg-plist
done

cd /quadstorvtl/quadstor/build/
rm quadstor-vtl-core-*.tbz
pkg_create -c pkg-comment -d pkg-info -f pkg-plist -p / -i pkg-pre.sh -I pkg-post.sh -k pkg-preun.sh -K pkg-postun.sh quadstor-vtl-core-$libvers
