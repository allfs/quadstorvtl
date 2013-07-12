#/bin/sh
set -x
curdir=`pwd`
libvers="2.2.8"

cd /quadstorvtl/quadstor/ && sh build.sh clean
cd /quadstorvtl/quadstor/ && sh build.sh
cd $curdir

DEBIAN_ROOT=$curdir/debian
rm -rf $DEBIAN_ROOT
mkdir $DEBIAN_ROOT
mkdir $DEBIAN_ROOT/DEBIAN
cp debian-core-control $DEBIAN_ROOT/DEBIAN/control
install -m 755 debian-core-prerm $DEBIAN_ROOT/DEBIAN/prerm
install -m 755 debian-core-postrm $DEBIAN_ROOT/DEBIAN/postrm
install -m 755 debian-core-preinst $DEBIAN_ROOT/DEBIAN/preinst
install -m 755 debian-core-postinst $DEBIAN_ROOT/DEBIAN/postinst

install -m 755 -d $DEBIAN_ROOT/quadstorvtl/lib/modules
install -m 755 -d $DEBIAN_ROOT/quadstorvtl/bin
install -m 755 -d $DEBIAN_ROOT/quadstorvtl/sbin
install -m 755 -d $DEBIAN_ROOT/quadstorvtl/lib
install -m 755 -d $DEBIAN_ROOT/quadstorvtl/lib/modules
install -m 755 -d $DEBIAN_ROOT/quadstorvtl/etc
install -m 755 -d $DEBIAN_ROOT/quadstorvtl/etc/iet
install -m 755 -d $DEBIAN_ROOT/usr/lib/cgi-bin
install -m 755 -d $DEBIAN_ROOT/var/www/
install -m 755 -d $DEBIAN_ROOT/var/www/quadstorvtl
install -m 755 -d $DEBIAN_ROOT/etc/init.d

cd /quadstorvtl/quadstor/pgsql
make install DESTDIR=$DEBIAN_ROOT
rm -rf $DEBIAN_ROOT/quadstorvtl/pgsql/data/

install -m 755 /quadstorvtl/quadstor/masterd/mdaemon $DEBIAN_ROOT/quadstorvtl/sbin/mdaemon
install -m 644  /quadstorvtl/quadstor/library/client/libtlclnt.so $DEBIAN_ROOT/quadstorvtl/lib/libtlclnt.so.$libvers
install -m 644 /quadstorvtl/quadstor/library/server/libtlsrv.so $DEBIAN_ROOT/quadstorvtl/lib/libtlsrv.so.$libvers
install -m 644 /quadstorvtl/quadstor/library/common/libtlmsg.so $DEBIAN_ROOT/quadstorvtl/lib/libtlmsg.so.$libvers
install -m 644 /quadstorvtl/lib/modules/corelib.o $DEBIAN_ROOT/quadstorvtl/lib/modules/
install -m 644 /quadstorvtl/quadstor/mapps/html/cgisrc/index.html $DEBIAN_ROOT/var/www/
install -m 755 /quadstorvtl/quadstor/mapps/html/cgisrc/*.cgi $DEBIAN_ROOT/usr/lib/cgi-bin/
install -m 755 /quadstorvtl/quadstor/mapps/html/cgisrc/*.css $DEBIAN_ROOT/var/www/quadstorvtl/
install -m 755 /quadstorvtl/quadstor/mapps/html/cgisrc/*.js $DEBIAN_ROOT/var/www/quadstorvtl/
install -m 755 /quadstorvtl/quadstor/mapps/html/cgisrc/*.png $DEBIAN_ROOT/var/www/quadstorvtl/
cp -pr /quadstorvtl/quadstor/mapps/html/cgisrc/yui/ $DEBIAN_ROOT/var/www/quadstorvtl/
install -m 744 /quadstorvtl/quadstor/scctl/scctl $DEBIAN_ROOT/quadstorvtl/bin/scctl
install -m 744 /quadstorvtl/quadstor/scctl/fcconfig $DEBIAN_ROOT/quadstorvtl/bin/fcconfig
install -m 744 /quadstorvtl/quadstor/scctl/dbrecover $DEBIAN_ROOT/quadstorvtl/bin/dbrecover
install -m 744 /quadstorvtl/quadstor/etc/quadstor.linux $DEBIAN_ROOT/etc/init.d/quadstor
sed -i -e "s/Default-Start.*/Default-Start:\t\t2 3 4 5/g" $DEBIAN_ROOT/etc/init.d/quadstor
sed -i -e "s/Default-Stop.*/Default-Stop:\t\t\t0 1 6/g" $DEBIAN_ROOT/etc/init.d/quadstor
install -m 444 /quadstorvtl/quadstor/build/LICENSE $DEBIAN_ROOT/quadstorvtl/
install -m 444 /quadstorvtl/quadstor/build/GPLv2 $DEBIAN_ROOT/quadstorvtl/

objcopy --strip-unneeded $DEBIAN_ROOT/quadstorvtl/bin/scctl
objcopy --strip-unneeded $DEBIAN_ROOT/quadstorvtl/bin/dbrecover

#Install src
install -m 755 -d $DEBIAN_ROOT/quadstorvtl/src/others
install -m 644 /quadstorvtl/quadstor/library/server/md5*.[ch] $DEBIAN_ROOT/quadstorvtl/src/others/
install -m 644 /quadstorvtl/quadstor/core/lzf*.[ch] $DEBIAN_ROOT/quadstorvtl/src/others/
install -m 644 /quadstorvtl/quadstor/core/lz4*.[ch] $DEBIAN_ROOT/quadstorvtl/src/others/
install -m 644 /quadstorvtl/quadstor/core/sysdefs/*.h $DEBIAN_ROOT/quadstorvtl/src/others/


cd $DEBIAN_ROOT/quadstorvtl/lib && ln -fs libtlclnt.so.$libvers libtlclnt.so.1
cd $DEBIAN_ROOT/quadstorvtl/lib && ln -fs libtlclnt.so.$libvers libtlclnt.so
cd $DEBIAN_ROOT/quadstorvtl/lib && ln -fs libtlsrv.so.$libvers libtlsrv.so.1
cd $DEBIAN_ROOT/quadstorvtl/lib && ln -fs libtlsrv.so.$libvers libtlsrv.so
cd $DEBIAN_ROOT/quadstorvtl/lib && ln -fs libtlmsg.so.$libvers libtlmsg.so.1
cd $DEBIAN_ROOT/quadstorvtl/lib && ln -fs libtlmsg.so.$libvers libtlmsg.so
rm -f debian.deb
fakeroot dpkg-deb --build $DEBIAN_ROOT 

