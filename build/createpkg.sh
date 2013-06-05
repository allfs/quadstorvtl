#!/bin/sh
libvers="2.2.2-FreeBSD8.2-x86_64"
rm -rf /quadstor/lib
rm -rf /quadstor/bin
rm -rf /quadstor/sbin
rm -rf /quadstor/etc
rm -rf /quadstor/share

cd /quadstor/quadstor/
sh build.sh clean
sh build.sh 
sh build.sh install

mkdir -p /quadstor/lib
mkdir -p /quadstor/lib/modules
mkdir -p /quadstor/bin
mkdir -p /quadstor/sbin
mkdir -p /quadstor/lib
mkdir -p /quadstor/lib/modules
mkdir -p /quadstor/etc
rm -f /quadstor/lib/modules/corelib.o
install -m 755  /quadstor/quadstor/export/vtlcore.ko /quadstor/lib/modules
install -m 755  /quadstor/quadstor/export/ldev.ko /quadstor/lib/modules
install -m 755  /quadstor/quadstor/library/client/libtlclnt.so /quadstor/lib/libtlclnt.so.$libvers
install -m 755 /quadstor/quadstor/library/server/libtlsrv.so /quadstor/lib/libtlsrv.so.$libvers
install -m 755 /quadstor/quadstor/library/common/libtlmsg.so /quadstor/lib/libtlmsg.so.$libvers
install -m 755 /quadstor/quadstor/scripts/free /quadstor/bin/free

cp /quadstor/quadstor/build/LICENSE /quadstor/
cp /quadstor/quadstor/build/GPLv2 /quadstor/
cd /quadstor/lib && ln -fs libtlclnt.so.$libvers libtlclnt.so.1
cd /quadstor/lib && ln -fs libtlclnt.so.$libvers libtlclnt.so
cd /quadstor/lib && ln -fs libtlsrv.so.$libvers libtlsrv.so.1
cd /quadstor/lib && ln -fs libtlsrv.so.$libvers libtlsrv.so
cd /quadstor/lib && ln -fs libtlmsg.so.$libvers libtlmsg.so.1
cd /quadstor/lib && ln -fs libtlmsg.so.$libvers libtlmsg.so

#install source
rm -rf /quadstor/src/
mkdir -p /quadstor/src/export

cp /quadstor/quadstor/export/bsddefs.h /quadstor/src/export/
cp /quadstor/quadstor/export/exportdefs.h /quadstor/src/export/
cp /quadstor/quadstor/export/missingdefs.h /quadstor/src/export/
cp /quadstor/quadstor/export/qsio_ccb.h /quadstor/src/export/

mkdir -p /quadstor/src/others
cp /quadstor/quadstor/core/lzf*.[ch] /quadstor/src/others/
cp /quadstor/quadstor/core/lz4*.[ch] /quadstor/src/others/
cp /quadstor/quadstor/library/server/md5*.[ch] /quadstor/src/others/

cd /quadstor/quadstor/build/
rm /quadstor/quadstor/build/pkg-plist

#bin dir
echo "quadstor/bin/cam" >> /quadstor/quadstor/build/pkg-plist
echo "quadstor/bin/free" >> /quadstor/quadstor/build/pkg-plist
echo "quadstor/bin/pidof" >> /quadstor/quadstor/build/pkg-plist
echo "quadstor/bin/scctl" >> /quadstor/quadstor/build/pkg-plist
echo "quadstor/bin/fcconfig" >> /quadstor/quadstor/build/pkg-plist
echo "quadstor/bin/dbrecover" >> /quadstor/quadstor/build/pkg-plist

echo "quadstor/sbin/mdaemon" >> /quadstor/quadstor/build/pkg-plist

echo "quadstor/etc/quadstor" >> /quadstor/quadstor/build/pkg-plist

cd / && find quadstor/lib/lib* >> /quadstor/quadstor/build/pkg-plist
#modules dir
echo "quadstor/lib/modules/vtlcore.ko" >> /quadstor/quadstor/build/pkg-plist
echo "quadstor/lib/modules/ldev.ko" >> /quadstor/quadstor/build/pkg-plist
echo "quadstor/lib/modules/ispmod.ko" >> /quadstor/quadstor/build/pkg-plist

cd / && find quadstor/httpd/www/*.html >> /quadstor/quadstor/build/pkg-plist
cd / && find quadstor/httpd/cgi-bin/* >> /quadstor/quadstor/build/pkg-plist
echo "quadstor/LICENSE" >> /quadstor/quadstor/build/pkg-plist
echo "quadstor/GPLv2" >> /quadstor/quadstor/build/pkg-plist

for i in `cd / && find quadstor/pgsql/`;do
	if [ -d $i ]; then
		continue
	fi
	echo $i >> /quadstor/quadstor/build/pkg-plist
done

for i in `cd / && find quadstor/src/`;do
	if [ -d $i ]; then
		continue
	fi
	echo $i >> /quadstor/quadstor/build/pkg-plist
done

for i in `cd / && find quadstor/httpd/www/quadstor/`;do
	if [ -d $i ]; then
		continue
	fi
	echo $i >> /quadstor/quadstor/build/pkg-plist
done

cd /quadstor/quadstor/build/
rm quadstor-vtl-core-*.tbz
pkg_create -c pkg-comment -d pkg-info -f pkg-plist -p / -i pkg-pre.sh -I pkg-post.sh -k pkg-preun.sh -K pkg-postun.sh quadstor-vtl-core-$libvers
